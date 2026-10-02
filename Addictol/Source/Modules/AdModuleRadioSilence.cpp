#include <Modules/AdModuleRadioSilence.h>
#include <Core/AdUtils.h>

namespace Addictol
{
	ModuleRadioSilence::ModuleRadioSilence() :
		Module("Experimental Radio Silence", &bFixesRadioSilence)
	{}

	bool ModuleRadioSilence::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		RELEX::WriteSafeNop(REL::Relocation<uintptr_t>{ REL::ID{ 67337, 2196837 }, REL::Offset{ 0x233, 0x2CF } }, 0x12);
		return true;
	}
}