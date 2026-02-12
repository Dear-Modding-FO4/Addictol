#include <Modules\AdModuleLeveledListCrash.h>
#include <AdUtils.h>

#include <RE/T/TESLeveledList.h>

// NOTE: WIP

namespace Addictol
{
	static REX::TOML::Bool<> bLeveledListCrash{"Fixes", "bLeveledListCrash", true};

	ModuleLeveledListCrash::ModuleLeveledListCrash() :
		Module("Leveled List Crash", &bLeveledListCrash)
	{}

	bool ModuleLeveledListCrash::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleLeveledListCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message *a_msg) noexcept
	{
		if (REX::W32::GetModuleHandleW(L"GLXRM_InjectionBlocker.dll"))
		{
			// compatibility with geluxrum's injection blocker
			return false;
		}

		// if (RELEX::IsRuntimeOG())
		// {
		// 	// og patch
		// 	REL::Relocation<std::uintptr_t> target{REL::ID(860553), 0x6C};
		// 	AddScriptAddedLeveledObject_Original = target.write_call<5>(AddScriptAddedLeveledObject_Hook);
		// }
		// else if (RELEX::IsRuntimeNG())
		// {
		// 	// ng patch
		// 	REL::Relocation<std::uintptr_t> target(REL::ID(2193269), 0x6D);
		// 	AddScriptAddedLeveledObject_Original = target.write_call<5>(AddScriptAddedLeveledObject_Hook);
		// }
		// else if (RELEX::IsRuntimeAE())
		// {
		// 	// ae patch (same as ng)
		// 	REL::Relocation<std::uintptr_t> target(REL::ID(2193269), 0x6D);
		// 	AddScriptAddedLeveledObject_Original = target.write_call<5>(AddScriptAddedLeveledObject_Hook);
		// }

		return true;
	}

	bool ModuleLeveledListCrash::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message *a_msg) noexcept
	{
		return true;
	}

	bool ModuleLeveledListCrash::DoPapyrusListener(RE::BSScript::IVirtualMachine *a_vm) noexcept
	{
		return true;
	}

	// static void AddScriptAddedLeveledObject_Hook(RE::TESLeveledList *a_this, RE::TESForm *a_owner, uint16_t a_level, uint16_t a_count, RE::TESForm *a_form)
	// {
	// 	if (!a_this)
	// 	{
	// 		return;
	// 	}

	// 	std::uint32_t entryCount = a_this->baseListCount + a_this->scriptListCount;
	// 	if (entryCount > 254)
	// 	{
	// 		// log warning (todo)
	// 		return;
	// 	}
	// 	else
	// 	{
	// 		// return original function
	// 		return AddScriptAddedLeveledObject_Original(a_this, a_owner, a_level, a_count, a_form);
	// 	}
	// }
}
