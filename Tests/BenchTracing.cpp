#include "Harness.h"
#include "TracingCore.h"

#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
	inline constexpr std::array benchmark_sizes{
		std::size_t{ 8 },
		std::size_t{ 16 },
		std::size_t{ 32 },
		std::size_t{ 64 },
		std::size_t{ 128 },
		std::size_t{ 256 },
		std::size_t{ 512 },
		std::size_t{ 1024 },
		std::size_t{ 4096 },
		std::size_t{ 8192 },
		std::size_t{ 16384 },
		std::size_t{ 32768 },
		std::size_t{ 65536 },
		std::size_t{ 131072 }
	};

	inline constexpr std::array scaling_threads{ 1u, 2u, 4u, 8u, 16u };
	inline constexpr std::size_t size_repetitions = 11;
	inline constexpr std::size_t scaling_repetitions = 9;
	inline std::atomic<std::uintptr_t> measurement_sink{};

	class PerformanceClock
	{
	public:
		PerformanceClock()
		{
			QueryPerformanceFrequency(&_frequency);
		}

		[[nodiscard]] std::int64_t now() const
		{
			LARGE_INTEGER value{};
			QueryPerformanceCounter(&value);
			return value.QuadPart;
		}

		[[nodiscard]] std::int64_t frequency() const
		{
			return _frequency.QuadPart;
		}

		[[nodiscard]] double nanoseconds(std::int64_t ticks) const
		{
			return static_cast<double>(ticks) * 1'000'000'000.0 / static_cast<double>(_frequency.QuadPart);
		}

	private:
		LARGE_INTEGER _frequency{};
	};

	class ThreadAffinityScope final
	{
	public:
		explicit ThreadAffinityScope(DWORD_PTR mask) noexcept :
			_previous(SetThreadAffinityMask(GetCurrentThread(), mask))
		{}

		~ThreadAffinityScope() noexcept
		{
			if (_previous)
				SetThreadAffinityMask(GetCurrentThread(), _previous);
		}

		ThreadAffinityScope(const ThreadAffinityScope&) = delete;
		ThreadAffinityScope& operator=(const ThreadAffinityScope&) = delete;

	private:
		DWORD_PTR _previous{};
	};

	class ProcessPriorityScope final
	{
	public:
		ProcessPriorityScope() noexcept :
			_process(GetCurrentProcess()),
			_previous(GetPriorityClass(_process))
		{
			SetPriorityClass(_process, HIGH_PRIORITY_CLASS);
		}

		~ProcessPriorityScope() noexcept
		{
			if (_previous)
				SetPriorityClass(_process, _previous);
		}

		ProcessPriorityScope(const ProcessPriorityScope&) = delete;
		ProcessPriorityScope& operator=(const ProcessPriorityScope&) = delete;

	private:
		HANDLE _process;
		DWORD _previous;
	};

	std::vector<DWORD_PTR> available_affinities()
	{
		DWORD_PTR process_mask = 0;
		DWORD_PTR system_mask = 0;
		if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask))
			return {};

		std::vector<DWORD_PTR> masks;
		for (unsigned parity = 0; parity < 2; ++parity)
		{
			for (unsigned bit = parity; bit < sizeof(DWORD_PTR) * 8; bit += 2)
			{
				const auto mask = DWORD_PTR{ 1 } << bit;
				if (process_mask & mask)
					masks.push_back(mask);
			}
		}
		return masks;
	}

	class RawHeap final
	{
	public:
		[[nodiscard]] static RawHeap* GetSingleton() noexcept
		{
			static RawHeap singleton;
			return &singleton;
		}

		[[nodiscard]] void* malloc(std::size_t size) const noexcept
		{
			return voltek::scalable_alloc(size);
		}

		[[nodiscard]] void* aligned_malloc(std::size_t size, std::size_t) const noexcept
		{
			return voltek::scalable_alloc(size);
		}

		[[nodiscard]] void* realloc(void* block, std::size_t size) const noexcept
		{
			return block ? voltek::scalable_realloc(block, size) : voltek::scalable_alloc(size);
		}

		[[nodiscard]] void* aligned_realloc(void* block, std::size_t size, std::size_t) const noexcept
		{
			return realloc(block, size);
		}

		void free(void* block) const noexcept
		{
			voltek::scalable_free(block);
		}

		void aligned_free(void* block) const noexcept
		{
			voltek::scalable_free(block);
		}

		[[nodiscard]] std::size_t msize(void* block) const noexcept
		{
			return voltek::scalable_msize(block);
		}

		[[nodiscard]] std::size_t aligned_msize(void* block, std::size_t) const noexcept
		{
			return voltek::scalable_msize(block);
		}
	};

	template <class Inner, bool Histogram>
	class TracingHeap final
	{
	public:
		[[nodiscard]] static TracingHeap* GetSingleton() noexcept
		{
			static TracingHeap singleton;
			return &singleton;
		}

		[[nodiscard]] void* malloc(std::size_t size) const noexcept
		{
			record(size);
			return Inner::GetSingleton()->malloc(size);
		}

		[[nodiscard]] void* aligned_malloc(std::size_t size, std::size_t alignment) const noexcept
		{
			record(size);
			return Inner::GetSingleton()->aligned_malloc(size, alignment);
		}

		[[nodiscard]] void* realloc(void* block, std::size_t size) const noexcept
		{
			record(size);
			return Inner::GetSingleton()->realloc(block, size);
		}

		[[nodiscard]] void* aligned_realloc(
			void* block,
			std::size_t size,
			std::size_t alignment) const noexcept
		{
			record(size);
			return Inner::GetSingleton()->aligned_realloc(block, size, alignment);
		}

		void free(void* block) const noexcept
		{
			Inner::GetSingleton()->free(block);
		}

		void aligned_free(void* block) const noexcept
		{
			Inner::GetSingleton()->aligned_free(block);
		}

		[[nodiscard]] std::size_t msize(void* block) const noexcept
		{
			return Inner::GetSingleton()->msize(block);
		}

		[[nodiscard]] std::size_t aligned_msize(void* block, std::size_t alignment) const noexcept
		{
			return Inner::GetSingleton()->aligned_msize(block, alignment);
		}

	private:
		static void record(std::size_t size) noexcept
		{
			if constexpr (Histogram)
				vmm_tests::TracingCore::record(size);
			else
				vmm_tests::TracingCore::record_counter();
		}
	};

	using CounterHeap = TracingHeap<RawHeap, false>;
	using HistogramHeap = TracingHeap<RawHeap, true>;

	enum class Arm : std::uint8_t
	{
		raw,
		counter,
		histogram
	};

	enum Comparison : std::size_t
	{
		raw_counter,
		counter_histogram,
		raw_histogram,
		comparison_count
	};

	struct Step
	{
		Arm arm;
		std::size_t comparison;
		std::size_t repetition;
		bool right;
	};

	struct WorkResult
	{
		std::uintptr_t checksum{};
		std::uint64_t failures{};
	};

	struct Measurements
	{
		std::vector<double> nanoseconds_per_operation;
		std::vector<std::uint64_t> failures;
	};

	struct PairSamples
	{
		std::vector<double> left;
		std::vector<double> right;
	};

	struct Distribution
	{
		double median{};
		double p25{};
		double p75{};
		double minimum{};
		double maximum{};
	};

	struct PairSummary
	{
		Distribution left;
		Distribution right;
		Distribution delta;
		Distribution percentage;
	};

	struct SweepResult
	{
		std::size_t value;
		std::uint64_t operations_per_worker;
		std::array<PairSummary, comparison_count> comparisons;
	};

	template <class Heap>
	__declspec(noinline) WorkResult run_fixed_workload(std::size_t size, std::uint64_t operations) noexcept
	{
		WorkResult result;
		auto* heap = Heap::GetSingleton();
		for (std::uint64_t operation = 0; operation < operations; ++operation)
		{
			void* pointer = heap->malloc(size);
			if (pointer)
			{
				result.checksum += reinterpret_cast<std::uintptr_t>(pointer) ^ operation;
				heap->free(pointer);
			}
			else
			{
				++result.failures;
			}
		}
		return result;
	}

	template <class Heap>
	__declspec(noinline) WorkResult run_mixed_workload(
		std::uint64_t operations,
		std::size_t initial_size_index) noexcept
	{
		WorkResult result;
		auto* heap = Heap::GetSingleton();
		auto size_index = initial_size_index % benchmark_sizes.size();
		for (std::uint64_t operation = 0; operation < operations; ++operation)
		{
			void* pointer = heap->malloc(benchmark_sizes[size_index]);
			if (pointer)
			{
				result.checksum += reinterpret_cast<std::uintptr_t>(pointer) ^ operation;
				heap->free(pointer);
			}
			else
			{
				++result.failures;
			}

			if (++size_index == benchmark_sizes.size())
				size_index = 0;
		}
		return result;
	}

	template <Arm Instrumentation>
	__declspec(noinline) WorkResult run_core_workload(
		std::size_t size,
		std::uint64_t operations) noexcept
	{
		WorkResult result;
		for (std::uint64_t operation = 0; operation < operations; ++operation)
		{
			_ReadWriteBarrier();
			if constexpr (Instrumentation == Arm::counter)
				vmm_tests::TracingCore::record_counter();
			else if constexpr (Instrumentation == Arm::histogram)
				vmm_tests::TracingCore::record(size);
			result.checksum += operation ^ 0x9E3779B97F4A7C15ull;
			_ReadWriteBarrier();
		}
		return result;
	}

	void consume(const WorkResult& result) noexcept
	{
		measurement_sink.fetch_xor(
			result.checksum ^ static_cast<std::uintptr_t>(result.failures),
			std::memory_order_relaxed);
	}

	template <class Heap>
	double measure_fixed_arm(
		const PerformanceClock& clock,
		std::size_t size,
		std::uint64_t operations,
		std::uint64_t& failures)
	{
		const auto start = clock.now();
		const auto result = run_fixed_workload<Heap>(size, operations);
		const auto elapsed = clock.now() - start;
		consume(result);
		failures = result.failures;
		return clock.nanoseconds(elapsed) / static_cast<double>(operations);
	}

	template <Arm Instrumentation>
	double measure_core_arm(
		const PerformanceClock& clock,
		std::size_t size,
		std::uint64_t operations)
	{
		const auto start = clock.now();
		const auto result = run_core_workload<Instrumentation>(size, operations);
		const auto elapsed = clock.now() - start;
		consume(result);
		return clock.nanoseconds(elapsed) / static_cast<double>(operations);
	}

	void append_pair(
		std::vector<Step>& steps,
		std::size_t comparison,
		std::size_t repetition,
		Arm left,
		Arm right)
	{
		const bool left_first = (repetition + comparison) % 2 == 0;
		if (left_first)
		{
			steps.push_back({ left, comparison, repetition, false });
			steps.push_back({ right, comparison, repetition, true });
		}
		else
		{
			steps.push_back({ right, comparison, repetition, true });
			steps.push_back({ left, comparison, repetition, false });
		}
	}

	std::vector<Step> make_steps(std::size_t repetitions)
	{
		std::vector<Step> steps;
		steps.reserve(repetitions * comparison_count * 2);
		for (std::size_t repetition = 0; repetition < repetitions; ++repetition)
		{
			if (repetition % 2 == 0)
			{
				append_pair(steps, raw_counter, repetition, Arm::raw, Arm::counter);
				append_pair(steps, counter_histogram, repetition, Arm::counter, Arm::histogram);
				append_pair(steps, raw_histogram, repetition, Arm::raw, Arm::histogram);
			}
			else
			{
				append_pair(steps, raw_histogram, repetition, Arm::raw, Arm::histogram);
				append_pair(steps, counter_histogram, repetition, Arm::counter, Arm::histogram);
				append_pair(steps, raw_counter, repetition, Arm::raw, Arm::counter);
			}
		}
		return steps;
	}

	Measurements measure_fixed_steps(
		const PerformanceClock& clock,
		std::size_t size,
		std::uint64_t operations,
		const std::vector<Step>& steps)
	{
		Measurements measurements;
		measurements.nanoseconds_per_operation.resize(steps.size());
		measurements.failures.resize(steps.size());
		for (std::size_t index = 0; index < steps.size(); ++index)
		{
			switch (steps[index].arm)
			{
			case Arm::raw:
				measurements.nanoseconds_per_operation[index] =
					measure_fixed_arm<RawHeap>(clock, size, operations, measurements.failures[index]);
				break;
			case Arm::counter:
				measurements.nanoseconds_per_operation[index] =
					measure_fixed_arm<CounterHeap>(clock, size, operations, measurements.failures[index]);
				break;
			case Arm::histogram:
				measurements.nanoseconds_per_operation[index] =
					measure_fixed_arm<HistogramHeap>(clock, size, operations, measurements.failures[index]);
				break;
			}
		}
		return measurements;
	}

	Measurements measure_core_steps(
		const PerformanceClock& clock,
		std::size_t size,
		std::uint64_t operations,
		const std::vector<Step>& steps)
	{
		Measurements measurements;
		measurements.nanoseconds_per_operation.resize(steps.size());
		measurements.failures.resize(steps.size());
		for (std::size_t index = 0; index < steps.size(); ++index)
		{
			switch (steps[index].arm)
			{
			case Arm::raw:
				measurements.nanoseconds_per_operation[index] =
					measure_core_arm<Arm::raw>(clock, size, operations);
				break;
			case Arm::counter:
				measurements.nanoseconds_per_operation[index] =
					measure_core_arm<Arm::counter>(clock, size, operations);
				break;
			case Arm::histogram:
				measurements.nanoseconds_per_operation[index] =
					measure_core_arm<Arm::histogram>(clock, size, operations);
				break;
			}
		}
		return measurements;
	}

	Measurements measure_scaling_steps(
		const PerformanceClock& clock,
		unsigned thread_count,
		std::uint64_t operations_per_worker,
		const std::vector<Step>& steps,
		const std::vector<DWORD_PTR>& affinities)
	{
		std::barrier start_barrier{ static_cast<std::ptrdiff_t>(thread_count + 1) };
		std::barrier finish_barrier{ static_cast<std::ptrdiff_t>(thread_count + 1) };
		std::vector<WorkResult> work_results(steps.size() * thread_count);
		std::vector<std::thread> threads;
		threads.reserve(thread_count);

		for (unsigned thread_index = 0; thread_index < thread_count; ++thread_index)
		{
			threads.emplace_back([&, thread_index] {
				ThreadAffinityScope affinity(affinities[thread_index % affinities.size()]);
				consume(run_mixed_workload<RawHeap>(1000, thread_index));
				consume(run_mixed_workload<CounterHeap>(1000, thread_index));
				consume(run_mixed_workload<HistogramHeap>(1000, thread_index));

				for (std::size_t step_index = 0; step_index < steps.size(); ++step_index)
				{
					start_barrier.arrive_and_wait();
					WorkResult result;
					switch (steps[step_index].arm)
					{
					case Arm::raw:
						result = run_mixed_workload<RawHeap>(operations_per_worker, thread_index);
						break;
					case Arm::counter:
						result = run_mixed_workload<CounterHeap>(operations_per_worker, thread_index);
						break;
					case Arm::histogram:
						result = run_mixed_workload<HistogramHeap>(operations_per_worker, thread_index);
						break;
					}
					work_results[step_index * thread_count + thread_index] = result;
					finish_barrier.arrive_and_wait();
				}
			});
		}

		Measurements measurements;
		measurements.nanoseconds_per_operation.resize(steps.size());
		measurements.failures.resize(steps.size());
		const auto operation_count = static_cast<double>(operations_per_worker) * thread_count;
		for (std::size_t step_index = 0; step_index < steps.size(); ++step_index)
		{
			const auto start = clock.now();
			start_barrier.arrive_and_wait();
			finish_barrier.arrive_and_wait();
			const auto elapsed = clock.now() - start;
			measurements.nanoseconds_per_operation[step_index] = clock.nanoseconds(elapsed) / operation_count;
			for (unsigned thread_index = 0; thread_index < thread_count; ++thread_index)
			{
				const auto& result = work_results[step_index * thread_count + thread_index];
				measurements.failures[step_index] += result.failures;
				consume(result);
			}
		}

		for (auto& thread : threads)
			thread.join();
		return measurements;
	}

	Measurements measure_core_scaling_steps(
		const PerformanceClock& clock,
		unsigned thread_count,
		std::uint64_t operations_per_worker,
		const std::vector<Step>& steps,
		const std::vector<DWORD_PTR>& affinities)
	{
		std::barrier start_barrier{ static_cast<std::ptrdiff_t>(thread_count + 1) };
		std::barrier finish_barrier{ static_cast<std::ptrdiff_t>(thread_count + 1) };
		std::vector<WorkResult> work_results(steps.size() * thread_count);
		std::vector<std::thread> threads;
		threads.reserve(thread_count);

		for (unsigned thread_index = 0; thread_index < thread_count; ++thread_index)
		{
			threads.emplace_back([&, thread_index] {
				ThreadAffinityScope affinity(affinities[thread_index % affinities.size()]);
				consume(run_core_workload<Arm::raw>(64, 1000));
				consume(run_core_workload<Arm::counter>(64, 1000));
				consume(run_core_workload<Arm::histogram>(64, 1000));

				for (std::size_t step_index = 0; step_index < steps.size(); ++step_index)
				{
					start_barrier.arrive_and_wait();
					WorkResult result;
					switch (steps[step_index].arm)
					{
					case Arm::raw:
						result = run_core_workload<Arm::raw>(64, operations_per_worker);
						break;
					case Arm::counter:
						result = run_core_workload<Arm::counter>(64, operations_per_worker);
						break;
					case Arm::histogram:
						result = run_core_workload<Arm::histogram>(64, operations_per_worker);
						break;
					}
					work_results[step_index * thread_count + thread_index] = result;
					finish_barrier.arrive_and_wait();
				}
			});
		}

		Measurements measurements;
		measurements.nanoseconds_per_operation.resize(steps.size());
		measurements.failures.resize(steps.size());
		for (std::size_t step_index = 0; step_index < steps.size(); ++step_index)
		{
			const auto start = clock.now();
			start_barrier.arrive_and_wait();
			finish_barrier.arrive_and_wait();
			const auto elapsed = clock.now() - start;
			measurements.nanoseconds_per_operation[step_index] =
				clock.nanoseconds(elapsed) / static_cast<double>(operations_per_worker);
			for (unsigned thread_index = 0; thread_index < thread_count; ++thread_index)
				consume(work_results[step_index * thread_count + thread_index]);
		}

		for (auto& thread : threads)
			thread.join();
		return measurements;
	}

	[[nodiscard]] double percentile(const std::vector<double>& sorted, double fraction)
	{
		const auto rank = static_cast<std::size_t>(std::ceil(fraction * sorted.size()));
		return sorted[std::min(sorted.size() - 1, std::max<std::size_t>(1, rank) - 1)];
	}

	Distribution summarize(std::vector<double> values)
	{
		std::sort(values.begin(), values.end());
		return {
			percentile(values, 0.5),
			percentile(values, 0.25),
			percentile(values, 0.75),
			values.front(),
			values.back()
		};
	}

	PairSummary summarize(const PairSamples& samples)
	{
		std::vector<double> deltas;
		std::vector<double> percentages;
		deltas.reserve(samples.left.size());
		percentages.reserve(samples.left.size());
		for (std::size_t index = 0; index < samples.left.size(); ++index)
		{
			const auto delta = samples.right[index] - samples.left[index];
			deltas.push_back(delta);
			percentages.push_back(delta * 100.0 / samples.left[index]);
		}
		return {
			summarize(samples.left),
			summarize(samples.right),
			summarize(std::move(deltas)),
			summarize(std::move(percentages))
		};
	}

	std::array<PairSummary, comparison_count> summarize_steps(
		const std::vector<Step>& steps,
		const Measurements& measurements,
		std::size_t repetitions,
		std::uint64_t& failures)
	{
		std::array<PairSamples, comparison_count> samples;
		for (auto& sample : samples)
		{
			sample.left.resize(repetitions);
			sample.right.resize(repetitions);
		}

		for (std::size_t index = 0; index < steps.size(); ++index)
		{
			const auto& step = steps[index];
			auto& values = step.right ? samples[step.comparison].right : samples[step.comparison].left;
			values[step.repetition] = measurements.nanoseconds_per_operation[index];
			failures += measurements.failures[index];
		}

		std::array<PairSummary, comparison_count> summaries;
		for (std::size_t comparison = 0; comparison < comparison_count; ++comparison)
			summaries[comparison] = summarize(samples[comparison]);
		return summaries;
	}

	std::uint64_t calibrate_operations(const PerformanceClock& clock, std::size_t size)
	{
		std::uint64_t operations = 50000;
		while (operations < 4000000)
		{
			std::uint64_t failures = 0;
			const auto start = clock.now();
			const auto result = run_fixed_workload<RawHeap>(size, operations);
			const auto elapsed = clock.now() - start;
			consume(result);
			failures += result.failures;
			if (failures != 0 || clock.nanoseconds(elapsed) >= 50'000'000.0)
				break;
			operations *= 2;
		}
		return operations;
	}

	void warm_size(std::size_t size, std::uint64_t operations)
	{
		const auto warm_operations = std::min<std::uint64_t>(operations / 10, 100000);
		consume(run_fixed_workload<RawHeap>(size, warm_operations));
		consume(run_fixed_workload<CounterHeap>(size, warm_operations));
		consume(run_fixed_workload<HistogramHeap>(size, warm_operations));
	}

	bool validate_core_and_decorator()
	{
		for (const auto& allocation_case : vmm_tests::allocation_cases)
		{
			const auto expected = allocation_case.pool == 0xFF ?
				vmm_tests::TracingCore::oversize_bucket :
				allocation_case.pool;
			if (vmm_tests::TracingCore::size_class(allocation_case.size) != expected)
			{
				std::cerr << "tracing size-class mapping failed for " << allocation_case.size << '\n';
				return false;
			}
		}

		(void)vmm_tests::TracingCore::snapshot_and_reset();
		auto* heap = HistogramHeap::GetSingleton();
		void* first = heap->malloc(16);
		if (!first || heap->msize(first) < 16)
			return false;
		heap->free(first);

		void* aligned = heap->aligned_malloc(32, 16);
		if (!aligned || heap->aligned_msize(aligned, 16) < 32)
			return false;
		heap->aligned_free(aligned);

		void* resized = heap->malloc(16);
		void* resized_result = heap->realloc(resized, 32);
		if (!resized_result)
		{
			heap->free(resized);
			return false;
		}
		heap->free(resized_result);

		void* aligned_resized = heap->aligned_malloc(32, 16);
		void* aligned_resized_result = heap->aligned_realloc(aligned_resized, 64, 16);
		if (!aligned_resized_result)
		{
			heap->aligned_free(aligned_resized);
			return false;
		}
		heap->aligned_free(aligned_resized_result);

		const auto snapshot = vmm_tests::TracingCore::snapshot_and_reset();
		if (snapshot.total != 6 ||
			snapshot.histogram[1] != 2 ||
			snapshot.histogram[2] != 3 ||
			snapshot.histogram[3] != 1)
			return false;

		{
			vmm_tests::TracingCore::SamplingScope sampling_scope;
			void* suppressed = heap->malloc(64);
			if (!suppressed)
				return false;
			heap->free(suppressed);
		}
		return vmm_tests::TracingCore::snapshot_and_reset().total == 0;
	}

	std::vector<SweepResult> measure_size_sweep(const PerformanceClock& clock, std::uint64_t& failures)
	{
		const auto steps = make_steps(size_repetitions);
		std::vector<SweepResult> results;
		results.reserve(benchmark_sizes.size());
		for (const auto size : benchmark_sizes)
		{
			const auto operations = calibrate_operations(clock, size);
			warm_size(size, operations);
			const auto measurements = measure_fixed_steps(clock, size, operations, steps);
			results.push_back({
				size,
				operations,
				summarize_steps(steps, measurements, size_repetitions, failures)
			});
		}
		return results;
	}

	std::vector<SweepResult> measure_thread_sweep(
		const PerformanceClock& clock,
		const std::vector<DWORD_PTR>& affinities,
		std::uint64_t& failures)
	{
		const auto steps = make_steps(scaling_repetitions);
		std::vector<SweepResult> results;
		results.reserve(scaling_threads.size());
		for (const auto thread_count : scaling_threads)
		{
			const auto operations_per_worker =
				std::max<std::uint64_t>(100000, 800000 / thread_count);
			const auto measurements =
				measure_scaling_steps(clock, thread_count, operations_per_worker, steps, affinities);
			results.push_back({
				thread_count,
				operations_per_worker,
				summarize_steps(steps, measurements, scaling_repetitions, failures)
			});
		}
		return results;
	}

	SweepResult measure_core_single(const PerformanceClock& clock)
	{
		const auto steps = make_steps(size_repetitions);
		constexpr std::uint64_t operations = 8000000;
		const auto measurements = measure_core_steps(clock, 64, operations, steps);
		std::uint64_t failures = 0;
		return {
			64,
			operations,
			summarize_steps(steps, measurements, size_repetitions, failures)
		};
	}

	std::vector<SweepResult> measure_core_threads(
		const PerformanceClock& clock,
		const std::vector<DWORD_PTR>& affinities)
	{
		const auto steps = make_steps(scaling_repetitions);
		constexpr std::uint64_t operations_per_worker = 2000000;
		std::vector<SweepResult> results;
		results.reserve(scaling_threads.size());
		for (const auto thread_count : scaling_threads)
		{
			const auto measurements =
				measure_core_scaling_steps(clock, thread_count, operations_per_worker, steps, affinities);
			std::uint64_t failures = 0;
			results.push_back({
				thread_count,
				operations_per_worker,
				summarize_steps(steps, measurements, scaling_repetitions, failures)
			});
		}
		return results;
	}

	void print_comparison(std::string_view label, const SweepResult& result, bool show_percentage = true)
	{
		const auto& counter = result.comparisons[raw_counter];
		const auto& histogram = result.comparisons[counter_histogram];
		const auto& full = result.comparisons[raw_histogram];
		std::cout << "[INFO] " << label << '=' << std::setw(6) << result.value
				  << " ops/worker=" << std::setw(7) << result.operations_per_worker
				  << " raw=" << std::setw(7) << std::fixed << std::setprecision(2) << full.left.median
				  << " counter=" << std::setw(7) << counter.right.median
				  << " full=" << std::setw(7) << full.right.median
				  << " counter-delta=" << std::setw(7) << counter.delta.median
				  << " hist-marginal=" << std::setw(7) << histogram.delta.median
				  << " full-delta=" << std::setw(7) << full.delta.median
				  << " ns/op IQR=[" << full.delta.p25 << ',' << full.delta.p75
				  << "] range=[" << full.delta.minimum << ',' << full.delta.maximum << ']';
		if (show_percentage)
			std::cout << " overhead=" << full.percentage.median << '%';
		std::cout << '\n';
	}

	void print_results(
		const std::vector<SweepResult>& sizes,
		const std::vector<SweepResult>& threads,
		const SweepResult& core_single,
		const std::vector<SweepResult>& core_threads,
		const vmm_tests::TracingCore::Snapshot& snapshot)
	{
		std::cout << "\nAllocation tracing decorator overhead\n";
		std::cout << "[INFO] paired alternating repetitions: sizes=" << size_repetitions
				  << ", thread counts=" << scaling_repetitions << '\n';
		std::cout << "[INFO] each op is one malloc+free; only malloc/realloc calls are counted\n";
		std::cout << "[INFO] msize and aligned_msize are forwarded without instrumentation\n";
		std::cout << "[INFO] benchmark workers are pinned to stable logical processors\n";
		for (const auto& result : sizes)
			print_comparison("size", result);
		for (const auto& result : threads)
			print_comparison("threads", result);
		std::cout << "[INFO] isolated core subtracts an optimizer-retained empty loop\n";
		print_comparison("core-size", core_single, false);
		std::cout << "[INFO] isolated core thread results are per-thread ns/call, not aggregate ns/op\n";
		for (const auto& result : core_threads)
			print_comparison("core-threads", result, false);

		const auto common = std::find_if(sizes.begin(), sizes.end(), [](const SweepResult& result) {
			return result.value == 64;
		});
		if (common != sizes.end())
		{
			const auto& full = common->comparisons[raw_histogram];
			std::cout << "[INFO] headline size=64: " << full.delta.median << " ns/allocation, "
					  << full.percentage.median << "%, IQR=[" << full.delta.p25 << ',' << full.delta.p75
					  << "] ns, range=[" << full.delta.minimum << ',' << full.delta.maximum << "] ns\n";
		}

		std::vector<double> size_deltas;
		std::vector<double> size_percentages;
		size_deltas.reserve(sizes.size());
		size_percentages.reserve(sizes.size());
		for (const auto& result : sizes)
		{
			size_deltas.push_back(result.comparisons[raw_histogram].delta.median);
			size_percentages.push_back(result.comparisons[raw_histogram].percentage.median);
		}
		const auto delta_distribution = summarize(std::move(size_deltas));
		const auto percentage_distribution = summarize(std::move(size_percentages));
		std::cout << "[INFO] cross-size median: " << delta_distribution.median << " ns/allocation, "
				  << percentage_distribution.median << "%, size-IQR=[" << delta_distribution.p25 << ','
				  << delta_distribution.p75 << "] ns, size-range=[" << delta_distribution.minimum << ','
				  << delta_distribution.maximum << "] ns\n";
		std::cout << "[INFO] tracing slots leased=" << snapshot.leased_slots
				  << ", overflowed threads=" << snapshot.overflowed_threads
				  << ", spill count=" << snapshot.spill_total << '\n';
	}
}

namespace vmm_tests
{
	int run_tracing_benchmarks()
	{
		if (!validate_core_and_decorator())
		{
			std::cerr << "tracing core/decorator validation failed\n";
			return 1;
		}

		const PerformanceClock clock;
		ProcessPriorityScope priority;
		const auto affinities = available_affinities();
		if (affinities.empty())
		{
			std::cerr << "no processor affinity is available\n";
			return 1;
		}

		std::uint64_t failures = 0;
		std::vector<SweepResult> sizes;
		SweepResult core_single;
		{
			ThreadAffinityScope affinity(affinities.front());
			sizes = measure_size_sweep(clock, failures);
			core_single = measure_core_single(clock);
		}
		const auto threads = measure_thread_sweep(clock, affinities, failures);
		const auto core_threads = measure_core_threads(clock, affinities);
		const auto snapshot = TracingCore::snapshot_and_reset();
		print_results(sizes, threads, core_single, core_threads, snapshot);
		if (failures != 0)
		{
			std::cerr << "tracing benchmark allocation failures: " << failures << '\n';
			return 1;
		}
		return 0;
	}
}
