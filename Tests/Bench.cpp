#include "Harness.h"

#include <Memory/AdAllocator.h>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <psapi.h>
#include <random>
#include <system_error>
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
	inline constexpr std::array paced_rates{ 80000.0, 400000.0, 1600000.0, 8000000.0 };

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

		[[nodiscard]] double seconds(std::int64_t ticks) const
		{
			return static_cast<double>(ticks) / static_cast<double>(_frequency.QuadPart);
		}

		[[nodiscard]] double nanoseconds(std::int64_t ticks) const
		{
			return seconds(ticks) * 1'000'000'000.0;
		}

		void wait_until(std::int64_t target) const
		{
			for (;;)
			{
				const auto remaining = target - now();
				if (remaining <= 0)
					return;
				if (remaining > frequency() / 500)
					Sleep(1);
				else
					std::this_thread::yield();
			}
		}

	private:
		LARGE_INTEGER _frequency{};
	};

	struct SingleResult
	{
		std::size_t size;
		std::uint64_t operations;
		std::uint64_t failures;
		double seconds;
		double operations_per_second;
	};

	struct ScalingResult
	{
		unsigned threads;
		std::uint64_t operations;
		std::uint64_t failures;
		double seconds;
		double operations_per_second;
		double scaling;
	};

	struct LatencyResult
	{
		double target_rate;
		double achieved_rate;
		std::uint64_t failures;
		double median_ns;
		double p99_ns;
		double p999_ns;
		double max_ns;
	};

	template<class Heap>
	bool alloc_free(std::size_t size)
	{
		auto* heap = Heap::GetSingleton();
		void* pointer = heap->malloc(size);
		if (!pointer)
			return false;
		heap->free(pointer);
		return true;
	}

	template<class Heap>
	void warm_up()
	{
		for (std::size_t iteration = 0; iteration < 2000; ++iteration)
			alloc_free<Heap>(benchmark_sizes[iteration % benchmark_sizes.size()]);
	}

	std::uint64_t single_iterations(std::size_t size)
	{
		if (size <= 1024)
			return 200000;
		if (size <= 8192)
			return 100000;
		if (size <= 32768)
			return 50000;
		return 20000;
	}

	template<class Heap>
	std::vector<SingleResult> measure_single_thread(const PerformanceClock& clock)
	{
		std::vector<SingleResult> results;
		results.reserve(benchmark_sizes.size());

		for (const auto size : benchmark_sizes)
		{
			for (std::size_t iteration = 0; iteration < 1000; ++iteration)
				alloc_free<Heap>(size);

			const auto operations = single_iterations(size);
			std::uint64_t failures = 0;
			const auto start = clock.now();
			for (std::uint64_t operation = 0; operation < operations; ++operation)
				failures += alloc_free<Heap>(size) ? 0 : 1;
			const auto elapsed = clock.seconds(clock.now() - start);
			results.push_back({ size, operations, failures, elapsed, operations / elapsed });
		}
		return results;
	}

	template<class Heap>
	ScalingResult measure_scaling(const PerformanceClock& clock, unsigned thread_count)
	{
		struct ThreadResult
		{
			std::uint64_t operations{};
			std::uint64_t failures{};
			std::int64_t finish{};
		};

		std::vector<ThreadResult> thread_results(thread_count);
		std::vector<std::thread> threads;
		threads.reserve(thread_count);
		std::barrier start_barrier{ static_cast<std::ptrdiff_t>(thread_count + 1) };
		std::int64_t start = 0;
		std::int64_t deadline = 0;

		for (unsigned thread_index = 0; thread_index < thread_count; ++thread_index)
		{
			threads.emplace_back([&, thread_index] {
				start_barrier.arrive_and_wait();
				clock.wait_until(start);
				std::uint64_t operations = 0;
				std::uint64_t failures = 0;
				while (clock.now() < deadline)
				{
					const auto size = benchmark_sizes[(operations + thread_index) % benchmark_sizes.size()];
					failures += alloc_free<Heap>(size) ? 0 : 1;
					++operations;
				}
				const auto finish = clock.now();
				thread_results[thread_index] = { operations, failures, finish };
			});
		}

		start = clock.now() + clock.frequency() / 20;
		deadline = start + clock.frequency() / 2;
		start_barrier.arrive_and_wait();
		for (auto& thread : threads)
			thread.join();

		const auto operations = std::accumulate(
			thread_results.begin(),
			thread_results.end(),
			std::uint64_t{},
			[](std::uint64_t total, const ThreadResult& result) { return total + result.operations; });
		const auto failures = std::accumulate(
			thread_results.begin(),
			thread_results.end(),
			std::uint64_t{},
			[](std::uint64_t total, const ThreadResult& result) { return total + result.failures; });
		const auto finish = std::max_element(
			thread_results.begin(),
			thread_results.end(),
			[](const ThreadResult& left, const ThreadResult& right) { return left.finish < right.finish; })
							 ->finish;
		const auto elapsed = clock.seconds(finish - start);
		return { thread_count, operations, failures, elapsed, operations / elapsed, 0.0 };
	}

	double percentile(const std::vector<double>& sorted, double fraction)
	{
		const auto rank = static_cast<std::size_t>(std::ceil(fraction * sorted.size()));
		return sorted[std::min(sorted.size() - 1, std::max<std::size_t>(1, rank) - 1)];
	}

	template<class Heap>
	LatencyResult measure_paced_latency(const PerformanceClock& clock, double target_rate)
	{
		constexpr unsigned thread_count = 8;
		constexpr std::size_t samples_per_thread = 20000;
		std::vector<std::vector<double>> latencies(
			thread_count,
			std::vector<double>(samples_per_thread));
		std::vector<std::uint64_t> failures(thread_count);
		std::vector<std::int64_t> finishes(thread_count);
		std::vector<std::thread> threads;
		threads.reserve(thread_count);
		std::barrier start_barrier{ static_cast<std::ptrdiff_t>(thread_count + 1) };
		std::int64_t start = 0;

		for (unsigned thread_index = 0; thread_index < thread_count; ++thread_index)
		{
			threads.emplace_back([&, thread_index] {
				std::uint64_t local_failures = 0;
				start_barrier.arrive_and_wait();
				for (std::size_t sample = 0; sample < samples_per_thread; ++sample)
				{
					const auto sequence = sample * thread_count + thread_index;
					const auto scheduled = start + static_cast<std::int64_t>(
						static_cast<double>(sequence) * static_cast<double>(clock.frequency()) / target_rate);
					clock.wait_until(scheduled);

					const auto before = clock.now();
					const auto size = benchmark_sizes[sequence % benchmark_sizes.size()];
					const bool succeeded = alloc_free<Heap>(size);
					const auto after = clock.now();
					latencies[thread_index][sample] = clock.nanoseconds(after - before);
					local_failures += succeeded ? 0 : 1;
				}
				finishes[thread_index] = clock.now();
				failures[thread_index] = local_failures;
			});
		}

		start = clock.now() + clock.frequency() / 20;
		start_barrier.arrive_and_wait();
		for (auto& thread : threads)
			thread.join();

		std::vector<double> combined;
		combined.reserve(thread_count * samples_per_thread);
		for (const auto& samples : latencies)
			combined.insert(combined.end(), samples.begin(), samples.end());
		std::sort(combined.begin(), combined.end());

		const auto finish = *std::max_element(finishes.begin(), finishes.end());
		const auto elapsed = clock.seconds(finish - start);
		const auto operation_count = static_cast<double>(thread_count * samples_per_thread);
		const auto failure_count = std::accumulate(failures.begin(), failures.end(), std::uint64_t{});
		return {
			target_rate,
			operation_count / elapsed,
			failure_count,
			percentile(combined, 0.5),
			percentile(combined, 0.99),
			percentile(combined, 0.999),
			combined.back()
		};
	}

	struct ChurnResult
	{
		unsigned threads;
		std::uint64_t operations;
		std::uint64_t failures;
		double operations_per_second;
		std::uint64_t live_bytes;
		std::uint64_t private_growth_bytes;
		std::uint64_t retained_bytes;
	};

	[[nodiscard]] std::uint64_t private_bytes()
	{
		PROCESS_MEMORY_COUNTERS_EX counters{};
		GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
		return counters.PrivateUsage;
	}

	// Skewed toward small blocks with occasional large ones, like engine traffic.
	[[nodiscard]] std::size_t churn_size(std::mt19937_64& a_random)
	{
		const auto roll = a_random() % 100;
		if (roll < 70)
			return 8 + a_random() % 57;
		if (roll < 90)
			return 65 + a_random() % 960;
		if (roll < 98)
			return 1025 + a_random() % 15360;
		return 16385 + a_random() % 245760;
	}

	// Slots are shared, so most frees release a block another thread allocated.
	template<class Heap>
	ChurnResult measure_churn(const PerformanceClock& clock, unsigned thread_count)
	{
		constexpr std::size_t slot_count = 65536;
		auto* heap = Heap::GetSingleton();
		std::vector<std::atomic<std::size_t*>> slots(slot_count);
		const auto before = private_bytes();
		std::atomic<std::int64_t> live{ 0 };
		std::vector<std::uint64_t> operations(thread_count);
		std::vector<std::uint64_t> failures(thread_count);
		std::vector<std::thread> threads;
		std::barrier start_barrier{ static_cast<std::ptrdiff_t>(thread_count + 1) };
		std::int64_t deadline = 0;
		for (unsigned thread_index = 0; thread_index < thread_count; ++thread_index)
		{
			threads.emplace_back([&, thread_index] {
				std::mt19937_64 random{ 0x5EED0000ull + thread_index };
				std::int64_t local_live = 0;
				start_barrier.arrive_and_wait();
				while (clock.now() < deadline)
				{
					for (unsigned batch = 0; batch < 256; ++batch)
					{
						const auto size = churn_size(random);
						auto* block = static_cast<std::size_t*>(heap->malloc(size));
						if (!block)
						{
							++failures[thread_index];
							continue;
						}
						*block = size;
						local_live += static_cast<std::int64_t>(size);
						if (auto* old = slots[random() % slot_count].exchange(block, std::memory_order_acq_rel))
						{
							local_live -= static_cast<std::int64_t>(*old);
							heap->free(old);
						}
						++operations[thread_index];
					}
				}
				live.fetch_add(local_live, std::memory_order_relaxed);
			});
		}
		const auto start = clock.now();
		deadline = start + clock.frequency() * 2;
		start_barrier.arrive_and_wait();
		for (auto& thread : threads)
			thread.join();
		const auto elapsed = clock.seconds(clock.now() - start);
		const auto grown = private_bytes();
		for (auto& slot : slots)
		{
			if (auto* block = slot.exchange(nullptr, std::memory_order_relaxed))
				heap->free(block);
		}
		// Deferred purging runs on later allocator activity, as it would in a running game.
		Sleep(1000);
		for (std::size_t iteration = 0; iteration < 100000; ++iteration)
			alloc_free<Heap>(benchmark_sizes[iteration % benchmark_sizes.size()]);
		const auto released = private_bytes();
		const auto total = std::accumulate(operations.begin(), operations.end(), std::uint64_t{});
		return {
			thread_count,
			total,
			std::accumulate(failures.begin(), failures.end(), std::uint64_t{}),
			total / elapsed,
			static_cast<std::uint64_t>(live.load()),
			grown > before ? grown - before : 0,
			released > before ? released - before : 0
		};
	}

	void print_results(		const std::vector<SingleResult>& single,
		const std::vector<ScalingResult>& scaling,
		const std::vector<LatencyResult>& latency,
		const std::vector<ChurnResult>& churn)
	{
		std::cout << "\nSingle-threaded alloc+free throughput\n";
		std::cout << std::setw(12) << "size" << std::setw(18) << "ops/sec" << std::setw(12) << "failures" << '\n';
		for (const auto& result : single)
		{
			std::cout << std::setw(12) << result.size << std::setw(18) << std::fixed << std::setprecision(0)
					  << result.operations_per_second << std::setw(12) << result.failures << '\n';
		}

		std::cout << "\nSaturated thread scaling\n";
		std::cout << std::setw(10) << "threads" << std::setw(18) << "ops/sec" << std::setw(12) << "scaling"
				  << std::setw(12) << "failures" << '\n';
		for (const auto& result : scaling)
		{
			std::cout << std::setw(10) << result.threads << std::setw(18) << std::fixed << std::setprecision(0)
					  << result.operations_per_second << std::setw(11) << std::setprecision(2) << result.scaling << 'x'
					  << std::setw(12) << result.failures << '\n';
		}

		std::cout << "\nPaced latency across 8 threads\n";
		std::cout << std::setw(14) << "target/s" << std::setw(14) << "achieved/s" << std::setw(12) << "median us"
				  << std::setw(12) << "p99 us" << std::setw(12) << "p999 us" << std::setw(12) << "max us"
				  << std::setw(12) << "failures" << '\n';
		for (const auto& result : latency)
		{
			std::cout << std::setw(14) << std::fixed << std::setprecision(0) << result.target_rate << std::setw(14)
					  << result.achieved_rate << std::setw(12) << std::setprecision(2) << result.median_ns / 1000.0
					  << std::setw(12) << result.p99_ns / 1000.0 << std::setw(12) << result.p999_ns / 1000.0
					  << std::setw(12) << result.max_ns / 1000.0 << std::setw(12) << result.failures << '\n';
		}

		std::cout << "\nShared-slot churn (cross-thread frees)\n";
		std::cout << std::setw(10) << "threads" << std::setw(18) << "ops/sec" << std::setw(14) << "live MiB"
				  << std::setw(14) << "growth MiB" << std::setw(14) << "retained MiB" << std::setw(12) << "failures" << '\n';
		for (const auto& result : churn)
		{
			std::cout << std::setw(10) << result.threads << std::setw(18) << std::fixed << std::setprecision(0)
					  << result.operations_per_second << std::setw(14) << std::setprecision(1) << result.live_bytes / 1048576.0
					  << std::setw(14) << result.private_growth_bytes / 1048576.0 << std::setw(14)
					  << result.retained_bytes / 1048576.0 << std::setw(12) << result.failures << '\n';
		}
	}

	bool write_json(
		std::string_view backend,
		const std::vector<SingleResult>& single,
		const std::vector<ScalingResult>& scaling,
		const std::vector<LatencyResult>& latency,
		const std::vector<ChurnResult>& churn)
	{
		std::error_code error;
		std::filesystem::create_directories(".Build/Tests", error);
		if (error)
		{
			std::cerr << "failed to create .Build/Tests: " << error.message() << '\n';
			return false;
		}

		const auto path = ".Build/Tests/bench-" + std::string{ backend } + ".json";
		std::ofstream output{ path, std::ios::trunc };
		if (!output)
		{
			std::cerr << "failed to open " << path << '\n';
			return false;
		}

		output << std::fixed << std::setprecision(3);
		output << "{\n  \"backend\": \"" << backend << "\",\n  \"single_thread\": [\n";
		for (std::size_t index = 0; index < single.size(); ++index)
		{
			const auto& result = single[index];
			output << "    {\"size\": " << result.size << ", \"operations\": " << result.operations
				   << ", \"failures\": " << result.failures << ", \"seconds\": " << result.seconds
				   << ", \"operations_per_second\": " << result.operations_per_second << "}"
				   << (index + 1 == single.size() ? "\n" : ",\n");
		}
		output << "  ],\n  \"thread_scaling\": [\n";
		for (std::size_t index = 0; index < scaling.size(); ++index)
		{
			const auto& result = scaling[index];
			output << "    {\"threads\": " << result.threads << ", \"operations\": " << result.operations
				   << ", \"failures\": " << result.failures << ", \"seconds\": " << result.seconds
				   << ", \"operations_per_second\": " << result.operations_per_second << ", \"scaling\": "
				   << result.scaling << "}" << (index + 1 == scaling.size() ? "\n" : ",\n");
		}
		output << "  ],\n  \"paced_latency\": [\n";
		for (std::size_t index = 0; index < latency.size(); ++index)
		{
			const auto& result = latency[index];
			output << "    {\"threads\": 8, \"target_rate\": " << result.target_rate
				   << ", \"achieved_rate\": " << result.achieved_rate << ", \"failures\": " << result.failures
				   << ", \"median_ns\": " << result.median_ns << ", \"p99_ns\": " << result.p99_ns
				   << ", \"p999_ns\": " << result.p999_ns << ", \"max_ns\": " << result.max_ns << "}"
				   << (index + 1 == latency.size() ? "\n" : ",\n");
		}
		output << "  ],\n  \"churn\": [\n";
		for (std::size_t index = 0; index < churn.size(); ++index)
		{
			const auto& result = churn[index];
			output << "    {\"threads\": " << result.threads << ", \"operations\": " << result.operations
				   << ", \"failures\": " << result.failures << ", \"operations_per_second\": " << result.operations_per_second
				   << ", \"live_bytes\": " << result.live_bytes << ", \"private_growth_bytes\": " << result.private_growth_bytes
				   << ", \"retained_bytes\": " << result.retained_bytes << "}" << (index + 1 == churn.size() ? "\n" : ",\n");
		}
		output << "  ]\n}\n";
		return output.good();
	}
}

