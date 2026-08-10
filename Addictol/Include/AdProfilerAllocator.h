#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>

namespace Addictol
{
	inline constexpr std::size_t kAllocatorSizeClassCount{ 15 };
	inline constexpr std::size_t kAllocatorRequestHistogramBucketCount{ 16 };
	inline constexpr std::uint8_t kAllocatorOversizeClass{ 14 };
	inline constexpr std::uint8_t kAllocatorOversizeRequestBucket{ 15 };

	struct AllocatorBlockInfo
	{
		std::size_t requestedSize{};
		std::uint8_t bucket{ kAllocatorOversizeClass };
	};

	[[nodiscard]] constexpr std::uint8_t AllocatorSizeClass(std::size_t a_size) noexcept
	{
		if (a_size > 131072)
			return kAllocatorOversizeClass;

		const auto width = std::bit_width(static_cast<std::uint64_t>(a_size == 0 ? 0 : a_size - 1));
		const auto logarithmicBucket = width > 3 ? width - 3 : 0;
		return static_cast<std::uint8_t>(
			logarithmicBucket - static_cast<unsigned>(a_size > 2048));
	}

	[[nodiscard]] constexpr std::uint8_t AllocatorRequestHistogramBucket(std::size_t a_size) noexcept
	{
		if (a_size > 131072)
			return kAllocatorOversizeRequestBucket;

		const auto width = std::bit_width(static_cast<std::uint64_t>(a_size == 0 ? 0 : a_size - 1));
		return static_cast<std::uint8_t>(width > 3 ? width - 3 : 0);
	}

	class ProfilerAllocator
	{
		ProfilerAllocator(const ProfilerAllocator&) = delete;
		ProfilerAllocator& operator=(const ProfilerAllocator&) = delete;

	public:
		class SamplingScope
		{
			SamplingScope(const SamplingScope&) = delete;
			SamplingScope& operator=(const SamplingScope&) = delete;

		public:
			SamplingScope() noexcept;
			~SamplingScope() noexcept;

		private:
			bool m_previous;
		};

		ProfilerAllocator() = default;
		~ProfilerAllocator() = default;

		[[nodiscard]] static ProfilerAllocator* GetSingleton() noexcept;
		[[nodiscard]] static bool IsEnabled() noexcept;
		[[nodiscard]] static bool IsEnabledInConfig() noexcept;
		[[nodiscard]] static bool ShouldRecord() noexcept;
		[[nodiscard]] static bool ReadBlockInfo(
			const void* a_pointer,
			AllocatorBlockInfo& a_info) noexcept;

		static void RecordAllocation(void* a_result, std::size_t a_size) noexcept;
		static void RecordReallocation(
			bool a_hadPointer,
			bool a_hadOwnedBlock,
			const AllocatorBlockInfo& a_oldInfo,
			void* a_result,
			std::size_t a_requestedSize,
			bool a_hasResultInfo,
			const AllocatorBlockInfo& a_resultInfo) noexcept;
		static void RecordFree(const AllocatorBlockInfo& a_info) noexcept;

		[[nodiscard]] bool Install() noexcept;
		void Disable() noexcept;
		[[nodiscard]] bool IsInstalled() const noexcept { return m_installed; }

	private:
		bool m_installed{ false };
	};
}
