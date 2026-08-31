#pragma once

#include "Core/AdLogControl.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Addictol
{
	using namespace std::literals;

	inline constexpr std::array kMenuLogLevels{
		LogControl::Level::kTrace,
		LogControl::Level::kDebug,
		LogControl::Level::kInfo,
		LogControl::Level::kWarn,
		LogControl::Level::kError,
		LogControl::Level::kCritical,
		LogControl::Level::kOff
	};
	static_assert(
		kMenuLogLevels.size() ==
		static_cast<size_t>(LogControl::Level::kOff) + 1);

	struct MenuToggleKey
	{
		uint32_t virtualKey{ 0 };
		bool recognized{ false };
	};

	inline constexpr uint32_t kMenuDefaultToggleKey{ 0x7A };

	struct MenuKeyName
	{
		std::string_view name;
		uint32_t virtualKey;
	};

	inline constexpr std::array kMenuToggleKeys{
		MenuKeyName{ "F1"sv, 0x70 },
		MenuKeyName{ "F2"sv, 0x71 },
		MenuKeyName{ "F3"sv, 0x72 },
		MenuKeyName{ "F4"sv, 0x73 },
		MenuKeyName{ "F5"sv, 0x74 },
		MenuKeyName{ "F6"sv, 0x75 },
		MenuKeyName{ "F7"sv, 0x76 },
		MenuKeyName{ "F8"sv, 0x77 },
		MenuKeyName{ "F9"sv, 0x78 },
		MenuKeyName{ "F10"sv, 0x79 },
		MenuKeyName{ "F11"sv, 0x7A },
		MenuKeyName{ "F12"sv, 0x7B },
		MenuKeyName{ "Home"sv, 0x24 },
		MenuKeyName{ "End"sv, 0x23 },
		MenuKeyName{ "Insert"sv, 0x2D },
		MenuKeyName{ "Delete"sv, 0x2E }
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

	[[nodiscard]] constexpr MenuToggleKey ParseMenuToggleKey(std::string_view a_name) noexcept
	{
		for (const auto& key : kMenuToggleKeys)
		{
			if (EqualsIgnoringCase(a_name, key.name))
				return { key.virtualKey, true };
		}
		return { kMenuDefaultToggleKey, false };
	}

	[[nodiscard]] constexpr std::string_view MenuToggleKeyName(uint32_t a_virtualKey) noexcept
	{
		for (const auto& key : kMenuToggleKeys)
		{
			if (key.virtualKey == a_virtualKey)
				return key.name;
		}
		return "Unknown"sv;
	}

	inline constexpr uint32_t kMenuMinRefreshMs{ 100 };
	inline constexpr uint32_t kMenuMaxRefreshMs{ 2000 };

	[[nodiscard]] constexpr uint32_t ClampMenuRefreshMs(uint32_t a_refreshMs) noexcept
	{
		if (a_refreshMs < kMenuMinRefreshMs)
			return kMenuMinRefreshMs;
		if (a_refreshMs > kMenuMaxRefreshMs)
			return kMenuMaxRefreshMs;
		return a_refreshMs;
	}

	[[nodiscard]] constexpr bool ShouldRefreshPanel(
		bool a_hasData,
		uint64_t a_nowQpc,
		uint64_t a_refreshedAtQpc,
		uint64_t a_qpcFrequency,
		uint32_t a_refreshMs) noexcept
	{
		if (!a_hasData || !a_qpcFrequency)
			return true;
		if (a_nowQpc <= a_refreshedAtQpc)
			return false;

		const auto elapsedMs =
			((a_nowQpc - a_refreshedAtQpc) * 1000ull) / a_qpcFrequency;
		return elapsedMs >= ClampMenuRefreshMs(a_refreshMs);
	}

	[[nodiscard]] constexpr size_t ClampMenuFormattedLength(
		int a_written,
		size_t a_capacity) noexcept
	{
		if (a_written <= 0 || !a_capacity)
			return 0;

		const auto written = static_cast<size_t>(a_written);
		return written < a_capacity ? written : a_capacity - 1;
	}

	struct MenuToggleDecision
	{
		bool matched{ false };
		bool open{ false };
	};

	[[nodiscard]] constexpr MenuToggleDecision DecideMenuToggle(
		uint32_t a_virtualKey,
		uint32_t a_toggleKey,
		bool a_open,
		bool a_drawingEnabled) noexcept
	{
		if (a_virtualKey != a_toggleKey)
			return { false, a_open };
		return { true, !(a_open && a_drawingEnabled) };
	}

	using MenuPanelDraw = void (*)(void*) noexcept;
}