namespace vmm_tests
{
	namespace
	{
		template<class Heap>
		int run_backend_benchmarks()
		{
			const std::string_view backend = Heap::BackendType::kName;
			std::cout << "\n=== " << backend << " ===\n";
			const PerformanceClock clock;
			// Runs first so its footprint starts from a fresh heap.
			std::vector<ChurnResult> churn{ measure_churn<Heap>(clock, 8) };
			warm_up<Heap>();

			const auto single = measure_single_thread<Heap>(clock);
			std::vector<ScalingResult> scaling;
			scaling.reserve(scaling_threads.size());
			for (const auto thread_count : scaling_threads)
				scaling.push_back(measure_scaling<Heap>(clock, thread_count));
			const auto baseline = scaling.front().operations_per_second;
			for (auto& result : scaling)
				result.scaling = result.operations_per_second / baseline;

			std::vector<LatencyResult> latency;
			latency.reserve(paced_rates.size());
			for (const auto rate : paced_rates)
			{
				warm_up<Heap>();
				latency.push_back(measure_paced_latency<Heap>(clock, rate));
			}

			print_results(single, scaling, latency, churn);
			if (!write_json(backend, single, scaling, latency, churn))
			{
				std::cerr << "benchmark JSON was not written\n";
				return 1;
			}
			std::cout << "\nJSON written to .Build/Tests/bench-" << backend << ".json\n";
			return 0;
		}
	}

	// Footprint figures are only meaningful when each backend runs in its own process.
	int run_benchmarks(std::string_view a_backend)
	{
		int result = 0;
		bool matched = false;
		[&]<class... Backends>(Addictol::HeapBackendList<Backends...>) {
			((a_backend.empty() || a_backend == Backends::kName ?
				(matched = true, result |= run_backend_benchmarks<Addictol::ProxyHeap<Backends>>()) : 0), ...);
		}(Addictol::HeapBackends{});
		if (!matched)
		{
			std::cerr << "unknown benchmark backend " << a_backend << '\n';
			return 1;
		}
		return result;
	}
}