#include <Modules\AdModuleFacegen.h>
#include <AdPlugin.h>
#include <AdUtils.h>
#include <Shlwapi.h>

// SimpleIni include windows.h
#include <INI\SimpleIni.h>
#undef MEM_RELEASE
#undef MAX_PATH
#undef ERROR

#include <RE\C\ConsoleLog.h>
#include <RE\B\BSResource_ID.h>
#include <RE\T\TESNPC.h>
#include <RE\T\TESDataHandler.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPathesFacegen{ "Patches", "bFacegen", true };
	static REX::TOML::Bool<> bAdditionalDbgFacegenOutput{ "Additional", "bDbgFacegenOutput", false };

	static bool __stdcall CanUsePreprocessingHead(RE::TESNPC* NPC) noexcept;

	namespace BSTextureDB
	{
		// Working buried function.
		static uintptr_t FacegenPathPrintf{ 0 };
		static uintptr_t CreateEntryID{ 0 };

		static bool __stdcall FormatPath__And__ExistIn(RE::TESNPC* a_NPC, const char* a_destPath,
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
		std::vector<uint32_t> facegenPrimaryExceptionFormIDs =
		{
			// Since it has no protection in the game

			0x26F0A,	// MQ102PlayerSpouseCorpseMale
			0x26F36,	// MQ102PlayerSpouseCorpseFemale
			0xA7D34,	// MQ101PlayerSpouseMale
			0xA7D35,	// MQ101PlayerSpouseFemale
			0x246BF0,	// MQ101PlayerSpouseMale_NameOnly
			0x246BF1,	// MQ101PlayerSpouseFemale_NameOnly
		};
		std::vector<uint32_t> facegenExceptionFormIDs;
		RE::TESDataHandler* dataHandler{ nullptr };

		FacegenSystem(const FacegenSystem&) = delete;
		FacegenSystem operator=(const FacegenSystem&) = delete;

		[[nodiscard]] bool GetLoadOrderByFormID(const char* a_pluginName, uint32_t& a_formID) const noexcept;
		void ReadExceptions() noexcept;
	public:
		FacegenSystem() = default;
		~FacegenSystem() = default;

		bool Init() noexcept;
		bool InitContinue([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept;
		bool NeedSkipNPC(RE::TESNPC* a_NPC) noexcept;
	};

	bool FacegenSystem::GetLoadOrderByFormID(const char* a_pluginName, uint32_t& a_formID) const noexcept
	{
		constexpr static uint16_t INVALID_INDEX = std::numeric_limits<uint16_t>::max();

		__try
		{
			if (a_pluginName && a_pluginName[0])
			{
				// Search among master, default plugins
				std::optional<uint16_t> id = dataHandler->GetLoadedModIndex(a_pluginName);
				if (!id)
				{
					// Search among light master plugins
					id = dataHandler->GetLoadedLightModIndex(a_pluginName);
					// If there is no such thing, then it is a waste of a stupid user's time
					if (!id)
					{
						REX::WARN("[FACEGEN] Failed NPC added (no found plugin) \"{}\" (0x{:08X})", a_pluginName, a_formID);
						return false;
					}

					a_formID = (a_formID & (0x00000FFF)) | (*id << 12) | 0xFE000000;
				}
				else
					a_formID = (a_formID & (0x00FFFFFF)) | (*id << 24);	
			}
			else
			{
				REX::WARN("[FACEGEN] Failed NPC added (empty name plugin) (0x{:08X})", a_formID);
				return false;
			}

			return true;
		}
		__except (1)
		{
			REX::ERROR("[FACEGEN] Failed NPC added (fatal error) \"{}\" (0x{:08X})", a_pluginName, a_formID);
			return false;
		}
	}

	void FacegenSystem::ReadExceptions() noexcept
	{
		constexpr static auto FILE_NAME = "Data\\F4SE\\Plugins\\" _PluginName "_FacegenExceptions.ini";

		CSimpleIniA ini;
		SI_Error rc = ini.LoadFile(FILE_NAME);
		if (rc != SI_OK)
		{
			REX::WARN("[FACEGEN] Can't find the exception file \"{}\"", FILE_NAME);
			return;
		}

		// get all keys in a section
		auto Section = ini.GetSection("FacegenException");
		for (auto& key : *Section)
		{
			PathUnquoteSpacesA(const_cast<char*>(key.second));

			uint32_t FormID = 0;
			std::string KeyValue = key.second;
			
			if (KeyValue.empty() || !KeyValue.length())
				continue;

			//REX::INFO("[DBG] KeyValue \"{}\"", KeyValue);

			auto It = KeyValue.find_first_of(':');
			if (It != std::string::npos)
			{
				std::string PluginName = KeyValue.substr(It + 1);
				std::string Value = KeyValue.substr(0, It);
				Trim(PluginName);
				Trim(Value);

				if (PluginName.empty() || !PluginName.length())
				{
					REX::WARN("[FACEGEN] The plugin file was not specified \"{}\"", key.first.pItem);
					continue;
				}

				//REX::INFO("[DBG] Value \"{}\"", Value);
				//REX::INFO("[DBG] PluginName \"{}\"", PluginName);

				if (Value.find_first_of("0x") == 0)
					FormID = strtoul(Value.c_str() + 2, nullptr, 16);
				else
					FormID = strtoul(Value.c_str(), nullptr, 10);

				if (GetLoadOrderByFormID(PluginName.c_str(), FormID))
				{
					REX::INFO("[FACEGEN] Skip NPC added \"{}\" (0x{:08X})", key.first.pItem, FormID);
					facegenExceptionFormIDs.emplace_back(FormID);
				}
			}
			else
			{
				if (KeyValue.find_first_of("0x") == 0)
					FormID = strtoul(KeyValue.c_str() + 2, nullptr, 16);
				else
					FormID = strtoul(KeyValue.c_str(), nullptr, 10);

				REX::INFO("[FACEGEN] Skip NPC added \"{}\" (0x{:08X})", key.first.pItem, FormID);
				facegenExceptionFormIDs.emplace_back(FormID);
			}
		}
	}

	bool FacegenSystem::Init() noexcept
	{
		facegenExceptionFormIDs = facegenPrimaryExceptionFormIDs;

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
			RELEX::DetourCall(Offset + 0x4A, BSTextureDB::FacegenPathPrintf);
			RELEX::DetourJump(REL::ID(2209308).address(), (uintptr_t)&CanUsePreprocessingHead);
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
			RELEX::DetourCall(Offset + 0x4D, BSTextureDB::FacegenPathPrintf);
			RELEX::DetourJump(REL::ID(969238).address(), (uintptr_t)&CanUsePreprocessingHead);
		}

		return true;
	}

	bool FacegenSystem::InitContinue([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		keywordIsChildPlayer = RELEX::GetTSingletonByID<RE::BGSKeyword>(4799417, 2692125, 533357);
		dataHandler = RE::TESDataHandler::GetSingleton();

		ReadExceptions();

		return true;
	}

	bool FacegenSystem::NeedSkipNPC(RE::TESNPC* a_NPC) noexcept
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
		for (auto it_except : facegenExceptionFormIDs)
			if (a_NPC->formID == it_except)
				return false;
		// player form can't have a facegen.
		if (a_NPC->formID == 0x7)
			return false;
		// check exists diffuse texture.
		static char buf[REX::W32::MAX_PATH]{};
		bool result = BSTextureDB::FormatPath__And__ExistIn(a_NPC, buf, REX::W32::MAX_PATH, 0);
		if (!result && bAdditionalDbgFacegenOutput.GetValue())
		{
			auto fullName = a_NPC->GetFullName();
			if (!fullName) fullName = "<Unknown>";

			RE::ConsoleLog::GetSingleton()->Log("FACEGEN: NPC \"{}\" (0x{:08X}) don't have facegen", fullName, a_NPC->formID);
			REX::WARN("NPC \"{}\" (0x{:08X}) don't have facegen", fullName, a_NPC->formID);
		}

		// ConsoleLog::GetSingleton()->Log("FACEGEN: NPC \"{}\" (0x{:08X}) have facegen", a_NPC->GetFullName(), a_NPC->formID);

		return result;
	}

	static bool __stdcall CanUsePreprocessingHead(RE::TESNPC* NPC) noexcept
	{
		return FacegenSystem::GetSingleton()->NeedSkipNPC(NPC);
	}

	ModuleFacegen::ModuleFacegen() :
		Module("Module Facegen", &bPathesFacegen)
	{}

	bool ModuleFacegen::DoQuery() const noexcept
	{
		return true;
	}

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

	bool ModuleFacegen::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}
}