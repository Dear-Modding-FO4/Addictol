#pragma once

#include <Telemetry/AdTelemetry.h>

#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Addictol
{
	class TelemetryHub;

#ifdef AD_TELEMETRY_TESTS
	namespace TelemetryTest
	{
		struct OperationProfileAccess;
	}
#endif

	struct OperationProfileDescriptor
	{
		std::string_view series;
		std::string_view label;
		std::string_view allOperationsMetricKey;
		uint32_t samplingPeriod{ 1 };
		// Empty means only this descriptor; otherwise End may classify within this group.
		std::string_view resultGroup{};
	};

	struct OperationProfileDurationBucket
	{
		uint64_t upperNanoseconds;
		std::string_view label;
	};

	struct OperationProfileMetadataLabel
	{
		std::string_view key;
		std::string_view value;
	};

	inline constexpr std::array kDefaultOperationProfileDurationBuckets{
		OperationProfileDurationBucket{ 0, "zero" },
		OperationProfileDurationBucket{ 1'000, "1us" },
		OperationProfileDurationBucket{ 2'000, "2us" },
		OperationProfileDurationBucket{ 4'000, "4us" },
		OperationProfileDurationBucket{ 8'000, "8us" },
		OperationProfileDurationBucket{ 16'000, "16us" },
		OperationProfileDurationBucket{ 32'000, "32us" },
		OperationProfileDurationBucket{ 64'000, "64us" },
		OperationProfileDurationBucket{ 128'000, "128us" },
		OperationProfileDurationBucket{ 256'000, "256us" },
		OperationProfileDurationBucket{ 512'000, "512us" },
		OperationProfileDurationBucket{ 1'000'000, "1ms" },
		OperationProfileDurationBucket{ 2'000'000, "2ms" },
		OperationProfileDurationBucket{ 4'000'000, "4ms" },
		OperationProfileDurationBucket{ 8'000'000, "8ms" },
		OperationProfileDurationBucket{ 16'000'000, "16ms" },
		OperationProfileDurationBucket{ 32'000'000, "32ms" },
		OperationProfileDurationBucket{ 64'000'000, "64ms" },
		OperationProfileDurationBucket{ 128'000'000, "128ms" },
		OperationProfileDurationBucket{ 256'000'000, "256ms" },
		OperationProfileDurationBucket{ 512'000'000, "512ms" },
		OperationProfileDurationBucket{ 1'000'000'000, "1s" },
		OperationProfileDurationBucket{
			std::numeric_limits<uint64_t>::max(),
			"overflow"
		}
	};

	using OperationProfileClock = uint64_t (*)() noexcept;

	struct OperationProfileConfiguration
	{
		std::string_view sourceId{};
		std::string_view sourceName{};
		std::span<const OperationProfileDescriptor> descriptors{};
		std::span<const OperationProfileDurationBucket> durationBuckets{
			kDefaultOperationProfileDurationBuckets
		};
		std::span<const OperationProfileMetadataLabel> metadataLabels{};
		size_t recordCapacity{ 16384 };
		uint32_t publicationShardCount{ 16 };
		OperationProfileClock clock{ nullptr };
	};

	enum class OperationProfileQuality : size_t
	{
		kSampledAdmissions,
		kAcceptedRecords,
		kAdmissionContentionDrops,
		kPublicationContentionDrops,
		kCapacityDrops,
		kStaleTokens,
		kSuppressedOperations,
		kUnfinishedOperations,
		kInvalidResults,
		kProducerCapacityDrops,
		kCount
	};

	inline constexpr std::array kOperationProfileQualityNames{
		std::string_view{ "sampled_admissions" },
		std::string_view{ "accepted_records" },
		std::string_view{ "admission_contention_drops" },
		std::string_view{ "publication_contention_drops" },
		std::string_view{ "capacity_drops" },
		std::string_view{ "stale_tokens" },
		std::string_view{ "suppressed_operations" },
		std::string_view{ "unfinished_operations" },
		std::string_view{ "invalid_results" },
		std::string_view{ "producer_capacity_drops" }
	};
	static_assert(kOperationProfileQualityNames.size() == static_cast<size_t>(OperationProfileQuality::kCount));

	struct OperationProfileCounters
	{
		std::array<uint64_t, kOperationProfileQualityNames.size()> values{};

		[[nodiscard]] uint64_t& operator[](OperationProfileQuality a_quality) noexcept
		{
			return values[static_cast<size_t>(a_quality)];
		}

		[[nodiscard]] uint64_t operator[](OperationProfileQuality a_quality) const noexcept
		{
			return values[static_cast<size_t>(a_quality)];
		}
	};

	class OperationProfileSource;

	struct OperationProfileToken
	{
		OperationProfileToken() noexcept = default;
		OperationProfileToken(const OperationProfileToken&) = delete;
		OperationProfileToken& operator=(const OperationProfileToken&) = delete;
		OperationProfileToken& operator=(OperationProfileToken&&) = delete;
		OperationProfileToken(OperationProfileToken&& a_other) noexcept :
			source(std::exchange(a_other.source, nullptr)),
			captureState(std::exchange(a_other.captureState, nullptr)),
			startQpc(a_other.startQpc),
			descriptorIndex(a_other.descriptorIndex),
			shardIndex(a_other.shardIndex)
		{}

		[[nodiscard]] operator bool() const noexcept { return source != nullptr; }

	private:
		OperationProfileToken(
			OperationProfileSource* a_source,
			void* a_captureState,
			uint64_t a_startQpc,
			uint32_t a_descriptorIndex,
			uint32_t a_shardIndex) noexcept :
			source(a_source),
			captureState(a_captureState),
			startQpc(a_startQpc),
			descriptorIndex(a_descriptorIndex),
			shardIndex(a_shardIndex)
		{}

		OperationProfileSource* source{ nullptr };
		void* captureState{ nullptr };
		uint64_t startQpc{ 0 };
		uint32_t descriptorIndex{ 0 };
		uint32_t shardIndex{ 0 };
		friend class OperationProfileSource;
#ifdef AD_TELEMETRY_TESTS
		friend struct TelemetryTest::OperationProfileAccess;
#endif
	};

	class ScopedOperationProfileSuppression
	{
	public:
		ScopedOperationProfileSuppression() noexcept;
		~ScopedOperationProfileSuppression() noexcept;

		ScopedOperationProfileSuppression(const ScopedOperationProfileSuppression&) = delete;
		ScopedOperationProfileSuppression& operator=(
			const ScopedOperationProfileSuppression&) = delete;
	};

	[[nodiscard]] bool OperationProfileSuppressed() noexcept;
	[[nodiscard]] uint64_t QpcTicksToNanosecondsSaturated(
		uint64_t a_ticks,
		uint64_t a_qpcFrequency) noexcept;

	class OperationProfileSource final :
		public MetricSource,
		public SeriesSource
	{
	public:
		static constexpr size_t kCounterLaneCount{ 256 };
		explicit OperationProfileSource(
			OperationProfileConfiguration a_configuration,
			uint64_t a_qpcFrequency,
			std::pmr::memory_resource* a_memoryResource =
				std::pmr::get_default_resource()) noexcept;

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] OperationProfileToken Begin(uint32_t a_descriptorIndex) noexcept;
		[[nodiscard]] OperationProfileToken BeginAccumulated(uint32_t a_descriptorIndex) noexcept;
		[[nodiscard]] uint64_t ReadClock() const noexcept { return m_clock(); }
		// Long-lived consumers can retire stale tokens without pinning old capture slots.
		[[nodiscard]] bool IsCurrent(const OperationProfileToken& a_token) const noexcept;
		void End(OperationProfileToken a_token, uint64_t a_bytes = 0) noexcept;
		void End(OperationProfileToken a_token, uint64_t a_bytes, uint32_t a_resultDescriptor) noexcept;
		void EndWithDuration(OperationProfileToken a_token, uint64_t a_elapsedQpc,
			uint64_t a_bytes, uint32_t a_resultDescriptor) noexcept;
		[[nodiscard]] std::span<const MetricDescriptor> Schema() const noexcept override;
		[[nodiscard]] size_t SeriesCapacity() const noexcept override;
		[[nodiscard]] std::string_view SourceId() const noexcept;
		[[nodiscard]] std::string_view SourceName() const noexcept;
		[[nodiscard]] std::span<const OperationProfileDescriptor> Descriptors() const noexcept;
		[[nodiscard]] std::span<const OperationProfileDurationBucket> DurationBuckets() const noexcept;
		[[nodiscard]] std::span<const OperationProfileMetadataLabel> MetadataLabels() const noexcept;
		[[nodiscard]] size_t RecordCapacity() const noexcept;
		[[nodiscard]] uint32_t PublicationShardCount() const noexcept;
		[[nodiscard]] uint64_t QpcFrequency() const noexcept;
		[[nodiscard]] uint64_t CaptureGeneration() const noexcept;
		[[nodiscard]] uint64_t AllOperationCount(size_t a_descriptorIndex) const noexcept;
		[[nodiscard]] OperationProfileCounters Counters() const noexcept;

	private:
		[[nodiscard]] OperationProfileToken Begin(uint32_t a_descriptorIndex, bool a_measureLifetime) noexcept;
		void Publish(OperationProfileToken a_token, uint64_t a_bytes,
			uint32_t a_resultDescriptor, std::optional<uint64_t> a_elapsedQpc) noexcept;

		static constexpr size_t kCaptureStateCount{ 8 };
		static constexpr size_t kMaximumPublicationShards{ 16 };

		struct alignas(64) AtomicCounter
		{
			std::atomic<uint64_t> value{ 0 };
			std::atomic<uint64_t> generation{ 0 };

			AtomicCounter() noexcept = default;
			AtomicCounter(AtomicCounter&& a_other) noexcept :
				value(a_other.value.load(std::memory_order_relaxed)),
				generation(a_other.generation.load(std::memory_order_relaxed))
			{}
			AtomicCounter& operator=(AtomicCounter&& a_other) noexcept
			{
				value.store(
					a_other.value.load(std::memory_order_relaxed),
					std::memory_order_relaxed);
				generation.store(a_other.generation.load(std::memory_order_relaxed), std::memory_order_relaxed);
				return *this;
			}
			AtomicCounter(const AtomicCounter&) = delete;
			AtomicCounter& operator=(const AtomicCounter&) = delete;
		};

		struct CaptureState
		{
			std::atomic<uint64_t> activeBegins{ 0 };
			std::atomic<uint64_t> activeSampled{ 0 };
			std::array<std::atomic<uint64_t>, kOperationProfileQualityNames.size()> counters{};
			std::atomic<bool> admissionOpen{ false };
			std::atomic<uint64_t> generation{ 0 };

			[[nodiscard]] std::atomic<uint64_t>& operator[](OperationProfileQuality a_quality) noexcept
			{
				return counters[static_cast<size_t>(a_quality)];
			}
		};

		struct Record
		{
			uint64_t durationTicks{ 0 };
			uint64_t bytes{ 0 };
			uint32_t descriptorIndex{ 0 };
		};

		struct CapturePin
		{
			CaptureState& state;
			bool valid;

			CapturePin(CaptureState& a_state, uint64_t a_generation) noexcept :
				state(a_state)
			{
				state.activeBegins.fetch_add(1, std::memory_order_seq_cst);
				valid = state.generation.load(std::memory_order_seq_cst) == a_generation &&
					state.admissionOpen.load(std::memory_order_relaxed);
			}
			~CapturePin()
			{
				state.activeBegins.fetch_sub(1, std::memory_order_seq_cst);
			}
			CapturePin(const CapturePin&) = delete;
			CapturePin& operator=(const CapturePin&) = delete;
		};

		struct PublicationShard
		{
			std::mutex mutex{};
			size_t offset{ 0 };
			size_t capacity{ 0 };
			size_t count{ 0 };
		};

		void Drain(std::span<MetricValue> a_out) noexcept override;
		[[nodiscard]] size_t DrainSeries(
			std::span<SeriesSample> a_out) noexcept override;
		[[nodiscard]] bool StartCapture() noexcept;
		[[nodiscard]] uint64_t CloseCaptureAdmission() noexcept;
		[[nodiscard]] size_t DurationBucketIndex(uint64_t a_ticks) const noexcept;
		[[nodiscard]] std::string_view OwnString(std::string_view a_value);
		void ResetCaptureState(CaptureState& a_state) noexcept;

		std::pmr::vector<std::pmr::string> m_ownedStrings;
		std::pmr::vector<OperationProfileDescriptor> m_descriptors;
		std::pmr::vector<OperationProfileDurationBucket> m_durationBuckets;
		std::pmr::vector<OperationProfileMetadataLabel> m_metadataLabels;
		std::pmr::vector<MetricDescriptor> m_schema;
		std::pmr::vector<uint64_t> m_descriptorSalts;
		std::pmr::vector<AtomicCounter> m_allOperations;
		std::pmr::vector<Record> m_records;
		std::pmr::vector<Record> m_drainRecords;
		std::pmr::vector<HistogramBucket> m_intervalBuckets;
		std::array<CaptureState, kCaptureStateCount> m_captureStates{};
		std::array<PublicationShard, kMaximumPublicationShards> m_publicationShards{};
		std::array<size_t, kMaximumPublicationShards> m_drainCounts{};
		std::atomic<CaptureState*> m_currentCapture{ nullptr };
		OperationProfileClock m_clock{ nullptr };
		std::string_view m_sourceId{};
		std::string_view m_sourceName{};
		uint64_t m_sourceSalt{ 0 };
		uint64_t m_qpcFrequency{ 0 };
		size_t m_recordCapacity{ 0 };
		uint32_t m_publicationShardCount{ 0 };
		bool m_valid{ false };

		friend class TelemetryHub;
#ifdef AD_TELEMETRY_TESTS
		friend struct TelemetryTest::OperationProfileAccess;
#endif
	};

	template<bool Enabled>
	class OperationProfileConsumer;

	// Install-time owner keeps a hooked consumer valid through hub teardown.
	class OperationProfileSourceOwner
	{
	public:
		OperationProfileSourceOwner() = default;
		OperationProfileSourceOwner(const OperationProfileSourceOwner&) = delete;
		OperationProfileSourceOwner& operator=(const OperationProfileSourceOwner&) = delete;
		~OperationProfileSourceOwner() { m_source = nullptr; }
		[[nodiscard]] OperationProfileSource* Get() const noexcept { return m_source; }
		[[nodiscard]] bool Register(TelemetryHub& a_hub, OperationProfileConfiguration a_configuration) noexcept;

	private:
		std::shared_ptr<OperationProfileSource> m_owner;
		OperationProfileSource* m_source{ nullptr };
	};

	template<>
	class OperationProfileConsumer<false>
	{
	public:
		explicit OperationProfileConsumer(OperationProfileSource* = nullptr) noexcept {}
		[[nodiscard]] OperationProfileToken Begin(uint32_t) const noexcept { return {}; }
		void End(OperationProfileToken, uint64_t = 0) const noexcept {}
		void End(OperationProfileToken, uint64_t, uint32_t) const noexcept {}
	};

	template<>
	class OperationProfileConsumer<true>
	{
	public:
		explicit OperationProfileConsumer(OperationProfileSource* a_source) noexcept :
			m_source(a_source)
		{}

		[[nodiscard]] OperationProfileToken Begin(uint32_t a_descriptorIndex) const noexcept
		{
			return m_source ? m_source->Begin(a_descriptorIndex) : OperationProfileToken{};
		}

		void End(OperationProfileToken a_token, uint64_t a_bytes = 0) const noexcept
		{
			if (m_source)
				m_source->End(std::move(a_token), a_bytes);
		}

		void End(OperationProfileToken a_token, uint64_t a_bytes, uint32_t a_resultDescriptor) const noexcept
		{
			if (m_source)
				m_source->End(std::move(a_token), a_bytes, a_resultDescriptor);
		}

	private:
		OperationProfileSource* m_source;
	};

	template<class Installer>
	[[nodiscard]] bool InstallSelectedOperationProfileConsumer(
		bool a_enabled,
		Installer&& a_installer)
	{
		return a_enabled ?
			std::forward<Installer>(a_installer).template operator()<true>() :
			std::forward<Installer>(a_installer).template operator()<false>();
	}
}
