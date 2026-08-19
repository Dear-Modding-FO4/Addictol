#include <Modules/AdModuleHighResBloom.h>
#include <Core/AdUtils.h>

#include <algorithm>
#include <array>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesHighResBloom{ "Patches"sv, "bHighResBloom"sv, false };
	static REX::TOML::I32<> nAdditionalBloomScale{ "Additional"sv, "nBloomScale"sv, 2 };

	// Vanilla bloom RT downsample: shr esi, 2; shr r12d, 2 (width >> 2; height >> 2).
	static constexpr std::array<std::uint8_t, 7> kVanillaPattern{ 0xC1, 0xEE, 0x02, 0x41, 0xC1, 0xEC, 0x02 };
	static constexpr std::size_t kScanRange = 0x500;

	ModuleHighResBloom::ModuleHighResBloom() :
		Module("High Res Bloom", &bPatchesHighResBloom)
	{}

	bool ModuleHighResBloom::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto scale = nAdditionalBloomScale.GetValue();
		if (scale != 1 && scale != 2 && scale != 4 && scale != 8)
		{
			REX::WARN("[HighResBloom] nBloomScale must be 1, 2, 4, or 8 (got {}); skipping patch."sv, scale);
			return false;
		}

		if (scale == 4)
		{
			REX::INFO("[HighResBloom] nBloomScale = 4 (vanilla); no patch applied."sv);
			return true;
		}

		const auto base = REL::ID{ 1118299, 2318909 }.address();
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(base);

		const auto* hit = std::search(bytes, bytes + kScanRange, kVanillaPattern.begin(), kVanillaPattern.end());
		if (hit == bytes + kScanRange)
		{
			REX::WARN("[HighResBloom] Could not locate vanilla bloom shr pair within 0x{:X} bytes of base 0x{:X}."sv, kScanRange, base);
			return false;
		}

		const auto target = reinterpret_cast<std::uintptr_t>(hit);
		if (scale == 1)
		{
			RELEX::WriteSafeNop(target, kVanillaPattern.size());
		}
		else
		{
			const auto imm = static_cast<std::uint8_t>(scale == 2 ? 1 : 3);
			RELEX::WriteSafe(target,     { 0xC1, 0xEE, imm });
			RELEX::WriteSafe(target + 3, { 0x41, 0xC1, 0xEC, imm });
		}

		REX::INFO("[HighResBloom] Patched bloom RT downsample at 0x{:X} (scale {})."sv, target, scale);
		return true;
	}

}
