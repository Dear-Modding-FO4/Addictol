#include <Modules/AdModuleLeveledListCrash.h>
#include <Core/AdUtils.h>

#include <RE/T/TESForm.h>
#include <RE/T/TESLeveledList.h>

namespace Addictol
{
	static REX::TOML::Bool<> bLeveledListCrash{ "Fixes"sv, "bLeveledListCrash"sv, true };

	typedef void(AddScriptAddedLeveledObject_Signature)(RE::TESLeveledList*, RE::TESForm*, uint16_t, uint16_t, RE::TESForm*);
	REL::Relocation<AddScriptAddedLeveledObject_Signature> AddScriptAddedLeveledObject_Original;

	// it's worth noting that this may be susceptible to the same issue as LeveledListEntryCount where
	// entryCount is inaccurate sometimes, so we should keep an eye on it
	static void AddScriptAddedLeveledObject_Hook(RE::TESLeveledList* a_this, RE::TESForm* a_owner, uint16_t a_level,
		uint16_t a_count, RE::TESForm* a_form)
	{
		if (!a_this)
			return;

		// baseListCount can sometimes be -128, clamped to prevent a large uint32_t and a false trigger..
		std::uint32_t entryCount = std::max<int8_t>(a_this->baseListCount, 0) + a_this->scriptListCount;
		if (entryCount > 254)
		{
			// warn
			/*auto* formFile = a_form->GetFile(0);
			REX::INFO("LeveledListCrash: Prevented problematic injection of <FormID: {:08X} in Plugin: \"{}\">"sv,
				a_form->GetFormID(), formFile ? formFile->GetFilename() : "MODNAME_NOT_FOUND"sv);*/

			REX::INFO("LeveledListCrash: Prevented a problematic injection."sv);
			return;
		}
		else
		{
			// return original function
			return AddScriptAddedLeveledObject_Original(a_this, a_owner, a_level, a_count, a_form);
		}
	}

	ModuleLeveledListCrash::ModuleLeveledListCrash() :
		Module("Leveled List Crash", &bLeveledListCrash)
	{}

	bool ModuleLeveledListCrash::DoQuery() const noexcept
	{
		if (IsModDLLPresent("GLXRM_InjectionBlocker.dll"))
		{
			Skip("Standalone 'GLXRM_InjectionBlocker.dll' is installed, skipping module"sv);
			return false;
		}

		return true;
	}

	bool ModuleLeveledListCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation target{ REL::ID{ 860553, 2193269 }, REL::Offset{ 0x6C, 0x6D } };
		AddScriptAddedLeveledObject_Original = target.write_call<5>(AddScriptAddedLeveledObject_Hook);

		return true;
	}

}
