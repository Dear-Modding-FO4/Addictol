#include <Memory/Heaps/AdMimallocHeap.h>
#include <mimalloc.h>
#include <mimalloc-stats.h>

namespace Addictol::Heaps
{
	void Mimalloc::Initialize() noexcept
	{
		// A failed frame-loop allocation reports immediately instead of stalling 400 ms.
		mi_option_set(mi_option_retry_on_oom, 0);
		mi_option_set(mi_option_show_errors, 0);
		mi_process_init();
	}

	void* Mimalloc::Allocate(size_t a_size) noexcept
	{
		return mi_malloc(a_size);
	}

	void* Mimalloc::AllocateAligned(size_t a_size, size_t a_alignment) noexcept
	{
		return a_alignment > kNaturalHeapAlignment ?
			mi_malloc_aligned(a_size, a_alignment) :
			mi_malloc(a_size);
	}

	void* Mimalloc::Reallocate(void* a_block, size_t a_size) noexcept
	{
		if (!mi_is_in_heap_region(a_block))
			return nullptr;
		return mi_realloc(a_block, a_size);
	}

	void* Mimalloc::ReallocateAligned(void* a_block, size_t a_size, size_t a_alignment) noexcept
	{
		if (!mi_is_in_heap_region(a_block))
			return nullptr;
		return a_alignment > kNaturalHeapAlignment ?
			mi_realloc_aligned(a_block, a_size, a_alignment) :
			mi_realloc(a_block, a_size);
	}

	void Mimalloc::Free(void* a_block) noexcept
	{
		// Built with MI_FREE_IS_CHECKED, so foreign pointers are ignored.
		mi_free(a_block);
	}

	size_t Mimalloc::Size(void* a_block) noexcept
	{
		return mi_is_in_heap_region(a_block) ? mi_usable_size(a_block) : 0;
	}

	HeapStatistics Mimalloc::Statistics() noexcept
	{
		mi_stats_t stats{};
		stats.size = sizeof(stats);
		stats.version = MI_STAT_VERSION;
		HeapStatistics result{};
		if (!mi_stats_get(&stats))
			return result;
		result.committedBytes = static_cast<uint64_t>(stats.committed.current);
		result.reservedBytes = static_cast<uint64_t>(stats.reserved.current);
		return result;
	}
}
