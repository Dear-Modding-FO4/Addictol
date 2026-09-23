#include "../Addictol/Include/Core/AdClock.h"
#include "../Addictol/Include/Telemetry/AdOperationProfile.h"
#include "../Addictol/Include/Telemetry/AdTelemetryHub.h"
#include <Memory/AdProfiledHeap.h>
#include "Harness.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <numeric>
#include <thread>
#include <vector>

namespace
{
	using namespace Addictol;

	inline constexpr uint32_t kSamplingPeriod{ 16 };
	inline constexpr size_t kRecordCapacity{ 65536 };
	inline constexpr uint32_t kPublicationShards{ 16 };
	inline constexpr uint32_t kCollectorCadenceMs{ 10 };
	inline constexpr uint32_t kWarmupMs{ 250 };
	inline constexpr uint32_t kMeasurementMs{ 750 };
	inline constexpr uint32_t kRepeats{ 3 };
	inline constexpr uint32_t kOperationBatch{ 256 };
	inline constexpr uint32_t kLatencySampleStride{ 4096 };
	inline constexpr size_t kLatencySamplesPerThread{ 2048 };

	inline constexpr std::array kBenchmarkDescriptor{
		OperationProfileDescriptor{
			"benchmark.operation",
			"Benchmark operation",
			"profile.benchmark_operations",
			kSamplingPeriod
		}
	};

	class PerformanceClock
	{
	public:
		PerformanceClock()
		{
			QueryPerformanceFrequency(&m_frequency);
		}

		[[nodiscard]] uint64_t Now() const noexcept
		{
			LARGE_INTEGER value{};
			QueryPerformanceCounter(&value);
			return static_cast<uint64_t>(value.QuadPart);
		}

		[[nodiscard]] uint64_t Frequency() const noexcept
		{
			return static_cast<uint64_t>(m_frequency.QuadPart);
		}

		[[nodiscard]] uint64_t TicksForMilliseconds(uint32_t a_ms) const noexcept
		{
			return Frequency() * a_ms / 1000;
		}

		[[nodiscard]] double Seconds(uint64_t a_ticks) const noexcept
		{
			return static_cast<double>(a_ticks) /
				static_cast<double>(m_frequency.QuadPart);
		}

		[[nodiscard]] double Nanoseconds(uint64_t a_ticks) const noexcept
		{
			return Seconds(a_ticks) * 1'000'000'000.0;
		}

	private:
		LARGE_INTEGER m_frequency{};
	};

	struct QpcCalibration
	{
		double resolutionNanoseconds{ 0.0 };
		double averageReadNanoseconds{ 0.0 };
		double minimumPairNanoseconds{ 0.0 };
		double medianPairNanoseconds{ 0.0 };
	};

	struct ProfileBenchmarkResult
	{
		std::string mode;
		uint32_t threads{ 0 };
		uint32_t repeat{ 0 };
		uint64_t attempts{ 0 };
		uint64_t selected{ 0 };
		uint64_t accepted{ 0 };
		uint64_t rejected{ 0 };
		double seconds{ 0.0 };
		double operationsPerSecond{ 0.0 };
		size_t latencySamples{ 0 };
		double medianNanoseconds{ 0.0 };
		double p99Nanoseconds{ 0.0 };
		double p999Nanoseconds{ 0.0 };
		double maximumNanoseconds{ 0.0 };
		uint64_t collectorProgress{ 0 };
		OperationProfileCounters losses{};
		uint32_t samplingPeriod{ 0 };
		size_t recordCapacity{ 0 };
	};

	class CadenceCollector
	{
	public:
		explicit CadenceCollector(std::function<void()> a_collect) :
			m_collect(std::move(a_collect))
		{}

		~CadenceCollector()
		{
			Stop();
		}

