#pragma once

#include <stdint.h>
#include <REX\REX\Singleton.h>

namespace Addictol
{
	constexpr inline static auto MEM_GB = 1073741824;

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
		public REX::Singleton<ProxyVoltekHeap>
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

	template<typename Heap = ProxyVoltekHeap>
	struct StdStuff
	{
		[[nodiscard]] static void* calloc(size_t nCount, size_t nSize) noexcept(true)
		{
			auto totalSize = nCount * nSize;
			auto ptr = Heap::GetSingleton()->malloc(totalSize);
			if (ptr) memset(ptr, 0, totalSize);
			return ptr;
		}

		[[nodiscard]] static void* malloc(size_t nSize) noexcept(true)
		{
			return Heap::GetSingleton()->malloc(nSize);
		}

		[[nodiscard]] static void* aligned_malloc(size_t nSize, size_t alignment) noexcept(true)
		{
			return Heap::GetSingleton()->aligned_malloc(nSize, alignment);
		}

		[[nodiscard]] static void* realloc(void* lpBlock, size_t nNewSize) noexcept(true)
		{
			return Heap::GetSingleton()->realloc(lpBlock, nNewSize);
		}

		static void free(void* block) noexcept(true)
		{
			Heap::GetSingleton()->free(block);
		}

		static void aligned_free(void* block) noexcept(true)
		{
			Heap::GetSingleton()->aligned_free(block);
		}

		[[nodiscard]] static size_t msize(void* block) noexcept(true)
		{
			return Heap::GetSingleton()->msize(block);
		}
	};
}