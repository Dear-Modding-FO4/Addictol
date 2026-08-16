#pragma once

#include "AdLogControl.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Addictol
{
	using namespace std::literals;

	enum class ProfilerMenuTab : uint32_t
	{
		kOverview,
		kFrameHitch,
		kDecompression,
		kAllocator,
		kMemory,
		kModules,
		kTextureDecode
	};

	inline constexpr std::array kProfilerMenuTabNames{
		"Overview"sv,
		"Frame Hitch"sv,
		"Decompression"sv,
		"Allocator"sv,
		"Memory"sv,
		"Modules"sv,
		"Texture Decode"sv
	};
	inline constexpr size_t kProfilerMenuTabCount{ kProfilerMenuTabNames.size() };

	[[nodiscard]] constexpr std::string_view Describe(ProfilerMenuTab a_tab) noexcept
	{
		const auto index = static_cast<size_t>(a_tab);
		return index < kProfilerMenuTabCount ? kProfilerMenuTabNames[index] : "Unknown"sv;
	}

	inline constexpr std::array kProfilerMenuLogLevels{
		LogControl::Level::kTrace,
		LogControl::Level::kDebug,
		LogControl::Level::kInfo,
		LogControl::Level::kWarn,
		LogControl::Level::kError,
		LogControl::Level::kCritical,
		LogControl::Level::kOff
	};
	static_assert(
		kProfilerMenuLogLevels.size() ==
		static_cast<size_t>(LogControl::Level::kOff) + 1);

	struct ProfilerMenuToggleKey
	{
		uint32_t virtualKey{ 0 };
		bool recognized{ false };
	};

	inline constexpr uint32_t kProfilerMenuDefaultToggleKey{ 0x7A };

	struct ProfilerMenuKeyName
	{
		std::string_view name;
		uint32_t virtualKey;
	};

	// Function keys plus the navigation cluster: everything else belongs to the game.
	inline constexpr std::array kProfilerMenuToggleKeys{
		ProfilerMenuKeyName{ "F1"sv, 0x70 },
		ProfilerMenuKeyName{ "F2"sv, 0x71 },
		ProfilerMenuKeyName{ "F3"sv, 0x72 },
		ProfilerMenuKeyName{ "F4"sv, 0x73 },
		ProfilerMenuKeyName{ "F5"sv, 0x74 },
		ProfilerMenuKeyName{ "F6"sv, 0x75 },
		ProfilerMenuKeyName{ "F7"sv, 0x76 },
		ProfilerMenuKeyName{ "F8"sv, 0x77 },
		ProfilerMenuKeyName{ "F9"sv, 0x78 },
		ProfilerMenuKeyName{ "F10"sv, 0x79 },
		ProfilerMenuKeyName{ "F11"sv, 0x7A },
		ProfilerMenuKeyName{ "F12"sv, 0x7B },
		ProfilerMenuKeyName{ "Home"sv, 0x24 },
		ProfilerMenuKeyName{ "End"sv, 0x23 },
		ProfilerMenuKeyName{ "Insert"sv, 0x2D },
		ProfilerMenuKeyName{ "Delete"sv, 0x2E }
	};

	[[nodiscard]] constexpr char AsciiUpper(char a_character) noexcept
	{
		return a_character >= 'a' && a_character <= 'z' ?
			static_cast<char>(a_character - ('a' - 'A')) :
			a_character;
	}

	[[nodiscard]] constexpr bool EqualsIgnoringCase(std::string_view a_left, std::string_view a_right) noexcept
	{
		if (a_left.size() != a_right.size())
			return false;
		for (size_t index = 0; index < a_left.size(); ++index)
		{
			if (AsciiUpper(a_left[index]) != AsciiUpper(a_right[index]))
				return false;
		}
		return true;
	}

	// An unrecognized name falls back to F11 so a typo never silently disables the toggle.
	[[nodiscard]] constexpr ProfilerMenuToggleKey ParseProfilerMenuToggleKey(std::string_view a_name) noexcept
	{
		for (const auto& key : kProfilerMenuToggleKeys)
		{
			if (EqualsIgnoringCase(a_name, key.name))
				return { key.virtualKey, true };
		}
		return { kProfilerMenuDefaultToggleKey, false };
	}

	[[nodiscard]] constexpr std::string_view ProfilerMenuToggleKeyName(uint32_t a_virtualKey) noexcept
	{
		for (const auto& key : kProfilerMenuToggleKeys)
		{
			if (key.virtualKey == a_virtualKey)
				return key.name;
		}
		return "Unknown"sv;
	}

	inline constexpr uint32_t kProfilerMenuMinRefreshMs{ 100 };
	inline constexpr uint32_t kProfilerMenuMaxRefreshMs{ 2000 };

	[[nodiscard]] constexpr uint32_t ClampProfilerMenuRefreshMs(uint32_t a_refreshMs) noexcept
	{
		if (a_refreshMs < kProfilerMenuMinRefreshMs)
			return kProfilerMenuMinRefreshMs;
		if (a_refreshMs > kProfilerMenuMaxRefreshMs)
			return kProfilerMenuMaxRefreshMs;
		return a_refreshMs;
	}

	// Closed or inactive panels never refresh; the active panel also waits for its cadence.
	[[nodiscard]] constexpr bool ShouldRefreshPanel(
		bool a_open,
		bool a_active,
		bool a_hasData,
		uint64_t a_nowQpc,
		uint64_t a_refreshedAtQpc,
		uint64_t a_qpcFrequency,
		uint32_t a_refreshMs) noexcept
	{
		if (!a_open || !a_active)
			return false;
		if (!a_hasData || !a_qpcFrequency)
			return true;
		if (a_nowQpc <= a_refreshedAtQpc)
			return false;

		const auto elapsedMs =
			((a_nowQpc - a_refreshedAtQpc) * 1000ull) / a_qpcFrequency;
		return elapsedMs >= ClampProfilerMenuRefreshMs(a_refreshMs);
	}

	[[nodiscard]] constexpr double QpcToMilliseconds(uint64_t a_ticks, uint64_t a_qpcFrequency) noexcept
	{
		return a_qpcFrequency ?
			(static_cast<double>(a_ticks) * 1000.0) / static_cast<double>(a_qpcFrequency) :
			0.0;
	}

	[[nodiscard]] constexpr size_t ClampProfilerMenuFormattedLength(
		int a_written,
		size_t a_capacity) noexcept
	{
		if (a_written <= 0 || !a_capacity)
			return 0;

		const auto written = static_cast<size_t>(a_written);
		return written < a_capacity ? written : a_capacity - 1;
	}

	namespace ProfilerMenu
	{
		// Registers the permanent platform sinks; the menu itself starts closed.
		[[nodiscard]] bool Install() noexcept;
	}
}
