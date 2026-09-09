#pragma once

#include <Core/AdUtils.h>
#include <Menu/AdMenuTargets.h>

#include <cstdint>
#include <cstdio>
#include <string_view>

namespace Addictol::MenuUi
{
	inline constexpr size_t kFormatCapacity{ 96 };

	// Rotate so one drawing call can consume several formatted values.
	[[nodiscard]] char* NextFormatBuffer() noexcept;

	[[nodiscard]] std::string_view Print(const char* a_format, auto... a_args) noexcept
	{
		auto* buffer = NextFormatBuffer();
		const auto written = std::snprintf(buffer, kFormatCapacity, a_format, a_args...);
		return std::string_view{ buffer, ClampMenuFormattedLength(written, kFormatCapacity) };
	}

	[[nodiscard]] std::string_view FormatBytes(uint64_t a_bytes) noexcept;
	[[nodiscard]] std::string_view FormatSignedBytes(int64_t a_bytes) noexcept;
	[[nodiscard]] std::string_view FormatCount(uint64_t a_count) noexcept;
	[[nodiscard]] std::string_view FormatSigned(int64_t a_value) noexcept;
	[[nodiscard]] std::string_view FormatMs(double a_milliseconds) noexcept;
	[[nodiscard]] std::string_view FormatTicks(uint64_t a_ticks, uint64_t a_qpcFrequency) noexcept;
	[[nodiscard]] std::string_view FormatRatio(uint64_t a_numerator, uint64_t a_denominator) noexcept;
	[[nodiscard]] std::string_view FormatLinesInLastMinute(double a_lines) noexcept;
	[[nodiscard]] std::string_view FormatBool(bool a_value) noexcept;
}
