#include <AdProfilerAllocator.h>
#include <AdProfilerCore.h>
#include <AdProfilerFrameHitch.h>
#include <AdProfilerRuntimeChannel.h>
#include <vmmblock.h>
#include <vmmmain.h>

#include <Windows.h>
#include <process.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <limits>
#include <new>
#include <ostream>
#include <string_view>
#include <utility>

namespace Addictol
{
	using namespace std::literals;

	static REX::TOML::Bool<> bProfiler{ "Profiler"sv, "bProfiler"sv, false };
	static REX::TOML::Bool<> bAllocatorProfiler{ "Profiler"sv, "bAllocatorProfiler"sv, false };
	static REX::TOML::U32<> uAllocatorProfilerDrainFrames{
		"Profiler"sv, "uAllocatorProfilerDrainFrames"sv, 30
	};
	static REX::TOML::Bool<> bMemoryManager{ "Patches"sv, "bMemoryManager"sv, true };

	static_assert(AllocatorSizeClass(0) == 0);
	static_assert(AllocatorSizeClass(1) == 0);
	static_assert(AllocatorSizeClass(8) == 0);
	static_assert(AllocatorSizeClass(9) == 1);
	static_assert(AllocatorSizeClass(16) == 1);
	static_assert(AllocatorSizeClass(17) == 2);
	static_assert(AllocatorSizeClass(1024) == 7);
	static_assert(AllocatorSizeClass(1025) == 8);
	static_assert(AllocatorSizeClass(2048) == 8);
	static_assert(AllocatorSizeClass(2049) == 8);
	static_assert(AllocatorSizeClass(4096) == 8);
	static_assert(AllocatorSizeClass(4097) == 9);
	static_assert(AllocatorSizeClass(131072) == 13);
	static_assert(AllocatorSizeClass(131073) == 14);
	static_assert(AllocatorRequestHistogramBucket(1025) == 8);
	static_assert(AllocatorRequestHistogramBucket(2048) == 8);
	static_assert(AllocatorRequestHistogramBucket(2049) == 9);

	namespace allocatorProfilerDetail
	{
		inline constexpr std::size_t kSlotCount{ 512 };
		inline constexpr std::size_t kRingCapacity{ 128 };
		inline constexpr std::size_t kRuntimeEntryCapacity{ 64 };
		inline constexpr std::array<std::uint64_t, kAllocatorSizeClassCount> kPoolPayloadCapacities{
			sizeof(voltek::memory_manager::block8_t) - sizeof(voltek::memory_manager::block_base),
			sizeof(voltek::memory_manager::block16_t) - sizeof(voltek::memory_manager::block_base),
			sizeof(voltek::memory_manager::block32_t) - sizeof(voltek::memory_manager::block_base),
			sizeof(voltek::memory_manager::block64_t) - sizeof(voltek::memory_manager::block_base),
			sizeof(voltek::memory_manager::block128_t) - sizeof(voltek::memory_manager::block_base),
			sizeof(voltek::memory_manager::block256_t) - sizeof(voltek::memory_manager::block_base),
			sizeof(voltek::memory_manager::block512_t) - sizeof(voltek::memory_manager::block_base),
			sizeof(voltek::memory_manager::block1024_t) - sizeof(voltek::memory_manager::block_base),
			sizeof(voltek::memory_manager::block4096_t) - sizeof(voltek::memory_manager::block_base),
			sizeof(voltek::memory_manager::block8192_t) - sizeof(voltek::memory_manager::block_base),
			sizeof(voltek::memory_manager::block16384_t) - sizeof(voltek::memory_manager::block_base),
			sizeof(voltek::memory_manager::block32768_t) - sizeof(voltek::memory_manager::block_base),
			sizeof(voltek::memory_manager::block65536_t) - sizeof(voltek::memory_manager::block_base),
			sizeof(voltek::memory_manager::block131072_t) - sizeof(voltek::memory_manager::block_base),
			0
		};
		inline constexpr std::array<std::uint64_t, kAllocatorSizeClassCount> kPoolStrides{
			sizeof(voltek::memory_manager::block8_t),
			sizeof(voltek::memory_manager::block16_t),
			sizeof(voltek::memory_manager::block32_t),
			sizeof(voltek::memory_manager::block64_t),
			sizeof(voltek::memory_manager::block128_t),
			sizeof(voltek::memory_manager::block256_t),
			sizeof(voltek::memory_manager::block512_t),
			sizeof(voltek::memory_manager::block1024_t),
			sizeof(voltek::memory_manager::block4096_t),
			sizeof(voltek::memory_manager::block8192_t),
			sizeof(voltek::memory_manager::block16384_t),
			sizeof(voltek::memory_manager::block32768_t),
			sizeof(voltek::memory_manager::block65536_t),
			sizeof(voltek::memory_manager::block131072_t),
			0
		};
		inline constexpr std::array<std::string_view, kAllocatorSizeClassCount> kClassNames{
			"Class8"sv,
			"Class16"sv,
			"Class32"sv,
			"Class64"sv,
			"Class128"sv,
			"Class256"sv,
			"Class512"sv,
			"Class1024"sv,
			"Class4096"sv,
			"Class8192"sv,
			"Class16384"sv,
			"Class32768"sv,
			"Class65536"sv,
			"Class131072"sv,
			"ClassOversize"sv
		};
		static_assert(
			kPoolStrides[voltek::memory_manager::POOL_8] ==
			kPoolStrides[voltek::memory_manager::POOL_16]);