		void Start()
		{
			m_running.store(true, std::memory_order_release);
			m_thread = std::thread([this] {
				const auto cadence =
					std::chrono::milliseconds{ kCollectorCadenceMs };
				auto deadline = std::chrono::steady_clock::now() + cadence;
				while (m_running.load(std::memory_order_acquire))
				{
					std::this_thread::sleep_until(deadline);
					if (!m_running.load(std::memory_order_acquire))
						break;
					m_collect();
					m_progress.fetch_add(1, std::memory_order_relaxed);
					deadline += cadence;
				}
			});
		}

		void Stop()
		{
			m_running.store(false, std::memory_order_release);
			if (m_thread.joinable())
				m_thread.join();
		}

		[[nodiscard]] uint64_t Progress() const noexcept
		{
			return m_progress.load(std::memory_order_relaxed);
		}

	private:
		std::function<void()> m_collect;
		std::atomic<bool> m_running{ false };
		std::atomic<uint64_t> m_progress{ 0 };
		std::thread m_thread{};
	};

	[[nodiscard]] double Percentile(
		const std::vector<double>& a_sorted,
		double a_fraction)
	{
		if (a_sorted.empty())
			return 0.0;
		const auto rank = static_cast<size_t>(
			std::ceil(a_fraction * a_sorted.size()));
		return a_sorted[
			std::min(a_sorted.size() - 1, (std::max)(size_t{ 1 }, rank) - 1)];
	}

	[[nodiscard]] QpcCalibration CalibrateQpc(
		const PerformanceClock& a_clock)
	{
		std::vector<double> pairs(10000);
		for (auto& pair : pairs)
		{
			const auto before = a_clock.Now();
			const auto after = a_clock.Now();
			pair = a_clock.Nanoseconds(after - before);
		}
		std::sort(pairs.begin(), pairs.end());
		constexpr uint64_t readCount{ 100000 };
		auto sink = uint64_t{ 0 };
		const auto begin = a_clock.Now();
		for (uint64_t index = 0; index < readCount; ++index)
			sink ^= a_clock.Now();
		const auto end = a_clock.Now();
		std::atomic_signal_fence(std::memory_order_seq_cst);
		(void)sink;
		return {
			1'000'000'000.0 / static_cast<double>(a_clock.Frequency()),
			a_clock.Nanoseconds(end - begin) /
				static_cast<double>(readCount),
			pairs.front(),
			Percentile(pairs, 0.5)
		};
	}

	[[nodiscard]] OperationProfileCounters CounterDelta(
		const OperationProfileCounters& a_after,
		const OperationProfileCounters& a_before) noexcept
	{
		OperationProfileCounters delta{};
		for (size_t index = 0; index < delta.values.size(); ++index)
			delta.values[index] = a_after.values[index] - a_before.values[index];
		return delta;
	}

	[[nodiscard]] uint64_t Rejected(
		const OperationProfileCounters& a_counters) noexcept
	{
		return a_counters[OperationProfileQuality::kAdmissionContentionDrops] +
			a_counters[OperationProfileQuality::kPublicationContentionDrops] +
			a_counters[OperationProfileQuality::kCapacityDrops] +
			a_counters[OperationProfileQuality::kStaleTokens] +
			a_counters[OperationProfileQuality::kInvalidResults] +
			a_counters[OperationProfileQuality::kProducerCapacityDrops];
	}

