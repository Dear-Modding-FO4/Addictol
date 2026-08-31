#include <Core/AdClock.h>

#include <Windows.h>

namespace Addictol
{
	uint64_t ReadQpc() noexcept
	{
		LARGE_INTEGER value{};
		QueryPerformanceCounter(&value);
		return static_cast<uint64_t>(value.QuadPart);
	}

	uint64_t GetQpcFrequency() noexcept
	{
		static const uint64_t frequency = [] {
			LARGE_INTEGER value{};
			return QueryPerformanceFrequency(&value) && (value.QuadPart > 0) ?
				static_cast<uint64_t>(value.QuadPart) : 0;
		}();
		return frequency;
	}

	double QpcToMilliseconds(uint64_t a_ticks, uint64_t a_qpcFrequency) noexcept
	{
		return a_qpcFrequency ?
			(static_cast<double>(a_ticks) * 1000.0) / static_cast<double>(a_qpcFrequency) :
			0.0;
	}
}
