#include <Modules/AdModuleLeveledListCrash.h>
#include <Core/AdUtils.h>

#include <RE/T/TESForm.h>
#include <RE/T/TESLeveledList.h>

namespace Addictol
{
	typedef RE::LEVELED_OBJECT* (AddLeveledObject_Signature)(RE::TESLeveledList*, std::uint16_t, std::uint16_t, std::int8_t, RE::TESForm*,
		RE::ContainerItemExtra*);
	REL::Relocation<AddLeveledObject_Signature> AddLeveledObject_Original;

	// it's worth noting that this may be susceptible to the same issue as LeveledListEntryCount where
	// entryCount is inaccurate sometimes, so we should keep an eye on it
	static RE::LEVELED_OBJECT* AddLeveledObject_Hook(RE::TESLeveledList* a_this, std::uint16_t a_level, std::uint16_t a_count,
		std::int8_t a_chanceNone, RE::TESForm* a_item, RE::ContainerItemExtra* a_itemExtra)
	{
		if (!a_this)
			return nullptr;

		if (a_this->baseListCount >= 255)
		{
			// warn
			/*auto* formFile = a_form->GetFile(0);
			REX::INFO("LeveledListCrash: Prevented problematic injection of <FormID: {:08X} in Plugin: \"{}\">"sv,
				a_form->GetFormID(), formFile ? formFile->GetFilename() : "MODNAME_NOT_FOUND"sv);*/

			REX::INFO("LeveledListCrash: Prevented a problematic injection."sv);
			return nullptr;
		}
		else
		{
			// return original function
			return AddLeveledObject_Original(a_this, a_level, a_count, a_chanceNone, a_item, a_itemExtra);
		}
	}

	ModuleLeveledListCrash::ModuleLeveledListCrash() :
		Module("Leveled List Crash", &bLeveledListCrash)
	{}

	bool ModuleLeveledListCrash::DoQuery() const noexcept
	{
		if (IsModDLLPresent("GLXRM_InjectionBlocker.dll"))
		{
			Skip("standalone 'GLXRM_InjectionBlocker.dll' is installed"sv);
			return false;
		}

		return true;
	}

	bool ModuleLeveledListCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation target{ REL::ID{ 860553, 2193269 }, REL::Offset{ 0x6C, 0x6D } };
		AddLeveledObject_Original = target.write_call<5>(AddLeveledObject_Hook);

		return true;
	}
}
