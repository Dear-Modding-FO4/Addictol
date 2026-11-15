#include <Modules/AdModuleFixBrokenAudioDataSrc.h>
#include <Core/AdUtils.h>
#include <REL/REL.h>
#include <RE/IDs.h>

#include <array>

namespace Addictol
{
	ModuleFixBrokenAudioDataSrc::ModuleFixBrokenAudioDataSrc() :
		Module("Fix Broken AudioDataSrc", &bFixesBrokenAudioDataSrc)
	{}

	bool ModuleFixBrokenAudioDataSrc::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// cmp r14d, eax; jbe (OG) / cmp r15d, eax; jbe (NG/AE)
		static constexpr std::initializer_list<uint8_t> s_syncCompareOG{ 0x44, 0x3B, 0xF0, 0x0F, 0x86 };
		static constexpr std::initializer_list<uint8_t> s_syncCompare{ 0x44, 0x3B, 0xF8, 0x0F, 0x86 };
		// cmp ebp, eax; jbe
		static constexpr std::initializer_list<uint8_t> s_asyncCompare{ 0x3B, 0xE8, 0x0F, 0x86 };

		struct Site
		{
			uintptr_t compare;
			std::initializer_list<uint8_t> bytes;
		};

		// An overclaiming data chunk wraps uint32 avail to ~4 GB; compare signed.
		const std::array<Site, 2> sites
		{
			Site{ REL::Relocation<uintptr_t>{ RE::ID::BSXAudio2DataSrc::DoQueuedSyncOpen, REL::Offset{ 0x215, 0x270 } }.address(),
				RELEX::IsRuntimeOG() ? s_syncCompareOG : s_syncCompare },
			Site{ REL::Relocation<uintptr_t>{ RE::ID::BSXAudio2DataSrc::DoTestAsyncOpenReady, REL::Offset{ 0x2A7, 0x348 } }.address(),
				s_asyncCompare },
		};

		for (const auto& site : sites)
		{
			if (!RELEX::Validate(site.compare, site.bytes))
			{
				Skip("a trailing chunk size check does not match; unsupported runtime or another plugin patched it"sv);
				return false;
			}
		}

		// jbe -> jle
		for (const auto& site : sites)
			RELEX::WriteSafe(site.compare + site.bytes.size() - 1, { 0x8E });
		return true;
	}
}
