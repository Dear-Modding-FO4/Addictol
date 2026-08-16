#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace Addictol::LogControl
{
	enum class Level : uint32_t { kTrace, kDebug, kInfo, kWarn, kError, kCritical, kOff };

	[[nodiscard]] std::string_view LevelName(Level a_level) noexcept;
	[[nodiscard]] std::optional<Level> ParseLevel(std::string_view a_name) noexcept;

	void Install() noexcept;

	[[nodiscard]] Level GetLevel() noexcept;
	void SetLevel(Level a_level) noexcept;
	[[nodiscard]] Level GetFlushLevel() noexcept;
	void SetFlushLevel(Level a_level) noexcept;

	struct Stats
	{
		uint64_t written{ 0 };
		uint64_t flushed{ 0 };
		double linesPerMinute{ 0.0 };
	};
	[[nodiscard]] Stats CopyStats() noexcept;
}