		struct BucketCounters
		{
			std::atomic<std::uint64_t> allocations{};
			std::atomic<std::uint64_t> frees{};
			std::atomic<std::uint64_t> allocationBytes{};
			std::atomic<std::uint64_t> freeBytes{};
		};

		struct alignas(64) CounterSlot
		{
			std::array<BucketCounters, kAllocatorSizeClassCount> buckets{};
			std::atomic<std::uint64_t> failedAllocations{};
			std::atomic<std::uint64_t> zeroSizeAllocations{};
			std::atomic<std::uint64_t> zeroSizeFrees{};
			std::atomic<std::uint64_t> pool4096Le2048Allocations{};
		};

		static_assert(alignof(CounterSlot) == 64);
		static_assert(sizeof(CounterSlot) % 64 == 0);
		static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

		struct BucketSnapshot
		{
			std::uint64_t allocations{};
			std::uint64_t frees{};
			std::uint64_t allocationBytes{};
			std::uint64_t freeBytes{};
			std::uint64_t spillAllocations{};
			std::uint32_t touchingThreads{};
			std::uint32_t allocatingThreads{};
			std::uint32_t freeingThreads{};
		};

		struct CoreSnapshot
		{
			std::array<BucketSnapshot, kAllocatorSizeClassCount> buckets{};
			std::uint64_t failedAllocations{};
			std::uint64_t zeroSizeAllocations{};
			std::uint64_t zeroSizeFrees{};
			std::uint64_t pool4096Le2048Allocations{};
			std::size_t leasedSlots{};
			std::size_t overflowedThreads{};
		};

		struct BucketEntry
		{
			std::uint64_t allocations{};
			std::uint64_t frees{};
			std::uint64_t allocationBytes{};
			std::uint64_t freeBytes{};
			std::int64_t liveBlocks{};
			std::int64_t liveBytes{};
			std::int64_t highWaterLiveBlocks{};
			std::int64_t highWaterLiveBytes{};
			std::uint64_t cumulativeAllocations{};
			std::uint64_t cumulativeFrees{};
			std::uint64_t cumulativeAllocationBytes{};
			std::uint64_t cumulativeFreeBytes{};
			std::uint64_t cumulativeSpillAllocations{};
			std::uint32_t touchingThreads{};
			std::uint32_t allocatingThreads{};
			std::uint32_t freeingThreads{};
		};

		struct AllocatorProfileEntry
		{
			std::uint64_t saveLoadEpoch{};
			std::uint64_t monotonicUs{};
			std::uint64_t frameSequence{};
			std::uint64_t frameEndQpc{};
			std::uint64_t frameElapsedQpc{};
			double frameMs{};
			std::uint64_t intervalQpc{};
			double intervalSeconds{};
			std::uint64_t maxFrameElapsedQpc{};
			double maxFrameMs{};
			bool spansGap{};
			std::uint64_t intervalOversizeAllocations{};
			std::uint64_t cumulativeOversizeAllocations{};
			std::uint64_t intervalFailedAllocations{};
			std::uint64_t cumulativeFailedAllocations{};
			std::uint64_t intervalZeroSizeAllocations{};
			std::uint64_t cumulativeZeroSizeAllocations{};
			std::uint64_t intervalZeroSizeFrees{};
			std::uint64_t cumulativeZeroSizeFrees{};
			std::uint64_t intervalPool4096Le2048Allocations{};
			std::uint64_t cumulativePool4096Le2048Allocations{};
			std::uint64_t droppedSamples{};
			std::size_t leasedSlots{};
			std::size_t overflowedThreads{};
			std::array<BucketEntry, kAllocatorSizeClassCount> buckets{};
		};

		template <class T, std::size_t Capacity>
		class SpscRing
		{
		public:
			[[nodiscard]] bool Push(const T& a_entry) noexcept
			{
				const auto write = m_write.load(std::memory_order_relaxed);
				const auto next = (write + 1) % Capacity;
				if (next == m_read.load(std::memory_order_acquire))
					return false;

				m_entries[write] = a_entry;
				m_write.store(next, std::memory_order_release);
				return true;
			}

			[[nodiscard]] bool Pop(T& a_entry) noexcept
			{
				const auto read = m_read.load(std::memory_order_relaxed);
				if (read == m_write.load(std::memory_order_acquire))
					return false;

				a_entry = std::move(m_entries[read]);
				m_read.store((read + 1) % Capacity, std::memory_order_release);
				return true;
			}

		private:
			std::array<T, Capacity> m_entries{};
			alignas(64) std::atomic<std::size_t> m_write{};
			alignas(64) std::atomic<std::size_t> m_read{};
		};

		struct SamplerState
		{
			CoreSnapshot baseline{};
			std::array<std::int64_t, kAllocatorSizeClassCount> highWaterLiveBlocks{};
			std::array<std::int64_t, kAllocatorSizeClassCount> highWaterLiveBytes{};
			std::uint64_t lastFrameSequence{};
			std::uint64_t lastDrainQpc{};
			std::uint64_t maxFrameElapsedQpc{};
			double maxFrameMs{};
			std::uint32_t framesSinceDrain{};
			bool initialized{};
			bool spansGap{};
		};

		static std::array<CounterSlot, kSlotCount> g_slots;
		static CounterSlot g_spill;
		static std::atomic<std::size_t> g_nextSlot;
		static std::atomic<bool> g_spillLogged;
		static std::atomic<bool> g_recordingEnabled{ true };
		static thread_local CounterSlot* g_threadSlot;
		static thread_local bool g_suppressed;

