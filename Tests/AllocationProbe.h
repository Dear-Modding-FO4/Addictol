#pragma once

#include <cstddef>

namespace vmm_tests
{
	// Counts C++ heap allocations on this thread without counting other test workers.
	class AllocationProbe
	{
	public:
		AllocationProbe() noexcept;
		~AllocationProbe() noexcept;
		AllocationProbe(const AllocationProbe&) = delete;
		AllocationProbe& operator=(const AllocationProbe&) = delete;
		[[nodiscard]] size_t Count() const noexcept;

	private:
		size_t m_initial;
		bool m_previous;
	};
}
