#include "../Addictol/Include/AdProfilerAllocator.h"
#include "Harness.h"

using Addictol::AllocatorRequestHistogramBucket;
using Addictol::AllocatorSizeClass;

namespace
{
	constexpr std::array<std::size_t, 14> poolMaximums{
		8, 16, 32, 64, 128, 256, 512, 1024, 4096, 8192, 16384, 32768, 65536, 131072
	};

	constexpr bool halfSplitCandidateOnlyExistsForPool4096()
	{
		bool foundPool4096Candidate = false;
		for (const auto& allocation : vmm_tests::allocation_cases)
		{
			if (allocation.pool == 0xFF || allocation.pool == 0)
				continue;
			if (allocation.size <= poolMaximums[allocation.pool] / 2)
			{
				if (allocation.pool != 8)
					return false;
				foundPool4096Candidate = true;
			}
		}
		return foundPool4096Candidate;
	}
}

static_assert(AllocatorSizeClass(0) == 0);
static_assert(AllocatorSizeClass(1) == 0);
static_assert(AllocatorSizeClass(8) == 0);
static_assert(AllocatorSizeClass(9) == 1);
static_assert(AllocatorSizeClass(16) == 1);
static_assert(AllocatorSizeClass(17) == 2);
static_assert(AllocatorSizeClass(1024) == 7);
static_assert(AllocatorSizeClass(1025) == 8);
static_assert(AllocatorSizeClass(2048) == 8);
static_assert(AllocatorSizeClass(2049) == 8);
static_assert(AllocatorSizeClass(4096) == 8);
static_assert(AllocatorSizeClass(4097) == 9);
static_assert(AllocatorSizeClass(131072) == 13);
static_assert(AllocatorSizeClass(131073) == 14);

static_assert(AllocatorRequestHistogramBucket(1024) == 7);
static_assert(AllocatorRequestHistogramBucket(1025) == 8);
static_assert(AllocatorRequestHistogramBucket(2048) == 8);
static_assert(AllocatorRequestHistogramBucket(2049) == 9);
static_assert(AllocatorRequestHistogramBucket(4096) == 9);
static_assert(halfSplitCandidateOnlyExistsForPool4096());
