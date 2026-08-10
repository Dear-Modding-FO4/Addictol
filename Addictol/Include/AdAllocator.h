#pragma once

#include <array>
#include <stdint.h>
#include <string_view>
#include <AdProfilerAllocator.h>
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

	template<typename Heap>
	class TracingHeap :
		public REX::TSingleton<TracingHeap<Heap>>
	{
	public:
		TracingHeap() noexcept :
			m_heap(*Heap::GetSingleton())
		{}
		~TracingHeap() noexcept = default;

		[[nodiscard]] void* malloc(size_t nSize) const noexcept
		{
			auto& heap = m_heap;
			if (!ProfilerAllocator::ShouldRecord())
				return heap.malloc(nSize);

			ProfilerAllocator::SamplingScope scope;
			auto result = heap.malloc(nSize);
			ProfilerAllocator::RecordAllocation(result, nSize);
			return result;
		}

		[[nodiscard]] void* aligned_malloc(size_t nSize, size_t nAlignment) const noexcept
		{
			auto& heap = m_heap;
			if (!ProfilerAllocator::ShouldRecord())
				return heap.aligned_malloc(nSize, nAlignment);

			ProfilerAllocator::SamplingScope scope;
			auto result = heap.aligned_malloc(nSize, nAlignment);
			ProfilerAllocator::RecordAllocation(result, nSize);
			return result;
		}

		[[nodiscard]] void* realloc(void* lpBlock, size_t nNewSize) const noexcept
		{
			auto& heap = m_heap;
			if (!ProfilerAllocator::ShouldRecord())
				return heap.realloc(lpBlock, nNewSize);

			ProfilerAllocator::SamplingScope scope;
			AllocatorBlockInfo oldInfo;
			const auto hadOwnedBlock =
				lpBlock && ProfilerAllocator::ReadBlockInfo(lpBlock, oldInfo);
			auto result = heap.realloc(lpBlock, nNewSize);
			AllocatorBlockInfo resultInfo;
			bool hasResultInfo = false;
			if (lpBlock && nNewSize && result)
				hasResultInfo = ProfilerAllocator::ReadBlockInfo(result, resultInfo);
			ProfilerAllocator::RecordReallocation(
				lpBlock != nullptr,
				hadOwnedBlock,
				oldInfo,
				result,
				nNewSize,
				hasResultInfo,
				resultInfo);
			return result;
		}

		[[nodiscard]] void* aligned_realloc(
			void* lpBlock,
			size_t nNewSize,
			size_t nAlignment) const noexcept
		{
			auto& heap = m_heap;
			if (!ProfilerAllocator::ShouldRecord())
				return heap.aligned_realloc(lpBlock, nNewSize, nAlignment);

			ProfilerAllocator::SamplingScope scope;
			AllocatorBlockInfo oldInfo;
			const auto hadOwnedBlock =
				lpBlock && ProfilerAllocator::ReadBlockInfo(lpBlock, oldInfo);
			auto result = heap.aligned_realloc(lpBlock, nNewSize, nAlignment);
			AllocatorBlockInfo resultInfo;
			bool hasResultInfo = false;
			if (lpBlock && nNewSize && result)
				hasResultInfo = ProfilerAllocator::ReadBlockInfo(result, resultInfo);
			ProfilerAllocator::RecordReallocation(
				lpBlock != nullptr,
				hadOwnedBlock,
				oldInfo,
				result,
				nNewSize,
				hasResultInfo,
				resultInfo);
			return result;
		}

		void free(void* lpBlock) const noexcept
		{
			auto& heap = m_heap;
			if (!lpBlock || !ProfilerAllocator::ShouldRecord())
			{
				heap.free(lpBlock);
				return;
			}

			ProfilerAllocator::SamplingScope scope;
			AllocatorBlockInfo info;
			const auto measured = ProfilerAllocator::ReadBlockInfo(lpBlock, info);
			heap.free(lpBlock);
			if (measured)
				ProfilerAllocator::RecordFree(info);
		}

		void aligned_free(void* lpBlock) const noexcept
		{
			auto& heap = m_heap;
			if (!lpBlock || !ProfilerAllocator::ShouldRecord())
			{
				heap.aligned_free(lpBlock);
				return;
			}

			ProfilerAllocator::SamplingScope scope;
			AllocatorBlockInfo info;
			const auto measured = ProfilerAllocator::ReadBlockInfo(lpBlock, info);
			heap.aligned_free(lpBlock);
			if (measured)
				ProfilerAllocator::RecordFree(info);
		}

		[[nodiscard]] size_t msize(void* lpBlock) const noexcept
		{
			return m_heap.msize(lpBlock);
		}

		[[nodiscard]] size_t aligned_msize(void* lpBlock, size_t nAlignment) const noexcept
		{
			return m_heap.aligned_msize(lpBlock, nAlignment);
		}

	private:
		Heap& m_heap;
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
			if (ProfilerAllocator::IsEnabled())
				return a_fn.template operator()<TracingHeap<ProxyVoltekHeap>>();
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