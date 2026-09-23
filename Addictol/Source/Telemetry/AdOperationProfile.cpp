#include <Telemetry/AdOperationProfile.h>
#include <Core/AdClock.h>
#include <Telemetry/AdTelemetryHub.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace Addictol
{
	namespace
	{
		inline constexpr std::array<std::string_view, 10> kQualityMetricSuffixes{
			"sampled_admissions",
			"accepted_records",
			"admission_contention_drops",
			"publication_contention_drops",
			"capacity_drops",
			"stale_tokens",
			"suppressed_operations",
			"unfinished_operations",
			"invalid_results",
			"producer_capacity_drops"
		};
		inline constexpr uint32_t kMaximumPublicationShards{ 16 };

		thread_local uint32_t s_suppressionDepth{ 0 };
		std::atomic<uint64_t> s_nextCaptureGeneration{ 1 };
		std::atomic<uint64_t> s_nextThreadSalt{ 1 };
		inline constexpr size_t kCounterLanes{ OperationProfileSource::kCounterLaneCount };
		std::array<std::atomic<bool>, kCounterLanes> s_counterLaneOwners{};

		[[nodiscard]] uint64_t Mix64(uint64_t a_value) noexcept
		{
			a_value += 0x9E3779B97F4A7C15ull;
			a_value = (a_value ^ (a_value >> 30)) * 0xBF58476D1CE4E5B9ull;
			a_value = (a_value ^ (a_value >> 27)) * 0x94D049BB133111EBull;
			return a_value ^ (a_value >> 31);
		}

		[[nodiscard]] uint64_t HashString(std::string_view a_value) noexcept
		{
			uint64_t hash{ 1469598103934665603ull };
			for (const auto character : a_value)
			{
				hash ^= static_cast<unsigned char>(character);
				hash *= 1099511628211ull;
			}
			return Mix64(hash);
		}

		struct SamplingState
		{
			uint64_t sequence{ 0 };
			uint64_t threadSalt{
				Mix64(s_nextThreadSalt.fetch_add(1, std::memory_order_relaxed))
			};
			size_t counterLane{ kCounterLanes };

			SamplingState() noexcept
			{
				for (size_t index = 0; index < kCounterLanes; ++index)
				{
					bool expected{ false };
					if (s_counterLaneOwners[index].compare_exchange_strong(
						expected, true, std::memory_order_acquire, std::memory_order_relaxed))
					{
						counterLane = index;
						break;
					}
				}
			}

			~SamplingState()
			{
				// Later CRT/TLS destructors must not reuse a lane already returned to another thread.
				++s_suppressionDepth;
				if (counterLane < kCounterLanes)
					s_counterLaneOwners[counterLane].store(false, std::memory_order_release);
			}
		};

		thread_local SamplingState* s_samplingState{ nullptr };

		[[nodiscard]] SamplingState& ThreadSamplingState() noexcept
		{
			if (!s_samplingState)
			{
				ScopedOperationProfileSuppression suppression;
				static thread_local SamplingState state;
				s_samplingState = &state;
			}
			return *s_samplingState;
		}

		struct SamplingDecision
		{
			uint64_t hash{ 0 };
			bool selected{ false };
		};

		[[nodiscard]] SamplingDecision ShouldSample(
			SamplingState& a_state,
			uint64_t a_sourceSalt,
			uint64_t a_descriptorSalt,
			uint64_t a_generation,
			uint32_t a_period) noexcept
		{
			const auto sequence = ++a_state.sequence;
			const auto hash = Mix64(
				sequence ^
				a_state.threadSalt ^
				a_sourceSalt ^
				a_descriptorSalt ^
				Mix64(a_generation));
			return {
				hash,
				a_period <= 1 || hash % a_period == 0
			};
		}

		[[nodiscard]] bool ValidMetricKey(std::string_view a_key) noexcept
		{
			if (a_key.empty() || a_key.front() == '.' || a_key.back() == '.' ||
				a_key.find('.') == std::string_view::npos)
				return false;
			for (const auto character : a_key)
			{
				if ((character < 'a' || character > 'z') &&
					(character < '0' || character > '9') &&
					character != '_' && character != '.')
					return false;
			}
			return true;
		}

		[[nodiscard]] bool ValidSourceId(std::string_view a_id) noexcept
		{
			if (a_id.empty() || a_id.front() < 'a' || a_id.front() > 'z')
				return false;
			for (const auto character : a_id)
			{
				if ((character < 'a' || character > 'z') &&
					(character < '0' || character > '9') &&
					character != '_')
					return false;
			}
			return true;
		}

		[[nodiscard]] bool ValidConfiguration(
			const OperationProfileConfiguration& a_configuration) noexcept
		{
			if (!ValidSourceId(a_configuration.sourceId) ||
				a_configuration.sourceName.empty() ||
				a_configuration.descriptors.empty() ||
				a_configuration.durationBuckets.empty() ||
				a_configuration.durationBuckets.front().upperNanoseconds != 0 ||
				a_configuration.durationBuckets.back().upperNanoseconds !=
					std::numeric_limits<uint64_t>::max() ||
				!a_configuration.recordCapacity ||
				!a_configuration.publicationShardCount ||
				a_configuration.publicationShardCount >
					kMaximumPublicationShards ||
				a_configuration.publicationShardCount >
					a_configuration.recordCapacity)
				return false;

			uint64_t previous{ 0 };
			for (size_t index = 0;
				index < a_configuration.durationBuckets.size();
				++index)
			{
				const auto& bucket = a_configuration.durationBuckets[index];
				if (bucket.label.empty() ||
					(index && bucket.upperNanoseconds <= previous))
					return false;
				previous = bucket.upperNanoseconds;
			}
			for (size_t index = 0;
				index < a_configuration.descriptors.size();
				++index)
			{
				const auto& descriptor = a_configuration.descriptors[index];
				if (!ValidMetricKey(descriptor.series) ||
					descriptor.label.empty() ||
					!ValidMetricKey(descriptor.allOperationsMetricKey) ||
					!descriptor.samplingPeriod)
					return false;
				for (size_t other = 0; other < index; ++other)
				{
					if (a_configuration.descriptors[other].series ==
							descriptor.series ||
						a_configuration.descriptors[other].allOperationsMetricKey ==
							descriptor.allOperationsMetricKey)
						return false;
				}
			}
			for (const auto& label : a_configuration.metadataLabels)
			{
				if (label.key.empty())
					return false;
			}
			return true;
		}
	}

	ScopedOperationProfileSuppression::ScopedOperationProfileSuppression() noexcept
	{
		++s_suppressionDepth;
	}

	ScopedOperationProfileSuppression::~ScopedOperationProfileSuppression() noexcept
	{
		--s_suppressionDepth;
	}

	bool OperationProfileSuppressed() noexcept
	{
		return s_suppressionDepth != 0;
	}

	uint64_t QpcTicksToNanosecondsSaturated(
		uint64_t a_ticks,
		uint64_t a_qpcFrequency) noexcept
	{
		if (!a_qpcFrequency)
			return 0;
		const long double nanoseconds =
			static_cast<long double>(a_ticks) * 1'000'000'000.0L /
			static_cast<long double>(a_qpcFrequency);
		if (nanoseconds >=
			static_cast<long double>(std::numeric_limits<uint64_t>::max()))
			return std::numeric_limits<uint64_t>::max();
		return static_cast<uint64_t>(std::ceil(nanoseconds));
	}

	OperationProfilePercentile EstimateOperationProfilePercentile(
		std::span<const HistogramBucket> a_distribution,
		std::span<const OperationProfileDurationBucket> a_buckets,
		double a_percentile) noexcept
	{
		if (a_distribution.size() != a_buckets.size() ||
			a_distribution.empty() || !(a_percentile > 0.0) ||
			a_percentile > 1.0)
			return {};

		uint64_t total{ 0 };
		for (const auto& bucket : a_distribution)
		{
			if (bucket.calls > std::numeric_limits<uint64_t>::max() - total)
				total = std::numeric_limits<uint64_t>::max();
			else
				total += bucket.calls;
		}
		if (!total)
			return {};

		const auto rankValue = std::ceil(
			static_cast<long double>(total) * a_percentile);
		const auto rank = rankValue >= static_cast<long double>(total) ?
			total : (std::max)(uint64_t{ 1 }, static_cast<uint64_t>(rankValue));
		uint64_t cumulative{ 0 };
		for (size_t index = 0; index < a_distribution.size(); ++index)
		{
			const auto calls = a_distribution[index].calls;
			cumulative = calls > std::numeric_limits<uint64_t>::max() - cumulative ?
				std::numeric_limits<uint64_t>::max() : cumulative + calls;
			if (cumulative < rank)
				continue;
			const auto upper = a_buckets[index].upperNanoseconds;
			const auto lower = index && a_buckets[index - 1].upperNanoseconds !=
				std::numeric_limits<uint64_t>::max() ?
				a_buckets[index - 1].upperNanoseconds + 1 : 0;
			return {
				lower,
				upper,
				true,
				upper == std::numeric_limits<uint64_t>::max()
			};
		}
		return {};
	}

	OperationProfileSource::OperationProfileSource(
		OperationProfileConfiguration a_configuration,
		uint64_t a_qpcFrequency,
		std::pmr::memory_resource* a_memoryResource) noexcept :
		m_ownedStrings(a_memoryResource),
		m_descriptors(a_memoryResource),
		m_durationBuckets(a_memoryResource),
		m_metadataLabels(a_memoryResource),
		m_schema(a_memoryResource),
		m_descriptorSalts(a_memoryResource),
		m_allOperations(a_memoryResource),
		m_records(a_memoryResource),
		m_drainRecords(a_memoryResource),
		m_intervalBuckets(a_memoryResource),
		m_clock(a_configuration.clock ? a_configuration.clock : &Addictol::ReadQpc),
		m_qpcFrequency(a_qpcFrequency)
	{
		try
		{
			if (!ValidConfiguration(a_configuration) ||
				!m_qpcFrequency || !m_clock)
				return;

			const auto ownedStringCount =
				2 + kQualityMetricSuffixes.size() +
				a_configuration.descriptors.size() * 4 +
				a_configuration.durationBuckets.size() +
				a_configuration.metadataLabels.size() * 2;
			m_ownedStrings.reserve(ownedStringCount);
			m_descriptors.reserve(a_configuration.descriptors.size());
			m_durationBuckets.reserve(a_configuration.durationBuckets.size());
			m_metadataLabels.reserve(a_configuration.metadataLabels.size());
			m_schema.reserve(
				kQualityMetricSuffixes.size() +
				a_configuration.descriptors.size());
			m_descriptorSalts.reserve(a_configuration.descriptors.size());

			m_sourceId = OwnString(a_configuration.sourceId);
			m_sourceName = OwnString(a_configuration.sourceName);
			m_sourceSalt = HashString(m_sourceId);

			for (const auto suffix : kQualityMetricSuffixes)
			{
				std::string key{ "profile." };
				key.append(m_sourceId);
				key.push_back('.');
				key.append(suffix);
				m_schema.push_back({ OwnString(key), Unit::kCount });
			}
			for (size_t index = 0;
				index < a_configuration.descriptors.size();
				++index)
			{
				const auto& descriptor = a_configuration.descriptors[index];
				const auto series = OwnString(descriptor.series);
				m_descriptors.push_back({
					series,
					OwnString(descriptor.label),
					OwnString(descriptor.allOperationsMetricKey),
					descriptor.samplingPeriod,
					OwnString(descriptor.resultGroup)
				});
				m_schema.push_back({
					m_descriptors.back().allOperationsMetricKey,
					Unit::kCount
				});
				m_descriptorSalts.push_back(
					Mix64(HashString(series) ^ static_cast<uint64_t>(index)));
			}
			for (const auto& bucket : a_configuration.durationBuckets)
			{
				m_durationBuckets.push_back({
					bucket.upperNanoseconds,
					OwnString(bucket.label)
				});
			}
			for (const auto& label : a_configuration.metadataLabels)
			{
				m_metadataLabels.push_back({
					OwnString(label.key),
					OwnString(label.value)
				});
			}

			m_recordCapacity = a_configuration.recordCapacity;
			m_publicationShardCount =
				a_configuration.publicationShardCount;
			m_allOperations.resize(
				m_descriptors.size() * kCounterLanes);
			m_records.resize(m_recordCapacity);
			m_drainRecords.resize(m_recordCapacity);
			m_intervalBuckets.resize(
				m_descriptors.size() * m_durationBuckets.size());

			size_t shardOffset{ 0 };
			const auto baseCapacity =
				m_recordCapacity / m_publicationShardCount;
			const auto remainder =
				m_recordCapacity % m_publicationShardCount;
			for (uint32_t index = 0;
				index < m_publicationShardCount;
				++index)
			{
				auto& shard = m_publicationShards[index];
				shard.offset = shardOffset;
				shard.capacity =
					baseCapacity + (index < remainder ? 1 : 0);
				shardOffset += shard.capacity;
			}
			m_valid = true;
		}
		catch (...)
		{
			m_valid = false;
			m_sourceId = {};
			m_sourceName = {};
			m_ownedStrings.clear();
			m_descriptors.clear();
			m_durationBuckets.clear();
			m_metadataLabels.clear();
			m_schema.clear();
			m_descriptorSalts.clear();
			m_allOperations.clear();
			m_records.clear();
			m_drainRecords.clear();
			m_intervalBuckets.clear();
			m_recordCapacity = 0;
			m_publicationShardCount = 0;
		}
	}

	bool OperationProfileSource::IsValid() const noexcept
	{
		return m_valid;
	}

	bool OperationProfileSourceOwner::Register(
		TelemetryHub& a_hub, OperationProfileConfiguration a_configuration) noexcept
	{
		if (m_source)
			return true;
		ScopedOperationProfileSuppression suppression;
		try
		{
			auto source = std::make_shared<OperationProfileSource>(a_configuration, GetQpcFrequency());
			if (a_hub.Register(source) != TelemetryRegistration::kAccepted)
				return false;
			m_owner = std::move(source);
			m_source = m_owner.get();
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	OperationProfileToken OperationProfileSource::Begin(
		uint32_t a_descriptorIndex) noexcept
	{
		auto* capture = m_currentCapture.load(std::memory_order_acquire);
		if (!m_valid || !capture ||
			a_descriptorIndex >= m_descriptors.size() ||
			!capture->admissionOpen.load(std::memory_order_acquire))
			return {};
		const auto generation = capture->generation.load(std::memory_order_relaxed);
		if (OperationProfileSuppressed())
		{
			CapturePin pin{ *capture, generation };
			if (pin.valid)
				capture->suppressedOperations.fetch_add(1, std::memory_order_relaxed);
			return {};
		}

		// Lane acquisition is once per thread; the steady unsampled path has no RMW.
		auto& sampling = ThreadSamplingState();
		if (sampling.counterLane == kCounterLanes)
		{
			CapturePin pin{ *capture, generation };
			if (pin.valid)
				capture->producerCapacityDrops.fetch_add(1, std::memory_order_relaxed);
			return {};
		}
		auto& counter = m_allOperations[
			a_descriptorIndex * kCounterLanes + sampling.counterLane];
		if (counter.generation.load(std::memory_order_relaxed) != generation)
		{
			counter.generation.store(0, std::memory_order_relaxed);
			counter.value.store(1, std::memory_order_relaxed);
			counter.generation.store(generation, std::memory_order_release);
		}
		else
			counter.value.store(counter.value.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
		const auto decision = ShouldSample(
			sampling,
			m_sourceSalt,
			m_descriptorSalts[a_descriptorIndex],
			generation,
			m_descriptors[a_descriptorIndex].samplingPeriod);
		if (!decision.selected)
			return {};
		CapturePin pin{ *capture, generation };
		if (!pin.valid)
			return {};
		const auto shardIndex = static_cast<uint32_t>(
			Mix64(decision.hash ^ 0xD1B54A32D192ED03ull) %
				m_publicationShardCount);
		auto& shard = m_publicationShards[shardIndex];
		if (!shard.mutex.try_lock())
		{
			capture->admissionContentionDrops.fetch_add(
				1, std::memory_order_relaxed);
			return {};
		}
		if (m_currentCapture.load(std::memory_order_relaxed) != capture ||
			capture->generation.load(std::memory_order_relaxed) != generation ||
			!capture->admissionOpen.load(std::memory_order_relaxed))
		{
			shard.mutex.unlock();
			return {};
		}

		capture->activeSampled.fetch_add(1, std::memory_order_relaxed);
		capture->sampledAdmissions.fetch_add(1, std::memory_order_relaxed);
		const auto startQpc = m_clock();
		shard.mutex.unlock();
		return {
			this,
			capture,
			startQpc,
			a_descriptorIndex,
			shardIndex
		};
	}

	bool OperationProfileSource::IsCurrent(const OperationProfileToken& a_token) const noexcept
	{
		if (a_token.source != this || !a_token.captureState)
			return false;
		const auto* capture = static_cast<const CaptureState*>(a_token.captureState);
		return m_currentCapture.load(std::memory_order_acquire) == capture &&
			capture->admissionOpen.load(std::memory_order_acquire);
	}

	void OperationProfileSource::End(
		OperationProfileToken a_token,
		uint64_t a_bytes) noexcept
	{
		const auto descriptor = a_token.descriptorIndex;
		End(std::move(a_token), a_bytes, descriptor);
	}

	void OperationProfileSource::End(
		OperationProfileToken a_token,
		uint64_t a_bytes,
		uint32_t a_resultDescriptor) noexcept
	{
		if (a_token.source != this || !a_token.captureState)
			return;
		auto* capture = static_cast<CaptureState*>(a_token.captureState);
		if (!capture->admissionOpen.load(std::memory_order_acquire))
		{
			capture->staleTokens.fetch_add(1, std::memory_order_relaxed);
			capture->activeSampled.fetch_sub(1, std::memory_order_relaxed);
			return;
		}
		const auto group = m_descriptors[a_token.descriptorIndex].resultGroup;
		if (a_resultDescriptor >= m_descriptors.size() ||
			(a_resultDescriptor != a_token.descriptorIndex &&
				(group.empty() || group != m_descriptors[a_resultDescriptor].resultGroup)))
		{
			capture->invalidResults.fetch_add(1, std::memory_order_relaxed);
			capture->activeSampled.fetch_sub(1, std::memory_order_relaxed);
			return;
		}

		const auto finishQpc = m_clock();
		auto& shard = m_publicationShards[a_token.shardIndex];
		if (!shard.mutex.try_lock())
		{
			capture->publicationContentionDrops.fetch_add(
				1, std::memory_order_relaxed);
			capture->activeSampled.fetch_sub(1, std::memory_order_relaxed);
			return;
		}
		if (m_currentCapture.load(std::memory_order_relaxed) != capture ||
			!capture->admissionOpen.load(std::memory_order_relaxed))
		{
			capture->staleTokens.fetch_add(1, std::memory_order_relaxed);
			shard.mutex.unlock();
			capture->activeSampled.fetch_sub(1, std::memory_order_relaxed);
			return;
		}

		if (shard.count == shard.capacity)
			capture->capacityDrops.fetch_add(1, std::memory_order_relaxed);
		else
		{
			m_records[shard.offset + shard.count++] = {
				finishQpc > a_token.startQpc ?
					finishQpc - a_token.startQpc : 0,
				a_bytes,
				a_resultDescriptor
			};
			capture->acceptedRecords.fetch_add(
				1, std::memory_order_relaxed);
		}
		shard.mutex.unlock();
		capture->activeSampled.fetch_sub(1, std::memory_order_relaxed);
	}

	std::span<const MetricDescriptor> OperationProfileSource::Schema() const noexcept
	{
		return m_schema;
	}

	size_t OperationProfileSource::SeriesCapacity() const noexcept
	{
		return m_intervalBuckets.size();
	}

	std::string_view OperationProfileSource::SourceId() const noexcept
	{
		return m_sourceId;
	}

	std::string_view OperationProfileSource::SourceName() const noexcept
	{
		return m_sourceName;
	}

	std::span<const OperationProfileDescriptor>
	OperationProfileSource::Descriptors() const noexcept
	{
		return m_descriptors;
	}

	std::span<const OperationProfileDurationBucket>
	OperationProfileSource::DurationBuckets() const noexcept
	{
		return m_durationBuckets;
	}

	std::span<const OperationProfileMetadataLabel>
	OperationProfileSource::MetadataLabels() const noexcept
	{
		return m_metadataLabels;
	}

	size_t OperationProfileSource::RecordCapacity() const noexcept
	{
		return m_recordCapacity;
	}

	uint32_t OperationProfileSource::PublicationShardCount() const noexcept
	{
		return m_publicationShardCount;
	}

	uint64_t OperationProfileSource::QpcFrequency() const noexcept
	{
		return m_qpcFrequency;
	}

	uint64_t OperationProfileSource::CaptureGeneration() const noexcept
	{
		const auto capture =
			m_currentCapture.load(std::memory_order_acquire);
		return capture ? capture->generation.load(std::memory_order_relaxed) : 0;
	}

	uint64_t OperationProfileSource::AllOperationCount(
		size_t a_descriptorIndex) const noexcept
	{
		const auto capture =
			m_currentCapture.load(std::memory_order_acquire);
		if (!capture || a_descriptorIndex >= m_descriptors.size())
			return 0;
		const auto generation = capture->generation.load(std::memory_order_relaxed);
		uint64_t count{ 0 };
		for (size_t lane = 0; lane < kCounterLanes; ++lane)
		{
			const auto& counter = m_allOperations[a_descriptorIndex * kCounterLanes + lane];
			if (counter.generation.load(std::memory_order_acquire) != generation)
				continue;
			const auto value = counter.value.load(std::memory_order_relaxed);
			if (counter.generation.load(std::memory_order_acquire) == generation)
				count += value;
		}
		return count;
	}

	OperationProfileCounters OperationProfileSource::Counters() const noexcept
	{
		const auto capture =
			m_currentCapture.load(std::memory_order_acquire);
		if (!capture)
			return {};
		return {
			capture->sampledAdmissions.load(std::memory_order_relaxed),
			capture->acceptedRecords.load(std::memory_order_relaxed),
			capture->admissionContentionDrops.load(std::memory_order_relaxed),
			capture->publicationContentionDrops.load(std::memory_order_relaxed),
			capture->capacityDrops.load(std::memory_order_relaxed),
			capture->staleTokens.load(std::memory_order_relaxed),
			capture->suppressedOperations.load(std::memory_order_relaxed),
			capture->unfinishedOperations.load(std::memory_order_relaxed),
			capture->invalidResults.load(std::memory_order_relaxed),
			capture->producerCapacityDrops.load(std::memory_order_relaxed)
		};
	}

	void OperationProfileSource::Drain(std::span<MetricValue> a_out) noexcept
	{
		ScopedOperationProfileSuppression suppression;
		if (a_out.size() != m_schema.size())
			return;

		for (uint32_t shardIndex = 0;
			shardIndex < m_publicationShardCount;
			++shardIndex)
		{
			auto& shard = m_publicationShards[shardIndex];
			const std::lock_guard lock{ shard.mutex };
			m_drainCounts[shardIndex] = shard.count;
			std::copy_n(
				m_records.begin() + shard.offset,
				shard.count,
				m_drainRecords.begin() + shard.offset);
			shard.count = 0;
		}

		for (uint32_t shardIndex = 0;
			shardIndex < m_publicationShardCount;
			++shardIndex)
		{
			const auto& shard = m_publicationShards[shardIndex];
			for (size_t index = 0;
				index < m_drainCounts[shardIndex];
				++index)
			{
				const auto& record =
					m_drainRecords[shard.offset + index];
				const auto offset =
					static_cast<size_t>(record.descriptorIndex) *
						m_durationBuckets.size() +
					DurationBucketIndex(record.durationTicks);
				auto& bucket = m_intervalBuckets[offset];
				++bucket.calls;
				bucket.ticks += record.durationTicks;
				bucket.bytes += record.bytes;
			}
		}

		const auto counters = Counters();
		const auto valid = CaptureGeneration() != 0;
		const std::array values{
			counters.sampledAdmissions,
			counters.acceptedRecords,
			counters.admissionContentionDrops,
			counters.publicationContentionDrops,
			counters.capacityDrops,
			counters.staleTokens,
			counters.suppressedOperations,
			counters.unfinishedOperations,
			counters.invalidResults,
			counters.producerCapacityDrops
		};
		for (size_t index = 0; index < values.size(); ++index)
			a_out[index] = { static_cast<double>(values[index]), valid };
		for (size_t index = 0; index < m_descriptors.size(); ++index)
		{
			a_out[kQualityMetricSuffixes.size() + index] = {
				static_cast<double>(AllOperationCount(index)),
				valid
			};
		}
	}

	size_t OperationProfileSource::DrainSeries(
		std::span<SeriesSample> a_out) noexcept
	{
		ScopedOperationProfileSuppression suppression;
		if (a_out.size() != m_intervalBuckets.size())
			return 0;
		size_t offset{ 0 };
		for (size_t descriptorIndex = 0;
			descriptorIndex < m_descriptors.size();
			++descriptorIndex)
		{
			for (size_t bucketIndex = 0;
				bucketIndex < m_durationBuckets.size();
				++bucketIndex)
			{
				const auto index =
					descriptorIndex * m_durationBuckets.size() + bucketIndex;
				const auto bucket = std::exchange(
					m_intervalBuckets[index],
					HistogramBucket{});
				a_out[offset++] = {
					m_descriptors[descriptorIndex].series,
					m_durationBuckets[bucketIndex].label,
					bucket.calls,
					bucket.ticks,
					bucket.bytes
				};
			}
		}
		return offset;
	}

	bool OperationProfileSource::StartCapture() noexcept
	{
		if (!m_valid)
			return false;

		for (uint32_t index = 0; index < m_publicationShardCount; ++index)
			m_publicationShards[index].mutex.lock();

		CaptureState* capture{ nullptr };
		for (auto& candidate : m_captureStates)
		{
			if (!candidate.admissionOpen.load(std::memory_order_relaxed) &&
				!candidate.activeBegins.load(std::memory_order_seq_cst) &&
				!candidate.activeSampled.load(std::memory_order_relaxed))
			{
				// Invalidate readers before the second pin check; never reset their pin count.
				candidate.generation.store(
					s_nextCaptureGeneration.fetch_add(1, std::memory_order_relaxed),
					std::memory_order_seq_cst);
				if (candidate.activeBegins.load(std::memory_order_seq_cst))
					continue;
				capture = &candidate;
				break;
			}
		}
		if (!capture)
		{
			for (uint32_t index = m_publicationShardCount; index-- > 0;)
				m_publicationShards[index].mutex.unlock();
			return false;
		}

		ResetCaptureState(*capture);
		for (uint32_t index = 0; index < m_publicationShardCount; ++index)
		{
			m_publicationShards[index].count = 0;
			m_drainCounts[index] = 0;
		}
		std::fill(
			m_intervalBuckets.begin(),
			m_intervalBuckets.end(),
			HistogramBucket{});
		capture->admissionOpen.store(true, std::memory_order_release);
		m_currentCapture.store(capture, std::memory_order_release);

		for (uint32_t index = m_publicationShardCount; index-- > 0;)
			m_publicationShards[index].mutex.unlock();
		return true;
	}

	uint64_t OperationProfileSource::CloseCaptureAdmission() noexcept
	{
		auto* capture =
			m_currentCapture.load(std::memory_order_acquire);
		if (!capture)
			return 0;
		if (!capture->admissionOpen.exchange(
				false, std::memory_order_acq_rel))
			return capture->unfinishedOperations.load(
				std::memory_order_relaxed);

		for (uint32_t index = 0; index < m_publicationShardCount; ++index)
			m_publicationShards[index].mutex.lock();
		for (uint32_t index = m_publicationShardCount; index-- > 0;)
			m_publicationShards[index].mutex.unlock();

		const auto unfinished =
			capture->activeSampled.load(std::memory_order_relaxed);
		capture->unfinishedOperations.store(
			unfinished, std::memory_order_relaxed);
		return unfinished;
	}

	size_t OperationProfileSource::DurationBucketIndex(uint64_t a_ticks) const noexcept
	{
		const auto nanoseconds =
			QpcTicksToNanosecondsSaturated(a_ticks, m_qpcFrequency);
		for (size_t index = 0; index < m_durationBuckets.size(); ++index)
		{
			if (nanoseconds <= m_durationBuckets[index].upperNanoseconds)
				return index;
		}
		return m_durationBuckets.size() - 1;
	}

	std::string_view OperationProfileSource::OwnString(
		std::string_view a_value)
	{
		m_ownedStrings.emplace_back(a_value);
		return m_ownedStrings.back();
	}

	void OperationProfileSource::ResetCaptureState(
		CaptureState& a_state) noexcept
	{
		a_state.activeSampled.store(0, std::memory_order_relaxed);
		a_state.sampledAdmissions.store(0, std::memory_order_relaxed);
		a_state.acceptedRecords.store(0, std::memory_order_relaxed);
		a_state.admissionContentionDrops.store(0, std::memory_order_relaxed);
		a_state.publicationContentionDrops.store(0, std::memory_order_relaxed);
		a_state.capacityDrops.store(0, std::memory_order_relaxed);
		a_state.staleTokens.store(0, std::memory_order_relaxed);
		a_state.suppressedOperations.store(0, std::memory_order_relaxed);
		a_state.unfinishedOperations.store(0, std::memory_order_relaxed);
		a_state.invalidResults.store(0, std::memory_order_relaxed);
		a_state.producerCapacityDrops.store(0, std::memory_order_relaxed);
		a_state.admissionOpen.store(false, std::memory_order_relaxed);
	}
}
