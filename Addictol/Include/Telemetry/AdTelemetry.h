#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace Addictol
{
	enum class ZlibFallbackReason : uint8_t;

	enum class Unit : uint8_t
	{
		kCount,
		kBytes,
		kMilliseconds,
		kPercent
	};

	struct MetricDescriptor
	{
		std::string_view key;
		Unit unit;
	};

	struct MetricValue
	{
		double value{ 0.0 };
		bool valid{ false };
	};

	struct HistogramBucket
	{
		uint64_t calls{ 0 };
		uint64_t ticks{ 0 };
		uint64_t bytes{ 0 };

		constexpr bool operator==(const HistogramBucket&) const noexcept = default;
	};

	template<size_t N>
	struct Histogram
	{
		static_assert(N > 0);
		static_assert(std::atomic<uint64_t>::is_always_lock_free);
		inline static constexpr size_t kOverflowBucket{ N - 1 };

		std::array<std::atomic<uint64_t>, N> calls{};
		std::array<std::atomic<uint64_t>, N> ticks{};
		std::array<std::atomic<uint64_t>, N> bytes{};

		void Add(
			size_t a_index,
			uint64_t a_calls,
			uint64_t a_ticks,
			uint64_t a_bytes) noexcept
		{
			const auto index = a_index < N ? a_index : kOverflowBucket;
			calls[index].fetch_add(a_calls, std::memory_order_relaxed);
			ticks[index].fetch_add(a_ticks, std::memory_order_relaxed);
			bytes[index].fetch_add(a_bytes, std::memory_order_relaxed);
		}

	// fields may span adjacent intervals
		[[nodiscard]] std::array<HistogramBucket, N> Drain() noexcept
		{
			std::array<HistogramBucket, N> drained{};
			for (size_t index = 0; index < N; ++index)
			{
				drained[index].calls =
					calls[index].exchange(0, std::memory_order_relaxed);
				drained[index].ticks =
					ticks[index].exchange(0, std::memory_order_relaxed);
				drained[index].bytes =
					bytes[index].exchange(0, std::memory_order_relaxed);
			}
			return drained;
		}

		[[nodiscard]] HistogramBucket Total() const noexcept
		{
			HistogramBucket total{};
			for (size_t index = 0; index < N; ++index)
			{
				total.calls += calls[index].load(std::memory_order_relaxed);
				total.ticks += ticks[index].load(std::memory_order_relaxed);
				total.bytes += bytes[index].load(std::memory_order_relaxed);
			}
			return total;
		}
	};

	struct ZlibSizeBucketDescriptor
	{
		uint64_t upperBound;
		std::string_view label;
	};

	inline constexpr std::array kZlibSizeBuckets{
		ZlibSizeBucketDescriptor{ 0, "empty" },
		ZlibSizeBucketDescriptor{ 255, "1-255" },
		ZlibSizeBucketDescriptor{ 1023, "256-1023" },
		ZlibSizeBucketDescriptor{ 4095, "1024-4095" },
		ZlibSizeBucketDescriptor{ 16383, "4096-16383" },
		ZlibSizeBucketDescriptor{ 65535, "16384-65535" },
		ZlibSizeBucketDescriptor{ 262143, "65536-262143" },
		ZlibSizeBucketDescriptor{
			std::numeric_limits<uint64_t>::max(),
			"262144+"
		},
		ZlibSizeBucketDescriptor{
			std::numeric_limits<uint64_t>::max(),
			"unknown"
		}
	};
	inline constexpr size_t kZlibUnknownSizeBucket{ kZlibSizeBuckets.size() - 1 };

	[[nodiscard]] constexpr size_t ZlibSizeBucketIndex(uint64_t a_bytes) noexcept
	{
		for (size_t index = 0; index < kZlibUnknownSizeBucket; ++index)
		{
			if (a_bytes <= kZlibSizeBuckets[index].upperBound)
				return index;
		}
		return kZlibUnknownSizeBucket;
	}

	[[nodiscard]] constexpr size_t ZlibThreadBucketIndex(
		uint32_t a_currentThreadId,
		uint32_t a_renderThreadId) noexcept
	{
		return a_renderThreadId && a_currentThreadId == a_renderThreadId ? 0 : 1;
	}

	[[nodiscard]] constexpr size_t ZlibFlushBucketIndex(int32_t a_flush) noexcept
	{
		switch (a_flush)
		{
		case 0:
			return 0;
		case 2:
			return 1;
		case 4:
			return 2;
		case 5:
			return 3;
		case 6:
			return 4;
		default:
			return 5;
		}
	}

	struct SeriesSample
	{
		std::string_view series;
		std::string_view bucket;
		uint64_t calls;
		uint64_t ticks;
		uint64_t bytes;
	};

	[[nodiscard]] inline bool AppendSeriesSample(
		std::span<SeriesSample> a_out,
		size_t& a_offset,
		SeriesSample a_sample) noexcept
	{
		if (a_offset >= a_out.size())
			return false;
		a_out[a_offset++] = a_sample;
		return true;
	}

	class SeriesSource
	{
	public:
		virtual ~SeriesSource() = default;
		[[nodiscard]] virtual size_t SeriesCapacity() const noexcept = 0;

	private:
		[[nodiscard]] virtual size_t DrainSeries(
			std::span<SeriesSample> a_out) noexcept = 0;
		friend class TelemetryHub;
	};

	class MetricSource
	{
	public:
		virtual ~MetricSource() = default;
		[[nodiscard]] virtual std::span<const MetricDescriptor> Schema() const noexcept = 0;

	private:
		virtual void Drain(std::span<MetricValue> a_out) noexcept = 0;
		virtual void BeginInterval(uint64_t) noexcept {}
		friend class TelemetryHub;
	};

	template<size_t N>
	class SnapshotMetricSource : public MetricSource
	{
	public:
		using Values = std::array<double, N>;
		using Reader = bool (*)(Values&) noexcept;

		SnapshotMetricSource(
			std::span<const MetricDescriptor, N> a_schema,
			Reader a_reader) noexcept :
			m_schema(a_schema),
			m_reader(a_reader)
		{}

		[[nodiscard]] std::span<const MetricDescriptor> Schema() const noexcept override
		{
			return m_schema;
		}

	private:
		void Drain(std::span<MetricValue> a_out) noexcept override
		{
			if (a_out.size() != N)
				return;
			Values values{};
			const auto valid = m_reader && m_reader(values);
			for (size_t index = 0; index < N; ++index)
				a_out[index] = { values[index], valid };
		}

		std::span<const MetricDescriptor, N> m_schema;
		Reader m_reader;
	};

	template<class... Values>
	[[nodiscard]] constexpr auto MetricDoubles(Values... a_values) noexcept
	{
		return std::array{ static_cast<double>(a_values)... };
	}

	inline constexpr std::array kAudioPerformanceMetricSchema{
		MetricDescriptor{ "audio.glitches", Unit::kCount },
		MetricDescriptor{ "audio.active_voices", Unit::kCount },
		MetricDescriptor{ "audio.total_voices", Unit::kCount },
		MetricDescriptor{ "audio.latency_samples", Unit::kCount },
		MetricDescriptor{ "audio.memory_bytes", Unit::kBytes }
	};

	inline constexpr std::array kEscapeFreezeMetricSchema{
		MetricDescriptor{ "escape.stall_candidates", Unit::kCount },
		MetricDescriptor{ "escape.forced_orphan_releases", Unit::kCount },
		MetricDescriptor{ "escape.aborted_orphan_releases", Unit::kCount },
		MetricDescriptor{ "escape.renderer_resumptions_clean", Unit::kCount },
		MetricDescriptor{ "escape.renderer_resumptions_after_release", Unit::kCount },
		MetricDescriptor{ "escape.healthy_sample_sequences", Unit::kCount },
		MetricDescriptor{ "escape.corrupt_count_observations", Unit::kCount },
		MetricDescriptor{ "escape.owner_alive_observations", Unit::kCount },
		MetricDescriptor{ "escape.owner_unknown_observations", Unit::kCount },
		MetricDescriptor{ "escape.unresolved_candidates", Unit::kCount }
	};

	inline constexpr std::array kReferenceHandleMetricSchema{
		MetricDescriptor{ "references.handle_count", Unit::kCount },
		MetricDescriptor{ "references.handle_usage", Unit::kPercent }
	};

	inline constexpr std::array kModuleOutcomeMetricSchema{
		MetricDescriptor{ "modules.install_events", Unit::kCount },
		MetricDescriptor{ "modules.disable_events", Unit::kCount },
		MetricDescriptor{ "modules.skip_events", Unit::kCount },
		MetricDescriptor{ "modules.query_failures", Unit::kCount },
		MetricDescriptor{ "modules.install_failures", Unit::kCount },
		MetricDescriptor{ "modules.outcome_events", Unit::kCount }
	};

	using AudioPerformanceMetricSource =
		SnapshotMetricSource<kAudioPerformanceMetricSchema.size()>;
	using EscapeFreezeMetricSource = SnapshotMetricSource<kEscapeFreezeMetricSchema.size()>;
	using ReferenceHandleMetricSource = SnapshotMetricSource<kReferenceHandleMetricSchema.size()>;
	using ModuleOutcomeMetricSource = SnapshotMetricSource<kModuleOutcomeMetricSchema.size()>;

	template<class PerformanceData>
	[[nodiscard]] constexpr auto AudioPerformanceMetricValues(
		const PerformanceData& a_performance) noexcept
	{
		return MetricDoubles(
			a_performance.GlitchesSinceEngineStarted, a_performance.ActiveSourceVoiceCount,
			a_performance.TotalSourceVoiceCount,
			a_performance.CurrentLatencyInSamples,
			a_performance.MemoryUsageInBytes);
	}

	template<class PerformanceData, class Engine, class MasteringVoice>
	[[nodiscard]] bool ReadAudioPerformanceMetricValues(
		Engine* a_engine,
		MasteringVoice* a_masteringVoice,
		AudioPerformanceMetricSource::Values& a_values) noexcept
	{
		if (!a_engine) return false;
		if (!a_masteringVoice) return false;
		PerformanceData performance{};
		a_engine->GetPerformanceData(&performance);
		a_values = AudioPerformanceMetricValues(performance);
		return true;
	}

	[[nodiscard]] constexpr double ReferenceHandleUsagePercent(
		uint32_t a_count,
		uint32_t a_limit) noexcept
	{
		return a_limit ? static_cast<double>(a_count) * 100.0 /
			static_cast<double>(a_limit) : 0.0;
	}

	[[nodiscard]] constexpr uint64_t ModuleOutcomeTotal(
		const std::array<uint64_t, 5>& a_counts) noexcept
	{
		return a_counts[0] + a_counts[1] + a_counts[2] + a_counts[3] + a_counts[4];
	}

	[[nodiscard]] constexpr auto ModuleOutcomeMetricValues(
		const std::array<uint64_t, 5>& a_counts) noexcept
	{
		return MetricDoubles(
			a_counts[0], a_counts[1], a_counts[2], a_counts[3], a_counts[4],
			ModuleOutcomeTotal(a_counts));
	}

	struct alignas(64) ZlibIntervalCounters
	{
		inline static constexpr size_t kFallbackReasonCount{ 7 };

		std::atomic<uint64_t> packed{ 0 };
		std::atomic<uint64_t> bytesIn{ 0 };
		std::atomic<uint64_t> bytesOut{ 0 };
		std::atomic<uint64_t> fallbackBytesOut{ 0 };
		std::array<std::atomic<uint64_t>, kFallbackReasonCount> fallbackReasons{};
		Histogram<kZlibSizeBuckets.size()> servedLibDeflateOutput{};
		Histogram<kZlibSizeBuckets.size()> servedStockOutput{};
		Histogram<kZlibSizeBuckets.size()> servedLibDeflateInput{};
		Histogram<kZlibSizeBuckets.size()> servedStockInput{};
		Histogram<2> fallbackThread{};
		Histogram<2> servedThread{};
		Histogram<6> flush{};

		[[nodiscard]] static std::span<const MetricDescriptor> Schema() noexcept
		{
			static constexpr std::array schema{
				MetricDescriptor{ "libdeflate.primary_count", Unit::kCount },
				MetricDescriptor{ "libdeflate.fallback_count", Unit::kCount },
				MetricDescriptor{ "libdeflate.bytes_out", Unit::kBytes },
				MetricDescriptor{ "libdeflate.bytes_in", Unit::kBytes },
				MetricDescriptor{ "libdeflate.fallback_bytes_out", Unit::kBytes },
				MetricDescriptor{ "libdeflate.fallback_state", Unit::kCount },
				MetricDescriptor{ "libdeflate.fallback_allocation", Unit::kCount },
				MetricDescriptor{ "libdeflate.fallback_decode", Unit::kCount },
				MetricDescriptor{ "libdeflate.fallback_commit", Unit::kCount },
				MetricDescriptor{ "libdeflate.fallback_capacity", Unit::kCount },
				MetricDescriptor{ "libdeflate.fallback_size_mismatch", Unit::kCount },
				MetricDescriptor{ "libdeflate.fallback_restart", Unit::kCount }
			};
			return schema;
		}

		void Observe(
			ZlibFallbackReason a_fallbackReason,
			uint64_t a_bytesIn,
			uint64_t a_bytesOut) noexcept
		{
			const auto reason = std::to_underlying(a_fallbackReason);
			packed.fetch_add(reason ? (1ull << 32) : 1ull, std::memory_order_relaxed);
			bytesIn.fetch_add(a_bytesIn, std::memory_order_relaxed);
			bytesOut.fetch_add(a_bytesOut, std::memory_order_relaxed);
			if (reason > 0 && reason <= kFallbackReasonCount)
			{
				fallbackBytesOut.fetch_add(a_bytesOut, std::memory_order_relaxed);
				fallbackReasons[reason - 1].fetch_add(1, std::memory_order_relaxed);
			}
		}

		void ObserveSeries(
			ZlibFallbackReason a_fallbackReason,
			bool a_servedByLibDeflate,
			int32_t a_flush,
			uint32_t a_currentThreadId,
			uint32_t a_renderThreadId,
			uint64_t a_bytesIn,
			uint64_t a_bytesOut,
			uint64_t a_ticks) noexcept
		{
			auto& output =
				a_servedByLibDeflate ? servedLibDeflateOutput : servedStockOutput;
			auto& input =
				a_servedByLibDeflate ? servedLibDeflateInput : servedStockInput;
			output.Add(ZlibSizeBucketIndex(a_bytesOut), 1, a_ticks, a_bytesOut);
			input.Add(ZlibSizeBucketIndex(a_bytesIn), 1, a_ticks, a_bytesIn);
			const auto threadBucket =
				ZlibThreadBucketIndex(a_currentThreadId, a_renderThreadId);
			servedThread.Add(threadBucket, 1, a_ticks, a_bytesOut);
			if (std::to_underlying(a_fallbackReason))
				fallbackThread.Add(threadBucket, 1, a_ticks, a_bytesOut);
			flush.Add(ZlibFlushBucketIndex(a_flush), 1, a_ticks, a_bytesOut);
		}

		[[nodiscard]] uint64_t Drain() noexcept
		{
			return packed.exchange(0, std::memory_order_relaxed);
		}

	// bytes may land in the next interval
		[[nodiscard]] uint64_t DrainBytesIn() noexcept
		{
			return bytesIn.exchange(0, std::memory_order_relaxed);
		}

		[[nodiscard]] uint64_t DrainBytesOut() noexcept
		{
			return bytesOut.exchange(0, std::memory_order_relaxed);
		}

		[[nodiscard]] uint64_t DrainFallbackBytesOut() noexcept
		{
			return fallbackBytesOut.exchange(0, std::memory_order_relaxed);
		}

		[[nodiscard]] uint64_t DrainFallbackReason(size_t a_index) noexcept
		{
			return fallbackReasons[a_index].exchange(0, std::memory_order_relaxed);
		}
	};

	namespace Telemetry
	{
		[[nodiscard]] bool EnabledRelaxed() noexcept;
		[[nodiscard]] uint32_t RenderThreadIdRelaxed() noexcept;
		void CaptureRenderThread(uint32_t a_threadId) noexcept;
		void ObserveZlibCall(
			ZlibIntervalCounters& a_counters,
			bool a_enabled,
			ZlibFallbackReason a_fallbackReason,
			bool a_servedByLibDeflate,
			int32_t a_flush,
			uint32_t a_currentThreadId,
			uint64_t a_bytesIn,
			uint64_t a_bytesOut,
			uint64_t a_ticks) noexcept;
	}
}
