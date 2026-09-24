#include <Memory/Heaps/AdVoltekHeap.h>
#include <Voltek.MemoryManager.h>

namespace Addictol::Heaps
{
	void Voltek::Initialize() noexcept
	{
		voltek::scalable_memory_manager_initialize();
	}

	void* Voltek::Allocate(size_t a_size) noexcept
	{
		return voltek::scalable_alloc(a_size);
	}

	void* Voltek::AllocateAligned(size_t a_size, size_t a_alignment) noexcept
	{
		return voltek::scalable_aligned_alloc(a_size, a_alignment);
	}

	// VMM validates a block by reading its header, which faults for a foreign block at a page start.
	void* Voltek::Reallocate(void* a_block, size_t a_size) noexcept
	{
		__try
		{
			return voltek::scalable_realloc(a_block, a_size);
		}
		__except (1)
		{
			return nullptr;
		}
	}

	void* Voltek::ReallocateAligned(void* a_block, size_t a_size, size_t a_alignment) noexcept
	{
		__try
		{
			return voltek::scalable_aligned_realloc(a_block, a_size, a_alignment);
		}
		__except (1)
		{
			return nullptr;
		}
	}

	void Voltek::Free(void* a_block) noexcept
	{
		__try
		{
			voltek::scalable_free(a_block);
		}
		__except (1)
		{
			// CTD: free memory no vmm maybe
			// malloc excluded - this hooked

			// [2] 0x7FF6BD6600C1     Fallout4.exe+01E00C1	nop |  sub_1401E0080_1E00C1	nop
			// [3] 0x7FF6BD796BD1     Fallout4.exe+0316BD1	mov rsi, [rsp + 0x38] | sub_140316B80_316BD1	mov rsi, [rsp + 0x38]

			// this called MemoryManager::Deallocate (maybe bug game???)
		}
	}

	size_t Voltek::Size(void* a_block) noexcept
	{
		__try
		{
			return voltek::scalable_msize(a_block);
		}
		__except (1)
		{
			return 0;
		}
	}

	HeapStatistics Voltek::Statistics() noexcept
	{
		voltek::scalable_pool_stats stats{};
		voltek::scalable_get_pool_stats(&stats);
		HeapStatistics result{};
		result.poolCount = stats.pool_count;
		result.pagesBusy = stats.pages_busy;
		result.pageCapacity = stats.page_capacity;
		return result;
	}
}
