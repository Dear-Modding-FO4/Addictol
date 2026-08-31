#pragma once

#include "Core/AdLogControl.h"
#include <array>
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

	using MenuPanelDraw = void (*)(void*) noexcept;
}
