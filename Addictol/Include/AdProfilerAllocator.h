#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Addictol
{
	inline constexpr std::size_t kAllocatorSizeClassCount{ 15 };
	inline constexpr std::size_t kAllocatorRequestHistogramBucketCount{ 16 };
	inline constexpr std::uint8_t kAllocatorOversizeClass{ 14 };
	inline constexpr std::uint8_t kAllocatorOversizeRequestBucket{ 15 };
	inline constexpr std::size_t kAllocatorProfileEntryCapacity{ 64 };

	// One published row per size class; the recorder fills these and never reads them back.
	struct AllocatorBucketEntry
	{
		uint64_t allocations{};
		uint64_t frees{};
		uint64_t allocationBytes{};
		uint64_t freeBytes{};
		int64_t liveBlocks{};
		int64_t liveBytes{};
		int64_t highWaterLiveBlocks{};
		int64_t highWaterLiveBytes{};
		uint64_t cumulativeAllocations{};
		uint64_t cumulativeFrees{};
		uint64_t cumulativeAllocationBytes{};
		uint64_t cumulativeFreeBytes{};
		uint64_t cumulativeSpillAllocations{};
		uint32_t touchingThreads{};
		uint32_t allocatingThreads{};
		uint32_t freeingThreads{};
	};

	struct AllocatorProfileEntry
	{
		uint64_t saveLoadEpoch{};
		uint64_t monotonicUs{};
		uint64_t frameSequence{};
		uint64_t frameEndQpc{};
		uint64_t frameElapsedQpc{};
		double frameMs{};
		uint64_t intervalQpc{};
		double intervalSeconds{};
		uint64_t maxFrameElapsedQpc{};
		double maxFrameMs{};
		bool spansGap{};
		uint64_t intervalOversizeAllocations{};
		uint64_t cumulativeOversizeAllocations{};
		uint64_t intervalFailedAllocations{};
		uint64_t cumulativeFailedAllocations{};
		uint64_t intervalZeroSizeAllocations{};
		uint64_t cumulativeZeroSizeAllocations{};
		uint64_t intervalZeroSizeFrees{};
		uint64_t cumulativeZeroSizeFrees{};
		uint64_t intervalPool4096Le2048Allocations{};
		uint64_t cumulativePool4096Le2048Allocations{};
		uint64_t droppedSamples{};
		std::size_t leasedSlots{};
		std::size_t overflowedThreads{};
		std::array<AllocatorBucketEntry, kAllocatorSizeClassCount> buckets{};
	};

	[[nodiscard]] std::string_view AllocatorSizeClassName(std::size_t a_index) noexcept;

	// Derived occupancy for one published bucket; the pool geometry stays inside the recorder.
	struct AllocatorBucketDerived
	{
		int64_t payloadCapacityBytes{};
		int64_t allocatorBytes{};
		int64_t overheadBytes{};
		int64_t granularityWasteBytes{};
	};

	[[nodiscard]] AllocatorBucketDerived AllocatorBucketDerivedBytes(
		std::size_t a_class,
		const AllocatorBucketEntry& a_bucket) noexcept;

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

		// Copies of published intervals; the SPSC ring and recorder counters stay private.
		[[nodiscard]] static bool CopyLatestInterval(AllocatorProfileEntry& a_out) noexcept;
		static void CopyIntervals(std::vector<AllocatorProfileEntry>& a_out) noexcept;

	private:
		bool m_installed{ false };
	};
}