		void BumpOwned(
			std::atomic<std::uint64_t>& a_counter,
			std::uint64_t a_delta = 1) noexcept
		{
			a_counter.store(
				a_counter.load(std::memory_order_relaxed) + a_delta,
				std::memory_order_relaxed);
		}

		[[nodiscard]] CounterSlot& LeaseSlot() noexcept
		{
			if (g_threadSlot)
				return *g_threadSlot;

			const auto index = g_nextSlot.fetch_add(1, std::memory_order_acq_rel);
			g_threadSlot = index < kSlotCount ? &g_slots[index] : &g_spill;
			if (index >= kSlotCount && !g_spillLogged.exchange(true, std::memory_order_acq_rel))
			{
				REX::INFO(
					"Allocator profiler: {} thread slots exhausted; subsequent distinct threads share the spill slot."sv,
					kSlotCount);
			}
			return *g_threadSlot;
		}

		void RecordSuccessfulAllocation(
			std::uint8_t a_bucketIndex,
			std::size_t a_size) noexcept
		{
			auto& slot = LeaseSlot();
			if (!a_size)
			{
				if (&slot == &g_spill) [[unlikely]]
					slot.zeroSizeAllocations.fetch_add(1, std::memory_order_relaxed);
				else
					BumpOwned(slot.zeroSizeAllocations);
				return;
			}

			auto& bucket = slot.buckets[a_bucketIndex];
			const auto pool4096Le2048 =
				a_bucketIndex == voltek::memory_manager::POOL_4096 &&
				a_size <= 2048;
			if (&slot == &g_spill) [[unlikely]]
			{
				bucket.allocations.fetch_add(1, std::memory_order_relaxed);
				bucket.allocationBytes.fetch_add(
					static_cast<std::uint64_t>(a_size),
					std::memory_order_relaxed);
				if (pool4096Le2048)
				{
					slot.pool4096Le2048Allocations.fetch_add(
						1,
						std::memory_order_relaxed);
				}
			}
			else
			{
				BumpOwned(bucket.allocations);
				BumpOwned(bucket.allocationBytes, static_cast<std::uint64_t>(a_size));
				if (pool4096Le2048)
					BumpOwned(slot.pool4096Le2048Allocations);
			}
		}

		void RecordFailedAllocation() noexcept
		{
			auto& slot = LeaseSlot();
			if (&slot == &g_spill) [[unlikely]]
				slot.failedAllocations.fetch_add(1, std::memory_order_relaxed);
			else
				BumpOwned(slot.failedAllocations);
		}

		void RecordFreedAllocation(const AllocatorBlockInfo& a_info) noexcept
		{
			auto& slot = LeaseSlot();
			if (!a_info.requestedSize)
			{
				if (&slot == &g_spill) [[unlikely]]
					slot.zeroSizeFrees.fetch_add(1, std::memory_order_relaxed);
				else
					BumpOwned(slot.zeroSizeFrees);
				return;
			}

			auto& bucket = slot.buckets[a_info.bucket];
			if (&slot == &g_spill) [[unlikely]]
			{
				bucket.frees.fetch_add(1, std::memory_order_relaxed);
				bucket.freeBytes.fetch_add(
					static_cast<std::uint64_t>(a_info.requestedSize),
					std::memory_order_relaxed);
			}
			else
			{
				BumpOwned(bucket.frees);
				BumpOwned(
					bucket.freeBytes,
					static_cast<std::uint64_t>(a_info.requestedSize));
			}
		}

		[[nodiscard]] bool ReadBlockInfo(
			const void* a_pointer,
			AllocatorBlockInfo& a_info) noexcept
		{
			// The lock-free read is valid only for the audited VMM block-header layout.
			static_assert(VOLTEK_MM_BLOCK_VERSION == 1);
			static_assert(
				voltek::memory_manager::POOL_8 == 0 &&
				voltek::memory_manager::POOL_16 == 1 &&
				voltek::memory_manager::POOL_32 == 2 &&
				voltek::memory_manager::POOL_64 == 3 &&
				voltek::memory_manager::POOL_128 == 4 &&
				voltek::memory_manager::POOL_256 == 5 &&
				voltek::memory_manager::POOL_512 == 6 &&
				voltek::memory_manager::POOL_1024 == 7 &&
				voltek::memory_manager::POOL_4096 == 8 &&
				voltek::memory_manager::POOL_8192 == 9 &&
				voltek::memory_manager::POOL_16384 == 10 &&
				voltek::memory_manager::POOL_32768 == 11 &&
				voltek::memory_manager::POOL_65536 == 12 &&
				voltek::memory_manager::POOL_131072 == 13 &&
				voltek::memory_manager::POOL_MAX == kAllocatorOversizeClass);
			__try
			{
				const auto block =
					voltek::memory_manager::get_block_handle_from_ptr(a_pointer);
				if (!voltek::memory_manager::is_valid_block(block))
				{
					a_info = {};
					return false;
				}
				const auto flags = block->flags;
				if ((flags & voltek::memory_manager::flag_block_default_used) ==
					voltek::memory_manager::flag_block_default_used)
				{
					a_info.requestedSize = block->default_block.size;
					a_info.bucket = kAllocatorOversizeClass;
					return true;
				}
				if ((flags & voltek::memory_manager::flag_block_pool_used) !=
					voltek::memory_manager::flag_block_pool_used)
				{
					a_info = {};
					return false;
				}
				a_info.requestedSize = block->size;
				if (!a_info.requestedSize)
				{
					a_info.bucket = kAllocatorOversizeClass;
					return true;
				}
				const auto poolID = block->pool_id;
				a_info.bucket = poolID < kAllocatorOversizeClass ?
					poolID :
					kAllocatorOversizeClass;
				return true;
			}
			__except (1)
			{
				a_info = {};
				return false;
			}
		}

