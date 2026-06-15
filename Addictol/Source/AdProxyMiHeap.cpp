#include <AdAssert.h>
#include <AdAllocator.h>
#include <mimalloc.h>
#include <mutex>

#define AD_PROXY_MIMALLOC_SEH_PROTECTED 1

namespace Addictol
{
	void* ProxyMiHeap::malloc(size_t nSize) const noexcept
	{
		return CheckPtr(mi_malloc(nSize), nSize);
	}

	void* ProxyMiHeap::aligned_malloc(size_t nSize, size_t nAlignment) const noexcept
	{
		return CheckPtr(mi_aligned_alloc(nAlignment, nSize), nSize);
	}

	void* ProxyMiHeap::realloc(void* lpBlock, size_t nNewSize) const noexcept
	{
		if (!lpBlock)
			return malloc(nNewSize);

		return CheckPtr(mi_realloc(lpBlock, nNewSize), nNewSize);
	}

	void* ProxyMiHeap::aligned_realloc(void* lpBlock, size_t nNewSize, size_t nAlignment) const noexcept
	{
		if (!lpBlock)
			return aligned_malloc(nNewSize, nAlignment);

		return CheckPtr(mi_realloc_aligned(lpBlock, nNewSize, nAlignment), nNewSize);
	}

	void ProxyMiHeap::free(void* lpBlock) const noexcept
	{
		if (!lpBlock) return;

#ifdef AD_PROXY_MIMALLOC_SEH_PROTECTED
		__try { mi_free(lpBlock); }
		__except (1)
		{}
#else
		mi_free(lpBlock);
#endif // AD_PROXY_MIMALLOC_SEH_PROTECTED
	}

	void ProxyMiHeap::aligned_free(void* lpBlock) const noexcept
	{
		free(lpBlock);
	}

	size_t ProxyMiHeap::msize(void* lpBlock) const noexcept
	{
		return lpBlock ? mi_usable_size(lpBlock) : 0;
	}

	size_t ProxyMiHeap::aligned_msize(void* lpBlock, [[maybe_unused]] std::size_t nAlignment) const noexcept
	{
		return msize(lpBlock);
	}
}