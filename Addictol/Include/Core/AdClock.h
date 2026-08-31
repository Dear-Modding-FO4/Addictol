#pragma once

#include <stdint.h>

namespace Addictol
{
	[[nodiscard]] uint64_t ReadQpc() noexcept;
	[[nodiscard]] uint64_t GetQpcFrequency() noexcept;
	[[nodiscard]] double QpcToMilliseconds(uint64_t a_ticks, uint64_t a_qpcFrequency) noexcept;
}
