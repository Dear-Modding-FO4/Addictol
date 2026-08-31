#include <Modules/AdModuleFacegen.h>
#include <Core/AdPlugin.h>
#include <Core/AdUtils.h>
#include <Shlwapi.h>

// SimpleIni include windows.h
#include <INI/SimpleIni.h>
#undef MEM_RELEASE
#undef MAX_PATH
#undef ERROR

#include <RE/C/ConsoleLog.h>
#include <RE/B/BSResource_ID.h>
#include <RE/T/TESNPC.h>
#include <RE/T/TESDataHandler.h>
#include <RE/B/BGSSaveLoadGame.h>
#include <RE/C/CHANGE_TYPES.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>

namespace Addictol
{
	static bool __stdcall CanUsePreprocessingHead(const RE::TESNPC* NPC) noexcept;

	namespace BSTextureDB
	{
		// Working buried function.
		static uintptr_t FacegenPathPrintf{ 0 };
		static uintptr_t CreateEntryID{ 0 };

		static bool __stdcall FormatPath__And__ExistIn(const RE::TESNPC* a_NPC, const char* a_destPath,
			uint32_t a_size, uint32_t a_textureIndex) noexcept
		{
			RE::BSResource::ID ID;
			RELEX::FastCall<void>(FacegenPathPrintf, a_NPC, a_destPath, a_size, a_textureIndex);
			return RELEX::FastCall<bool>(CreateEntryID, a_destPath + 14, &ID);
		}
	};

	namespace
	{
		using FacegenExceptionSet = std::unordered_set<uint32_t>;

		std::atomic_uint64_t g_facegenTemporaryFileSequence{ 0 };

		[[nodiscard]] std::filesystem::path FacegenTemporaryPath(
			const std::filesystem::path& a_target)
		{
			auto name = a_target.filename().wstring();
			name += L".tmp.";
			name += std::to_wstring(GetCurrentProcessId());
			name += L".";
			name += std::to_wstring(
				g_facegenTemporaryFileSequence.fetch_add(
					1,
					std::memory_order_relaxed));
			return a_target.parent_path() / name;
		}

		[[nodiscard]] bool WriteFacegenExceptionsAtomically(
			const std::filesystem::path& a_target,
			std::string_view a_contents,
			std::string& a_error)
		{
			std::error_code filesystemError;
			const auto temporary = FacegenTemporaryPath(a_target);
			{
				std::ofstream file{
					temporary,
					std::ios::binary | std::ios::trunc
				};
				if (!file)
				{
					a_error = "Could not create a temporary exceptions file.";
					return false;
				}
				file.write(
					a_contents.data(),
					static_cast<std::streamsize>(a_contents.size()));
				file.flush();
				if (!file)
				{
					file.close();
					std::filesystem::remove(temporary, filesystemError);
					a_error = "Could not write the temporary exceptions file.";
					return false;
				}
			}

			if (!MoveFileExW(
					temporary.c_str(),
					a_target.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				std::filesystem::remove(temporary, filesystemError);
				a_error = "Could not replace the exceptions file.";
				return false;
			}
			return true;
		}

		[[nodiscard]] std::string FieldValidationMessage(
			FacegenExceptionValidationIssue a_issue)
		{
			switch (a_issue)
			{
			case FacegenExceptionValidationIssue::kNone:
				return {};
			case FacegenExceptionValidationIssue::kEmptyKey:
				return "Unique name is required.";
			case FacegenExceptionValidationIssue::kMalformedKey:
				return "Unique name cannot contain '=' or a line break.";
			case FacegenExceptionValidationIssue::kDuplicateKey:
				return "Unique name is already in use.";
			case FacegenExceptionValidationIssue::kEmptyFormID:
				return "FormID is required.";
			case FacegenExceptionValidationIssue::kMalformedFormID:
				return "Malformed FormID. Use 0x hexadecimal or decimal.";
			}
			return "Invalid exception entry.";
		}

		void AddPrimaryExceptions(FacegenExceptionSet& a_exceptions)
		{
			for (const auto& exception : kFacegenPrimaryExceptions)
				a_exceptions.insert(exception.formID);
		}

		[[nodiscard]] FacegenExceptionDraft NormalizeExceptionDraft(
			const FacegenExceptionDraft& a_entry)
		{
			auto normalized = a_entry;
			TrimFacegenExceptionField(normalized.key);
			TrimFacegenExceptionField(normalized.formID);
			if (normalized.pluginName)
				TrimFacegenExceptionField(*normalized.pluginName);
			return normalized;
		}
	}

