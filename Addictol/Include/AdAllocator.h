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

	class ProxyHeap :
		public ICheckerPointer,
		public REX::Singleton<ProxyHeap>
	{
		ProxyHeap(const ProxyHeap&) = delete;
		ProxyHeap(ProxyHeap&&) = delete;
		ProxyHeap& operator=(const ProxyHeap&) = delete;
		ProxyHeap& operator=(ProxyHeap&&) = delete;
	public:
		ProxyHeap() noexcept;
		~ProxyHeap() noexcept = default;

		[[nodiscard]] void* malloc(size_t nSize) const noexcept;
		[[nodiscard]] void* aligned_malloc(size_t nSize, [[maybe_unused]] size_t nAlignment) const noexcept;

		[[nodiscard]] void* realloc(void* lpBlock, size_t nNewSize) const noexcept;
		[[nodiscard]] void* aligned_realloc(void* lpBlock, size_t nNewSize, [[maybe_unused]] size_t nAlignment) const noexcept;

		void free(void* lpBlock) const noexcept;
		void aligned_free(void* lpBlock) const noexcept;

		[[nodiscard]] size_t msize(void* lpBlock) const noexcept;
		[[nodiscard]] size_t aligned_msize(void* lpBlock, [[maybe_unused]] size_t nAlignment) const noexcept;
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

		[[nodiscard]] void* malloc(std::size_t nSize) const noexcept;
		[[nodiscard]] void* aligned_malloc(std::size_t nSize, [[maybe_unused]] std::size_t nAlignment) const noexcept;

		[[nodiscard]] void* realloc(void* lpBlock, std::size_t nNewSize) const noexcept;
		[[nodiscard]] void* aligned_realloc(void* lpBlock, std::size_t nNewSize, [[maybe_unused]] std::size_t nAlignment) const noexcept;

		void free(void* lpBlock) const noexcept;
		void aligned_free(void* lpBlock) const noexcept;

		[[nodiscard]] std::size_t msize(void* lpBlock) const noexcept;
		[[nodiscard]] std::size_t aligned_msize(void* lpBlock, [[maybe_unused]] std::size_t nAlignment) const noexcept;
	};
}