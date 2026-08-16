#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string_view>

#include "AdProfilerESPCompileFiles.h"

namespace Addictol::ESPSubHooks
{
	using ConstructObjectList = bool(__fastcall*)(void*, void*, bool);
	using InitAllForms = void(__fastcall*)(void*);

	struct Target
	{
		uint64_t id;
		std::string_view slot;
		const uint8_t* signature;
		size_t signatureSize;
	};

	inline constexpr std::initializer_list<uint8_t> kConstructOG{
		0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
		0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xDA, 0x45, 0x0F, 0xB6,
		0xE8, 0x4C, 0x8B, 0xF9, 0x45, 0x33, 0xC0, 0x33, 0xD2, 0x48, 0x8B, 0xCB
	};
	inline constexpr std::initializer_list<uint8_t> kConstructNG{
		0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x41,
		0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
		0x8B, 0xDA, 0x45, 0x0F, 0xB6, 0xE0, 0x4C, 0x8B, 0xE9, 0x45, 0x33, 0xC0,
		0x48, 0x8B, 0xCB, 0x33, 0xD2
	};
	inline constexpr auto kConstructAE = kConstructNG;

	// The OG rel32 is build-fixed by intent, so drift fails closed.
	inline constexpr std::initializer_list<uint8_t> kInitOG{
		0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57,
		0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0xE8, 0x16, 0x07,
		0xC2, 0x00
	};
	inline constexpr std::initializer_list<uint8_t> kInitNG{
		0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC,
		0x58, 0x45, 0x33, 0xFF, 0x48, 0x8B, 0xF9, 0x41, 0x8B, 0xEF, 0x44, 0x89,
		0xBC, 0x24, 0x90, 0x00, 0x00, 0x00
	};
	inline constexpr auto kInitAE = kInitNG;

	inline constexpr std::array<Target, 3> kConstructTargets{ {
		{ 1043280, "OG", kConstructOG.begin(), kConstructOG.size() },
		{ 2192326, "NG", kConstructNG.begin(), kConstructNG.size() },
		{ 2192326, "AE", kConstructAE.begin(), kConstructAE.size() }
	} };
	inline constexpr std::array<Target, 3> kInitTargets{ {
		{ 189223, "OG", kInitOG.begin(), kInitOG.size() },
		{ 2192344, "NG", kInitNG.begin(), kInitNG.size() },
		{ 2192344, "AE", kInitAE.begin(), kInitAE.size() }
	} };

	[[nodiscard]] constexpr const Target& GetConstructTarget(
		ESPCompileFiles::Runtime a_runtime) noexcept
	{
		return kConstructTargets[static_cast<size_t>(a_runtime)];
	}

	[[nodiscard]] constexpr const Target& GetInitTarget(
		ESPCompileFiles::Runtime a_runtime) noexcept
	{
		return kInitTargets[static_cast<size_t>(a_runtime)];
	}

	[[nodiscard]] inline bool Matches(
		std::span<const uint8_t> a_code,
		const Target& a_target) noexcept
	{
		return a_code.size() >= a_target.signatureSize &&
			std::memcmp(a_code.data(), a_target.signature, a_target.signatureSize) == 0;
	}
}