	template<class Operation>
	[[nodiscard]] ProfileBenchmarkResult MeasureWindow(
		const PerformanceClock& a_clock,
		std::string a_mode,
		uint32_t a_threads,
		uint32_t a_repeat,
		Operation&& a_operation,
		uint64_t a_progressBefore,
		const std::function<uint64_t()>& a_progress,
		OperationProfileCounters a_before = {},
		OperationProfileSource* a_source = nullptr)
	{
		std::vector<std::vector<double>> latencies(a_threads);
		for (auto& samples : latencies)
			samples.reserve(kLatencySamplesPerThread);
		std::vector<uint64_t> operationCounts(a_threads);
		std::vector<std::thread> threads;
		threads.reserve(a_threads);
		std::barrier start{ static_cast<ptrdiff_t>(a_threads + 1) };
		std::atomic<uint64_t> deadline{ 0 };
		for (uint32_t thread = 0; thread < a_threads; ++thread)
		{
			threads.emplace_back([&, thread] {
				start.arrive_and_wait();
				auto operations = uint64_t{ 0 };
				auto& samples = latencies[thread];
				const auto stopAt = deadline.load(std::memory_order_acquire);
				for (;;)
				{
					for (uint32_t index = 0;
						index < kOperationBatch;
						++index)
					{
						if (!(operations % kLatencySampleStride) &&
							samples.size() < kLatencySamplesPerThread)
						{
							const auto before = a_clock.Now();
							a_operation(thread, operations);
							const auto after = a_clock.Now();
							samples.push_back(
								a_clock.Nanoseconds(after - before));
						}
						else
							a_operation(thread, operations);
						++operations;
					}
					if (a_clock.Now() >= stopAt)
						break;
				}
				operationCounts[thread] = operations;
			});
		}

		const auto begin = a_clock.Now();
		deadline.store(
			begin + a_clock.TicksForMilliseconds(kMeasurementMs),
			std::memory_order_release);
		start.arrive_and_wait();
		for (auto& thread : threads)
			thread.join();
		const auto seconds = a_clock.Seconds(a_clock.Now() - begin);

		std::vector<double> combined;
		combined.reserve(
			static_cast<size_t>(a_threads) * kLatencySamplesPerThread);
		for (const auto& values : latencies)
			combined.insert(combined.end(), values.begin(), values.end());
		std::sort(combined.begin(), combined.end());
		const auto operations = std::accumulate(
			operationCounts.begin(),
			operationCounts.end(),
			uint64_t{ 0 });
		auto losses = a_source ?
			CounterDelta(a_source->Counters(), a_before) :
			OperationProfileCounters{};
		const auto selected = a_source ?
			losses[OperationProfileQuality::kSampledAdmissions] +
				losses[OperationProfileQuality::kAdmissionContentionDrops] : operations;
		const auto accepted = a_source ?
			losses[OperationProfileQuality::kAcceptedRecords] : operations;
		return {
			std::move(a_mode),
			a_threads,
			a_repeat,
			operations,
			selected,
			accepted,
			Rejected(losses),
			seconds,
			operations / seconds,
			combined.size(),
			Percentile(combined, 0.5),
			Percentile(combined, 0.99),
			Percentile(combined, 0.999),
			combined.empty() ? 0.0 : combined.back(),
			a_progress() - a_progressBefore,
			losses,
			a_source ? a_source->Descriptors().front().samplingPeriod : 0,
			a_source ? a_source->RecordCapacity() : 0
		};
	}

	template<class Operation>
	void Warmup(
		const PerformanceClock& a_clock,
		uint32_t a_threads,
		Operation&& a_operation)
	{
		std::vector<std::thread> threads;
		threads.reserve(a_threads);
		std::barrier start{ static_cast<ptrdiff_t>(a_threads + 1) };
		std::atomic<uint64_t> deadline{ 0 };
		for (uint32_t thread = 0; thread < a_threads; ++thread)
		{
			threads.emplace_back([&, thread] {
				start.arrive_and_wait();
				const auto stopAt = deadline.load(std::memory_order_acquire);
				uint64_t operation{ 0 };
				for (;;)
				{
					for (uint32_t index = 0;
						index < kOperationBatch;
						++index)
						a_operation(thread, operation++);
					if (a_clock.Now() >= stopAt)
						break;
				}
			});
		}
		deadline.store(
			a_clock.Now() + a_clock.TicksForMilliseconds(kWarmupMs),
			std::memory_order_release);
		start.arrive_and_wait();
		for (auto& thread : threads)
			thread.join();
	}

