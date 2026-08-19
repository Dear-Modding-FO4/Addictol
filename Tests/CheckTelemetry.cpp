#include "../Addictol/Include/AdTelemetryHub.h"
#include "../Addictol/Include/AdAllocatorPoolTelemetry.h"
#include "../Addictol/Include/AdZlibBackend.h"
#include "../Addictol/Include/AdZlibTelemetry.h"
#include "../Addictol/Include/Menu/AdMenuTelemetry.h"
#include "Harness.h"

#include <locale>
#include <sstream>

namespace Addictol::TelemetryTest
{
	struct HubAccess
	{
		static void Collect(
			TelemetryHub& a_hub,
			uint64_t a_qpc,
			double a_intervalMs,
			double a_latenessMs) noexcept
		{
			(void)a_hub.Collect(a_qpc, a_qpc, a_intervalMs, a_latenessMs);
		}

		static void CollectInterval(
			TelemetryHub& a_hub,
			uint64_t a_startQpc,
			uint64_t a_endQpc,
			double a_intervalMs,
			double a_latenessMs) noexcept
		{
			(void)a_hub.Collect(
				a_startQpc, a_endQpc, a_intervalMs, a_latenessMs);
		}

		[[nodiscard]] static uint64_t FrameCountAndTotalDurationUs(
			const FrameMetricSource& a_source) noexcept
		{
			return a_source.m_countAndTotalDurationUs.load(std::memory_order_relaxed);
		}

		[[nodiscard]] static uint32_t FrameMaximumDurationUs(
			const FrameMetricSource& a_source) noexcept
		{
			return static_cast<uint32_t>(
				a_source.m_maxAndOffsetUs.load(std::memory_order_relaxed) >> 32);
		}

		[[nodiscard]] static uint32_t FrameMinimumDurationUs(
			const FrameMetricSource& a_source) noexcept
		{
			return a_source.m_minDurationUs.load(std::memory_order_relaxed);
		}

		[[nodiscard]] static uint64_t FrameMaximumAndOffsetUs(
			const FrameMetricSource& a_source) noexcept
		{
			return a_source.m_maxAndOffsetUs.load(std::memory_order_relaxed);
		}

		static void BeginFrameInterval(
			FrameMetricSource& a_source,
			uint64_t a_qpc) noexcept
		{
			a_source.BeginInterval(a_qpc);
		}

		static void UpdateFrameMaximum(
			FrameMetricSource& a_source,
			uint32_t a_durationUs,
			uint32_t a_offsetUs) noexcept
		{
			a_source.UpdateMaximum(a_durationUs, a_offsetUs);
		}

		static void StageFrameExtrema(
			FrameMetricSource& a_source,
			uint32_t a_maximumUs,
			uint32_t a_offsetUs,
			uint32_t a_minimumUs) noexcept
		{
			a_source.m_maxAndOffsetUs.store(
				(static_cast<uint64_t>(a_maximumUs) << 32) | a_offsetUs,
				std::memory_order_relaxed);
			a_source.m_minDurationUs.store(a_minimumUs, std::memory_order_relaxed);
			a_source.m_active.store(true, std::memory_order_relaxed);
		}

		static void StageFrameCount(
			FrameMetricSource& a_source,
			uint32_t a_count,
			uint64_t a_totalDurationUs) noexcept
		{
			a_source.m_countAndTotalDurationUs.store(
				(a_totalDurationUs << 20) | a_count,
				std::memory_order_relaxed);
		}
	};
}

namespace
{
	using namespace Addictol;

	class TestIntervalSource final : public MetricSource
	{
	public:
		explicit TestIntervalSource(std::string_view a_key = "test.interval_count") :
			m_key(a_key)
		{}

		void Observe(double a_value) noexcept
		{
			m_value += a_value;
		}

		[[nodiscard]] std::span<const MetricDescriptor> Schema() const noexcept override
		{
			if (m_key == "test.duplicate")
			{
				static constexpr std::array duplicate{
					MetricDescriptor{ "test.duplicate", Unit::kCount }
				};
				return duplicate;
			}
			static constexpr std::array schema{
				MetricDescriptor{ "test.interval_count", Unit::kCount }
			};
			return schema;
		}

	private:
		void Drain(std::span<MetricValue> a_out) noexcept override
		{
			a_out[0] = { m_value, true };
			m_value = 0.0;
		}

		std::string_view m_key;
		double m_value{ 0.0 };
	};

	class InactiveSource final : public MetricSource
	{
	public:
		[[nodiscard]] std::span<const MetricDescriptor> Schema() const noexcept override
		{
			static constexpr std::array schema{
				MetricDescriptor{ "inactive.value", Unit::kCount }
			};
			return schema;
		}

	private:
		void Drain(std::span<MetricValue> a_out) noexcept override
		{
			a_out[0] = {};
		}
	};

	class OverreportingSeriesSource final :
		public MetricSource,
		public SeriesSource
	{
	public:
		[[nodiscard]] std::span<const MetricDescriptor> Schema() const noexcept override
		{
			static constexpr std::array schema{
				MetricDescriptor{ "test.series_count", Unit::kCount }
			};
			return schema;
		}

		[[nodiscard]] size_t SeriesCapacity() const noexcept override
		{
			return 2;
		}

	private:
		void Drain(std::span<MetricValue> a_out) noexcept override
		{
			a_out[0] = { 2.0, true };
		}

		[[nodiscard]] size_t DrainSeries(
			std::span<SeriesSample> a_out) noexcept override
		{
			if (!a_out.empty())
				a_out[0] = { "test.series", "first", 1, 2, 3 };
			if (a_out.size() > 1)
				a_out[1] = { "test.series", "second", 4, 5, 6 };
			return 3;
		}
	};

	class TestAllocatorPoolSource final : public MetricSource
	{
	public:
		TestAllocatorPoolSource(bool a_active, voltek::scalable_pool_stats a_stats) :
			m_active(a_active),
			m_stats(a_stats)
		{}

		[[nodiscard]] std::span<const MetricDescriptor> Schema() const noexcept override
		{
			return AllocatorPoolTelemetry::Schema();
		}

	private:
		void Drain(std::span<MetricValue> a_out) noexcept override
		{
			AllocatorPoolTelemetry::Populate(a_out, m_active, m_stats);
		}

		bool m_active;
		voltek::scalable_pool_stats m_stats;
	};

	template<class T>
	concept PublicDrain = requires(T& a_source, std::span<MetricValue> a_values)
	{
		a_source.Drain(a_values);
	};

	// Only TelemetryHub can name Drain; the runtime check below proves one destructive read per sample.
	static_assert(!PublicDrain<TestIntervalSource>);

	template<class T>
	concept PublicCollect = requires(T& a_hub)
	{
		a_hub.Collect(0, 0, 0.0, 0.0);
	};

	static_assert(!PublicCollect<TelemetryHub>);

	template<class T>
	concept PublicSeriesDrain = requires(T& a_source, std::span<SeriesSample> a_values)
	{
		a_source.DrainSeries(a_values);
	};

	static_assert(!PublicSeriesDrain<OverreportingSeriesSource>);

	class GroupedNumberPunct final : public std::numpunct<char>
	{
	protected:
		[[nodiscard]] char do_thousands_sep() const override
		{
			return '_';
		}

		[[nodiscard]] std::string do_grouping() const override
		{
			return "\3";
		}
	};

	std::atomic<uint32_t> s_frameClockReads{ 0 };
	std::atomic<uint32_t> s_threadIdReads{ 0 };
	std::array<ProcessMemoryMetricSource::Sample, 3> s_processMemorySamples{
		ProcessMemoryMetricSource::Sample{ 1000, 2000, 3000, 4000, 100 },
		ProcessMemoryMetricSource::Sample{ 1100, 2100, 3100, 4100, 125 },
		ProcessMemoryMetricSource::Sample{ 1200, 2200, 3200, 4200, 10 }
	};
	size_t s_processMemorySampleIndex{ 0 };

	uint64_t ReadFrameClock() noexcept
	{
		s_frameClockReads.fetch_add(1, std::memory_order_relaxed);
		return 1;
	}

	uint32_t ReadThreadId() noexcept
	{
		s_threadIdReads.fetch_add(1, std::memory_order_relaxed);
		return 77;
	}

