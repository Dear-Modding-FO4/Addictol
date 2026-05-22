#include <Modules/AdModuleAnimSignedCrash.h>
#include <AdUtils.h>
#include <REL/REL.h>
#include <RE/IDs.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesAnimSignedCrash{ "Fixes"sv, "bAnimSignedCrash"sv, true };

	ModuleAnimSignedCrash::ModuleAnimSignedCrash() :
		Module("Anim Signed Crash", &bFixesAnimSignedCrash)
	{}

	bool ModuleAnimSignedCrash::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleAnimSignedCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// hkbBehaviorGraph::processEventlessGlobalTransitions, movsx->movzx on the 16 - bit event - id read.

		if (!RELEX::IsRuntimeOG())
			// NG/AE
			RELEX::WriteSafe(REL::ID(2260478).address() + 0x8E, { 0xB7 });
		else
			// OG
			RELEX::WriteSafe(REL::ID(919820).address() + 0x8B, { 0xB7 });

		return true;
	}

	bool ModuleAnimSignedCrash::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleAnimSignedCrash::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