	template<class Operation>
	void RunCase(
		std::vector<ProfileBenchmarkResult>& a_results,
		const PerformanceClock& a_clock,
		std::string_view a_mode,
		uint32_t a_threads,
		Operation&& a_operation,
		const std::function<uint64_t()>& a_progress,
		OperationProfileSource* a_source = nullptr)
	{
		Warmup(a_clock, a_threads, a_operation);
		for (uint32_t repeat = 1; repeat <= kRepeats; ++repeat)
		{
			const auto before = a_source ?
				a_source->Counters() : OperationProfileCounters{};
			const auto progressBefore = a_progress();
			a_results.push_back(MeasureWindow(
				a_clock,
				std::string{ a_mode },
				a_threads,
				repeat,
				a_operation,
				progressBefore,
				a_progress,
				before,
				a_source));
		}
	}

	[[nodiscard]] uint64_t HubProgress(const TelemetryHub& a_hub)
	{
		TelemetrySnapshot snapshot{};
		return a_hub.CopyLatest(snapshot) ? snapshot.sequence : 0;
	}

	void PrintResults(
		std::span<const ProfileBenchmarkResult> a_results,
		const PerformanceClock& a_clock,
		const QpcCalibration& a_calibration)
	{
		std::cout << "\nProfiling overhead benchmark\n";
		std::cout << "warmup=" << kWarmupMs <<
			"ms duration=" << kMeasurementMs <<
			"ms repeats=" << kRepeats <<
			" collector=" << kCollectorCadenceMs <<
			"ms sampling=1/" << kSamplingPeriod <<
			" capacity=" << kRecordCapacity <<
			" shards=" << kPublicationShards << '\n';
		std::cout << "qpc_frequency=" << a_clock.Frequency() <<
			" resolution_ns=" << a_calibration.resolutionNanoseconds <<
			" average_read_ns=" << a_calibration.averageReadNanoseconds <<
			" pair_min_ns=" << a_calibration.minimumPairNanoseconds <<
			" pair_median_ns=" << a_calibration.medianPairNanoseconds << '\n';
		std::cout << "Voltek operation = malloc(64) + free; sampling=1/" << kHeapProfileSamplingPeriod <<
			" capacity=" << kHeapProfileRecordCapacity << "; ns/op = threads * 1e9 / throughput\n";
		std::cout << std::setw(22) << "mode" <<
			std::setw(8) << "thr" <<
			std::setw(8) << "run" <<
			std::setw(12) << "seconds" <<
			std::setw(16) << "attempts" <<
			std::setw(16) << "accepted" <<
			std::setw(12) << "rejected" <<
			std::setw(16) << "ops/sec" <<
			std::setw(12) << "median" <<
			std::setw(12) << "p99" <<
			std::setw(12) << "p999" <<
			std::setw(12) << "collect" << '\n';
		for (const auto& result : a_results)
		{
			std::cout << std::setw(22) << result.mode <<
				std::setw(8) << result.threads <<
				std::setw(8) << result.repeat <<
				std::setw(12) << std::fixed << std::setprecision(3) <<
					result.seconds <<
				std::setw(16) << result.attempts <<
				std::setw(16) << result.accepted <<
				std::setw(12) << result.rejected <<
				std::setw(16) << std::setprecision(0) <<
					result.operationsPerSecond <<
				std::setw(12) << result.medianNanoseconds <<
				std::setw(12) << result.p99Nanoseconds <<
				std::setw(12) << result.p999Nanoseconds <<
				std::setw(12) << result.collectorProgress << '\n';
		}
	}

