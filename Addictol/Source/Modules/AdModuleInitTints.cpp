#include <Modules/AdModuleInitTints.h>
#include <AdUtils.h>
#include <REL/REL.h>
#include <RE/IDs.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesInitTints{ "Fixes"sv, "bInitTints"sv, true };

	ModuleInitTints::ModuleInitTints() :
		Module("Init Tints", &bFixesInitTints)
	{}

	bool ModuleInitTints::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleInitTints::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!RELEX::IsRuntimeOG())
		{
			// NG/AE

			// Removing all checks that block the load for tints by 0x0 plugin and the IsChargenPresent flag.
			auto Target = REL::ID(2207355).address();
			RELEX::WriteSafeNop(Target + 0x654, 0x35);
			RELEX::WriteSafeNop(Target + 0x89B, 0x35);

			// This function check load CCC plugins
			// Causes error sometimes load game
			//if (RELEX::IsRuntimeNG())
			//	RELEX::WriteSafeNop(REL::ID(2192358).address() + 0x44, 0x19);
			//else
			//	RELEX::WriteSafeNop(REL::ID(2192358).address() + 0x47, 0x3B);

			// Skipping the entire section, can't delete the pointer, otherwise will go back to where started.
			RELEX::WriteSafe(REL::ID(2207492).address() + 0xF3, { 0xE9, 0x7D, 0x00, 0x00, 0x00, 0x90 });
		}
		else
		{
			// OG

			// Removing all checks that block the load for tints by 0x0 plugin and the IsChargenPresent flag.
			auto Target = REL::ID(588473).address();		
			RELEX::WriteSafeNop(Target + 0x63D, 0x35);
			RELEX::WriteSafeNop(Target + 0x850, 0x35);

			// This function check load CCC plugins
			// Causes error sometimes load game
			//RELEX::WriteSafeNop(REL::ID(779416).address() + 0x21, 0x19);		
			//RELEX::WriteSafeNop(REL::ID(1444263).address() + 0x54, 0x19);

			// Skipping the entire section, can't delete the pointer, otherwise will go back to where started.
			RELEX::WriteSafe(REL::ID(1264833).address() + 0xF9, { 0xE9, 0x9C, 0x00, 0x00, 0x00, 0x90 });
		}

		return true;
	}

	bool ModuleInitTints::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleInitTints::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}