		[[nodiscard]] std::uint8_t ReadPoolBucket(const void* a_pointer) noexcept
		{
			const auto poolID =
				voltek::memory_manager::get_pool_id_from_ptr(a_pointer);
			return poolID < kAllocatorOversizeClass ?
				poolID :
				kAllocatorOversizeClass;
		}

		void MergeSlot(CoreSnapshot& a_snapshot, const CounterSlot& a_slot, bool a_spill) noexcept
		{
			for (std::size_t index = 0; index < a_snapshot.buckets.size(); ++index)
			{
				const auto allocations = a_slot.buckets[index].allocations.load(std::memory_order_relaxed);
				const auto frees = a_slot.buckets[index].frees.load(std::memory_order_relaxed);
				auto& bucket = a_snapshot.buckets[index];
				bucket.allocations += allocations;
				bucket.frees += frees;
				bucket.allocationBytes +=
					a_slot.buckets[index].allocationBytes.load(std::memory_order_relaxed);
				bucket.freeBytes +=
					a_slot.buckets[index].freeBytes.load(std::memory_order_relaxed);
				if (a_spill)
				{
					bucket.spillAllocations += allocations;
				}
				else
				{
					bucket.touchingThreads += static_cast<std::uint32_t>(allocations != 0 || frees != 0);
					bucket.allocatingThreads += static_cast<std::uint32_t>(allocations != 0);
					bucket.freeingThreads += static_cast<std::uint32_t>(frees != 0);
				}
			}

			a_snapshot.failedAllocations +=
				a_slot.failedAllocations.load(std::memory_order_relaxed);
			a_snapshot.zeroSizeAllocations +=
				a_slot.zeroSizeAllocations.load(std::memory_order_relaxed);
			a_snapshot.zeroSizeFrees +=
				a_slot.zeroSizeFrees.load(std::memory_order_relaxed);
			a_snapshot.pool4096Le2048Allocations +=
				a_slot.pool4096Le2048Allocations.load(std::memory_order_relaxed);
		}

		[[nodiscard]] CoreSnapshot CaptureSnapshot() noexcept
		{
			CoreSnapshot snapshot;
			const auto leases = g_nextSlot.load(std::memory_order_acquire);
			snapshot.leasedSlots = std::min(leases, kSlotCount);
			snapshot.overflowedThreads = leases > kSlotCount ? leases - kSlotCount : 0;
			for (std::size_t index = 0; index < snapshot.leasedSlots; ++index)
				MergeSlot(snapshot, g_slots[index], false);
			MergeSlot(snapshot, g_spill, true);
			return snapshot;
		}

		[[nodiscard]] std::int64_t SignedDifference(
			std::uint64_t a_left,
			std::uint64_t a_right) noexcept
		{
			return a_left >= a_right ?
				static_cast<std::int64_t>(a_left - a_right) :
				-static_cast<std::int64_t>(a_right - a_left);
		}

		[[nodiscard]] std::int64_t LivePayloadCapacityBytes(
			std::size_t a_class,
			std::int64_t a_liveBlocks,
			std::int64_t a_liveBytes) noexcept
		{
			return a_class == kAllocatorOversizeClass ?
				a_liveBytes :
				a_liveBlocks * static_cast<std::int64_t>(kPoolPayloadCapacities[a_class]);
		}

		[[nodiscard]] std::int64_t LiveAllocatorBytes(
			std::size_t a_class,
			std::int64_t a_liveBlocks,
			std::int64_t a_liveBytes) noexcept
		{
			return a_class == kAllocatorOversizeClass ?
				a_liveBytes +
					a_liveBlocks * static_cast<std::int64_t>(sizeof(voltek::memory_manager::block_base)) :
				a_liveBlocks * static_cast<std::int64_t>(kPoolStrides[a_class]);
		}

		[[nodiscard]] std::int64_t LiveOverheadBytes(
			std::size_t a_class,
			std::int64_t a_liveBlocks) noexcept
		{
			const auto overhead = a_class == kAllocatorOversizeClass ?
				sizeof(voltek::memory_manager::block_base) :
				kPoolStrides[a_class] - kPoolPayloadCapacities[a_class];
			return a_liveBlocks * static_cast<std::int64_t>(overhead);
		}

