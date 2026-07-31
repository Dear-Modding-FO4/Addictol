#include <AdAssert.h>
#include <AdAllocator.h>
#include <Voltek.MemoryManager.h>
#include <AdVisperMemoryAllocator.h>

#define AD_VISPER_TEST_MODE_DISABLE 0
#define AD_VISPER_TEST_MODE_FATHER0 0

namespace Addictol
{
	static Visper::TMemoryManager g_VisperMemoryManager{};
#if AD_VISPER_TEST_MODE_DISABLE
#	if AD_VISPER_TEST_MODE_FATHER0
	REX::W32::HMODULE g_hMallocDll{ nullptr };
	using MFuncAlloc = void* (uint32_t);
	using MFuncRealloc = void* (uint32_t, void*);
	using MFuncDealloc = void(void*);
	using MFuncSize = uint32_t (void*);
	std::function<MFuncAlloc> Malloc{};
	std::function<MFuncRealloc> MRealloc{};
	std::function<MFuncDealloc> MFree{};
	std::function<MFuncSize> MGetSize{};
#	else
	static uintptr_t test_null_mem = 0;
#	endif
#endif

	ProxyVisperHeap::ProxyVisperHeap() noexcept
	{
#if !AD_VISPER_TEST_MODE_DISABLE
		if (!g_VisperMemoryManager.InitializeDefaultSettings())
			REX::WARN("VisperMemoryManager::InitializeDefaultSettings() return failed");

		REX::INFO("VisperMemoryManager::ConsumptionMemory {}Mb",
			g_VisperMemoryManager.GetRealConsumptionMemory() >> 20);
#elif AD_VISPER_TEST_MODE_FATHER0
		g_hMallocDll = REX::W32::LoadLibraryA("Data\\F4SE\\Plugins\\Malloc.dll");
		if (g_hMallocDll)
		{
			Malloc = reinterpret_cast<MFuncAlloc*>(REX::W32::GetProcAddress(g_hMallocDll, "MAlloc"));
			MRealloc = reinterpret_cast<MFuncRealloc*>(REX::W32::GetProcAddress(g_hMallocDll, "MReAllock"));
			MFree = reinterpret_cast<MFuncDealloc*>(REX::W32::GetProcAddress(g_hMallocDll, "MFree"));
			MGetSize = reinterpret_cast<MFuncSize*>(REX::W32::GetProcAddress(g_hMallocDll, "GetSize"));
		}
		else
			REX::WARN("REX::W32::GetModuleHandleA(\"Malloc.dll\") return failed");
#endif
	}

	void* ProxyVisperHeap::malloc(size_t nSize) const noexcept
	{
#if !AD_VISPER_TEST_MODE_DISABLE
		return CheckPtr(g_VisperMemoryManager.Alloc(nSize), nSize);
#	elif AD_VISPER_TEST_MODE_FATHER0
		return nSize ? CheckPtr(Malloc(static_cast<uint32_t>(nSize)), nSize) : nullptr;
#	else
		return nSize ? CheckPtr(_aligned_malloc(nSize, 16), nSize) : &test_null_mem;
#endif
	}

	void* ProxyVisperHeap::aligned_malloc(size_t nSize, [[maybe_unused]] size_t nAlignment) const noexcept
	{
		return malloc(nSize);
	}

	void* ProxyVisperHeap::realloc([[maybe_unused]] void* lpBlock, [[maybe_unused]] size_t nNewSize) const noexcept
	{
#if !AD_VISPER_TEST_MODE_DISABLE
		if (!lpBlock)
			return CheckPtr(g_VisperMemoryManager.Alloc(static_cast<int32_t>(nNewSize)), nNewSize);
		return CheckPtr(g_VisperMemoryManager.Realloc(lpBlock, static_cast<int32_t>(nNewSize)), nNewSize);
#	elif AD_VISPER_TEST_MODE_FATHER0
		return lpBlock ? 
			CheckPtr(MRealloc(static_cast<uint32_t>(nNewSize), lpBlock), nNewSize) :
			CheckPtr(Malloc(static_cast<uint32_t>(nNewSize)), nNewSize);
#	else
		return lpBlock == &test_null_mem ? malloc(nNewSize) :
			CheckPtr(_aligned_realloc(lpBlock, nNewSize, 16), nNewSize);
#endif
	}

	void* ProxyVisperHeap::aligned_realloc(void* lpBlock, size_t nNewSize, [[maybe_unused]] size_t nAlignment) const noexcept
	{
		return realloc(lpBlock, nNewSize);
	}

	void ProxyVisperHeap::free(void* lpBlock) const noexcept
	{
		__try
		{
#if !AD_VISPER_TEST_MODE_DISABLE
			g_VisperMemoryManager.Free(lpBlock);
#	elif AD_VISPER_TEST_MODE_FATHER0
			return MFree(lpBlock);
#	else
			if (lpBlock && lpBlock != &test_null_mem)
				_aligned_free(lpBlock);
#endif
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

	void ProxyVisperHeap::aligned_free(void* lpBlock) const noexcept
	{
		free(lpBlock);
	}

	size_t ProxyVisperHeap::msize([[maybe_unused]] void* lpBlock) const noexcept
	{
#if !AD_VISPER_TEST_MODE_DISABLE
		return g_VisperMemoryManager.GetSize(lpBlock);
#	elif AD_VISPER_TEST_MODE_FATHER0
		return lpBlock ? MGetSize(lpBlock) : 0;
#	else
		if (lpBlock && lpBlock != &test_null_mem)
			return _aligned_msize(lpBlock, 16, 0);
		return 0;
#endif
	}

	size_t ProxyVisperHeap::aligned_msize(void* lpBlock, [[maybe_unused]] std::size_t nAlignment) const noexcept
	{
		return msize(lpBlock);
	}
}