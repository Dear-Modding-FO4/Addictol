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
		__try
		{
			voltek::scalable_free(lpBlock);
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