	[[nodiscard]] bool WriteResults(
		std::span<const ProfileBenchmarkResult> a_results,
		const TelemetryCaptureStatus& a_capture,
		const TelemetryCaptureStatus& a_heapCapture,
		const PerformanceClock& a_clock,
		const QpcCalibration& a_calibration)
	{
		std::filesystem::create_directories(".Build\\Tests");
		std::ofstream output{
			".Build\\Tests\\profile-bench.json",
			std::ios::trunc
		};
		if (!output)
			return false;
		output << std::fixed << std::setprecision(3);
		output << "{\n"
			"  \"configuration\": {"
			"\"warmup_ms\": " << kWarmupMs <<
			", \"measurement_ms\": " << kMeasurementMs <<
			", \"repeats\": " << kRepeats <<
			", \"collector_cadence_ms\": " << kCollectorCadenceMs <<
			", \"sampling_period\": " << kSamplingPeriod <<
			", \"record_capacity\": " << kRecordCapacity <<
			", \"publication_shards\": " << kPublicationShards <<
			"},\n"
			"  \"qpc\": {\"frequency\": " << a_clock.Frequency() <<
			", \"resolution_ns\": " << a_calibration.resolutionNanoseconds <<
			", \"average_read_ns\": " <<
				a_calibration.averageReadNanoseconds <<
			", \"pair_min_ns\": " << a_calibration.minimumPairNanoseconds <<
			", \"pair_median_ns\": " <<
				a_calibration.medianPairNanoseconds << "},\n"
			"  \"capture_directory\": \"" <<
				a_capture.directory.generic_string() <<
			"\",\n  \"heap_capture_directory\": \"" << a_heapCapture.directory.generic_string() <<
			"\",\n  \"results\": [\n";
		for (size_t index = 0; index < a_results.size(); ++index)
		{
			const auto& result = a_results[index];
			output << "    {\"mode\": \"" << result.mode <<
				"\", \"threads\": " << result.threads <<
				", \"repeat\": " << result.repeat <<
				", \"attempts\": " << result.attempts <<
				", \"selected\": " << result.selected <<
				", \"accepted\": " << result.accepted <<
				", \"rejected\": " << result.rejected <<
				", \"seconds\": " << result.seconds <<
				", \"operations_per_second\": " <<
					result.operationsPerSecond <<
				", \"nanoseconds_per_operation\": " << result.threads * 1'000'000'000.0 / result.operationsPerSecond <<
				", \"sampling_period\": " << result.samplingPeriod <<
				", \"record_capacity\": " << result.recordCapacity <<
				", \"latency_samples\": " << result.latencySamples <<
				", \"median_ns\": " << result.medianNanoseconds <<
				", \"p99_ns\": " << result.p99Nanoseconds <<
				", \"p999_ns\": " << result.p999Nanoseconds <<
				", \"max_ns\": " << result.maximumNanoseconds <<
				", \"collector_progress\": " << result.collectorProgress <<
				", \"sampled_admissions\": " <<
					result.losses[OperationProfileQuality::kSampledAdmissions] <<
				", \"accepted_records\": " <<
					result.losses[OperationProfileQuality::kAcceptedRecords] <<
				", \"admission_contention_drops\": " <<
					result.losses[OperationProfileQuality::kAdmissionContentionDrops] <<
				", \"publication_contention_drops\": " <<
					result.losses[OperationProfileQuality::kPublicationContentionDrops] <<
				", \"capacity_drops\": " <<
					result.losses[OperationProfileQuality::kCapacityDrops] <<
				", \"stale_tokens\": " <<
					result.losses[OperationProfileQuality::kStaleTokens] <<
				", \"invalid_results\": " << result.losses[OperationProfileQuality::kInvalidResults] <<
				", \"producer_capacity_drops\": " << result.losses[OperationProfileQuality::kProducerCapacityDrops] <<
				", \"capacity_saturated\": " <<
					(result.losses[OperationProfileQuality::kCapacityDrops] ? "true" : "false") <<
				", \"rejection_bias\": " <<
					(result.rejected ?
						"\"accepted sample excludes rejected operations\"" :
						"null") << "}" <<
				(index + 1 == a_results.size() ? "\n" : ",\n");
		}
		output << "  ]\n}\n";
		return output.good();
	}
}