		void WriteAllocatorCSVHeader(std::ostream& a_file)
		{
			WriteRuntimeCSVMetadataHeader(a_file);
			// A row may only contain values already in hand; anything requiring a game query is out of bounds.
			a_file << "FrameSequence,FrameEndQpc,FrameElapsedQpc,FrameMs,IntervalQpc,IntervalSeconds,MaxFrameElapsedQpc,MaxFrameMs,SpansGap,IntervalOversizeAllocations,CumulativeOversizeAllocations,IntervalFailedAllocations,CumulativeFailedAllocations,IntervalZeroSizeAllocations,CumulativeZeroSizeAllocations,IntervalZeroSizeFrees,CumulativeZeroSizeFrees,IntervalPool4096Le2048Allocations,CumulativePool4096Le2048Allocations,LeasedSlots,OverflowedThreads,DroppedSamples"sv;
			for (const auto name : kClassNames)
			{
				a_file << ","sv << name << "Allocations,"sv
					<< name << "Frees,"sv
					<< name << "AllocationBytes,"sv
					<< name << "FreeBytes,"sv
					<< name << "LiveBlocks,"sv
					<< name << "LiveRequestedBytes,"sv
					<< name << "LivePayloadCapacityBytes,"sv
					<< name << "LiveAllocatorBytes,"sv
					<< name << "LiveOverheadBytes,"sv
					<< name << "LiveGranularityWasteBytes,"sv
					<< name << "SampledHighWaterLiveBlocks,"sv
					<< name << "SampledHighWaterLiveRequestedBytes,"sv
					<< name << "CumulativeAllocations,"sv
					<< name << "CumulativeFrees,"sv
					<< name << "CumulativeAllocationBytes,"sv
					<< name << "CumulativeFreeBytes,"sv
					<< name << "TouchingThreads,"sv
					<< name << "AllocatingThreads,"sv
					<< name << "FreeingThreads,"sv
					<< name << "CumulativeSpillAllocations"sv;
			}
			a_file << "\n"sv;
		}

		void WriteSampleMetadata(
			std::ostream& a_file,
			const AllocatorProfileEntry& a_entry,
			const RuntimeRowMetadata& a_metadata)
		{
			a_file << a_metadata.sessionID << ","sv
				<< a_entry.saveLoadEpoch << ","sv
				<< a_entry.monotonicUs << ","sv
				<< a_metadata.channelSequence << ","sv;
		}

		void WriteAllocatorCSVEntry(
			std::ostream& a_file,
			const AllocatorProfileEntry& a_entry,
			const RuntimeRowMetadata& a_metadata)
		{
			WriteSampleMetadata(a_file, a_entry, a_metadata);
			a_file << a_entry.frameSequence << ","sv
				<< a_entry.frameEndQpc << ","sv
				<< a_entry.frameElapsedQpc << ","sv
				<< std::fixed << std::setprecision(3) << a_entry.frameMs << ","sv
				<< a_entry.intervalQpc << ","sv
				<< std::setprecision(9) << a_entry.intervalSeconds << ","sv
				<< a_entry.maxFrameElapsedQpc << ","sv
				<< std::setprecision(3) << a_entry.maxFrameMs << ","sv
				<< static_cast<unsigned>(a_entry.spansGap) << ","sv
				<< a_entry.intervalOversizeAllocations << ","sv
				<< a_entry.cumulativeOversizeAllocations << ","sv
				<< a_entry.intervalFailedAllocations << ","sv
				<< a_entry.cumulativeFailedAllocations << ","sv
				<< a_entry.intervalZeroSizeAllocations << ","sv
				<< a_entry.cumulativeZeroSizeAllocations << ","sv
				<< a_entry.intervalZeroSizeFrees << ","sv
				<< a_entry.cumulativeZeroSizeFrees << ","sv
				<< a_entry.intervalPool4096Le2048Allocations << ","sv
				<< a_entry.cumulativePool4096Le2048Allocations << ","sv
				<< a_entry.leasedSlots << ","sv
				<< a_entry.overflowedThreads << ","sv
				<< a_entry.droppedSamples;

			for (std::size_t index = 0; index < a_entry.buckets.size(); ++index)
			{
				const auto& bucket = a_entry.buckets[index];
				const auto livePayloadCapacityBytes =
					LivePayloadCapacityBytes(index, bucket.liveBlocks, bucket.liveBytes);
				const auto liveAllocatorBytes =
					LiveAllocatorBytes(index, bucket.liveBlocks, bucket.liveBytes);
				const auto liveOverheadBytes =
					LiveOverheadBytes(index, bucket.liveBlocks);
				a_file << ","sv << bucket.allocations
					<< ","sv << bucket.frees
					<< ","sv << bucket.allocationBytes
					<< ","sv << bucket.freeBytes
					<< ","sv << bucket.liveBlocks
					<< ","sv << bucket.liveBytes
					<< ","sv << livePayloadCapacityBytes
					<< ","sv << liveAllocatorBytes
					<< ","sv << liveOverheadBytes
					<< ","sv << livePayloadCapacityBytes - bucket.liveBytes
					<< ","sv << bucket.highWaterLiveBlocks
					<< ","sv << bucket.highWaterLiveBytes
					<< ","sv << bucket.cumulativeAllocations
					<< ","sv << bucket.cumulativeFrees
					<< ","sv << bucket.cumulativeAllocationBytes
					<< ","sv << bucket.cumulativeFreeBytes
					<< ","sv << bucket.touchingThreads
					<< ","sv << bucket.allocatingThreads
					<< ","sv << bucket.freeingThreads
					<< ","sv << bucket.cumulativeSpillAllocations;
			}
			a_file << "\n"sv;
		}

		struct RuntimeState
		{
			RuntimeState(
				RuntimeSessionContext& a_session,
				std::uint64_t a_frequency,
				std::uint32_t a_drainFrames,
				bool a_exportCSV) :
				channel(
					a_session,
					kRuntimeEntryCapacity,
					"allocator_runtime"sv,
					WriteAllocatorCSVHeader,
					WriteAllocatorCSVEntry),
				frequency(a_frequency),
				drainFrames(a_drainFrames),
				exportCSV(a_exportCSV)
			{}

