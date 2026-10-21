#include <Modules/AdModuleAnimSignedCrash.h>
#include <Core/AdUtils.h>
#include <REL/REL.h>
#include <RE/IDs.h>

namespace Addictol
{
	ModuleAnimSignedCrash::ModuleAnimSignedCrash() :
		Module("Anim Signed Crash", &bFixesAnimSignedCrash)
	{}

	bool ModuleAnimSignedCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// hkbBehaviorGraph::processEventlessGlobalTransitions, movsx->movzx on the 16 - bit event - id read.

		const auto target = REL::Relocation{ RE::ID::hkbBehaviorGraph::processEventlessGlobalTransitions, REL::Offset{ 0x8B, 0x8E } }.address();

		if (!RELEX::Validate(target, { 0xBF }))
			return false;

		RELEX::WriteSafe(target, { 0xB7 });

		return true;
	}
}