	int32_t ServeStockForGate(ZlibInflate::Stream*, int32_t) noexcept
	{
		return 0;
	}

	bool ReadProcessMemorySample(ProcessMemoryMetricSource::Sample& a_sample) noexcept
	{
		if (s_processMemorySampleIndex >= s_processMemorySamples.size())
			return false;
		a_sample = s_processMemorySamples[s_processMemorySampleIndex++];
		return true;
	}

	bool ReadGpuVideoMemory(uint64_t& a_used, uint64_t& a_budget) noexcept
	{
		a_used = 5000;
		a_budget = 6000;
		return true;
	}

	bool ReadSystemMemorySample(SystemMemoryMetricSource::Sample& a_sample) noexcept
	{
		a_sample = { 7000, 10000, 2000 };
		return true;
	}

	bool ReadEscapeFixture(
		EscapeFreezeMetricSource::Values& a_values) noexcept
	{
		a_values = MetricDoubles(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
		return true;
	}

	bool ReadReferenceHandleFixture(
		ReferenceHandleMetricSource::Values& a_values) noexcept
	{
		constexpr uint32_t limit{ 1u << 21 };
		constexpr uint32_t count{ limit / 2 };
		a_values = MetricDoubles(count, ReferenceHandleUsagePercent(count, limit));
		return true;
	}

	bool ReadModuleOutcomeFixture(
		ModuleOutcomeMetricSource::Values& a_values) noexcept
	{
		constexpr std::array<uint64_t, 5> outcomes{ 1, 2, 3, 4, 5 };
		a_values = ModuleOutcomeMetricValues(outcomes);
		return true;
	}

	struct AudioPerformanceFixture
	{
		uint32_t MemoryUsageInBytes;
		uint32_t CurrentLatencyInSamples;
		uint32_t GlitchesSinceEngineStarted;
		uint32_t ActiveSourceVoiceCount;
		uint32_t TotalSourceVoiceCount;
	};

	constexpr AudioPerformanceFixture kAudioPerformanceFixture{ 55, 44, 11, 22, 33 };

	struct AudioEngineFixture
	{
		void GetPerformanceData(AudioPerformanceFixture* a_performance) const noexcept
		{
			*a_performance = kAudioPerformanceFixture;
		}
	};

	constexpr AudioEngineFixture kAudioEngineFixture{};
	constexpr uint8_t kAudioMasteringVoiceFixture{};

	bool ReadAudioPerformanceFixture(
		AudioPerformanceMetricSource::Values& a_values) noexcept
	{
		return ReadAudioPerformanceMetricValues<AudioPerformanceFixture>(
			std::addressof(kAudioEngineFixture),
			std::addressof(kAudioMasteringVoiceFixture),
			a_values);
	}

	bool ReadUnavailableEscape(
		EscapeFreezeMetricSource::Values&) noexcept
	{
		return false;
	}

	bool ReadUnavailableReferenceHandles(
		ReferenceHandleMetricSource::Values&) noexcept
	{
		return false;
	}

	bool ReadUnavailableAudioPerformance(
		AudioPerformanceMetricSource::Values& a_values) noexcept
	{
		return ReadAudioPerformanceMetricValues<AudioPerformanceFixture>(
			static_cast<const AudioEngineFixture*>(nullptr),
			std::addressof(kAudioMasteringVoiceFixture),
			a_values);
	}

	bool ReadHalfInitializedAudioPerformance(
		AudioPerformanceMetricSource::Values& a_values) noexcept
	{
		return ReadAudioPerformanceMetricValues<AudioPerformanceFixture>(
			std::addressof(kAudioEngineFixture),
			static_cast<const uint8_t*>(nullptr),
			a_values);
	}
}

namespace vmm_tests
{
	void run_telemetry_checks(Runner& runner)
	{
		runner.test("disabled telemetry leaves zlib counters untouched", [] {
			ZlibIntervalCounters counters{};
			require(!Telemetry::EnabledRelaxed(), "telemetry unexpectedly started enabled");
			s_frameClockReads.store(0, std::memory_order_relaxed);
			s_threadIdReads.store(0, std::memory_order_relaxed);
			auto original = &ServeStockForGate;
			auto clock = &ReadFrameClock;
			auto threadId = &ReadThreadId;
			const auto recorder = [&counters](
				const ZlibInflateOutcome& a_outcome,
				bool a_enabled,
				uint32_t a_threadId) noexcept {
				Telemetry::ObserveZlibCall(
					counters,
					a_enabled,
					static_cast<ZlibFallbackReason>(a_outcome.fallbackReasonId),
					false,
					4,
					a_threadId,
					4096,
					8192,
					a_outcome.totalQpc);
			};
			(void)TelemetryDetail::ServeTelemetryZlib<StockZlibBackend>(
				nullptr, 4, original, clock, threadId, recorder);
			require(
				s_frameClockReads.load(std::memory_order_relaxed) == 0,
				"disabled zlib instrumentation read the clock");
			require(
				s_threadIdReads.load(std::memory_order_relaxed) == 0,
				"disabled zlib instrumentation read the thread id");
			require(
				counters.packed.load(std::memory_order_relaxed) == 0,
				"disabled zlib instrumentation performed an atomic update");
			require(
				counters.bytesIn.load(std::memory_order_relaxed) == 0,
				"disabled zlib instrumentation accumulated input byte volume");
			require(
				counters.bytesOut.load(std::memory_order_relaxed) == 0,
				"disabled zlib instrumentation accumulated output byte volume");
			require(
				counters.fallbackBytesOut.load(std::memory_order_relaxed) == 0,
				"disabled zlib instrumentation accumulated fallback output volume");
			for (size_t index = 0; index < counters.fallbackReasons.size(); ++index)
			{
				require(
					counters.fallbackReasons[index].load(std::memory_order_relaxed) == 0,
					"disabled zlib instrumentation incremented fallback reason " +
						std::to_string(index));
			}
			require(
				counters.servedLibDeflateOutput.Total() == HistogramBucket{},
				"disabled zlib instrumentation updated libdeflate output buckets");
			require(
				counters.servedStockOutput.Total() == HistogramBucket{},
				"disabled zlib instrumentation updated stock output buckets");
			require(
				counters.servedLibDeflateInput.Total() == HistogramBucket{},
				"disabled zlib instrumentation updated libdeflate input buckets");
			require(
				counters.servedStockInput.Total() == HistogramBucket{},
				"disabled zlib instrumentation updated stock input buckets");
			require(
				counters.fallbackThread.Total() == HistogramBucket{},
				"disabled zlib instrumentation updated fallback thread buckets");
			require(
				counters.servedThread.Total() == HistogramBucket{},
				"disabled zlib instrumentation updated served thread buckets");
			require(
				counters.flush.Total() == HistogramBucket{},
				"disabled zlib instrumentation updated flush buckets");
		});

		runner.test("enabled zlib hook uses injected instrumentation", [] {
			TelemetryHub hub{ 1000000 };
			require(hub.Freeze(1), "enabled zlib hook hub freeze failed");
			require(hub.Start(60000), "enabled zlib hook hub worker did not start");
			ZlibIntervalCounters counters{};
			s_frameClockReads.store(0, std::memory_order_relaxed);
			s_threadIdReads.store(0, std::memory_order_relaxed);
			auto original = &ServeStockForGate;
			auto clock = &ReadFrameClock;
			auto threadId = &ReadThreadId;
			const auto recorder = [&counters](
				const ZlibInflateOutcome& a_outcome,
				bool a_enabled,
				uint32_t a_threadId) noexcept {
				Telemetry::ObserveZlibCall(
					counters,
					a_enabled,
					static_cast<ZlibFallbackReason>(a_outcome.fallbackReasonId),
					false,
					4,
					a_threadId,
					4096,
					8192,
					a_outcome.totalQpc);
			};
			(void)TelemetryDetail::ServeTelemetryZlib<StockZlibBackend>(
				nullptr, 4, original, clock, threadId, recorder);
			hub.Stop();
			require(
				s_frameClockReads.load(std::memory_order_relaxed) > 0,
				"enabled zlib instrumentation skipped the clock");
			require(
				s_threadIdReads.load(std::memory_order_relaxed) == 1,
				"enabled zlib instrumentation skipped thread attribution");
			require(
				counters.packed.load(std::memory_order_relaxed) == 1,
				"enabled zlib instrumentation skipped interval counters");
			require(
				counters.servedStockOutput.Total().calls == 1,
				"enabled zlib instrumentation skipped stock output buckets");
			require(
				counters.servedThread.Total().calls == 1,
				"enabled zlib instrumentation skipped thread buckets");
		});

		runner.test("disabled telemetry skips the frame clock", [] {
			TelemetryHub hub{ 1000000 };
			FrameMetricSource frame{ hub, 1000000, 10 };
			s_frameClockReads.store(0, std::memory_order_relaxed);
			s_threadIdReads.store(0, std::memory_order_relaxed);
			TelemetryDetail::CaptureRenderThread(&ReadThreadId);
			TelemetryDetail::ObserveFrame(&frame, &ReadFrameClock);
			require(
				s_threadIdReads.load(std::memory_order_relaxed) == 0,
				"disabled frame instrumentation read the thread id");
			require(
				s_frameClockReads.load(std::memory_order_relaxed) == 0,
				"disabled frame instrumentation read the clock");
			frame.ObserveAt(1000);
			frame.ObserveAt(2000);
			require(
				TelemetryTest::HubAccess::FrameCountAndTotalDurationUs(frame) == 0,
				"disabled frame instrumentation updated count or total duration");
			require(
				TelemetryTest::HubAccess::FrameMaximumDurationUs(frame) == 0,
				"disabled frame instrumentation updated maximum duration");
			require(
				TelemetryTest::HubAccess::FrameMinimumDurationUs(frame) ==
					std::numeric_limits<uint32_t>::max(),
				"disabled frame instrumentation updated minimum duration");
		});

		runner.test("zlib size bucket boundaries are stable", [] {
			require(ZlibSizeBucketIndex(0) == 0,
				"size zero did not map to the empty bucket");
			require(ZlibSizeBucketIndex(1) == 1,
				"size 1 did not map to the first bucket");
			require(ZlibSizeBucketIndex(255) == 1,
				"size 255 did not map to the first bucket");
			require(ZlibSizeBucketIndex(256) == 2,
				"size 256 did not map to the second bucket");
			require(ZlibSizeBucketIndex(1023) == 2,
				"size 1023 did not map to the second bucket");
			require(ZlibSizeBucketIndex(1024) == 3,
				"size 1024 did not map to the third bucket");
			require(ZlibSizeBucketIndex(4095) == 3,
				"size 4095 did not map to the third bucket");
			require(ZlibSizeBucketIndex(4096) == 4,
				"size 4096 did not map to the fourth bucket");
			require(ZlibSizeBucketIndex(16383) == 4,
				"size 16383 did not map to the fourth bucket");
			require(ZlibSizeBucketIndex(16384) == 5,
				"size 16384 did not map to the fifth bucket");
			require(ZlibSizeBucketIndex(65535) == 5,
				"size 65535 did not map to the fifth bucket");
			require(ZlibSizeBucketIndex(65536) == 6,
				"size 65536 did not map to the sixth bucket");
			require(ZlibSizeBucketIndex(262143) == 6,
				"size 262143 did not map to the sixth bucket");
			require(ZlibSizeBucketIndex(262144) == 7,
				"size 262144 did not map to the largest bucket");
			require(ZlibSizeBucketIndex(std::numeric_limits<uint64_t>::max()) == 7,
				"maximum size did not map to the largest bucket");
		});

		runner.test("zlib size bucket labels are stable", [] {
			constexpr std::array<std::string_view, 9> expected{
				"empty",
				"1-255",
				"256-1023",
				"1024-4095",
				"4096-16383",
				"16384-65535",
				"65536-262143",
				"262144+",
				"unknown"
			};
			require(kZlibSizeBuckets.size() == expected.size(),
				"zlib size bucket label count changed");
			for (size_t index = 0; index < expected.size(); ++index)
			{
				require(kZlibSizeBuckets[index].label == expected[index],
					"zlib size bucket label changed at index " +
						std::to_string(index));
			}
		});

		runner.test("every telemetry metric has one owning panel", [] {
			std::vector<MetricDescriptor> columns;
			const auto append = [&columns](std::span<const MetricDescriptor> a_schema) {
				columns.insert(columns.end(), a_schema.begin(), a_schema.end());
			};

			TelemetryHub frameHub{ 1000000 };
			ProcessMemoryMetricSource process;
			GpuVideoMemoryMetricSource gpu{ nullptr };
			SystemMemoryMetricSource system;
			FrameMetricSource frame{ frameHub, 1000000, 50 };
			append(ZlibIntervalCounters::Schema());
			append(AllocatorPoolTelemetry::Schema());
			append(kEscapeFreezeMetricSchema);
			append(kReferenceHandleMetricSchema);
			append(kModuleOutcomeMetricSchema);
			append(kAudioPerformanceMetricSchema);
			append(process.Schema());
			append(gpu.Schema());
			append(system.Schema());
			append(frame.Schema());
			require(columns.size() == 52, "real telemetry column count changed");
			for (const auto key : kTelemetryOverviewMetrics)
			{
				require(
					std::ranges::count(columns, key, &MetricDescriptor::key) == 1,
					"overview telemetry metric is missing");
			}

			std::array<size_t, static_cast<size_t>(TelemetryPanel::kCount)> counts{};
			for (const auto& column : columns)
			{
				const auto classification = ClassifyTelemetryMetric(column.key);
				require(
					classification.matches == 1,
					"telemetry metric is orphaned or duplicated");
				require(
					classification.panel != TelemetryPanel::kNone,
					"telemetry metric has no owning panel");
				++counts[static_cast<size_t>(classification.panel)];
			}
			require(
				counts == std::array<size_t, 5>{ 5, 12, 12, 18, 5 },
				"telemetry panel metric distribution changed");
		});

		runner.test("zlib flush modes map to stable buckets", [] {
			require(ZlibFlushBucketIndex(0) == 0,
				"Z_NO_FLUSH did not map to no_flush");
			require(ZlibFlushBucketIndex(2) == 1,
				"Z_SYNC_FLUSH did not map to sync");
			require(ZlibFlushBucketIndex(4) == 2,
				"Z_FINISH did not map to finish");
			require(ZlibFlushBucketIndex(5) == 3,
				"Z_BLOCK did not map to block");
			require(ZlibFlushBucketIndex(6) == 4,
				"Z_TREES did not map to trees");
			require(ZlibFlushBucketIndex(1) == 5,
				"Z_PARTIAL_FLUSH did not map to other");
			require(ZlibFlushBucketIndex(3) == 5,
				"Z_FULL_FLUSH did not map to other");
			require(ZlibFlushBucketIndex(-1) == 5,
				"unknown flush mode did not map to other");
		});

		runner.test("histogram add drain and overflow are coherent", [] {
			Histogram<3> histogram{};
			histogram.Add(0, 1, 2, 3);
			histogram.Add(9, 4, 5, 6);
			require(histogram.Total() == HistogramBucket{ 5, 7, 9 },
				"histogram total did not sum all buckets");
			const auto drained = histogram.Drain();
			require(drained[0].calls == 1,
				"histogram first bucket lost its calls");
			require(drained[0].ticks == 2,
				"histogram first bucket lost its ticks");
			require(drained[0].bytes == 3,
				"histogram first bucket lost its bytes");
			require(drained[2].calls == 4,
				"histogram overflow bucket lost its calls");
			require(drained[2].ticks == 5,
				"histogram overflow bucket lost its ticks");
			require(drained[2].bytes == 6,
				"histogram overflow bucket lost its bytes");
			require(histogram.Drain() == std::array<HistogramBucket, 3>{},
				"histogram drain repeated counters");
		});

		runner.test("known render thread classification is stable", [] {
			require(ZlibThreadBucketIndex(77, 77) == 0,
				"known render thread did not map to the render bucket");
			require(ZlibThreadBucketIndex(78, 77) == 1,
				"known worker thread did not map to the worker bucket");
			require(ZlibThreadBucketIndex(77, 0) == 1,
				"unknown render thread did not map conservatively to worker");
		});

		runner.test("series append stays within the provided span", [] {
			std::array storage{
				SeriesSample{},
				SeriesSample{ "guard", "guard", 99, 99, 99 }
			};
			size_t offset{ 0 };
			const auto out = std::span{ storage }.first(1);
			require(
				AppendSeriesSample(
					out, offset, { "test.series", "first", 1, 2, 3 }),
				"bounded series append rejected an in-capacity row");
			require(
				!AppendSeriesSample(
					out, offset, { "test.series", "second", 4, 5, 6 }),
				"bounded series append accepted an out-of-capacity row");
			require(offset == 1,
				"bounded series append advanced past capacity");
			require(storage[1].series == "guard" && storage[1].calls == 99,
				"bounded series append overwrote the guard row");
		});

		runner.test("series source row counts are clamped to capacity", [] {
			TelemetryHub hub{ 1000000 };
			require(
				hub.Register(std::make_shared<OverreportingSeriesSource>()) ==
					TelemetryRegistration::kAccepted,
				"overreporting series source registration was rejected");
			require(hub.Freeze(2), "overreporting series hub freeze failed");
			TelemetryTest::HubAccess::Collect(hub, 10, 1.0, 0.0);
			TelemetrySnapshot snapshot{};
			require(hub.CopyLatest(snapshot),
				"overreporting series snapshot was not published");
			require(snapshot.series.size() == 2,
				"series source exceeded its declared row capacity");
			require(snapshot.series[0].bucket == "first",
				"first in-capacity series row was lost");
			require(snapshot.series[1].bucket == "second",
				"second in-capacity series row was lost");
		});

		runner.test("series CSV is exact and locale independent", [] {
			const std::array samples{
				SeriesSample{ "zlib.served.stock", "256-1023", 1234, 5678, 9012 },
				SeriesSample{ "zlib.flush", "finish", 0, 99, 88 },
				SeriesSample{ "zlib.served.thread", "worker", 3, 4, 5 }
			};
			std::ostringstream csv;
			csv.imbue(std::locale{ std::locale::classic(), new GroupedNumberPunct });
			require(TelemetryHub::WriteSeriesCsvHeader(csv),
				"series CSV header write failed");
			require(TelemetryHub::WriteSeriesCsvRows(csv, 1234567, samples),
				"series CSV row write failed");
			require(
				csv.str() ==
					"qpc,series,bucket,calls,ticks,bytes\n"
					"1234567,zlib.served.stock,256-1023,1234,5678,9012\n"
					"1234567,zlib.served.thread,worker,3,4,5\n",
				"series CSV serialization changed or honored the stream locale");
			require(csv.str().find("zlib.flush") == std::string::npos,
				"zero-call series row was not skipped");
		});

		runner.test("wide and series CSV rows share interval qpc", [] {
			TelemetrySnapshot snapshot{};
			snapshot.sequence = 9;
			snapshot.qpc = 424242;
			snapshot.intervalMs = 1.0;
			snapshot.latenessMs = 0.0;
			const std::array samples{
				SeriesSample{ "zlib.flush", "finish", 1, 2, 3 }
			};
			std::ostringstream wide;
			std::ostringstream series;
			require(TelemetryHub::WriteCsvRow(wide, snapshot),
				"wide CSV join-control row write failed");
			require(TelemetryHub::WriteSeriesCsvRows(series, snapshot.qpc, samples),
				"series CSV join-control row write failed");
			require(wide.str() == "9,424242,1,0\n",
				"wide CSV did not serialize the interval-start qpc");
			require(series.str() == "424242,zlib.flush,finish,1,2,3\n",
				"series CSV did not serialize the same interval-start qpc");
		});

		runner.test("hub owns the only destructive interval drain", [] {
			TelemetryHub hub{ 1000000 };
			auto source = std::make_shared<TestIntervalSource>();
			require(
				hub.Register(source) == TelemetryRegistration::kAccepted,
				"interval source registration was rejected");
			require(hub.Freeze(2), "interval hub freeze failed");
			source->Observe(7.0);
			TelemetryTest::HubAccess::Collect(hub, 10, 1.0, 0.0);
			TelemetrySnapshot first{};
			require(hub.CopyLatest(first), "first interval was not published");
			require(first.values[0].valid && first.values[0].value == 7.0,
				"first destructive drain did not preserve the interval");
			TelemetryTest::HubAccess::Collect(hub, 20, 1.0, 0.0);
			TelemetrySnapshot second{};
			require(hub.CopyLatest(second), "second interval was not published");
			require(second.values[0].valid && second.values[0].value == 0.0,
				"second destructive drain repeated the first interval");
		});

		runner.test("registration rejects duplicate keys and post-freeze sources", [] {
			TelemetryHub hub{ 1000000 };
			require(
				hub.Register(std::make_shared<TestIntervalSource>("test.duplicate")) ==
					TelemetryRegistration::kAccepted,
				"control source registration was rejected");
			require(
				hub.Register(std::make_shared<TestIntervalSource>("test.duplicate")) ==
					TelemetryRegistration::kDuplicateKey,
				"duplicate metric key was not rejected");
			require(hub.Freeze(2), "registration hub freeze failed");
			require(
				hub.Register(std::make_shared<InactiveSource>()) ==
					TelemetryRegistration::kFrozen,
				"post-freeze source registration was not rejected");
		});

		runner.test("ring overwrite loss is counted", [] {
			TelemetryHub hub{ 1000000 };
			require(
				hub.Register(std::make_shared<InactiveSource>()) ==
					TelemetryRegistration::kAccepted,
				"ring source registration was rejected");
			require(hub.Freeze(2), "ring hub freeze failed");
			TelemetryTest::HubAccess::Collect(hub, 1, 1.0, 0.0);
			TelemetryTest::HubAccess::Collect(hub, 2, 1.0, 0.0);
			TelemetryTest::HubAccess::Collect(hub, 3, 1.0, 0.0);
			require(hub.Stats().overwrittenSamples == 1,
				"third sample did not expose one ring overwrite");
		});

		runner.test("metric history preserves partial ring order", [] {
			TelemetryHub hub{ 1000000 };
			auto source = std::make_shared<TestIntervalSource>();
			require(
				hub.Register(source) == TelemetryRegistration::kAccepted,
				"history source registration was rejected");
			require(hub.Freeze(5), "history hub freeze failed");
			for (uint64_t sample = 1; sample <= 3; ++sample)
			{
				source->Observe(static_cast<double>(sample));
				TelemetryTest::HubAccess::Collect(hub, sample, 1.0, 0.0);
			}

			std::array<MetricValue, 5> history{};
			const auto count = hub.CopyMetricHistory(0, history);
			require(count == 3, "partial metric history count changed");
			for (size_t index = 0; index < count; ++index)
			{
				require(history[index].valid, "partial metric history lost validity");
				require(
					history[index].value == static_cast<double>(index + 1),
					"partial metric history was not oldest to newest");
			}
		});

		runner.test("metric history keeps newest values after wrap", [] {
			TelemetryHub hub{ 1000000 };
			auto source = std::make_shared<TestIntervalSource>();
			require(
				hub.Register(source) == TelemetryRegistration::kAccepted,
				"wrapped history source registration was rejected");
			require(hub.Freeze(3), "wrapped history hub freeze failed");
			for (uint64_t sample = 1; sample <= 4; ++sample)
			{
				source->Observe(static_cast<double>(sample));
				TelemetryTest::HubAccess::Collect(hub, sample, 1.0, 0.0);
			}

			std::array<MetricValue, 2> history{};
			const auto count = hub.CopyMetricHistory(0, history);
			require(count == 2, "bounded metric history count changed");
			require(history[0].value == 3.0, "bounded metric history lost the older value");
			require(history[1].value == 4.0, "bounded metric history lost the newest value");
		});

		runner.test("metric history stays coherent during publication", [] {
			TelemetryHub hub{ 1000000 };
			auto source = std::make_shared<TestIntervalSource>();
			require(
				hub.Register(source) == TelemetryRegistration::kAccepted,
				"concurrent history source registration was rejected");
			require(hub.Freeze(8), "concurrent history hub freeze failed");

			std::atomic<bool> ready{ false };
			std::atomic<bool> done{ false };
			std::atomic<bool> coherent{ true };
			std::thread reader([&] {
				std::array<MetricValue, 8> history{};
				ready.store(true, std::memory_order_release);
				while (!done.load(std::memory_order_acquire))
				{
					const auto count = hub.CopyMetricHistory(0, history);
					for (size_t index = 0; index < count; ++index)
					{
						if (!history[index].valid ||
							(index && history[index].value != history[index - 1].value + 1.0))
							coherent.store(false, std::memory_order_relaxed);
					}
				}
			});
			while (!ready.load(std::memory_order_acquire))
				std::this_thread::yield();
			for (uint64_t sample = 1; sample <= 1000; ++sample)
			{
				source->Observe(static_cast<double>(sample));
				TelemetryTest::HubAccess::Collect(hub, sample, 1.0, 0.0);
			}
			done.store(true, std::memory_order_release);
			reader.join();
			require(
				coherent.load(std::memory_order_relaxed),
				"metric history exposed an in-flight sample");
		});

		runner.test("frame record publishes its timestamp and duration", [] {
			TelemetryHub hub{ 1000000 };
			auto frame = std::make_shared<FrameMetricSource>(hub, 1000000, 10);
			require(
				hub.Register(frame) == TelemetryRegistration::kAccepted,
				"frame source registration was rejected");
			require(hub.Freeze(2), "frame hub freeze failed");
			require(hub.Start(60000), "frame hub worker did not start");
			frame->ObserveAt(100);
			frame->ObserveAt(20100);
			std::array<FrameRecord, 2> records{};
			const auto count = hub.CopyFrameRecords(records);
			hub.Stop();
			require(count == 1,
				"induced frame interval did not publish exactly one record");
			require(records[0].qpc == 20100,
				"frame-record timestamp was altered");
			require(records[0].durationUs == 20000,
				"frame-record duration was altered");
		});

		runner.test("inactive values stay empty in snapshots and CSV", [] {
			TelemetryHub hub{ 1000000 };
			require(
				hub.Register(std::make_shared<InactiveSource>()) ==
					TelemetryRegistration::kAccepted,
				"inactive source registration was rejected");
			require(hub.Freeze(2), "inactive hub freeze failed");
			TelemetryTest::HubAccess::Collect(hub, 10, 1.0, 0.0);
			TelemetrySnapshot snapshot{};
			require(hub.CopyLatest(snapshot), "inactive snapshot was not published");
			require(!snapshot.values[0].valid, "inactive source emitted a valid zero");
			std::ostringstream csv;
			require(TelemetryHub::WriteCsvHeader(csv, hub.Columns()), "CSV header write failed");
			require(TelemetryHub::WriteCsvRow(csv, snapshot), "CSV row write failed");
			require(
				csv.str() == "sequence,qpc,interval_ms,lateness_ms,inactive.value\n1,10,1,0,\n",
				"invalid metric did not serialize as an empty CSV cell");
		});

		runner.test("packed zlib drains keep interval pairs coherent", [] {
			ZlibIntervalCounters counters{};
			counters.Observe(ZlibFallbackReason::None, 10, 100);
			counters.Observe(ZlibFallbackReason::State, 20, 200);
			const auto first = counters.Drain();
			require(static_cast<uint32_t>(first) == 1,
				"primary event left the first packed interval");
			require(static_cast<uint32_t>(first >> 32) == 1,
				"fallback event left the first packed interval");
			require(counters.DrainBytesIn() == 30, "input byte volume left the first interval");
			require(counters.DrainBytesOut() == 300, "output byte volume left the first interval");
			require(counters.Drain() == 0, "packed drain repeated an event");
			require(counters.DrainBytesIn() == 0, "input byte drain repeated a volume");
			require(counters.DrainBytesOut() == 0, "output byte drain repeated a volume");
			counters.Observe(ZlibFallbackReason::Decode, 5, 50);
			const auto second = counters.Drain();
			require(static_cast<uint32_t>(second) == 0,
				"second interval gained a primary event");
			require(static_cast<uint32_t>(second >> 32) == 1,
				"second interval lost its fallback event");
			require(counters.DrainBytesIn() == 5, "second interval lost its input byte volume");
			require(counters.DrainBytesOut() == 50, "second interval lost its output byte volume");
		});

		runner.test("zlib fallback reasons sum to the fallback interval", [] {
			ZlibIntervalCounters counters{};
			counters.Observe(ZlibFallbackReason::State, 1, 2);
			counters.Observe(ZlibFallbackReason::State, 1, 2);
			counters.Observe(ZlibFallbackReason::Allocation, 1, 2);
			counters.Observe(ZlibFallbackReason::Decode, 1, 2);
			counters.Observe(ZlibFallbackReason::Commit, 1, 2);
			counters.Observe(ZlibFallbackReason::Capacity, 1, 2);
			counters.Observe(ZlibFallbackReason::SizeMismatch, 1, 2);
			counters.Observe(ZlibFallbackReason::RequestRestart, 1, 2);
			const auto packed = counters.Drain();
			const auto fallbackCount = static_cast<uint32_t>(packed >> 32);
			uint64_t reasonCount{ 0 };
			for (size_t index = 0; index < ZlibIntervalCounters::kFallbackReasonCount; ++index)
				reasonCount += counters.DrainFallbackReason(index);
			require(fallbackCount == 8, "fallback interval did not contain all fallback events");
			require(reasonCount == fallbackCount,
				"per-reason fallback counts did not sum to fallback_count");
		});

		runner.test("process page faults report interval deltas", [] {
			s_processMemorySampleIndex = 0;
			TelemetryHub hub{ 1000000 };
			auto process = std::make_shared<ProcessMemoryMetricSource>(&ReadProcessMemorySample);
			require(
				hub.Register(process) == TelemetryRegistration::kAccepted,
				"process memory source registration was rejected");
			require(hub.Freeze(3), "process memory hub freeze failed");

			TelemetryTest::HubAccess::Collect(hub, 1, 1.0, 0.0);
			TelemetrySnapshot first{};
			require(hub.CopyLatest(first), "first process memory sample was not published");
			require(!first.values[4].valid,
				"first process page-fault sample emitted a bogus delta");

			TelemetryTest::HubAccess::Collect(hub, 2, 1.0, 0.0);
			TelemetrySnapshot second{};
			require(hub.CopyLatest(second), "second process memory sample was not published");
			require(second.values[4].valid,
				"second process page-fault sample was invalid");
			require(second.values[4].value == 25.0,
				"second process page-fault sample did not report the interval delta");

			TelemetryTest::HubAccess::Collect(hub, 3, 1.0, 0.0);
			TelemetrySnapshot third{};
			require(hub.CopyLatest(third), "third process memory sample was not published");
			require(third.values[4].valid,
				"backwards process page-fault sample was invalid");
			require(third.values[4].value == 0.0,
				"backwards process page-fault sample underflowed");
		});

		runner.test("frame aggregates are empty without intervals", [] {
			TelemetryHub hub{ 1000000 };
			auto frame = std::make_shared<FrameMetricSource>(hub, 1000000, 10);
			require(
				hub.Register(frame) == TelemetryRegistration::kAccepted,
				"empty frame source registration was rejected");
			require(hub.Freeze(2), "empty frame hub freeze failed");
			TelemetryTest::HubAccess::Collect(hub, 1, 1.0, 0.0);
			TelemetrySnapshot snapshot{};
			require(hub.CopyLatest(snapshot), "empty frame sample was not published");
			require(!snapshot.values[2].valid, "empty frame interval emitted a valid mean");
			require(!snapshot.values[3].valid, "empty frame interval emitted a valid minimum");
			require(!snapshot.values[4].valid,
				"empty frame interval emitted a valid maximum offset");
		});

		runner.test("split frame fields keep their own validity", [] {
			TelemetryHub hub{ 1000000 };
			auto frame = std::make_shared<FrameMetricSource>(hub, 1000000, 10);
			require(
				hub.Register(frame) == TelemetryRegistration::kAccepted,
				"split frame source registration was rejected");
			require(hub.Freeze(2), "split frame hub freeze failed");
			TelemetryTest::HubAccess::StageFrameExtrema(*frame, 2000, 500, 1000);
			TelemetryTest::HubAccess::Collect(hub, 1, 1.0, 0.0);
			TelemetrySnapshot extrema{};
			require(hub.CopyLatest(extrema), "split extrema interval was not published");
			require(extrema.values[0].valid && extrema.values[0].value == 2.0,
				"split maximum did not follow its drained accumulator");
			require(extrema.values[3].valid && extrema.values[3].value == 1.0,
				"split minimum did not follow its drained accumulator");
			require(extrema.values[4].valid && extrema.values[4].value == 0.5,
				"split maximum offset did not follow its drained accumulator");

			TelemetryTest::HubAccess::StageFrameCount(*frame, 1, 2000);
			TelemetryTest::HubAccess::Collect(hub, 2, 1.0, 0.0);
			TelemetrySnapshot count{};
			require(hub.CopyLatest(count), "split count interval was not published");
			require(count.values[1].valid && count.values[1].value == 1.0,
				"split count interval lost its frame count");
			require(!count.values[0].valid,
				"split count published a maximum without a drained maximum");
			require(!count.values[3].valid,
				"split count published the minimum sentinel");
			require(count.values[3].value == 0.0,
				"split count retained the minimum sentinel value");
			require(!count.values[4].valid,
				"split count published an offset without a drained maximum");
			std::ostringstream csv;
			require(TelemetryHub::WriteCsvRow(csv, count),
				"split count CSV row write failed");
			require(csv.str() == "2,2,1,0,,1,2,,\n",
				"split count CSV row contained stale extrema");
		});

		runner.test("frame mean averages known intervals", [] {
			TelemetryHub hub{ 1000000 };
			auto frame = std::make_shared<FrameMetricSource>(hub, 1000000, 10);
			require(
				hub.Register(frame) == TelemetryRegistration::kAccepted,
				"mean frame source registration was rejected");
			require(hub.Freeze(2), "mean frame hub freeze failed");
			require(hub.Start(60000), "mean frame hub worker did not start");
			TelemetryTest::HubAccess::BeginFrameInterval(*frame, 100);
			frame->ObserveAt(100);
			frame->ObserveAt(1100);
			frame->ObserveAt(3100);
			const auto packed =
				TelemetryTest::HubAccess::FrameCountAndTotalDurationUs(*frame);
			require((packed & ((1ull << 20) - 1)) == 2,
				"packed frame interval did not contain the known count");
			require((packed >> 20) == 3000,
				"packed frame interval did not contain the known total duration");
			TelemetryTest::HubAccess::CollectInterval(hub, 100, 3100, 3.0, 0.0);
			TelemetrySnapshot snapshot{};
			require(hub.CopyLatest(snapshot), "mean frame sample was not published");
			hub.Stop();
			require(snapshot.values[0].valid, "known frame maximum was invalid");
			require(snapshot.values[0].value == 2.0, "known frame maximum was incorrect");
			require(snapshot.values[1].valid, "known frame count was invalid");
			require(snapshot.values[1].value == 2.0, "known frame count was incorrect");
			require(snapshot.values[2].valid, "known frame mean was invalid");
			require(snapshot.values[2].value == 1.5, "known frame mean was incorrect");
			require(snapshot.values[3].valid, "known frame minimum was invalid");
			require(snapshot.values[3].value == 1.0, "known frame minimum was incorrect");
			require(snapshot.values[3].value <= snapshot.values[2].value,
				"known frame minimum exceeded the mean");
			require(snapshot.values[2].value <= snapshot.values[0].value,
				"known frame mean exceeded the maximum");
			require(snapshot.values[4].valid, "known frame maximum offset was invalid");
			require(snapshot.values[4].value == 3.0,
				"known frame maximum offset identified the wrong frame");
			require(snapshot.qpc == 100,
				"known frame snapshot did not retain the interval start qpc");
		});

		runner.test("frame minimum handles equal intervals", [] {
			TelemetryHub hub{ 1000000 };
			auto frame = std::make_shared<FrameMetricSource>(hub, 1000000, 10);
			require(
				hub.Register(frame) == TelemetryRegistration::kAccepted,
				"equal frame source registration was rejected");
			require(hub.Freeze(2), "equal frame hub freeze failed");
			require(hub.Start(60000), "equal frame hub worker did not start");
			TelemetryTest::HubAccess::BeginFrameInterval(*frame, 100);
			frame->ObserveAt(100);
			frame->ObserveAt(2100);
			frame->ObserveAt(4100);
			frame->ObserveAt(6100);
			TelemetryTest::HubAccess::Collect(hub, 6100, 6.0, 0.0);
			TelemetrySnapshot snapshot{};
			require(hub.CopyLatest(snapshot), "equal frame sample was not published");
			hub.Stop();
			require(snapshot.values[0].valid, "equal frame maximum was invalid");
			require(snapshot.values[0].value == 2.0, "equal frame maximum was incorrect");
			require(snapshot.values[1].valid, "equal frame count was invalid");
			require(snapshot.values[1].value == 3.0, "equal frame count was incorrect");
			require(snapshot.values[2].valid, "equal frame mean was invalid");
			require(snapshot.values[2].value == 2.0, "equal frame mean was incorrect");
			require(snapshot.values[3].valid, "equal frame minimum was invalid");
			require(snapshot.values[3].value == 2.0, "equal frame minimum was incorrect");
		});

		runner.test("frame maximum and offset stay paired under interleaved updates", [] {
			TelemetryHub hub{ 1000000 };
			FrameMetricSource frame{ hub, 1000000, 10 };
			std::thread first{ [&frame] {
				for (uint32_t durationUs = 1; durationUs < 1000; durationUs += 2)
				TelemetryTest::HubAccess::UpdateFrameMaximum(
					frame, durationUs, durationUs + 10000);
			} };
			std::thread second{ [&frame] {
				for (uint32_t durationUs = 2; durationUs <= 1000; durationUs += 2)
				TelemetryTest::HubAccess::UpdateFrameMaximum(
					frame, durationUs, durationUs + 20000);
			} };
			first.join();
			second.join();
			const auto maximum =
				TelemetryTest::HubAccess::FrameMaximumAndOffsetUs(frame);
			require(static_cast<uint32_t>(maximum >> 32) == 1000,
				"interleaved updates lost the maximum frame duration");
			require(static_cast<uint32_t>(maximum) == 21000,
				"interleaved updates paired the maximum with another frame's offset");
		});

		runner.test("telemetry metric schemas are stable", [] {
			const ProcessMemoryMetricSource process{};
			const auto processSchema = process.Schema();
			require(processSchema.size() == 5,
				"process schema did not contain exactly five metrics");
			require(processSchema[0].key == "process.working_set_bytes",
				"process working-set key changed");
			require(processSchema[0].unit == Unit::kBytes,
				"process working-set unit changed");
			require(processSchema[1].key == "process.private_bytes",
				"process private-bytes key changed");
			require(processSchema[1].unit == Unit::kBytes,
				"process private-bytes unit changed");
			require(processSchema[2].key == "process.peak_working_set_bytes",
				"process peak working-set key changed");
			require(processSchema[2].unit == Unit::kBytes,
				"process peak working-set unit changed");
			require(processSchema[3].key == "process.pagefile_bytes",
				"process pagefile key changed");
			require(processSchema[3].unit == Unit::kBytes,
				"process pagefile unit changed");
			require(processSchema[4].key == "process.page_faults",
				"process page-fault key changed");
			require(processSchema[4].unit == Unit::kCount,
				"process page-fault unit changed");

			const GpuVideoMemoryMetricSource gpu{ &ReadGpuVideoMemory };
			const auto gpuSchema = gpu.Schema();
			require(gpuSchema.size() == 2,
				"GPU schema did not contain exactly two metrics");
			require(gpuSchema[0].key == "gpu.vram_used_bytes",
				"GPU VRAM usage key changed");
			require(gpuSchema[0].unit == Unit::kBytes,
				"GPU VRAM usage unit changed");
			require(gpuSchema[1].key == "gpu.vram_budget_bytes",
				"GPU VRAM budget key changed");
			require(gpuSchema[1].unit == Unit::kBytes,
				"GPU VRAM budget unit changed");

			const SystemMemoryMetricSource system{ &ReadSystemMemorySample };
			const auto systemSchema = system.Schema();
			require(systemSchema.size() == 2,
				"system schema did not contain exactly two metrics");
			require(systemSchema[0].key == "system.available_physical_bytes",
				"system available physical memory key changed");
			require(systemSchema[0].unit == Unit::kBytes,
				"system available physical memory unit changed");
			require(systemSchema[1].key == "system.commit_used_bytes",
				"system commit usage key changed");
			require(systemSchema[1].unit == Unit::kBytes,
				"system commit usage unit changed");

			const auto zlibSchema = ZlibIntervalCounters::Schema();
			constexpr std::array<std::string_view, 12> zlibKeys{
				"libdeflate.primary_count",
				"libdeflate.fallback_count",
				"libdeflate.bytes_out",
				"libdeflate.bytes_in",
				"libdeflate.fallback_bytes_out",
				"libdeflate.fallback_state",
				"libdeflate.fallback_allocation",
				"libdeflate.fallback_decode",
				"libdeflate.fallback_commit",
				"libdeflate.fallback_capacity",
				"libdeflate.fallback_size_mismatch",
				"libdeflate.fallback_restart"
			};
			constexpr std::array zlibUnits{
				Unit::kCount,
				Unit::kCount,
				Unit::kBytes,
				Unit::kBytes,
				Unit::kBytes,
				Unit::kCount,
				Unit::kCount,
				Unit::kCount,
				Unit::kCount,
				Unit::kCount,
				Unit::kCount,
				Unit::kCount
			};
			require(zlibSchema.size() == zlibKeys.size(),
				"libdeflate schema size changed");
			for (size_t index = 0; index < zlibKeys.size(); ++index)
			{
				require(zlibSchema[index].key == zlibKeys[index],
					"libdeflate schema key changed at index " + std::to_string(index));
				require(zlibSchema[index].unit == zlibUnits[index],
					"libdeflate schema unit changed at index " + std::to_string(index));
			}

			TelemetryHub frameHub{ 1000000 };
			const FrameMetricSource frame{ frameHub, 1000000, 10 };
			const auto frameSchema = frame.Schema();
			require(frameSchema.size() == 5,
				"frame schema did not contain exactly five metrics");
			require(frameSchema[0].key == "frame.max_ms", "frame maximum key changed");
			require(frameSchema[0].unit == Unit::kMilliseconds,
				"frame maximum unit changed");
			require(frameSchema[1].key == "frame.count", "frame count key changed");
			require(frameSchema[1].unit == Unit::kCount, "frame count unit changed");
			require(frameSchema[2].key == "frame.mean_ms", "frame mean key changed");
			require(frameSchema[2].unit == Unit::kMilliseconds,
				"frame mean unit changed");
			require(frameSchema[3].key == "frame.min_ms", "frame minimum key changed");
			require(frameSchema[3].unit == Unit::kMilliseconds,
				"frame minimum unit changed");
			require(frameSchema[4].key == "frame.max_offset_ms",
				"frame maximum-offset key changed");
			require(frameSchema[4].unit == Unit::kMilliseconds,
				"frame maximum-offset unit changed");
		});

		runner.test("telemetry schemas match drained value counts", [] {
			s_processMemorySampleIndex = 0;
			TelemetryHub hub{ 1000000 };
			auto process = std::make_shared<ProcessMemoryMetricSource>(&ReadProcessMemorySample);
			auto gpu = std::make_shared<GpuVideoMemoryMetricSource>(&ReadGpuVideoMemory);
			auto system = std::make_shared<SystemMemoryMetricSource>(&ReadSystemMemorySample);
			auto frame = std::make_shared<FrameMetricSource>(hub, 1000000, 10);
			require(
				hub.Register(process) == TelemetryRegistration::kAccepted,
				"schema process source registration was rejected");
			require(
				hub.Register(gpu) == TelemetryRegistration::kAccepted,
				"schema GPU source registration was rejected");
			require(
				hub.Register(system) == TelemetryRegistration::kAccepted,
				"schema system source registration was rejected");
			require(
				hub.Register(frame) == TelemetryRegistration::kAccepted,
				"schema frame source registration was rejected");
			require(hub.Freeze(2), "schema value-count hub freeze failed");
			TelemetryTest::HubAccess::Collect(hub, 1, 1.0, 0.0);
			TelemetrySnapshot snapshot{};
			require(hub.CopyLatest(snapshot), "schema value-count sample was not published");
			require(hub.Columns().size() == 14,
				"combined telemetry schemas did not contain fourteen metrics");
			require(snapshot.values.size() == hub.Columns().size(),
				"drained telemetry value count did not match the schema");
			require(snapshot.values[5].valid && snapshot.values[5].value == 5000.0,
				"GPU source did not write its first schema slot");
			require(snapshot.values[8].valid && snapshot.values[8].value == 8000.0,
				"system source did not write its final schema slot");
			require(!snapshot.values[13].valid,
				"frame source did not write its final schema slot");
		});

		runner.test("audio telemetry schema is stable", [] {
			constexpr std::array<std::string_view, 5> keys{
				"audio.glitches",
				"audio.active_voices",
				"audio.total_voices",
				"audio.latency_samples",
				"audio.memory_bytes"
			};
			constexpr std::array units{
				Unit::kCount,
				Unit::kCount,
				Unit::kCount,
				Unit::kCount,
				Unit::kBytes
			};
			require(kAudioPerformanceMetricSchema.size() == keys.size(),
				"audio schema size changed");
			for (size_t index = 0; index < keys.size(); ++index)
			{
				require(kAudioPerformanceMetricSchema[index].key == keys[index],
					"audio schema key changed at index " + std::to_string(index));
				require(kAudioPerformanceMetricSchema[index].unit == units[index],
					"audio schema unit changed at index " + std::to_string(index));
			}
		});

		runner.test("escape telemetry schema is stable", [] {
			constexpr std::array<std::string_view, 10> keys{
				"escape.stall_candidates",
				"escape.forced_orphan_releases",
				"escape.aborted_orphan_releases",
				"escape.renderer_resumptions_clean",
				"escape.renderer_resumptions_after_release",
				"escape.healthy_sample_sequences",
				"escape.corrupt_count_observations",
				"escape.owner_alive_observations",
				"escape.owner_unknown_observations",
				"escape.unresolved_candidates"
			};
			require(kEscapeFreezeMetricSchema.size() == keys.size(),
				"escape schema size changed");
			for (size_t index = 0; index < keys.size(); ++index)
			{
				require(kEscapeFreezeMetricSchema[index].key == keys[index],
					"escape schema key changed at index " + std::to_string(index));
				require(kEscapeFreezeMetricSchema[index].unit == Unit::kCount,
					"escape schema unit changed at index " + std::to_string(index));
			}
		});

		runner.test("reference handle telemetry schema is stable", [] {
			require(kReferenceHandleMetricSchema.size() == 2,
				"reference handle schema size changed");
			require(kReferenceHandleMetricSchema[0].key == "references.handle_count",
				"reference handle count key changed");
			require(kReferenceHandleMetricSchema[0].unit == Unit::kCount,
				"reference handle count unit changed");
			require(kReferenceHandleMetricSchema[1].key == "references.handle_usage",
				"reference handle usage key changed");
			require(kReferenceHandleMetricSchema[1].unit == Unit::kPercent,
				"reference handle usage unit changed");
		});

		runner.test("module outcome telemetry schema is stable", [] {
			constexpr std::array<std::string_view, 6> keys{
				"modules.install_events",
				"modules.disable_events",
				"modules.skip_events",
				"modules.query_failures",
				"modules.install_failures",
				"modules.outcome_events"
			};
			require(kModuleOutcomeMetricSchema.size() == keys.size(),
				"module outcome schema size changed");
			for (size_t index = 0; index < keys.size(); ++index)
			{
				require(kModuleOutcomeMetricSchema[index].key == keys[index],
					"module outcome schema key changed at index " + std::to_string(index));
				require(kModuleOutcomeMetricSchema[index].unit == Unit::kCount,
					"module outcome schema unit changed at index " + std::to_string(index));
			}
		});

		runner.test("new telemetry schemas match drained value counts", [] {
			TelemetryHub hub{ 1000000 };
			require(
				hub.Register(std::make_shared<EscapeFreezeMetricSource>(
					kEscapeFreezeMetricSchema, &ReadEscapeFixture)) ==
					TelemetryRegistration::kAccepted,
				"escape fixture source registration was rejected");
			require(
				hub.Register(std::make_shared<ReferenceHandleMetricSource>(
					kReferenceHandleMetricSchema, &ReadReferenceHandleFixture)) ==
					TelemetryRegistration::kAccepted,
				"reference handle fixture source registration was rejected");
			require(
				hub.Register(std::make_shared<ModuleOutcomeMetricSource>(
					kModuleOutcomeMetricSchema, &ReadModuleOutcomeFixture)) ==
					TelemetryRegistration::kAccepted,
				"module outcome fixture source registration was rejected");
			require(
				hub.Register(std::make_shared<AudioPerformanceMetricSource>(
					kAudioPerformanceMetricSchema, &ReadAudioPerformanceFixture)) ==
					TelemetryRegistration::kAccepted,
				"audio performance fixture source registration was rejected");
			require(hub.Freeze(1), "new source value-count hub freeze failed");
			TelemetryTest::HubAccess::Collect(hub, 1, 1.0, 0.0);
			TelemetrySnapshot snapshot{};
			require(hub.CopyLatest(snapshot), "new source value-count sample was not published");
			require(hub.Columns().size() == 23,
				"new telemetry schemas did not contain twenty-three metrics");
			require(snapshot.values.size() == hub.Columns().size(),
				"new source drained value count did not match the schema");
			require(snapshot.values[9].valid && snapshot.values[9].value == 10.0,
				"escape source did not write its final schema slot");
			require(snapshot.values[11].valid && snapshot.values[11].value == 50.0,
				"reference handle source did not write its final schema slot");
			require(snapshot.values[17].valid && snapshot.values[17].value == 15.0,
				"module outcome source did not write its final schema slot");
			require(snapshot.values[22].valid && snapshot.values[22].value == 55.0,
				"audio performance source did not write its final schema slot");
		});

		runner.test("unavailable runtime telemetry stays invalid", [] {
			TelemetryHub hub{ 1000000 };
			require(
				hub.Register(std::make_shared<EscapeFreezeMetricSource>(
					kEscapeFreezeMetricSchema, &ReadUnavailableEscape)) ==
					TelemetryRegistration::kAccepted,
				"unavailable escape source registration was rejected");
			require(
				hub.Register(std::make_shared<ReferenceHandleMetricSource>(
					kReferenceHandleMetricSchema, &ReadUnavailableReferenceHandles)) ==
					TelemetryRegistration::kAccepted,
				"unavailable reference source registration was rejected");
			require(
				hub.Register(std::make_shared<AudioPerformanceMetricSource>(
					kAudioPerformanceMetricSchema, &ReadUnavailableAudioPerformance)) ==
					TelemetryRegistration::kAccepted,
				"unavailable audio source registration was rejected");
			require(hub.Freeze(1), "unavailable source hub freeze failed");
			TelemetryTest::HubAccess::Collect(hub, 1, 1.0, 0.0);
			TelemetrySnapshot snapshot{};
			require(hub.CopyLatest(snapshot), "unavailable source sample was not published");
			for (const auto value : snapshot.values)
				require(!value.valid, "unavailable runtime source emitted a valid zero");
		});

		runner.test("half-initialized audio telemetry stays invalid", [] {
			TelemetryHub hub{ 1000000 };
			require(
				hub.Register(std::make_shared<AudioPerformanceMetricSource>(
					kAudioPerformanceMetricSchema, &ReadHalfInitializedAudioPerformance)) ==
					TelemetryRegistration::kAccepted,
				"half-initialized audio source registration was rejected");
			require(hub.Freeze(1), "half-initialized audio hub freeze failed");
			TelemetryTest::HubAccess::Collect(hub, 1, 1.0, 0.0);
			TelemetrySnapshot snapshot{};
			require(hub.CopyLatest(snapshot),
				"half-initialized audio sample was not published");
			for (const auto value : snapshot.values)
				require(!value.valid,
					"half-initialized audio engine emitted a valid value");
		});

		runner.test("reference handle usage percentage is correct", [] {
			constexpr uint32_t limit{ 1u << 21 };
			require(ReferenceHandleUsagePercent(limit / 2, limit) == 50.0,
				"half of the handle limit did not report fifty percent");
		});

		runner.test("module outcome total includes every event", [] {
			constexpr std::array<uint64_t, 5> outcomes{ 1, 2, 3, 4, 5 };
			require(ModuleOutcomeTotal(outcomes) == 15,
				"module outcome total omitted an event");
		});

		runner.test("module outcome reader mapping is stable", [] {
			constexpr std::array<uint64_t, 5> outcomes{ 11, 22, 33, 44, 55 };
			constexpr std::array expected{ 11.0, 22.0, 33.0, 44.0, 55.0, 165.0 };
			const auto values = ModuleOutcomeMetricValues(outcomes);
			for (size_t index = 0; index < expected.size(); ++index)
				require(values[index] == expected[index],
					"module outcome mapping changed at index " + std::to_string(index));
		});

		runner.test("audio performance reader mapping is stable", [] {
			const auto values = AudioPerformanceMetricValues(kAudioPerformanceFixture);
			constexpr std::array expected{ 11.0, 22.0, 33.0, 44.0, 55.0 };
			for (size_t index = 0; index < expected.size(); ++index)
				require(values[index] == expected[index],
					"audio performance mapping changed at index " + std::to_string(index));
		});

		runner.test("vmm aggregate pool stats stay within capacity", [] {
			voltek::scalable_pool_stats stats{};
			voltek::scalable_get_pool_stats(&stats);
			require(stats.pool_count <= 14, "aggregate reported more than 14 allocator pools");
			require(stats.pages_busy <= stats.page_capacity,
				"aggregate busy pages exceeded the reported page capacity");
		});

		runner.test("allocator busy pages stay within capacity", [] {
			const voltek::scalable_pool_stats stats{ 2, 7, 4 };
			std::array<MetricValue, 3> values{};
			AllocatorPoolTelemetry::Populate(values, true, stats);
			require(values[0].valid, "active allocator pool count was invalid");
			require(values[1].valid, "active allocator busy pages were invalid");
			require(values[2].valid, "active allocator page capacity was invalid");
			require(values[1].value <= values[2].value,
				"allocator busy pages exceeded the reported page capacity");
		});

		runner.test("inactive allocator values stay empty in CSV", [] {
			TelemetryHub hub{ 1000000 };
			const voltek::scalable_pool_stats stats{ 2, 7, 4 };
			require(
				hub.Register(std::make_shared<TestAllocatorPoolSource>(false, stats)) ==
					TelemetryRegistration::kAccepted,
				"inactive allocator source registration was rejected");
			require(hub.Freeze(2), "inactive allocator hub freeze failed");
			TelemetryTest::HubAccess::Collect(hub, 10, 1.0, 0.0);
			TelemetrySnapshot snapshot{};
			require(hub.CopyLatest(snapshot), "inactive allocator snapshot was not published");
			require(!snapshot.values[0].valid, "inactive allocator pool count was valid");
			require(!snapshot.values[1].valid, "inactive allocator busy pages were valid");
			require(!snapshot.values[2].valid, "inactive allocator page capacity was valid");
			std::ostringstream csv;
			require(TelemetryHub::WriteCsvHeader(csv, hub.Columns()),
				"inactive allocator CSV header write failed");
			require(TelemetryHub::WriteCsvRow(csv, snapshot),
				"inactive allocator CSV row write failed");
			require(
				csv.str() ==
					"sequence,qpc,interval_ms,lateness_ms,allocator.pool_count,"
					"allocator.pages_busy,allocator.page_capacity\n1,10,1,0,,,\n",
				"inactive allocator metrics did not serialize as empty CSV cells");
		});

		runner.test("allocator schema is stable", [] {
			const auto schema = AllocatorPoolTelemetry::Schema();
			require(schema.size() == 3, "allocator schema did not contain exactly three metrics");
			require(schema[0].key == "allocator.pool_count", "allocator pool count key changed");
			require(schema[0].unit == Unit::kCount, "allocator pool count unit changed");
			require(schema[1].key == "allocator.pages_busy", "allocator busy pages key changed");
			require(schema[1].unit == Unit::kCount, "allocator busy pages unit changed");
			require(schema[2].key == "allocator.page_capacity", "allocator page capacity key changed");
			require(schema[2].unit == Unit::kCount, "allocator page capacity unit changed");
		});
	}
}
