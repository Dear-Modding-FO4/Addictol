#pragma once

#include <array>
#include <stdint.h>
#include <string_view>
#include <REX/REX.h>

// Not ready, not pulling, hanging for no reason!!!
#define AD_USE_VISPER_AS_DEFAULT 0

namespace Addictol
{
	constexpr inline static auto MEM_GB = 1073741824;

	enum class HeapKind
	{
		Voltek
	};

	struct HeapName
	{
		std::string_view name;
		HeapKind kind;
	};

	inline constexpr std::array HEAP_NAMES{
		HeapName{ "voltek", HeapKind::Voltek }
	};

	class ICheckerPointer
	{
		ICheckerPointer(const ICheckerPointer&) = delete;
		ICheckerPointer(ICheckerPointer&&) = delete;
		ICheckerPointer& operator=(const ICheckerPointer&) = delete;
		ICheckerPointer& operator=(ICheckerPointer&&) = delete;
	public:
		ICheckerPointer() noexcept = default;
		~ICheckerPointer() noexcept = default;

		void* CheckPtr(void* lpBlock, size_t nSize) const noexcept;
	};

	class ProxyVoltekHeap :
		public ICheckerPointer,
		public REX::TSingleton<ProxyVoltekHeap>
	{
		ProxyVoltekHeap(const ProxyVoltekHeap&) = delete;
		ProxyVoltekHeap(ProxyVoltekHeap&&) = delete;
		ProxyVoltekHeap& operator=(const ProxyVoltekHeap&) = delete;
		ProxyVoltekHeap& operator=(ProxyVoltekHeap&&) = delete;
	public:
		ProxyVoltekHeap() noexcept;
		~ProxyVoltekHeap() noexcept = default;

		[[nodiscard]] void* malloc(size_t nSize) const noexcept;
		[[nodiscard]] void* aligned_malloc(size_t nSize, [[maybe_unused]] size_t nAlignment) const noexcept;

		[[nodiscard]] void* realloc(void* lpBlock, size_t nNewSize) const noexcept;
		[[nodiscard]] void* aligned_realloc(void* lpBlock, size_t nNewSize, [[maybe_unused]] size_t nAlignment) const noexcept;

		void free(void* lpBlock) const noexcept;
		void aligned_free(void* lpBlock) const noexcept;

		[[nodiscard]] size_t msize(void* lpBlock) const noexcept;
		[[nodiscard]] size_t aligned_msize(void* lpBlock, [[maybe_unused]] size_t nAlignment) const noexcept;
	};

	class ProxyVisperHeap :
		public ICheckerPointer,
		public REX::TSingleton<ProxyVisperHeap>
	{
		ProxyVisperHeap(const ProxyVisperHeap&) = delete;
		ProxyVisperHeap(ProxyVisperHeap&&) = delete;
		ProxyVisperHeap& operator=(const ProxyVisperHeap&) = delete;
		ProxyVisperHeap& operator=(ProxyVisperHeap&&) = delete;
	public:
		ProxyVisperHeap() noexcept;
		~ProxyVisperHeap() noexcept = default;

		[[nodiscard]] void* malloc(size_t nSize) const noexcept;
		[[nodiscard]] void* aligned_malloc(size_t nSize, [[maybe_unused]] size_t nAlignment) const noexcept;

		[[nodiscard]] void* realloc(void* lpBlock, size_t nNewSize) const noexcept;
		[[nodiscard]] void* aligned_realloc(void* lpBlock, size_t nNewSize, [[maybe_unused]] size_t nAlignment) const noexcept;

		void free(void* lpBlock) const noexcept;
		void aligned_free(void* lpBlock) const noexcept;

		[[nodiscard]] size_t msize(void* lpBlock) const noexcept;
		[[nodiscard]] size_t aligned_msize(void* lpBlock, [[maybe_unused]] size_t nAlignment) const noexcept;
	};

#if AD_USE_VISPER_AS_DEFAULT
	using ProxyCurrentHeap = ProxyVisperHeap;
#else
	using ProxyCurrentHeap = ProxyVoltekHeap;
#endif

	bool ResolveHeapSelection(std::string_view a_name) noexcept;
	HeapKind GetSelectedHeapKind() noexcept;

	template<class F>
	decltype(auto) VisitSelectedHeap(F&& a_fn)
	{
		switch (GetSelectedHeapKind())
		{
		case HeapKind::Voltek:
		default:
			return a_fn.template operator()<ProxyVoltekHeap>();
		}
	}

	template<typename Heap = ProxyCurrentHeap>
	struct StdStuff
	{
		[[nodiscard]] static void* calloc(size_t nCount, size_t nSize) noexcept
		{
			if (nCount && nSize > SIZE_MAX / nCount)
				return nullptr;
			auto totalSize = nCount * nSize;
			auto ptr = Heap::GetSingleton()->malloc(totalSize);
			if (ptr) memset(ptr, 0, totalSize);
			return ptr;
		}

		[[nodiscard]] static void* malloc(size_t nSize) noexcept
		{
			return Heap::GetSingleton()->malloc(nSize);
		}

		[[nodiscard]] static void* aligned_malloc(size_t nSize, size_t alignment) noexcept
		{
			return Heap::GetSingleton()->aligned_malloc(nSize, alignment);
		}

		[[nodiscard]] static void* realloc(void* lpBlock, size_t nNewSize) noexcept
		{
			return Heap::GetSingleton()->realloc(lpBlock, nNewSize);
		}

		static void free(void* block) noexcept
		{
			Heap::GetSingleton()->free(block);
		}

		static void aligned_free(void* block) noexcept
		{
			Heap::GetSingleton()->aligned_free(block);
		}

		[[nodiscard]] static size_t msize(void* block) noexcept
		{
			return Heap::GetSingleton()->msize(block);
		}
	};
}