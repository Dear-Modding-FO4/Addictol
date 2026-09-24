#include <Memory/Heaps/AdVoltekHeap.h>
#include <Voltek.MemoryManager.h>

#include <algorithm>
#include <array>
#include <string>

namespace Addictol::Heaps
{
	bool Voltek::Initialize() noexcept
	{
		return voltek::scalable_memory_manager_initialize();
	}

	void* Voltek::Allocate(size_t a_size) noexcept
	{
		return voltek::scalable_alloc(a_size);
	}

	void* Voltek::AllocateAligned(size_t a_size, size_t a_alignment) noexcept
	{
		return voltek::scalable_aligned_alloc(a_size, a_alignment);
	}

	// A stale pointer into a released slot faults on its header read; treat it as foreign.
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
		voltek::scalable_pool_stats pools{};
		voltek::scalable_get_pool_stats(&pools);
		voltek::scalable_memory_stats memory{};
		voltek::scalable_get_memory_stats(&memory);
		HeapStatistics result{};
		result.poolCount = pools.pool_count;
		result.pagesBusy = pools.pages_busy;
		result.pageCapacity = pools.page_capacity;
		result.committedBytes = memory.committed_bytes;
		result.reservedBytes = memory.reserved_bytes;
		result.liveBlocks = memory.live_blocks;
		result.requestedBytes = memory.requested_bytes;
		return result;
	}

	void Voltek::EnableClassStatistics() noexcept
	{
		voltek::scalable_enable_statistics();
	}

	size_t Voltek::ClassStatistics(std::span<HeapClassStatistics> a_out) noexcept
	{
		std::array<voltek::scalable_class_stats, kMaxHeapClasses> classes{};
		const auto available = voltek::scalable_get_class_stats(classes.data(), classes.size());
		if (!available)
			return 0;
		static const auto labels = [&] {
			std::array<std::string, kMaxHeapClasses> result{};
			for (size_t index = 0; index < available; ++index)
				result[index] = classes[index].request_limit ? std::to_string(classes[index].request_limit) : "large";
			return result;
		}();
		const auto count = (std::min)(available, a_out.size());
		for (size_t index = 0; index < count; ++index)
		{
			const auto& source = classes[index];
			a_out[index] = { labels[index], source.allocations, source.allocated_bytes, source.pages_created,
				source.pages_released, source.lock_contended, source.lock_wait_ticks };
		}
		return count;
	}
}
