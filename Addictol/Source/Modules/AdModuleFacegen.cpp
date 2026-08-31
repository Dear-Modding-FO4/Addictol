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

	class FacegenSystem :
		public REX::TSingleton<FacegenSystem>
	{
		RE::BGSKeyword* keywordIsChildPlayer{ nullptr };
		std::unordered_set<uint32_t> facegenExceptionFormIDs;
		RE::TESDataHandler* dataHandler{ nullptr };
		mutable std::mutex exceptionSnapshotMutex;
		FacegenExceptionSnapshot exceptionSnapshot;

		FacegenSystem(const FacegenSystem&) = delete;
		FacegenSystem operator=(const FacegenSystem&) = delete;

		[[nodiscard]] FacegenExceptionStatus GetLoadOrderByFormID(
			const char* a_pluginName,
			uint32_t& a_formID) const noexcept;
		void PublishExceptionSnapshot(FacegenExceptionSnapshot a_snapshot);
		void ReadExceptions() noexcept;
	public:
		FacegenSystem() = default;
		~FacegenSystem() = default;

		bool Init() noexcept;
		bool InitContinue([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept;
		bool NeedSkipNPC(const RE::TESNPC* a_NPC) const noexcept;
		[[nodiscard]] FacegenExceptionSnapshot ExceptionSnapshot() const;
	};

	FacegenExceptionStatus FacegenSystem::GetLoadOrderByFormID(
		const char* a_pluginName,
		uint32_t& a_formID) const noexcept
	{
		__try
		{
			if (a_pluginName && a_pluginName[0])
			{
				// Search among master, default plugins
				std::optional<uint16_t> id = dataHandler->GetLoadedModIndex(a_pluginName);
				if (!id.has_value())
				{
					// Search among light master plugins
					id = dataHandler->GetLoadedLightModIndex(a_pluginName);
					// If there is no such thing, then it is a waste of a stupid user's time
					if (!id.has_value())
					{
						REX::WARN("[FACEGEN] Failed NPC added (no found plugin) \"{}\" (0x{:08X})"sv, a_pluginName, a_formID);
						return FacegenExceptionStatus::kPluginNotFound;
					}

					a_formID = (a_formID & (0x00000FFF)) | (*id << 12) | 0xFE000000;
				}
				else
					a_formID = (a_formID & (0x00FFFFFF)) | (*id << 24);	
			}
			else
			{
				REX::WARN("[FACEGEN] Failed NPC added (empty name plugin) (0x{:08X})"sv, a_formID);
				return FacegenExceptionStatus::kMissingPluginName;
			}

			return FacegenExceptionStatus::kResolved;
		}
		__except (1)
		{
			REX::ERROR("[FACEGEN] Failed NPC added (fatal error) \"{}\" (0x{:08X})"sv, a_pluginName, a_formID);
			return FacegenExceptionStatus::kFatalError;
		}
	}

	void FacegenSystem::PublishExceptionSnapshot(FacegenExceptionSnapshot a_snapshot)
	{
		const std::scoped_lock lock{ exceptionSnapshotMutex };
		exceptionSnapshot = std::move(a_snapshot);
	}

	void FacegenSystem::ReadExceptions() noexcept
	{
		constexpr static auto FILE_NAME = "Data\\F4SE\\Plugins\\" _PluginName "_FacegenExceptions.ini";
		FacegenExceptionSnapshot snapshot;
		snapshot.readAttempted = true;

		CSimpleIniA ini;
		SI_Error rc = ini.LoadFile(FILE_NAME);
		if (rc != SI_OK)
		{
			REX::WARN("[FACEGEN] Can't find the exception file \"{}\""sv, FILE_NAME);
			snapshot.effectiveExceptionCount = facegenExceptionFormIDs.size();
			PublishExceptionSnapshot(std::move(snapshot));
			return;
		}
		snapshot.iniFound = true;

		// get all keys in a section
		auto Section = ini.GetSection("FacegenException");
		if (!Section)
		{
			REX::WARN("[FACEGEN] Section \"FacegenException\" not found in \"{}\""sv, FILE_NAME);
			snapshot.effectiveExceptionCount = facegenExceptionFormIDs.size();
			PublishExceptionSnapshot(std::move(snapshot));
			return;
		}
		snapshot.sectionFound = true;
		for (auto& key : *Section)
		{
			FacegenExceptionRecord record;
			record.key = key.first.pItem ? key.first.pItem : "";
			record.rawValue = key.second ? key.second : "";
			PathUnquoteSpacesA(const_cast<char*>(key.second));

			uint32_t FormID = 0;
			std::string KeyValue = key.second;
			
			if (KeyValue.empty() || !KeyValue.length())
			{
				snapshot.entries.push_back(std::move(record));
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
					snapshot.entries.push_back(std::move(record));
					continue;
				}

				//REX::INFO("[DBG] Value \"{}\"", parsed.formID);
				//REX::INFO("[DBG] PluginName \"{}\"", *parsed.pluginName);

				FormID = ParseFacegenFormID(parsed.formID);
				record.status = GetLoadOrderByFormID(parsed.pluginName->c_str(), FormID);

				if (record.status == FacegenExceptionStatus::kResolved)
				{
					REX::INFO("[FACEGEN] Skip NPC added \"{}\" (0x{:08X})"sv, key.first.pItem, FormID);
					facegenExceptionFormIDs.insert(FormID);
					record.resolvedFormID = FormID;
				}
			}
			else
			{
				FormID = ParseFacegenFormID(parsed.formID);

				REX::INFO("[FACEGEN] Skip NPC added \"{}\" (0x{:08X})"sv, key.first.pItem, FormID);
				facegenExceptionFormIDs.insert(FormID);
				record.resolvedFormID = FormID;
				record.status = FacegenExceptionStatus::kResolved;
			}
			snapshot.entries.push_back(std::move(record));
		}
		snapshot.effectiveExceptionCount = facegenExceptionFormIDs.size();
		PublishExceptionSnapshot(std::move(snapshot));
	}

	bool FacegenSystem::Init() noexcept
	{
		facegenExceptionFormIDs.clear();
		for (const auto& exception : kFacegenPrimaryExceptions)
			facegenExceptionFormIDs.insert(exception.formID);
		FacegenExceptionSnapshot snapshot;
		snapshot.effectiveExceptionCount = facegenExceptionFormIDs.size();
		PublishExceptionSnapshot(std::move(snapshot));

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
		dataHandler = RE::TESDataHandler::GetSingleton();

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
		if (facegenExceptionFormIDs.contains(a_NPC->formID))
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

	FacegenExceptionSnapshot FacegenSystem::ExceptionSnapshot() const
	{
		const std::scoped_lock lock{ exceptionSnapshotMutex };
		return exceptionSnapshot;
	}

	FacegenExceptionSnapshot GetFacegenExceptionSnapshot()
	{
		return FacegenSystem::GetSingleton()->ExceptionSnapshot();
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