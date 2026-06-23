#include <Modules/AdModuleSaveCompression.h>
#include <AdUtils.h>
#include <libdeflate/libdeflate.h>

#include <cstring>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesSaveCompression{ "Patches"sv, "bSaveCompression"sv, true };

	namespace saveCompDetail
	{
		// Original engine CompressBuffer (zlib) from the detour trampoline; fallback when a libdeflate compressor can't be allocated.
		using TCompressBuffer = std::uint32_t(*)(void*, std::uint32_t, void*, std::uint32_t) noexcept;
		TCompressBuffer CompressBufferOrig;

		// libdeflate (zlib-wrapped, level 6) drop-in for BGSSaveLoadUtilities::CompressBuffer; the compress complement of AdModuleLibDeflate's inflate swap.
		static std::uint32_t CompressBuffer(void* a_src, std::uint32_t a_srcLen, void* a_dst, std::uint32_t a_dstCap) noexcept
		{
			// Engine gate: inputs below 0x20 bytes are stored uncompressed by the caller.
			if (a_srcLen < 0x20)
				return 0;

			// One compressor per thread, allocated once (mirrors AdModuleLibDeflate's thread_local decompressor).
			thread_local libdeflate_compressor* compressor = libdeflate_alloc_compressor(6);
			if (!compressor)
				return CompressBufferOrig(a_src, a_srcLen, a_dst, a_dstCap);

			// avail_out = min(dstCap, srcLen-1): never exceed dst, and force "incompressible -> 0".
			const std::size_t bound	  = (a_dstCap < a_srcLen - 1) ? a_dstCap : (a_srcLen - 1);

			// libdeflate returns 0 when output won't fit in `bound`, exactly the engine's store-uncompressed signal.
			const std::size_t written = libdeflate_zlib_compress(compressor, a_src, a_srcLen, a_dst, bound);
			return static_cast<std::uint32_t>(written);
		}
	}

	ModuleSaveCompression::ModuleSaveCompression() :
		Module("Save Compression", &bPatchesSaveCompression)
	{}

	bool ModuleSaveCompression::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleSaveCompression::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto target = REL::Relocation<std::uintptr_t>{ REL::ID{ 104318, 2228204, 2228204 } }.address();

		// CompressBuffer's prologue is byte-identical OG/NG/AE through `mov r14, rcx`; verify it so a bad resolve can't detour the wrong function.
		static constexpr std::uint8_t expected[] = {
			0x48, 0x8B, 0xC4,					// mov  rax, rsp
			0x48, 0x89, 0x68, 0x10,				// mov  [rax+10h], rbp
			0x48, 0x89, 0x70, 0x18,				// mov  [rax+18h], rsi
			0x48, 0x89, 0x78, 0x20,				// mov  [rax+20h], rdi
			0x41, 0x56,							// push r14
			0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00,	// sub  rsp, 90h
			0x33, 0xFF,							// xor  edi, edi
			0x49, 0x8B, 0xE8,					// mov  rbp, r8
			0x8B, 0xF2,							// mov  esi, edx
			0x4C, 0x8B, 0xF1					// mov  r14, rcx
		};
		if (std::memcmp(reinterpret_cast<const void*>(target), expected, sizeof(expected)) != 0)
		{
			REX::WARN("Save Compression: unexpected prologue at CompressBuffer -- skipping to avoid corruption."sv);
			return false;
		}

		saveCompDetail::CompressBufferOrig = reinterpret_cast<saveCompDetail::TCompressBuffer>(
			RELEX::DetourJump(target, reinterpret_cast<std::uintptr_t>(&saveCompDetail::CompressBuffer)));
		return true;
	}

	bool ModuleSaveCompression::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleSaveCompression::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