namespace vmm_tests
{
	int run_profile_benchmarks()
	{
		const PerformanceClock clock;
		const auto calibration = CalibrateQpc(clock);
		std::vector<ProfileBenchmarkResult> results;
		results.reserve(3 * 2 * kRepeats);

		CadenceCollector noOpCollector([] {
			std::atomic_signal_fence(std::memory_order_seq_cst);
		});
		noOpCollector.Start();
		OperationProfileConsumer<false> noProfile;
		const auto noOp = [&noProfile](uint32_t, uint64_t) noexcept {
			auto token = noProfile.Begin(0);
			noProfile.End(std::move(token));
			std::atomic_signal_fence(std::memory_order_seq_cst);
		};
		const std::function noOpProgress{
			[&noOpCollector] { return noOpCollector.Progress(); }
		};
		RunCase(results, clock, "no-op", 1, noOp, noOpProgress);
		RunCase(results, clock, "no-op", 8, noOp, noOpProgress);
		noOpCollector.Stop();

		Histogram<1> aggregate{};
		CadenceCollector telemetryCollector([&aggregate] {
			(void)aggregate.Drain();
		});
		telemetryCollector.Start();
		const auto telemetry = [&aggregate](uint32_t, uint64_t) noexcept {
			aggregate.Add(0, 1, 1, 1);
		};
		const std::function telemetryProgress{
			[&telemetryCollector] { return telemetryCollector.Progress(); }
		};
		RunCase(results, clock, "telemetry", 1, telemetry, telemetryProgress);
		RunCase(results, clock, "telemetry", 8, telemetry, telemetryProgress);
		telemetryCollector.Stop();

		OperationProfileConfiguration configuration{};
		configuration.sourceId = "benchmark";
		configuration.sourceName = "Profiling benchmark";
		configuration.descriptors = kBenchmarkDescriptor;
		configuration.recordCapacity = kRecordCapacity;
		configuration.publicationShardCount = kPublicationShards;
		auto source = std::make_shared<OperationProfileSource>(
			configuration,
			Addictol::GetQpcFrequency());
		if (!source->IsValid())
		{
			std::cerr << "profiling benchmark source configuration failed\n";
			return 1;
		}
		TelemetryHub hub{ Addictol::GetQpcFrequency() };
		constexpr std::array unsampledDescriptor{
			OperationProfileDescriptor{ "benchmark.unsampled", "Unsampled path", "profile.benchmark_unsampled", UINT32_MAX }
		};
		configuration.sourceId = "benchmark_unsampled";
		configuration.descriptors = unsampledDescriptor;
		auto unsampledSource = std::make_shared<OperationProfileSource>(configuration, GetQpcFrequency());
		if (hub.Register(source) != TelemetryRegistration::kAccepted ||
			hub.Register(unsampledSource) != TelemetryRegistration::kAccepted || !hub.Freeze(8))
		{
			std::cerr << "profiling benchmark hub setup failed\n";
			return 1;
		}
		TelemetryStartOptions options{};
		options.cadenceMs = kCollectorCadenceMs;
		options.captureRoot =
			".Build\\Tests\\profiling-benchmark-captures";
		options.productVersion = "benchmark";
		options.runtime = "test";
		if (!hub.Start(std::move(options)))
		{
			std::cerr << "profiling benchmark collector failed to start\n";
			return 1;
		}
		OperationProfileConsumer<true> profile{ source.get() };
		const auto instrumented = [&profile](uint32_t, uint64_t) noexcept {
			auto token = profile.Begin(0);
			if (token)
				profile.End(std::move(token), 64);
		};
		const std::function profileProgress{
			[&hub] { return HubProgress(hub); }
		};
		RunCase(
			results,
			clock,
			"profiling+collector",
			1,
			instrumented,
			profileProgress,
			source.get());
		RunCase(
			results,
			clock,
			"profiling+collector",
			8,
			instrumented,
			profileProgress,
			source.get());
		const auto unsampled = [&](uint32_t, uint64_t) noexcept {
			auto token = unsampledSource->Begin(0);
			if (token)
				unsampledSource->End(std::move(token), 64);
		};
		RunCase(results, clock, "unsampled+collector", 1, unsampled, profileProgress, unsampledSource.get());
		RunCase(results, clock, "unsampled+collector", 8, unsampled, profileProgress, unsampledSource.get());
		hub.Stop();

		TelemetryCaptureStatus capture{};
		(void)hub.CopyCaptureStatus(capture);
		TelemetryHub heapHub{ Addictol::GetQpcFrequency() };
		if (!InitializeHeapOperationProfile(heapHub, "voltek", "visper", "voltek") || !heapHub.Freeze(8))
			return 1;
		TelemetryStartOptions heapOptions{};
		heapOptions.cadenceMs = kCollectorCadenceMs;
		heapOptions.captureRoot = ".Build\\Tests\\profiling-benchmark-captures";
		heapOptions.productVersion = "benchmark";
		heapOptions.runtime = "test";
		if (!heapHub.Start(std::move(heapOptions)))
			return 1;
		const std::function heapProgress{ [&heapHub] { return HubProgress(heapHub); } };
		std::atomic<uint64_t> allocationFailures{ 0 };
		const auto heapCase = [&]<class Heap>(std::string_view mode, OperationProfileSource* profileSource) {
			const auto operation = [&](uint32_t, uint64_t) noexcept {
				auto* heap = Heap::GetSingleton();
				auto* block = heap->malloc(64);
				if (!block)
					allocationFailures.fetch_add(1, std::memory_order_relaxed);
				heap->free(block);
			};
			RunCase(results, clock, mode, 1, operation, heapProgress, profileSource);
			RunCase(results, clock, mode, 8, operation, heapProgress, profileSource);
		};
		heapCase.template operator()<ProxyVoltekHeap>("voltek-raw", nullptr);
		heapCase.template operator()<ProfiledHeap<ProxyVoltekHeap, HeapProfileSite::CRT>>(
			"voltek-profiled", HeapOperationProfile());
		heapHub.Stop();
		TelemetryCaptureStatus heapCapture{};
		(void)heapHub.CopyCaptureStatus(heapCapture);
		std::cout << "Voltek capture: " << heapCapture.directory << "; allocation failures=" << allocationFailures.load() << '\n';
		if (allocationFailures.load())
			return 1;
		for (const auto threads : { 1u, 8u })
		{
			double raw{ 0 }, profiled{ 0 };
			for (const auto& result : results)
			{
				if (result.threads != threads)
					continue;
				if (result.mode == "voltek-raw")
					raw += threads * 1'000'000'000.0 / result.operationsPerSecond / kRepeats;
				if (result.mode == "voltek-profiled")
					profiled += threads * 1'000'000'000.0 / result.operationsPerSecond / kRepeats;
			}
			std::cout << "Voltek " << threads << " threads: raw=" << raw << " ns/pair profiled=" << profiled <<
				" ns/pair overhead=" << profiled - raw << " ns (" << (profiled / raw - 1) * 100 << "%)\n";
		}
		PrintResults(results, clock, calibration);
		for (const auto& result : results)
		{
			if (result.seconds < 0.5 || !result.collectorProgress)
			{
				std::cerr <<
					"profiling benchmark did not sustain a measured collector run\n";
				return 1;
			}
		}
		if (!WriteResults(results, capture, heapCapture, clock, calibration))
		{
			std::cerr << "profiling benchmark JSON was not written\n";
			return 1;
		}
		std::cout << "\nJSON written to .Build\\Tests\\profile-bench.json\n";
		return 0;
	}
}