			RuntimeChannel<AllocatorProfileEntry> channel;
			SpscRing<AllocatorProfileEntry, kRingCapacity> ring;
			SamplerState sampler;
			std::atomic<std::uint64_t> droppedSamples{};
			std::atomic<bool> stopping{};
			HANDLE wakeEvent{};
			HANDLE worker{};
			std::uint64_t frequency{};
			std::uint32_t drainFrames{};
			bool exportCSV{};
		};

		static std::atomic<RuntimeState*> g_runtime;

		[[nodiscard]] AllocatorProfileEntry MakeEntry(
			RuntimeState& a_runtime,
			const FrameTick& a_tick,
			const CoreSnapshot& a_snapshot) noexcept
		{
			auto& sampler = a_runtime.sampler;
			AllocatorProfileEntry entry;
			const auto metadata = ProfilerCore::GetRuntimeSession().Capture();
			entry.saveLoadEpoch = metadata.saveLoadEpoch;
			entry.monotonicUs = metadata.monotonicUs;
			entry.frameSequence = a_tick.sequence;
			entry.frameEndQpc = a_tick.endQpc;
			entry.frameElapsedQpc = a_tick.elapsedQpc;
			entry.frameMs = a_tick.frameMs;
			entry.intervalQpc = a_tick.endQpc - sampler.lastDrainQpc;
			entry.intervalSeconds =
				static_cast<double>(entry.intervalQpc) / static_cast<double>(a_runtime.frequency);
			entry.maxFrameElapsedQpc = sampler.maxFrameElapsedQpc;
			entry.maxFrameMs = sampler.maxFrameMs;
			entry.spansGap = sampler.spansGap;
			entry.intervalFailedAllocations =
				a_snapshot.failedAllocations - sampler.baseline.failedAllocations;
			entry.cumulativeFailedAllocations = a_snapshot.failedAllocations;
			entry.intervalZeroSizeAllocations =
				a_snapshot.zeroSizeAllocations - sampler.baseline.zeroSizeAllocations;
			entry.cumulativeZeroSizeAllocations = a_snapshot.zeroSizeAllocations;
			entry.intervalZeroSizeFrees =
				a_snapshot.zeroSizeFrees - sampler.baseline.zeroSizeFrees;
			entry.cumulativeZeroSizeFrees = a_snapshot.zeroSizeFrees;
			entry.intervalPool4096Le2048Allocations =
				a_snapshot.pool4096Le2048Allocations -
				sampler.baseline.pool4096Le2048Allocations;
			entry.cumulativePool4096Le2048Allocations =
				a_snapshot.pool4096Le2048Allocations;
			entry.leasedSlots = a_snapshot.leasedSlots;
			entry.overflowedThreads = a_snapshot.overflowedThreads;
			entry.droppedSamples = a_runtime.droppedSamples.load(std::memory_order_relaxed);

			for (std::size_t index = 0; index < entry.buckets.size(); ++index)
			{
				const auto& current = a_snapshot.buckets[index];
				const auto& baseline = sampler.baseline.buckets[index];
				auto& bucket = entry.buckets[index];
				bucket.allocations = current.allocations - baseline.allocations;
				bucket.frees = current.frees - baseline.frees;
				bucket.allocationBytes = current.allocationBytes - baseline.allocationBytes;
				bucket.freeBytes = current.freeBytes - baseline.freeBytes;
				// Live is the absolute signed alloc-minus-free sum; transient negatives preserve scan-skew diagnostics.
				bucket.liveBlocks = SignedDifference(current.allocations, current.frees);
				bucket.liveBytes = SignedDifference(current.allocationBytes, current.freeBytes);
				bucket.highWaterLiveBlocks = sampler.highWaterLiveBlocks[index];
				bucket.highWaterLiveBytes = sampler.highWaterLiveBytes[index];
				bucket.cumulativeAllocations = current.allocations;
				bucket.cumulativeFrees = current.frees;
				bucket.cumulativeAllocationBytes = current.allocationBytes;
				bucket.cumulativeFreeBytes = current.freeBytes;
				bucket.cumulativeSpillAllocations = current.spillAllocations;
				bucket.touchingThreads = current.touchingThreads;
				bucket.allocatingThreads = current.allocatingThreads;
				bucket.freeingThreads = current.freeingThreads;
			}

			entry.intervalOversizeAllocations =
				entry.buckets[kAllocatorOversizeClass].allocations;
			entry.cumulativeOversizeAllocations =
				entry.buckets[kAllocatorOversizeClass].cumulativeAllocations;
			return entry;
		}

