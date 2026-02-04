#include <AdAssert.h>
#include <AdAllocator.h>
#include <Voltek.MemoryManager.h>
#include <REX\REX\TOML.h>
#include <windows.h>

namespace Addictol
{
	REX::TOML::Str<> bAdditionalAllocatorType{ "Additional", "sAllocatorType", "vmm" };

	namespace tbb
	{
		using scalable_malloc_t				= void*		(__cdecl*)(size_t size);
		using scalable_free_t				= void		(__cdecl*)(void* ptr);
		using scalable_realloc_t			= void*		(__cdecl*)(void* ptr, size_t size);
		using scalable_calloc_t				= void*		(__cdecl*)(size_t nobj, size_t size);
		using scalable_aligned_malloc_t		= void*		(__cdecl*)(size_t size, size_t alignment);
		using scalable_aligned_realloc_t	= void*		(__cdecl*)(void* ptr, size_t size, size_t alignment);
		using scalable_aligned_free_t		= void		(__cdecl*)(void* ptr);
		using scalable_msize_t				= size_t	(__cdecl*)(void* ptr);
		using scalable_allocation_mode_t	= void		(__cdecl*)(int, int);

		scalable_malloc_t			scalable_malloc{ nullptr };
		scalable_free_t				scalable_free{ nullptr };
		scalable_realloc_t			scalable_realloc{ nullptr };
		scalable_calloc_t			scalable_calloc{ nullptr };
		scalable_aligned_malloc_t	scalable_aligned_malloc{ nullptr };
		scalable_aligned_realloc_t	scalable_aligned_realloc{ nullptr };
		scalable_aligned_free_t		scalable_aligned_free{ nullptr };
		scalable_msize_t			scalable_msize{ nullptr };
		scalable_allocation_mode_t	scalable_allocation_mode{ nullptr };
	}

	void* ICheckerPointer::CheckPtr(void* lpBlock, size_t nSize) const noexcept
	{
		if (!lpBlock)
		{
			static MEMORYSTATUSEX statex = { 0 };
			statex.dwLength = sizeof(MEMORYSTATUSEX);
			if (!GlobalMemoryStatusEx(&statex))
				return lpBlock;

			AdAssertWithFormattedMessage(lpBlock,
				"A memory allocation failed.\n\nRequested chunk size: %llu bytes.\n\nAvail memory: %llu bytes, load (%u%%).",
				nSize, statex.ullAvailPageFile, statex.dwMemoryLoad);
		}

		return lpBlock;
	}

	ProxyHeap::ProxyHeap() noexcept
	{
		auto tbb = LoadLibraryA("Data\\F4SE\\Plugins\\tbbmalloc.dll");
		AdAssertWithMessage(tbb, "tbbmalloc.dll no found");

		*(uintptr_t*)&tbb::scalable_malloc			= (uintptr_t)GetProcAddress(tbb, "scalable_malloc");
		*(uintptr_t*)&tbb::scalable_free			= (uintptr_t)GetProcAddress(tbb, "scalable_free");
		*(uintptr_t*)&tbb::scalable_realloc			= (uintptr_t)GetProcAddress(tbb, "scalable_realloc");
		*(uintptr_t*)&tbb::scalable_calloc			= (uintptr_t)GetProcAddress(tbb, "scalable_calloc");
		*(uintptr_t*)&tbb::scalable_aligned_malloc	= (uintptr_t)GetProcAddress(tbb, "scalable_aligned_malloc");
		*(uintptr_t*)&tbb::scalable_aligned_realloc	= (uintptr_t)GetProcAddress(tbb, "scalable_aligned_realloc");
		*(uintptr_t*)&tbb::scalable_aligned_free	= (uintptr_t)GetProcAddress(tbb, "scalable_aligned_free");
		*(uintptr_t*)&tbb::scalable_msize			= (uintptr_t)GetProcAddress(tbb, "scalable_msize");
		*(uintptr_t*)&tbb::scalable_allocation_mode = (uintptr_t)GetProcAddress(tbb, "scalable_allocation_mode");
		
		tbb::scalable_allocation_mode(0, 1);
	}

	void* ProxyHeap::malloc(size_t nSize) const noexcept
	{
		return CheckPtr(tbb::scalable_malloc(nSize), nSize);
	}

	void* ProxyHeap::aligned_malloc(size_t nSize, [[maybe_unused]] size_t nAlignment) const noexcept
	{
		return CheckPtr(tbb::scalable_aligned_malloc(nSize, nAlignment), nSize);
	}

	void* ProxyHeap::realloc(void* lpBlock, size_t nNewSize) const noexcept
	{
		return CheckPtr(lpBlock ? tbb::scalable_realloc(lpBlock, nNewSize) : tbb::scalable_malloc(nNewSize), nNewSize);
	}

	void* ProxyHeap::aligned_realloc(void* lpBlock, size_t nNewSize, [[maybe_unused]] size_t nAlignment) const noexcept
	{
		return CheckPtr(lpBlock ? tbb::scalable_aligned_realloc(lpBlock, nNewSize, nAlignment) : 
			tbb::scalable_aligned_malloc(nNewSize, nAlignment), nNewSize);
	}

	void ProxyHeap::free(void* lpBlock) const noexcept
	{
		if (lpBlock)
			tbb::scalable_free(lpBlock);
	}

	void ProxyHeap::aligned_free(void* lpBlock) const noexcept
	{
		if (lpBlock)
			tbb::scalable_aligned_free(lpBlock);
	}

	std::size_t ProxyHeap::msize(void* lpBlock) const noexcept
	{
		return lpBlock ? tbb::scalable_msize(lpBlock) : 0;
	}

	std::size_t ProxyHeap::aligned_msize(void* lpBlock, [[maybe_unused]] size_t nAlignment) const noexcept
	{
		return lpBlock ? tbb::scalable_msize(lpBlock) : 0;
	}

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
		return CheckPtr(voltek::scalable_alloc(nSize), nSize);
	}

	void* ProxyVoltekHeap::realloc(void* lpBlock, std::size_t nNewSize) const noexcept
	{
		return CheckPtr(voltek::scalable_realloc(lpBlock, nNewSize), nNewSize);
	}

	void* ProxyVoltekHeap::aligned_realloc(void* lpBlock, std::size_t nNewSize, [[maybe_unused]] std::size_t nAlignment) const noexcept
	{
		return CheckPtr(voltek::scalable_realloc(lpBlock, nNewSize), nNewSize);
	}

	void ProxyVoltekHeap::free(void* lpBlock) const noexcept
	{
		voltek::scalable_free(lpBlock);
	}

	void ProxyVoltekHeap::aligned_free(void* lpBlock) const noexcept
	{
		voltek::scalable_free(lpBlock);
	}

	std::size_t ProxyVoltekHeap::msize(void* lpBlock) const noexcept
	{
		return voltek::scalable_msize(lpBlock);
	}

	std::size_t ProxyVoltekHeap::aligned_msize(void* lpBlock, [[maybe_unused]] std::size_t nAlignment) const noexcept
	{
		return voltek::scalable_msize(lpBlock);
	}
}