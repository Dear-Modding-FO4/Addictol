#include <AdAssert.h>
#include <AdAllocator.h>
#include <Voltek.MemoryManager.h>

namespace Addictol
{
	ProxyVoltekHeap::ProxyVoltekHeap() noexcept
	{
		voltek::scalable_memory_manager_initialize();
	}

	void* ProxyVoltekHeap::malloc(std::size_t nSize) const noexcept
	{
		return CheckPtr(voltek::scalable_alloc(nSize), nSize);
	}

	void* ProxyVoltekHeap::aligned_malloc(std::size_t nSize, [[maybe_unused]] std::size_t nAlignment) const noexcept
	{
		return malloc(nSize);
	}

	void* ProxyVoltekHeap::realloc(void* lpBlock, std::size_t nNewSize) const noexcept
	{
		auto mem = CheckPtr(voltek::scalable_alloc(nNewSize), nNewSize);

		if (!lpBlock)
			return mem;

		auto oldSize = msize(lpBlock);
		memcpy(mem, lpBlock, oldSize > nNewSize ? nNewSize : oldSize);
		return mem;
	}

	void* ProxyVoltekHeap::aligned_realloc(void* lpBlock, std::size_t nNewSize, [[maybe_unused]] std::size_t nAlignment) const noexcept
	{
		return realloc(lpBlock, nNewSize);
	}

	void ProxyVoltekHeap::free(void* lpBlock) const noexcept
	{
		voltek::scalable_free(lpBlock);
	}

	void ProxyVoltekHeap::aligned_free(void* lpBlock) const noexcept
	{
		free(lpBlock);
	}

	std::size_t ProxyVoltekHeap::msize(void* lpBlock) const noexcept
	{
		return voltek::scalable_msize(lpBlock);
	}

	std::size_t ProxyVoltekHeap::aligned_msize(void* lpBlock, [[maybe_unused]] std::size_t nAlignment) const noexcept
	{
		return msize(lpBlock);
	}
}