		void FrameTickCallback(const FrameTick& a_tick) noexcept
		{
			auto runtime = g_runtime.load(std::memory_order_acquire);
			if (!runtime)
				return;

			auto& sampler = runtime->sampler;
			if (sampler.initialized && a_tick.sequence != sampler.lastFrameSequence + 1)
				sampler.spansGap = true;
			sampler.initialized = true;
			sampler.lastFrameSequence = a_tick.sequence;
			sampler.maxFrameElapsedQpc =
				std::max(sampler.maxFrameElapsedQpc, a_tick.elapsedQpc);
			sampler.maxFrameMs = std::max(sampler.maxFrameMs, a_tick.frameMs);
			if (a_tick.elapsedQpc >= runtime->frequency)
				sampler.spansGap = true;
			if (sampler.framesSinceDrain != std::numeric_limits<std::uint32_t>::max())
				++sampler.framesSinceDrain;
			if (sampler.framesSinceDrain < runtime->drainFrames)
				return;
			if (a_tick.endQpc <= sampler.lastDrainQpc)
			{
				sampler.spansGap = true;
				runtime->droppedSamples.fetch_add(1, std::memory_order_relaxed);
				return;
			}

			ProfilerAllocator::SamplingScope scope;
			const auto snapshot = CaptureSnapshot();
			const auto previousHighWaterLiveBlocks = sampler.highWaterLiveBlocks;
			const auto previousHighWaterLiveBytes = sampler.highWaterLiveBytes;
			for (std::size_t index = 0; index < snapshot.buckets.size(); ++index)
			{
				const auto& bucket = snapshot.buckets[index];
				sampler.highWaterLiveBlocks[index] = std::max(
					sampler.highWaterLiveBlocks[index],
					SignedDifference(bucket.allocations, bucket.frees));
				sampler.highWaterLiveBytes[index] = std::max(
					sampler.highWaterLiveBytes[index],
					SignedDifference(bucket.allocationBytes, bucket.freeBytes));
			}
			const auto entry = MakeEntry(*runtime, a_tick, snapshot);
			if (!runtime->ring.Push(entry))
			{
				// High-water must stay reconstructible as the maximum over published rows.
				sampler.highWaterLiveBlocks = previousHighWaterLiveBlocks;
				sampler.highWaterLiveBytes = previousHighWaterLiveBytes;
				runtime->droppedSamples.fetch_add(1, std::memory_order_relaxed);
				sampler.spansGap = true;
				return;
			}

			sampler.baseline = snapshot;
			sampler.lastDrainQpc = a_tick.endQpc;
			sampler.framesSinceDrain = 0;
			sampler.maxFrameElapsedQpc = 0;
			sampler.maxFrameMs = 0.0;
			sampler.spansGap = false;
			SetEvent(runtime->wakeEvent);
		}

		unsigned __stdcall WorkerMain(void* a_context) noexcept
		{
			auto& runtime = *static_cast<RuntimeState*>(a_context);
			ProfilerAllocator::SamplingScope scope;
			bool overflowWarned = false;
			for (;;)
			{
				WaitForSingleObject(runtime.wakeEvent, 1000);
				AllocatorProfileEntry entry;
				while (runtime.ring.Pop(entry))
					runtime.channel.Record(std::move(entry), runtime.exportCSV);

				const auto dropped = runtime.droppedSamples.load(std::memory_order_relaxed);
				if (dropped && !overflowWarned)
				{
					REX::WARN(
						"Allocator profiler: sample ring overflowed; {} drain attempts have been coalesced."sv,
						dropped);
					overflowWarned = true;
				}
				if (runtime.stopping.load(std::memory_order_acquire))
					break;
			}
			return 0;
		}

		void DestroyFailedRuntime(RuntimeState* a_runtime, HANDLE a_worker) noexcept
		{
			if (a_worker)
			{
				a_runtime->stopping.store(true, std::memory_order_release);
				SetEvent(a_runtime->wakeEvent);
				WaitForSingleObject(a_worker, INFINITE);
				CloseHandle(a_worker);
			}
			if (a_runtime->wakeEvent)
				CloseHandle(a_runtime->wakeEvent);
			delete a_runtime;
		}
	}

	ProfilerAllocator::SamplingScope::SamplingScope() noexcept :
		m_previous(allocatorProfilerDetail::g_suppressed)
	{
		allocatorProfilerDetail::g_suppressed = true;
	}

	ProfilerAllocator::SamplingScope::~SamplingScope() noexcept
	{
		allocatorProfilerDetail::g_suppressed = m_previous;
	}

	ProfilerAllocator* ProfilerAllocator::GetSingleton() noexcept
	{
		static ProfilerAllocator singleton;
		return std::addressof(singleton);
	}

	bool ProfilerAllocator::IsEnabled() noexcept
	{
		return bProfiler.GetValue() && bAllocatorProfiler.GetValue();
	}

	bool ProfilerAllocator::IsEnabledInConfig() noexcept
	{
		return bAllocatorProfiler.GetValue();
	}

	bool ProfilerAllocator::ShouldRecord() noexcept
	{
		return allocatorProfilerDetail::g_recordingEnabled.load(std::memory_order_relaxed) &&
			!allocatorProfilerDetail::g_suppressed;
	}

	bool ProfilerAllocator::ReadBlockInfo(
		const void* a_pointer,
		AllocatorBlockInfo& a_info) noexcept
	{
		return allocatorProfilerDetail::ReadBlockInfo(a_pointer, a_info);
	}

	void ProfilerAllocator::RecordAllocation(void* a_result, std::size_t a_size) noexcept
	{
		if (!a_result)
		{
			if (a_size)
				allocatorProfilerDetail::RecordFailedAllocation();
			return;
		}
		if (!a_size)
		{
			allocatorProfilerDetail::RecordSuccessfulAllocation(
				kAllocatorOversizeClass,
				0);
			return;
		}

		allocatorProfilerDetail::RecordSuccessfulAllocation(
			allocatorProfilerDetail::ReadPoolBucket(a_result),
			a_size);
	}

