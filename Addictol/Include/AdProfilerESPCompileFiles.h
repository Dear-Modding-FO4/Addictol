#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace Addictol::ESPCompileFiles
{
	enum class Runtime : std::uint8_t
	{
		OG,
		NG,
		AE
	};

	struct Target
	{
		std::uint64_t id;
		std::string_view slot;
		std::array<std::uint8_t, 16> signature;
	};

	inline constexpr std::array<Target, 3> kTargets{ {
		{
			57137,
			"OG",
			{ 0x88, 0x54, 0x24, 0x10, 0x53, 0x55, 0x56, 0x57,
				0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 }
		},
		{
			2192321,
			"NG",
			{ 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
				0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 }
		},
		{
			2192321,
			"AE",
			{ 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
				0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 }
		}
	} };

	[[nodiscard]] constexpr const Target& GetTarget(Runtime a_runtime) noexcept
	{
		return kTargets[static_cast<std::size_t>(a_runtime)];
	}

	[[nodiscard]] inline bool Matches(
		std::span<const std::uint8_t> a_code,
		const Target& a_target) noexcept
	{
		return a_code.size() >= a_target.signature.size() &&
			std::memcmp(a_code.data(), a_target.signature.data(), a_target.signature.size()) == 0;
	}
}
