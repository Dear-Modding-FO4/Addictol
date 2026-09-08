#include <Menu/AdMenuFormatting.h>

#include <array>

namespace Addictol
{
	char* MenuUi::NextFormatBuffer() noexcept
	{
		constexpr size_t kBufferCount{ 12 };

		static thread_local std::array<std::array<char, kFormatCapacity>, kBufferCount> buffers{};
		static thread_local size_t next{ 0 };
		auto& buffer = buffers[next];
		next = (next + 1) % kBufferCount;
		return buffer.data();
	}

	std::string_view MenuUi::FormatBytes(uint64_t a_bytes) noexcept
	{
		constexpr std::array<std::string_view, 5> units{ "B"sv, "KiB"sv, "MiB"sv, "GiB"sv, "TiB"sv };
		auto value = static_cast<double>(a_bytes);
		size_t unit = 0;
		while (value >= 1024.0 && unit + 1 < units.size())
		{
			value /= 1024.0;
			++unit;
		}
		return unit == 0 ?
			Print("%llu B", static_cast<unsigned long long>(a_bytes)) :
			Print("%.2f %.*s", value, static_cast<int>(units[unit].size()), units[unit].data());
	}

	std::string_view MenuUi::FormatSignedBytes(int64_t a_bytes) noexcept
	{
		const auto magnitudeValue = a_bytes < 0 ?
			static_cast<uint64_t>(-(a_bytes + 1)) + 1 :
			static_cast<uint64_t>(a_bytes);
		const auto magnitude = FormatBytes(magnitudeValue);
		return Print(
			"%s%.*s",
			a_bytes < 0 ? "-" : "+",
			static_cast<int>(magnitude.size()),
			magnitude.data());
	}

	std::string_view MenuUi::FormatCount(uint64_t a_count) noexcept
	{
		return Print("%llu", static_cast<unsigned long long>(a_count));
	}

	std::string_view MenuUi::FormatSigned(int64_t a_value) noexcept
	{
		return Print("%lld", static_cast<long long>(a_value));
	}

	std::string_view MenuUi::FormatMs(double a_milliseconds) noexcept
	{
		return Print("%.3f ms", a_milliseconds);
	}

	std::string_view MenuUi::FormatTicks(uint64_t a_ticks, uint64_t a_qpcFrequency) noexcept
	{
		return a_qpcFrequency ?
			Print("%llu (%.3f ms)",
				static_cast<unsigned long long>(a_ticks),
				QpcToMilliseconds(a_ticks, a_qpcFrequency)) :
			Print("%llu", static_cast<unsigned long long>(a_ticks));
	}

	std::string_view MenuUi::FormatRatio(uint64_t a_numerator, uint64_t a_denominator) noexcept
	{
		return a_denominator ?
			Print("%llu / %llu (%.1f%%)",
				static_cast<unsigned long long>(a_numerator),
				static_cast<unsigned long long>(a_denominator),
				(100.0 * static_cast<double>(a_numerator)) / static_cast<double>(a_denominator)) :
			Print("%llu / 0", static_cast<unsigned long long>(a_numerator));
	}

	std::string_view MenuUi::FormatLinesInLastMinute(double a_lines) noexcept
	{
		return Print("%.0f lines / last 60 s", a_lines);
	}

	std::string_view MenuUi::FormatBool(bool a_value) noexcept
	{
		return a_value ? "yes"sv : "no"sv;
	}
}