	void ProfilerAllocator::RecordReallocation(
		bool a_hadPointer,
		bool a_hadOwnedBlock,
		const AllocatorBlockInfo& a_oldInfo,
		void* a_result,
		std::size_t a_requestedSize,
		bool a_hasResultInfo,
		const AllocatorBlockInfo& a_resultInfo) noexcept
	{
		if (!a_hadPointer)
		{
			RecordAllocation(a_result, a_requestedSize);
			return;
		}
		if (!a_requestedSize)
		{
			if (a_hadOwnedBlock)
				allocatorProfilerDetail::RecordFreedAllocation(a_oldInfo);
			return;
		}
		if (!a_hadOwnedBlock)
			return;
		if (!a_result)
		{
			allocatorProfilerDetail::RecordFailedAllocation();
			return;
		}
		if (!a_hasResultInfo)
			return;

		allocatorProfilerDetail::RecordFreedAllocation(a_oldInfo);
		allocatorProfilerDetail::RecordSuccessfulAllocation(
			a_resultInfo.bucket,
			a_resultInfo.requestedSize);
	}

	void ProfilerAllocator::RecordFree(const AllocatorBlockInfo& a_info) noexcept
	{
		allocatorProfilerDetail::RecordFreedAllocation(a_info);
	}

	void ProfilerAllocator::Disable() noexcept
	{
		using namespace allocatorProfilerDetail;

		SamplingScope scope;
		g_recordingEnabled.store(false, std::memory_order_release);
		auto runtime = g_runtime.exchange(nullptr, std::memory_order_acq_rel);
		if (runtime)
			DestroyFailedRuntime(runtime, runtime->worker);
		m_installed = false;
	}

	bool ProfilerAllocator::Install() noexcept
	{
		using namespace allocatorProfilerDetail;

		if (m_installed)
			return true;

		SamplingScope scope;
		if (!bMemoryManager.GetValue())
		{
			g_recordingEnabled.store(false, std::memory_order_release);
			REX::WARN(
				"Allocator profiler: bMemoryManager is disabled; no allocator hooks exist, so profiling was not installed."sv);
			return false;
		}
		if (!ProfilerCore::GetSingleton()->IsActive())
		{
			g_recordingEnabled.store(false, std::memory_order_release);
			REX::WARN("Allocator profiler: shared profiler session is inactive; profiling was not installed."sv);
			return false;
		}

		LARGE_INTEGER frequency{};
		if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
		{
			g_recordingEnabled.store(false, std::memory_order_release);
			REX::WARN("Allocator profiler: QueryPerformanceFrequency failed; profiling was not installed."sv);
			return false;
		}

		auto drainFrames = uAllocatorProfilerDrainFrames.GetValue();
		if (!drainFrames)
		{
			REX::WARN("Allocator profiler: uAllocatorProfilerDrainFrames is 0; using 1."sv);
			drainFrames = 1;
		}

		RuntimeState* runtime = nullptr;
		try
		{
			runtime = new RuntimeState(
				ProfilerCore::GetRuntimeSession(),
				static_cast<std::uint64_t>(frequency.QuadPart),
				drainFrames,
				ProfilerCore::IsCSVExportEnabled());
		}
		catch (const std::exception& error)
		{
			g_recordingEnabled.store(false, std::memory_order_release);
			REX::WARN("Allocator profiler: runtime-state creation failed: {}"sv, error.what());
			return false;
		}
		catch (...)
		{
			g_recordingEnabled.store(false, std::memory_order_release);
			REX::WARN("Allocator profiler: runtime-state creation failed."sv);
			return false;
		}

		runtime->wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!runtime->wakeEvent)
		{
			g_recordingEnabled.store(false, std::memory_order_release);
			REX::WARN("Allocator profiler: worker event creation failed; profiling was not installed."sv);
			DestroyFailedRuntime(runtime, nullptr);
			return false;
		}

		const auto workerValue = _beginthreadex(nullptr, 0, WorkerMain, runtime, 0, nullptr);
		auto worker = reinterpret_cast<HANDLE>(workerValue);
		if (!worker)
		{
			g_recordingEnabled.store(false, std::memory_order_release);
			REX::WARN("Allocator profiler: worker thread creation failed; profiling was not installed."sv);
			DestroyFailedRuntime(runtime, nullptr);
			return false;
		}

		// Seed the baseline before ticks arrive so the first published interval covers startup.
		{
			LARGE_INTEGER installQpc{};
			QueryPerformanceCounter(&installQpc);
			runtime->sampler.baseline = CaptureSnapshot();
			runtime->sampler.lastDrainQpc = static_cast<std::uint64_t>(installQpc.QuadPart);
			for (std::size_t index = 0; index < runtime->sampler.baseline.buckets.size(); ++index)
			{
				const auto& bucket = runtime->sampler.baseline.buckets[index];
				runtime->sampler.highWaterLiveBlocks[index] =
					SignedDifference(bucket.allocations, bucket.frees);
				runtime->sampler.highWaterLiveBytes[index] =
					SignedDifference(bucket.allocationBytes, bucket.freeBytes);
			}
		}

		if (!ProfilerFrameHitch::RegisterFrameTick(allocatorProfilerDetail::FrameTickCallback))
		{
			g_recordingEnabled.store(false, std::memory_order_release);
			REX::WARN("Allocator profiler: frame-tick registration failed; profiling was not installed."sv);
			DestroyFailedRuntime(runtime, worker);
			return false;
		}

		runtime->worker = worker;
		g_runtime.store(runtime, std::memory_order_release);
		m_installed = true;
		REX::INFO(
			"Allocator profiler: installed with {} thread slots and a {}-frame drain interval."sv,
			kSlotCount,
			drainFrames);
		return true;
	}
}
