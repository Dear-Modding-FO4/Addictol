#pragma once

#include <Memory/Heaps/AdMimallocHeap.h>
#include <Memory/Heaps/AdRpmallocHeap.h>
#include <Memory/Heaps/AdVoltekHeap.h>

#include <array>
#include <stdint.h>
#include <string_view>
#include <REX/REX.h>

// Not ready, not pulling, hanging for no reason!!!
#define AD_USE_VISPER_AS_DEFAULT 0

namespace Addictol
{
	constexpr inline static auto MEM_GB = 1073741824;

	template<HeapBackend... Backends>
	struct HeapBackendList
	{};

	// Selection order after stock; the first entry is the default.
	using HeapBackends = HeapBackendList<Heaps::Voltek, Heaps::Mimalloc, Heaps::Rpmalloc>;

	struct HeapName
	{
		std::string_view name;
		HeapKind kind;
	};

	inline constexpr auto HEAP_NAMES = []<class... Backends>(HeapBackendList<Backends...>) {
		// Stock keeps the game's own allocators, so it has no backend.
		return std::array{ HeapName{ "stock", HeapKind::Stock }, HeapName{ Backends::kName, Backends::kKind }... };
	}(HeapBackends{});

	[[nodiscard]] inline constexpr std::string_view HeapKindName(HeapKind a_kind) noexcept
	{
		for (const auto& entry : HEAP_NAMES)
		{
			if (entry.kind == a_kind)
				return entry.name;
		}
		return "unknown";
	}

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

	template<HeapBackend Backend>
	class ProxyHeap :
		public ICheckerPointer,
		public REX::TSingleton<ProxyHeap<Backend>>
	{
		ProxyHeap(const ProxyHeap&) = delete;
		ProxyHeap(ProxyHeap&&) = delete;
		ProxyHeap& operator=(const ProxyHeap&) = delete;
		ProxyHeap& operator=(ProxyHeap&&) = delete;
	public:
		using BackendType = Backend;

		ProxyHeap() noexcept : m_ready(Backend::Initialize()) {}
		~ProxyHeap() noexcept = default;

		[[nodiscard]] void* malloc(size_t nSize) const noexcept
		{
			return CheckPtr(Overflows(nSize) ? nullptr : Backend::Allocate(nSize + Backend::kTailPadding), nSize);
		}

		[[nodiscard]] void* aligned_malloc(size_t nSize, size_t nAlignment) const noexcept
		{
			return CheckPtr(
				Overflows(nSize) ? nullptr : Backend::AllocateAligned(nSize + Backend::kTailPadding, nAlignment),
				nSize);
		}

		[[nodiscard]] void* realloc(void* lpBlock, size_t nNewSize) const noexcept
		{
			if (!lpBlock)
				return malloc(nNewSize);
			return CheckPtr(
				Overflows(nNewSize) ? nullptr : Backend::Reallocate(lpBlock, nNewSize + Backend::kTailPadding),
				nNewSize);
		}

		[[nodiscard]] void* aligned_realloc(void* lpBlock, size_t nNewSize, size_t nAlignment) const noexcept
		{
			if (!lpBlock)
				return aligned_malloc(nNewSize, nAlignment);
			return CheckPtr(
				Overflows(nNewSize) ? nullptr : Backend::ReallocateAligned(lpBlock, nNewSize + Backend::kTailPadding, nAlignment),
				nNewSize);
		}

		void free(void* lpBlock) const noexcept { Backend::Free(lpBlock); }
		void aligned_free(void* lpBlock) const noexcept { Backend::Free(lpBlock); }

		[[nodiscard]] size_t msize(void* lpBlock) const noexcept
		{
			const auto size = Backend::Size(lpBlock);
			return size > Backend::kTailPadding ? size - Backend::kTailPadding : 0;
		}

		[[nodiscard]] size_t aligned_msize(void* lpBlock, [[maybe_unused]] size_t nAlignment) const noexcept
		{
			return msize(lpBlock);
		}

		[[nodiscard]] HeapStatistics Statistics() const noexcept { return Backend::Statistics(); }
		[[nodiscard]] bool Ready() const noexcept { return m_ready; }

	private:
		bool m_ready;

		[[nodiscard]] static constexpr bool Overflows(size_t a_size) noexcept
		{
			return a_size > SIZE_MAX - Backend::kTailPadding;
		}
	};

	using ProxyVoltekHeap = ProxyHeap<Heaps::Voltek>;

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

	namespace HeapDetail
	{
		template<class F, class First, class... Rest>
		decltype(auto) VisitHeap(HeapKind a_kind, F& a_fn, HeapBackendList<First, Rest...>)
		{
			if constexpr (sizeof...(Rest) > 0)
			{
				if (a_kind != First::kKind)
					return VisitHeap(a_kind, a_fn, HeapBackendList<Rest...>{});
			}
			return a_fn.template operator()<ProxyHeap<First>>();
		}
	}

	// Stock has no heap to visit; callers handle it first.
	template<class F>
	decltype(auto) VisitHeap(HeapKind a_kind, F&& a_fn)
	{
		return HeapDetail::VisitHeap(a_kind, a_fn, HeapBackends{});
	}

	template<class F>
	decltype(auto) VisitSelectedHeap(F&& a_fn)
	{
		return VisitHeap(GetSelectedHeapKind(), a_fn);
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