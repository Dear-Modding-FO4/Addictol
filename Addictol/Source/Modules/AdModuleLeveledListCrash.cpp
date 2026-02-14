#include <Modules\AdModuleLeveledListCrash.h>
#include <AdUtils.h>

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

		std::uint32_t entryCount = a_this->baseListCount + a_this->scriptListCount;
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
		return true;
	}

	bool ModuleLeveledListCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (REX::W32::GetModuleHandleW(L"GLXRM_InjectionBlocker.dll"))
			// compatibility with geluxrum's injection blocker
			return false;

		REL::Relocation target{ REL::ID{ 860553, 2193269 }, REL::Offset{ 0x6C, 0x6D } };
		AddScriptAddedLeveledObject_Original = target.write_call<5>(AddScriptAddedLeveledObject_Hook);

		return true;
	}

	bool ModuleLeveledListCrash::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleLeveledListCrash::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
