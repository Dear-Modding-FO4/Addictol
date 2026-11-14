#include <Modules/AdModuleFixBrokenAudioDataSrc.h>
#include <Core/AdUtils.h>
#include <REL/REL.h>
#include <RE/IDs.h>

namespace Addictol
{
	ModuleFixBrokenAudioDataSrc::ModuleFixBrokenAudioDataSrc() :
		Module("Fix Broken AudioDataSrc", &bFixesBrokenAudioDataSrc)
	{}

	bool ModuleFixBrokenAudioDataSrc::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// When reading audio in an archive larger than 4GB, overflow occurs and the negative 
		// value of available is required for reading, we block this.
		//
		// This will result in sound loss.

		auto relOff = REL::Relocation(REL::ID{ 1079185, 2267599 });
		auto off = RELEX::IsRuntimeAE() ? 0x34A : 0x2A9;

		if (!RELEX::Validate(relOff.address() + off, { 0x0F, 0x86 }))
		{
			REX::WARN("Module ModuleFixBrokenAudioDataSrc return failed, no match validate code"sv);
			return false;
		}

		auto relOff2 = REL::Relocation(REL::ID{ 363610, 2267597 });
		auto off2 = RELEX::IsRuntimeAE() ? 0x273 : 218;

		if (!RELEX::Validate(relOff.address() + off, { 0x0F, 0x86 }))
		{
			REX::WARN("Module ModuleFixBrokenAudioDataSrc return failed, no match validate code"sv);
			return false;
		}

		// sets as jle (so check sign, if more 2Gb then skips) 
		RELEX::WriteSafe(relOff.address() + off + 1, { 0x8E });
		RELEX::WriteSafe(relOff2.address() + off2 + 1, { 0x8E });

		// TODO: We can probably read the sound, but we don't know its exact size, so it's worth considering.

		return true;
	}
}