	class FacegenSystem :
		public REX::TSingleton<FacegenSystem>
	{
		RE::BGSKeyword* keywordIsChildPlayer{ nullptr };
		std::atomic<std::shared_ptr<const FacegenExceptionSet>> facegenExceptionFormIDs;
		std::atomic<RE::TESDataHandler*> dataHandler{ nullptr };
		mutable std::mutex exceptionSnapshotMutex;
		std::mutex exceptionPersistenceMutex;
		FacegenExceptionSnapshot exceptionSnapshot;
		uint64_t exceptionRevision{ 0 };

		FacegenSystem(const FacegenSystem&) = delete;
		FacegenSystem operator=(const FacegenSystem&) = delete;

		[[nodiscard]] FacegenExceptionStatus GetLoadOrderByFormID(
			const char* a_pluginName,
			uint32_t& a_formID,
			bool a_logFailure) const noexcept;
		void PublishExceptions(
			FacegenExceptionSet a_formIDs,
			FacegenExceptionSnapshot a_snapshot);
		void LoadExceptions(
			FacegenExceptionSnapshot& a_snapshot,
			FacegenExceptionSet& a_formIDs) const;
		void ReadExceptions() noexcept;
	public:
		FacegenSystem() = default;
		~FacegenSystem() = default;

		bool Init() noexcept;
		bool InitContinue([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept;
		bool NeedSkipNPC(const RE::TESNPC* a_NPC) const noexcept;
		[[nodiscard]] FacegenExceptionSnapshot ExceptionSnapshot() const;
		[[nodiscard]] FacegenExceptionValidation ValidateException(
			const FacegenExceptionDraft& a_entry,
			std::span<const FacegenExceptionDraft> a_entries,
			std::optional<size_t> a_ignoredIndex) const noexcept;
		[[nodiscard]] FacegenExceptionOperationResult SaveExceptions(
			std::span<const FacegenExceptionDraft> a_entries) noexcept;
		[[nodiscard]] FacegenExceptionOperationResult ReloadExceptions() noexcept;
	};

	FacegenExceptionStatus FacegenSystem::GetLoadOrderByFormID(
		const char* a_pluginName,
		uint32_t& a_formID,
		bool a_logFailure) const noexcept
	{
		__try
		{
			if (a_pluginName && a_pluginName[0])
			{
				auto* handler = dataHandler.load(std::memory_order_acquire);
				if (!handler)
					handler = RE::TESDataHandler::GetSingleton();
				if (!handler)
					return FacegenExceptionStatus::kDataNotReady;

				// Search among master, default plugins
				std::optional<uint16_t> id = handler->GetLoadedModIndex(a_pluginName);
				if (!id.has_value())
				{
					// Search among light master plugins
					id = handler->GetLoadedLightModIndex(a_pluginName);
					// If there is no such thing, then it is a waste of a stupid user's time
					if (!id.has_value())
					{
						if (a_logFailure)
						{
							REX::WARN(
								"[FACEGEN] Failed NPC added (no found plugin) \"{}\" (0x{:08X})"sv,
								a_pluginName,
								a_formID);
						}
						return FacegenExceptionStatus::kPluginNotFound;
					}

					a_formID = (a_formID & (0x00000FFF)) | (*id << 12) | 0xFE000000;
				}
				else
					a_formID = (a_formID & (0x00FFFFFF)) | (*id << 24);	
			}
			else
			{
				if (a_logFailure)
					REX::WARN("[FACEGEN] Failed NPC added (empty name plugin) (0x{:08X})"sv, a_formID);
				return FacegenExceptionStatus::kMissingPluginName;
			}

			return FacegenExceptionStatus::kResolved;
		}
		__except (1)
		{
			if (a_logFailure)
				REX::ERROR("[FACEGEN] Failed NPC added (fatal error) \"{}\" (0x{:08X})"sv, a_pluginName, a_formID);
			return FacegenExceptionStatus::kFatalError;
		}
	}

	void FacegenSystem::PublishExceptions(
		FacegenExceptionSet a_formIDs,
		FacegenExceptionSnapshot a_snapshot)
	{
		auto published =
			std::make_shared<const FacegenExceptionSet>(std::move(a_formIDs));
		facegenExceptionFormIDs.store(published, std::memory_order_release);
		const std::scoped_lock lock{ exceptionSnapshotMutex };
		a_snapshot.effectiveExceptionCount = published->size();
		a_snapshot.revision = ++exceptionRevision;
		exceptionSnapshot = std::move(a_snapshot);
	}

	void FacegenSystem::LoadExceptions(
		FacegenExceptionSnapshot& a_snapshot,
		FacegenExceptionSet& a_formIDs) const
	{
		a_snapshot.readAttempted = true;
		AddPrimaryExceptions(a_formIDs);

		CSimpleIniA ini;
		const SI_Error rc = ini.LoadFile(kFacegenExceptionsPath.data());
		if (rc != SI_OK)
		{
			REX::WARN("[FACEGEN] Can't find the exception file \"{}\""sv, kFacegenExceptionsPath);
			return;
		}
		a_snapshot.iniFound = true;

		// get all keys in a section
		auto Section = ini.GetSection("FacegenException");
		if (!Section)
		{
			REX::WARN(
				"[FACEGEN] Section \"FacegenException\" not found in \"{}\""sv,
				kFacegenExceptionsPath);
			return;
		}
		a_snapshot.sectionFound = true;
		for (auto& key : *Section)
		{
			FacegenExceptionRecord record;
			record.key = key.first.pItem ? key.first.pItem : "";
			record.rawValue = key.second ? key.second : "";

			uint32_t FormID = 0;
			std::string KeyValue = key.second ? key.second : "";
			PathUnquoteSpacesA(KeyValue.data());
			
			if (KeyValue.empty() || !KeyValue.length())
			{
				a_snapshot.entries.push_back(std::move(record));
				continue;
			}

			//REX::INFO("[DBG] KeyValue \"{}\"", KeyValue);

			auto parsed = ParseFacegenExceptionValue(KeyValue);
			record.pluginName = parsed.pluginName;
			if (parsed.pluginName.has_value())
			{
				if (parsed.pluginName->empty() || !parsed.pluginName->length())
				{
					REX::WARN("[FACEGEN] The plugin file was not specified \"{}\""sv, key.first.pItem);
					record.status = FacegenExceptionStatus::kMissingPluginName;
					a_snapshot.entries.push_back(std::move(record));
					continue;
				}

				//REX::INFO("[DBG] Value \"{}\"", parsed.formID);
				//REX::INFO("[DBG] PluginName \"{}\"", *parsed.pluginName);

				if (!TryParseFacegenFormID(parsed.formID, FormID))
				{
					record.status = FacegenExceptionStatus::kMalformedFormID;
					REX::WARN(
						"[FACEGEN] Failed NPC added (malformed FormID) \"{}\" ({})"sv,
						key.first.pItem,
						parsed.formID);
					a_snapshot.entries.push_back(std::move(record));
					continue;
				}
				record.status = GetLoadOrderByFormID(parsed.pluginName->c_str(), FormID, true);

				if (record.status == FacegenExceptionStatus::kResolved)
				{
					REX::INFO("[FACEGEN] Skip NPC added \"{}\" (0x{:08X})"sv, key.first.pItem, FormID);
					a_formIDs.insert(FormID);
					record.resolvedFormID = FormID;
				}
			}
			else
			{
				if (!TryParseFacegenFormID(parsed.formID, FormID))
				{
					record.status = FacegenExceptionStatus::kMalformedFormID;
					REX::WARN(
						"[FACEGEN] Failed NPC added (malformed FormID) \"{}\" ({})"sv,
						key.first.pItem,
						parsed.formID);
					a_snapshot.entries.push_back(std::move(record));
					continue;
				}

				REX::INFO("[FACEGEN] Skip NPC added \"{}\" (0x{:08X})"sv, key.first.pItem, FormID);
				a_formIDs.insert(FormID);
				record.resolvedFormID = FormID;
				record.status = FacegenExceptionStatus::kResolved;
			}
			a_snapshot.entries.push_back(std::move(record));
		}
	}

	void FacegenSystem::ReadExceptions() noexcept
	{
		const std::scoped_lock persistenceLock{ exceptionPersistenceMutex };
		FacegenExceptionSnapshot snapshot;
		FacegenExceptionSet formIDs;
		LoadExceptions(snapshot, formIDs);
		PublishExceptions(std::move(formIDs), std::move(snapshot));
	}

	bool FacegenSystem::Init() noexcept
	{
		FacegenExceptionSet formIDs;
		AddPrimaryExceptions(formIDs);
		FacegenExceptionSnapshot snapshot;
		PublishExceptions(std::move(formIDs), std::move(snapshot));

		if (!RELEX::IsRuntimeOG())
		{
			uintptr_t Offset = REL::ID(2209307).address() + 0x483;
			BSTextureDB::FacegenPathPrintf = REL::ID(2207434).address();
			BSTextureDB::CreateEntryID = REL::ID(2274880).address();

			// Remove useless stuff.
			RELEX::WriteSafeNop(Offset, 0x1C);
			RELEX::WriteSafeNop(Offset + 0x23, 0x36);

			// mov rcx, r13
			// lea rdx, qword ptr ss:[rbp-0x40]
			// mov r8d, 0x104
			// mov r9d, edi
			// cmp r9d, 0x2
			// mov eax, 0x7
			// cmove r9d, eax
			RELEX::WriteSafe(Offset + 0x2D, { 0x4C, 0x89, 0xE9, 0x48, 0x8D, 0x55, 0xC0, 0x41, 0xB8, 0x04, 0x01, 0x00, 0x00,
				0x41, 0x89, 0xF9, 0x41, 0x83, 0xF9, 0x02, 0xB8, 0x07, 0x00, 0x00, 0x00, 0x44, 0x0F, 0x44, 0xC8 });
			// call
			return 
				(RELEX::DetourCall(Offset + 0x4A, BSTextureDB::FacegenPathPrintf) != 0) &&
				(RELEX::DetourJump(REL::ID(2209308).address(), (uintptr_t)&CanUsePreprocessingHead) != 0);
		}
		else
		{
			uintptr_t Offset = REL::ID(1211128).address() + 0x350;
			BSTextureDB::FacegenPathPrintf = REL::ID(1464710).address();
			BSTextureDB::CreateEntryID = REL::ID(421736).address();

			// Remove useless stuff.
			RELEX::WriteSafeNop(Offset, 0x1F);
			RELEX::WriteSafeNop(Offset + 0x25, 0x37);

			// mov rcx, r13
			// lea rdx, qword ptr ss:[rbp-0x10]
			// mov r8d, 0x104
			// mov r9d, esi
			// cmp r9d, 0x2
			// mov eax, 0x7
			// cmove r9d, eax
			RELEX::WriteSafe(Offset + 0x30, { 0x4C, 0x89, 0xE9, 0x48, 0x8D, 0x55, 0xF0, 0x41, 0xB8, 0x04, 0x01, 0x00, 0x00,
				0x41, 0x89, 0xF1, 0x41, 0x83, 0xF9, 0x02, 0xB8, 0x07, 0x00, 0x00, 0x00, 0x44, 0x0F, 0x44, 0xC8 });
			// call
			return
				(RELEX::DetourCall(Offset + 0x4D, BSTextureDB::FacegenPathPrintf) != 0) &&
				(RELEX::DetourJump(REL::ID(969238).address(), (uintptr_t)&CanUsePreprocessingHead) != 0);
		}
	}

	bool FacegenSystem::InitContinue([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		keywordIsChildPlayer = REL::Relocation<RE::BGSKeyword*>{ REL::ID{ 533357, 2692125, 4799417 } }.get();
		dataHandler.store(RE::TESDataHandler::GetSingleton(), std::memory_order_release);

		ReadExceptions();

		return true;
	}

	bool FacegenSystem::NeedSkipNPC(const RE::TESNPC* a_NPC) const noexcept
	{
		if (!a_NPC) return false;
		// if template is specified, take face from template
		a_NPC = a_NPC->GetRootFaceNPC();
		// if the mod has set this option, i prohibit the use of preliminary data.
		if (a_NPC->IsPreset() || a_NPC->IsSimpleActor())
			return false;
		// check if the NPC is a relative or a template for the player.
		if (a_NPC->HasKeyword(keywordIsChildPlayer))
			return false;
		// optionally exclude some NPCs.
		const auto exceptions =
			facegenExceptionFormIDs.load(std::memory_order_acquire);
		if (exceptions && exceptions->contains(a_NPC->formID))
			return false;
		// player form can't have a facegen.
		if (a_NPC->formID == 0x7)
			return false;
		// Check if NPC face data been modified and marked for serialization into the save file with AddChange() function.
		auto* pSaveLoadGame = RE::BGSSaveLoadGame::GetSingleton();
		if (pSaveLoadGame)
		{
			if (pSaveLoadGame->GetChange(const_cast<RE::TESNPC*>(a_NPC), RE::BGSChangeFlags{ static_cast<int>(RE::CHANGE_TYPES::kNPCFace) }))
				return false;
		}
		// check exists diffuse texture.
		static char buf[REX::W32::MAX_PATH]{};
		bool result = BSTextureDB::FormatPath__And__ExistIn(a_NPC, buf, REX::W32::MAX_PATH, 0);
		if (!result && bAdditionalDbgFacegenOutput.GetValue())
		{
			auto fullName = a_NPC->GetFullName();
			if (!fullName) fullName = "<Unknown>";

			RE::ConsoleLog::GetSingleton()->Log("FACEGEN: NPC \"{}\" (0x{:08X}) don't have facegen"sv, fullName, a_NPC->formID);
			REX::WARN("NPC \"{}\" (0x{:08X}) don't have facegen"sv, fullName, a_NPC->formID);
		}

		// ConsoleLog::GetSingleton()->Log("FACEGEN: NPC \"{}\" (0x{:08X}) have facegen", a_NPC->GetFullName(), a_NPC->formID);

		return result;
	}

	FacegenExceptionValidation FacegenSystem::ValidateException(
		const FacegenExceptionDraft& a_entry,
		std::span<const FacegenExceptionDraft> a_entries,
		std::optional<size_t> a_ignoredIndex) const noexcept
	{
		const auto fields =
			ValidateFacegenExceptionFields(a_entry, a_entries, a_ignoredIndex);
		if (!fields.Valid())
		{
			return {
				false,
				fields.issue == FacegenExceptionValidationIssue::kMalformedFormID ?
					FacegenExceptionStatus::kMalformedFormID :
					FacegenExceptionStatus::kEmptyValue,
				std::nullopt,
				FieldValidationMessage(fields.issue)
			};
		}

		auto resolvedFormID = *fields.parsedFormID;
		if (!a_entry.pluginName)
		{
			return {
				true,
				FacegenExceptionStatus::kResolved,
				resolvedFormID,
				"Resolved runtime FormID."
			};
		}
		auto pluginName = *a_entry.pluginName;
		TrimFacegenExceptionField(pluginName);
		if (pluginName.empty())
		{
			return {
				false,
				FacegenExceptionStatus::kMissingPluginName,
				std::nullopt,
				"Plugin name is required after the separator."
			};
		}

		const auto status =
			GetLoadOrderByFormID(pluginName.c_str(), resolvedFormID, false);
		switch (status)
		{
		case FacegenExceptionStatus::kResolved:
			return {
				true,
				status,
				resolvedFormID,
				"Resolved runtime FormID."
			};
		case FacegenExceptionStatus::kPluginNotFound:
			return {
				false,
				status,
				std::nullopt,
				"Plugin not loaded: " + pluginName
			};
		case FacegenExceptionStatus::kDataNotReady:
			return {
				false,
				status,
				std::nullopt,
				"Game data is not ready for plugin validation."
			};
		case FacegenExceptionStatus::kMissingPluginName:
			return {
				false,
				status,
				std::nullopt,
				"Plugin name is required after the separator."
			};
		case FacegenExceptionStatus::kFatalError:
			return {
				false,
				status,
				std::nullopt,
				"Plugin load-order resolution failed."
			};
		case FacegenExceptionStatus::kEmptyValue:
		case FacegenExceptionStatus::kMalformedFormID:
			break;
		}
		return {
			false,
			status,
			std::nullopt,
			"Exception validation failed."
		};
	}

	FacegenExceptionOperationResult FacegenSystem::SaveExceptions(
		std::span<const FacegenExceptionDraft> a_entries) noexcept
	{
		try
		{
			const std::scoped_lock persistenceLock{ exceptionPersistenceMutex };
			std::vector<FacegenExceptionDraft> normalized;
			std::vector<FacegenExceptionValidation> validations;
			normalized.reserve(a_entries.size());
			validations.reserve(a_entries.size());
			for (const auto& entry : a_entries)
				normalized.push_back(NormalizeExceptionDraft(entry));
			for (size_t index = 0; index < normalized.size(); ++index)
			{
				auto validation =
					ValidateException(normalized[index], normalized, index);
				if (!validation.valid)
				{
					return {
						false,
						normalized[index].key.empty() ?
							validation.message :
							normalized[index].key + ": " + validation.message
					};
				}
				validations.push_back(std::move(validation));
			}

			std::ifstream existingFile{
				std::filesystem::path{ kFacegenExceptionsPath },
				std::ios::binary
			};
			if (!existingFile)
				return { false, "Could not read the exceptions INI." };
			const std::string existingContents{
				std::istreambuf_iterator<char>{ existingFile },
				std::istreambuf_iterator<char>{}
			};
			if (existingFile.bad())
				return { false, "Could not read the complete exceptions INI." };
			const auto leadingComments =
				ExtractFacegenExceptionLeadingComments(existingContents);

			CSimpleIniA ini;
			if (ini.LoadFile(kFacegenExceptionsPath.data()) != SI_OK)
				return { false, "Could not read the exceptions INI." };
			ini.SetSpaces(false);
			const auto* section = ini.GetSection("FacegenException");
			if (!section)
				return { false, "The exceptions INI has no [FacegenException] section." };

			std::vector<std::string> existingKeys;
			existingKeys.reserve(section->size());
			for (const auto& item : *section)
			{
				if (item.first.pItem)
					existingKeys.emplace_back(item.first.pItem);
			}
			for (const auto& key : existingKeys)
				(void)ini.Delete("FacegenException", key.c_str(), false);
			for (size_t index = 0; index < normalized.size(); ++index)
			{
				const auto& entry = normalized[index];
				const auto value =
					SerializeFacegenExceptionValue(entry.formID, entry.pluginName);
				if (ini.SetValue(
						"FacegenException",
						entry.key.c_str(),
						value.c_str(),
						index == 0 && !leadingComments.empty() ?
							leadingComments.c_str() :
							nullptr) < SI_OK)
					return { false, "Could not update the exceptions INI." };
			}

			constexpr std::string_view commentAnchor{
				"__AddictolFacegenCommentAnchor__"
			};
			if (normalized.empty() && !leadingComments.empty())
			{
				if (ini.SetValue(
						"FacegenException",
						commentAnchor.data(),
						"",
						leadingComments.c_str()) < SI_OK)
					return { false, "Could not preserve the exceptions INI comments." };
			}

			std::string contents;
			if (ini.Save(contents) < SI_OK)
				return { false, "Could not serialize the exceptions INI." };
			if (normalized.empty() && !leadingComments.empty())
			{
				const auto anchor = contents.find(commentAnchor);
				if (anchor == std::string::npos)
					return { false, "Could not preserve the exceptions INI comments." };
				const auto lineStart = contents.rfind('\n', anchor);
				const auto lineEnd = contents.find('\n', anchor);
				contents.erase(
					lineStart == std::string::npos ? 0 : lineStart + 1,
					lineEnd == std::string::npos ?
						std::string::npos :
						lineEnd - (lineStart == std::string::npos ? 0 : lineStart));
			}
			std::string writeError;
			if (!WriteFacegenExceptionsAtomically(
					std::filesystem::path{ kFacegenExceptionsPath },
					contents,
					writeError))
				return { false, std::move(writeError) };

			FacegenExceptionSet formIDs;
			AddPrimaryExceptions(formIDs);
			FacegenExceptionSnapshot snapshot;
			snapshot.readAttempted = true;
			snapshot.iniFound = true;
			snapshot.sectionFound = true;
			snapshot.entries.reserve(normalized.size());
			for (size_t index = 0; index < normalized.size(); ++index)
			{
				const auto& entry = normalized[index];
				const auto& validation = validations[index];
				FacegenExceptionRecord record;
				record.key = entry.key;
				record.rawValue =
					SerializeFacegenExceptionValue(entry.formID, entry.pluginName);
				record.pluginName = entry.pluginName;
				record.resolvedFormID = validation.resolvedFormID;
				record.status = validation.status;
				snapshot.entries.push_back(std::move(record));
				formIDs.insert(*validation.resolvedFormID);
			}
			PublishExceptions(std::move(formIDs), std::move(snapshot));
			return { true, {} };
		}
		catch (const std::exception& error)
		{
			return { false, error.what() };
		}
		catch (...)
		{
			return { false, "Unknown error while saving exceptions." };
		}
	}

	FacegenExceptionOperationResult FacegenSystem::ReloadExceptions() noexcept
	{
		try
		{
			const std::scoped_lock persistenceLock{ exceptionPersistenceMutex };
			FacegenExceptionSnapshot snapshot;
			FacegenExceptionSet formIDs;
			LoadExceptions(snapshot, formIDs);
			FacegenExceptionOperationResult result{ true, {} };
			if (!snapshot.iniFound)
				result = { false, "Could not read the exceptions INI." };
			else if (!snapshot.sectionFound)
				result = { false, "The exceptions INI has no [FacegenException] section." };
			PublishExceptions(std::move(formIDs), std::move(snapshot));
			return result;
		}
		catch (const std::exception& error)
		{
			return { false, error.what() };
		}
		catch (...)
		{
			return { false, "Unknown error while reloading exceptions." };
		}
	}

	FacegenExceptionSnapshot FacegenSystem::ExceptionSnapshot() const
	{
		const std::scoped_lock lock{ exceptionSnapshotMutex };
		return exceptionSnapshot;
	}

	FacegenExceptionSnapshot GetFacegenExceptionSnapshot()
	{
		return FacegenSystem::GetSingleton()->ExceptionSnapshot();
	}

	FacegenExceptionValidation ValidateFacegenException(
		const FacegenExceptionDraft& a_entry,
		std::span<const FacegenExceptionDraft> a_entries,
		std::optional<size_t> a_ignoredIndex) noexcept
	{
		return FacegenSystem::GetSingleton()->ValidateException(
			a_entry,
			a_entries,
			a_ignoredIndex);
	}

	FacegenExceptionOperationResult SaveFacegenExceptions(
		std::span<const FacegenExceptionDraft> a_entries) noexcept
	{
		return FacegenSystem::GetSingleton()->SaveExceptions(a_entries);
	}

	FacegenExceptionOperationResult ReloadFacegenExceptions() noexcept
	{
		return FacegenSystem::GetSingleton()->ReloadExceptions();
	}

	static bool __stdcall CanUsePreprocessingHead(const RE::TESNPC* NPC) noexcept
	{
		return FacegenSystem::GetSingleton()->NeedSkipNPC(NPC);
	}

	ModuleFacegen::ModuleFacegen() :
		Module("Facegen", &bPatchesFacegen)
	{}

	bool ModuleFacegen::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!a_msg)
			return FacegenSystem::GetSingleton()->Init();
		else if (a_msg->type == F4SE::MessagingInterface::kGameDataReady)
		{
			FacegenSystem::GetSingleton()->InitContinue(a_msg);
			return true;
		}

		return false;
	}

	bool ModuleFacegen::HasProcessDefender() noexcept
	{
		return true;
	}
}