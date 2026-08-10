#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace vmm_tests
{
	class TracingCore final
	{
	public:
		static constexpr std::size_t slot_count = 256;
		static constexpr std::size_t histogram_buckets = 15;
		static constexpr std::uint8_t oversize_bucket = 14;

		struct Snapshot
		{
			std::uint64_t total{};
			std::array<std::uint64_t, histogram_buckets> histogram{};
			std::uint64_t spill_total{};
			std::size_t leased_slots{};
			std::size_t overflowed_threads{};
		};

		class SamplingScope final
		{
		public:
			SamplingScope() noexcept :
				_previous(suppressed_)
			{
				suppressed_ = true;
			}

			~SamplingScope() noexcept
			{
				suppressed_ = _previous;
			}

			SamplingScope(const SamplingScope&) = delete;
			SamplingScope& operator=(const SamplingScope&) = delete;

		private:
			bool _previous;
		};

		[[nodiscard]] static std::uint8_t size_class(std::size_t size) noexcept
		{
			if (size > 131072)
				return oversize_bucket;

			const auto width = bit_width(size == 0 ? 0 : size - 1);
			const auto logarithmic_bucket = width > 3 ? width - 3 : 0;
			return static_cast<std::uint8_t>(logarithmic_bucket - static_cast<unsigned>(size > 2048));
		}

		static void record_counter() noexcept
		{
			if (suppressed_)
				return;

			auto& slot = lease_slot();
			bump(slot.total, &slot == &spill_);
		}

		static void record(std::size_t size) noexcept
		{
			if (suppressed_)
				return;

			auto& slot = lease_slot();
			const auto shared = &slot == &spill_;
			bump(slot.total, shared);
			bump(slot.histogram[size_class(size)], shared);
		}

		// Safe only because the benchmark samples after workers join; a live sampler needs monotonic counters and caller-side deltas.
		[[nodiscard]] static Snapshot snapshot_and_reset() noexcept
		{
			SamplingScope sampling_scope;
			Snapshot snapshot;
			const auto leases = next_slot_.load(std::memory_order_acquire);
			snapshot.leased_slots = leases < slot_count ? leases : slot_count;
			snapshot.overflowed_threads = leases > slot_count ? leases - slot_count : 0;

			for (std::size_t index = 0; index < snapshot.leased_slots; ++index)
				merge_and_reset(snapshot, slots_[index], false);
			merge_and_reset(snapshot, spill_, true);
			return snapshot;
		}

	private:
		struct alignas(64) CounterSlot
		{
			std::atomic<std::uint64_t> total{};
			std::array<std::atomic<std::uint64_t>, histogram_buckets> histogram{};
		};

		static_assert(alignof(CounterSlot) == 64);
		static_assert(sizeof(CounterSlot) % 64 == 0);

		// An owned slot has one writer so a relaxed load/store pair avoids the lock prefix; the shared spill slot needs a real RMW.
		static void bump(std::atomic<std::uint64_t>& counter, bool shared) noexcept
		{
			if (shared)
				counter.fetch_add(1, std::memory_order_relaxed);
			else
				counter.store(counter.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
		}

		[[nodiscard]] static unsigned bit_width(std::size_t value) noexcept
		{
			return static_cast<unsigned>(std::bit_width(static_cast<std::uint64_t>(value)));
		}

		[[nodiscard]] static CounterSlot& lease_slot() noexcept
		{
			if (thread_slot_)
				return *thread_slot_;

			const auto index = next_slot_.fetch_add(1, std::memory_order_acq_rel);
			thread_slot_ = index < slot_count ? &slots_[index] : &spill_;
			return *thread_slot_;
		}

		static void merge_and_reset(Snapshot& snapshot, CounterSlot& slot, bool spill) noexcept
		{
			const auto total = slot.total.exchange(0, std::memory_order_relaxed);
			snapshot.total += total;
			if (spill)
				snapshot.spill_total += total;
			for (std::size_t bucket = 0; bucket < histogram_buckets; ++bucket)
				snapshot.histogram[bucket] += slot.histogram[bucket].exchange(0, std::memory_order_relaxed);
		}

		inline static std::array<CounterSlot, slot_count> slots_{};
		inline static CounterSlot spill_{};
		inline static std::atomic<std::size_t> next_slot_{};
		inline static thread_local CounterSlot* thread_slot_ = nullptr;
		inline static thread_local bool suppressed_ = false;
	};
}
