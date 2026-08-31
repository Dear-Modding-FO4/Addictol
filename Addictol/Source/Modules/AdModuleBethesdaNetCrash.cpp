#include <Modules/AdModuleBethesdaNetCrash.h>
#include <Core/AdUtils.h>

#include <array>
#include <Windows.h>

namespace Addictol
{

	static errno_t __cdecl Hook_wcsrtombs_s(
		std::size_t*                     pReturnValue,
		char*                            mbstr,
		std::size_t                      sizeInBytes,
		const wchar_t**                  wcstr,
		std::size_t                      count,
		[[maybe_unused]] std::mbstate_t* mbstate) noexcept
	{
		if (!wcstr || !*wcstr)
		{
			if (pReturnValue) *pReturnValue = 0;
			if (mbstr && sizeInBytes > 0) mbstr[0] = '\0';
			return 0;
		}

		const wchar_t* src = *wcstr;
		int srcLen = -1;
		if (count != static_cast<std::size_t>(-1))
		{
			std::size_t n = 0;
			while (n < count && src[n] != L'\0') ++n;
			srcLen = static_cast<int>(n);
		}

		const int needed = WideCharToMultiByte(CP_UTF8, 0, src, srcLen, nullptr, 0, nullptr, nullptr);
		if (needed <= 0)
		{
			if (pReturnValue) *pReturnValue = 0;
			if (mbstr && sizeInBytes > 0) mbstr[0] = '\0';
			return 0;
		}

		if (mbstr == nullptr)
		{
			if (pReturnValue) *pReturnValue = static_cast<std::size_t>(needed);
			return 0;
		}

		const int dstCap = (static_cast<std::size_t>(needed) > sizeInBytes) ? static_cast<int>(sizeInBytes) : needed;
		const int written = WideCharToMultiByte(CP_UTF8, 0, src, srcLen, mbstr, dstCap, nullptr, nullptr);
		const std::size_t writtenSz = (written > 0) ? static_cast<std::size_t>(written) : 0;
		if (writtenSz < sizeInBytes) mbstr[writtenSz] = '\0';
		else if (sizeInBytes > 0) mbstr[sizeInBytes - 1] = '\0';

		if (pReturnValue) *pReturnValue = writtenSz;

		// CRT contract: signal end-of-input on full conversion, advance past consumed wchars otherwise.
		*wcstr = (srcLen == -1 || src[srcLen] == L'\0') ? nullptr : (src + srcLen);
		return 0;
	}

	ModuleBethesdaNetCrash::ModuleBethesdaNetCrash() :
		Module("Bethesda.net Crash", &bFixesBethesdaNetCrash)
	{}

	bool ModuleBethesdaNetCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// OG (1.10.163) imports wcsrtombs_s from MSVCR110.dll; NG/AE use the modern api-ms-* CRT shim.
		static constexpr std::array<const char*, 2> kCandidateDlls{
			"api-ms-win-crt-convert-l1-1-0.dll",
			"MSVCR110.dll",
		};

		std::uintptr_t original = 0;
		for (const auto* dll : kCandidateDlls)
		{
			original = RELEX::DetourIAT(dll, "wcsrtombs_s", reinterpret_cast<std::uintptr_t>(&Hook_wcsrtombs_s));
			if (original)
				break;
		}

		if (!original)
		{
			REX::WARN("Bethesda.net Crash: wcsrtombs_s not found in IAT."sv);
			return false;
		}

		return true;
	}

}
