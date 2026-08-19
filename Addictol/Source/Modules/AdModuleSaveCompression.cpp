// # original idea (vibe source): https://github.com/1001Bits/Fast-Saving-Fallout/blob/main/src/SaveCompression.cpp

#include <Modules/AdModuleSaveCompression.h>
#include <Core/AdUtils.h>
#include <libdeflate/libdeflate.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesSaveCompression{ "Patches"sv, "bSaveCompression"sv, true };

	namespace saveCompDetail
	{
		// Engine zlib CompressBuffer, kept as a fallback if a libdeflate compressor can't be allocated.
		using TCompressBuffer = uint32_t(*)(void*, uint32_t, void*, uint32_t) noexcept;
		TCompressBuffer CompressBufferOrig;

		// libdeflate (zlib, level 6) replacement for BGSSaveLoadUtilities::CompressBuffer.
		static uint32_t CompressBuffer(void* a_src, uint32_t a_srcLen, void* a_dst, uint32_t a_dstCap) noexcept
		{
			if (a_srcLen < 0x20)
				return 0;

			thread_local libdeflate_compressor* compressor = libdeflate_alloc_compressor(6);
			if (!compressor)
				return CompressBufferOrig(a_src, a_srcLen, a_dst, a_dstCap);

			// Engine caps output at min(dstCap, srcLen-1); a 0 return means store uncompressed.
			const auto bound = static_cast<size_t>((a_dstCap < a_srcLen - 1) ? a_dstCap : (a_srcLen - 1));
			const auto written = libdeflate_zlib_compress(compressor, a_src, a_srcLen, a_dst, bound);
			return static_cast<uint32_t>(written);
		}
	}

	ModuleSaveCompression::ModuleSaveCompression() :
		Module("Save Compression", &bPatchesSaveCompression)
	{}

	bool ModuleSaveCompression::DoQuery() const noexcept
	{
		if (IsModDLLPresent("FastSavingFallout.dll"))
		{
			Skip("Standalone 'FastSavingFallout.dll' is installed, skipping module"sv);
			return false;
		}

		return true;
	}

	bool ModuleSaveCompression::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto target = REL::Relocation{ REL::ID{ 104318, 2228204 } }.address();

		if (!(saveCompDetail::CompressBufferOrig = reinterpret_cast<saveCompDetail::TCompressBuffer>(
			RELEX::TryDetourJump(target, reinterpret_cast<uintptr_t>(&saveCompDetail::CompressBuffer),
			// CompressBuffer prologue, byte-identical OG/NG/AE.
			{
			0x48, 0x8B, 0xC4, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20,
			0x41, 0x56, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x33, 0xFF, 0x49, 0x8B, 0xE8, 0x8B,
			0xF2, 0x4C, 0x8B, 0xF1
			}))))
		{
			REX::WARN("Save Compression: unexpected prologue at CompressBuffer -- skipping to avoid corruption."sv);
			return false;
		}

		return true;
	}

}
