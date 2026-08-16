#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <ostream>
#include <span>
#include <string_view>
#include <type_traits>

namespace Addictol::BA2Profile
{
	using namespace std::literals;

	inline constexpr uint32_t kSchemaVersion{ 3 };

	using BackendId = uint32_t;

	inline constexpr BackendId kBackendNone{ 0 };
	inline constexpr BackendId kBackendStockZlib{ 1 };
	inline constexpr BackendId kBackendLibDeflate{ 2 };
	inline constexpr size_t kKnownBackendCount{ 3 };

	inline constexpr std::array<std::string_view, kKnownBackendCount> kBackendNames{
		"None"sv, "StockZlib"sv, "LibDeflate"sv
	};

	using SiteId = uint16_t;

	inline constexpr SiteId kSiteNone{ 0 };
	inline constexpr SiteId kSiteInflate{ 1 };
	inline constexpr SiteId kSiteTextureChunk{ 2 };
	inline constexpr size_t kKnownSiteCount{ 3 };

	inline constexpr std::array<std::string_view, kKnownSiteCount> kSiteNames{
		"None"sv, "Inflate"sv, "TextureChunk"sv
	};

	using CallerId = uint16_t;

	inline constexpr CallerId kCallerNone{ 0 };
	inline constexpr CallerId kCallerStreamingTexture{ 1 };
	inline constexpr CallerId kCallerArraySlice{ 2 };
	inline constexpr size_t kKnownCallerCount{ 3 };

	inline constexpr std::array<std::string_view, kKnownCallerCount> kCallerNames{
		"None"sv, "StreamingTexture"sv, "ArraySlice"sv
	};

	using FallbackReasonId = uint32_t;

	inline constexpr FallbackReasonId kReasonNone{ 0 };
	inline constexpr FallbackReasonId kReasonState{ 1 };
	inline constexpr FallbackReasonId kReasonAllocation{ 2 };
	inline constexpr FallbackReasonId kReasonDecode{ 3 };
	inline constexpr FallbackReasonId kReasonCommit{ 4 };
	inline constexpr FallbackReasonId kReasonCapacity{ 5 };
	inline constexpr FallbackReasonId kReasonSizeMismatch{ 6 };
	inline constexpr FallbackReasonId kReasonRequestRestart{ 7 };
	inline constexpr size_t kKnownReasonCount{ 8 };

	inline constexpr std::array<std::string_view, kKnownReasonCount> kReasonNames{
		"None"sv, "State"sv, "Allocation"sv, "Decode"sv, "Commit"sv,
		"Capacity"sv, "SizeMismatch"sv, "RequestRestart"sv
	};

	[[nodiscard]] constexpr std::string_view BackendName(BackendId a_backend) noexcept
	{
		return a_backend < kKnownBackendCount ? kBackendNames[a_backend] : "Unknown"sv;
	}

	[[nodiscard]] constexpr std::string_view ReasonName(FallbackReasonId a_reason) noexcept
	{
		return a_reason < kKnownReasonCount ? kReasonNames[a_reason] : "Unknown"sv;
	}

	[[nodiscard]] constexpr std::string_view SiteName(SiteId a_site) noexcept
	{
		return a_site < kKnownSiteCount ? kSiteNames[a_site] : "Unknown"sv;
	}

	[[nodiscard]] constexpr std::string_view CallerName(CallerId a_caller) noexcept
	{
		return a_caller < kKnownCallerCount ? kCallerNames[a_caller] : "Unknown"sv;
	}

	// Malformed bits invalidate the contract; unknown-id bits only mark the row.
	inline constexpr uint16_t kFlagByteRangeOverflow{ 1u << 0 };
	inline constexpr uint16_t kFlagByteAccounting{ 1u << 1 };
	inline constexpr uint16_t kFlagTickAccounting{ 1u << 2 };
	inline constexpr uint16_t kFlagServedMismatch{ 1u << 3 };
	inline constexpr uint16_t kFlagReasonMismatch{ 1u << 4 };
	inline constexpr uint16_t kFlagUnknownReason{ 1u << 5 };
	inline constexpr uint16_t kFlagUnknownBackend{ 1u << 6 };
	inline constexpr uint16_t kFlagQpcFrequency{ 1u << 7 };
	inline constexpr uint16_t kFlagChunkIdentity{ 1u << 8 };
	inline constexpr uint16_t kFlagRequestIdentity{ 1u << 9 };
	inline constexpr uint16_t kFlagPrimaryBytes{ 1u << 10 };
	inline constexpr uint16_t kFlagUnknownSite{ 1u << 11 };
	inline constexpr uint16_t kFlagUnknownCaller{ 1u << 12 };
	inline constexpr uint16_t kMalformedFlags{
		kFlagByteRangeOverflow | kFlagByteAccounting | kFlagTickAccounting |
		kFlagServedMismatch | kFlagReasonMismatch | kFlagQpcFrequency |
		kFlagChunkIdentity | kFlagRequestIdentity | kFlagPrimaryBytes
	};

	// Evidence bits describe what the call observed, never invalidating it.
	inline constexpr uint8_t kEvidenceChunkRow{ 1u << 0 };
	inline constexpr uint8_t kEvidenceRequestLeader{ 1u << 1 };
	inline constexpr uint8_t kEvidenceSizeMeasured{ 1u << 2 };
	inline constexpr uint8_t kEvidenceSizeMismatch{ 1u << 3 };
	inline constexpr uint8_t kEvidenceNominalDescMismatch{ 1u << 4 };
	inline constexpr uint8_t kEvidenceZeroCompressed{ 1u << 5 };
	inline constexpr uint8_t kEvidenceCapacity{ 1u << 6 };
	// Set only when the stock replay actually rewrote this chunk.
	inline constexpr uint8_t kEvidenceReplayed{ 1u << 7 };

	struct CallObservation
	{
		BackendId primaryBackendId{ kBackendNone };
		BackendId fallbackBackendId{ kBackendNone };
		BackendId servedBackendId{ kBackendNone };
		FallbackReasonId fallbackReasonId{ kReasonNone };
		uint64_t primaryQpc{ 0 };
		uint64_t fallbackQpc{ 0 };
		uint64_t totalQpc{ 0 };
		uint64_t qpcFrequency{ 0 };
		uint64_t requestWallQpc{ 0 };
		uint64_t requestSequence{ 0 };
		uint64_t streamAddress{ 0 };
		uint64_t inputBytesAvailable{ 0 };
		uint64_t outputBytesAvailable{ 0 };
		uint64_t inputBytesConsumed{ 0 };
		uint64_t outputBytesProduced{ 0 };
		uint32_t primaryInputBytesConsumed{ 0 };
		uint32_t primaryOutputBytesProduced{ 0 };
		uint32_t nominalOutputBytes{ 0 };
		uint32_t threadId{ 0 };
		int32_t zlibResult{ 0 };
		SiteId observationSiteId{ kSiteNone };
		CallerId callerId{ kCallerNone };
		uint16_t chunkIndex{ 0 };
		uint16_t chunkCount{ 0 };
		uint8_t evidenceFlags{ 0 };
		bool primaryAttempted{ false };
	};

	struct ObservationCheck
	{
		uint16_t flags{ 0 };

		[[nodiscard]] constexpr bool WellFormed() const noexcept { return (flags & kMalformedFlags) == 0; }
	};

	// Positive means the archive claimed more bytes than the member decoded to.
	[[nodiscard]] constexpr int64_t ObservedSizeDelta(
		uint32_t a_nominalOutputBytes,
		uint32_t a_primaryOutputBytesProduced) noexcept
	{
		return static_cast<int64_t>(a_nominalOutputBytes) -
			static_cast<int64_t>(a_primaryOutputBytesProduced);
	}

	[[nodiscard]] constexpr ObservationCheck ValidateObservation(
		const CallObservation& a_observation,
		uint64_t a_expectedQpcFrequency = 0) noexcept
	{
		constexpr uint64_t byteLimit{ 0xFFFFFFFFull };
		uint16_t flags{ 0 };

		if (a_observation.inputBytesAvailable > byteLimit ||
			a_observation.outputBytesAvailable > byteLimit ||
			a_observation.inputBytesConsumed > byteLimit ||
			a_observation.outputBytesProduced > byteLimit)
			flags |= kFlagByteRangeOverflow;

		if (a_observation.inputBytesConsumed > a_observation.inputBytesAvailable ||
			a_observation.outputBytesProduced > a_observation.outputBytesAvailable)
			flags |= kFlagByteAccounting;

		const auto tickSumOverflows =
			a_observation.primaryQpc >
			std::numeric_limits<uint64_t>::max() - a_observation.fallbackQpc;
		if (tickSumOverflows ||
			a_observation.totalQpc < a_observation.primaryQpc + a_observation.fallbackQpc)
			flags |= kFlagTickAccounting;

		if (!a_observation.primaryAttempted && a_observation.primaryQpc)
			flags |= kFlagTickAccounting;

		if (a_observation.requestWallQpc &&
			a_observation.requestWallQpc < a_observation.totalQpc)
			flags |= kFlagTickAccounting;

		if (!a_observation.primaryAttempted &&
			(a_observation.primaryInputBytesConsumed || a_observation.primaryOutputBytesProduced))
			flags |= kFlagPrimaryBytes;

		if (a_observation.primaryInputBytesConsumed > a_observation.inputBytesAvailable ||
			a_observation.primaryOutputBytesProduced > a_observation.outputBytesAvailable)
			flags |= kFlagPrimaryBytes;

		const auto chunkRow = (a_observation.evidenceFlags & kEvidenceChunkRow) != 0;
		if (chunkRow)
		{
			if (!a_observation.chunkCount ||
				a_observation.chunkIndex >= a_observation.chunkCount ||
				!a_observation.requestSequence ||
				!a_observation.streamAddress)
				flags |= kFlagChunkIdentity;
		}
		else if (a_observation.chunkCount || a_observation.chunkIndex)
		{
			flags |= kFlagChunkIdentity;
		}

		const auto leader = (a_observation.evidenceFlags & kEvidenceRequestLeader) != 0;
		if ((leader && !chunkRow) || (a_observation.requestWallQpc && !leader))
			flags |= kFlagRequestIdentity;

		const auto servedByPrimary = a_observation.primaryAttempted &&
			a_observation.servedBackendId == a_observation.primaryBackendId;
		const auto servedByFallback = a_observation.fallbackBackendId != kBackendNone &&
			a_observation.servedBackendId == a_observation.fallbackBackendId;
		if (a_observation.servedBackendId != kBackendNone && !servedByPrimary && !servedByFallback)
			flags |= kFlagServedMismatch;

		if ((a_observation.fallbackBackendId != kBackendNone) !=
			(a_observation.fallbackReasonId != kReasonNone))
			flags |= kFlagReasonMismatch;

		if (a_observation.fallbackReasonId >= kKnownReasonCount)
			flags |= kFlagUnknownReason;

		if (a_observation.primaryBackendId >= kKnownBackendCount ||
			a_observation.fallbackBackendId >= kKnownBackendCount ||
			a_observation.servedBackendId >= kKnownBackendCount)
			flags |= kFlagUnknownBackend;

		if (a_observation.observationSiteId >= kKnownSiteCount)
			flags |= kFlagUnknownSite;

		if (a_observation.callerId >= kKnownCallerCount)
			flags |= kFlagUnknownCaller;

		if (!a_observation.qpcFrequency ||
			(a_expectedQpcFrequency && a_observation.qpcFrequency != a_expectedQpcFrequency))
			flags |= kFlagQpcFrequency;

		return { flags };
	}

	struct CallRecord
	{
		uint64_t shardSequence;
		uint64_t monotonicUs;
		uint64_t primaryQpc;
		uint64_t fallbackQpc;
		uint64_t totalQpc;
		uint64_t requestWallQpc;
		uint64_t requestSequence;
		uint64_t streamAddress;
		uint32_t saveLoadEpoch;
		BackendId primaryBackendId;
		BackendId fallbackBackendId;
		FallbackReasonId fallbackReasonId;
		BackendId servedBackendId;
		uint32_t inputBytesAvailable;
		uint32_t outputBytesAvailable;
		uint32_t inputBytesConsumed;
		uint32_t outputBytesProduced;
		uint32_t primaryInputBytesConsumed;
		uint32_t primaryOutputBytesProduced;
		uint32_t nominalOutputBytes;
		uint32_t threadId;
		int32_t zlibResult;
		uint16_t shardIndex;
		SiteId observationSiteId;
		CallerId callerId;
		uint16_t chunkIndex;
		uint16_t chunkCount;
		uint16_t observationFlags;
		uint8_t primaryAttempted;
		uint8_t evidenceFlags;
	};

	static_assert(sizeof(CallRecord) == 136);
	static_assert(alignof(CallRecord) == 8);
	static_assert(std::is_standard_layout_v<CallRecord>);
	static_assert(std::is_trivially_copyable_v<CallRecord>);

	[[nodiscard]] constexpr uint32_t SaturateBytes(uint64_t a_value) noexcept
	{
		constexpr uint64_t byteLimit{ 0xFFFFFFFFull };
		return static_cast<uint32_t>(a_value > byteLimit ? byteLimit : a_value);
	}

	[[nodiscard]] constexpr CallRecord MakeCallRecord(
		const CallObservation& a_observation,
		const ObservationCheck& a_check,
		uint16_t a_shardIndex,
		uint64_t a_shardSequence,
		uint32_t a_saveLoadEpoch,
		uint64_t a_monotonicUs) noexcept
	{
		return {
			a_shardSequence,
			a_monotonicUs,
			a_observation.primaryQpc,
			a_observation.fallbackQpc,
			a_observation.totalQpc,
			a_observation.requestWallQpc,
			a_observation.requestSequence,
			a_observation.streamAddress,
			a_saveLoadEpoch,
			a_observation.primaryBackendId,
			a_observation.fallbackBackendId,
			a_observation.fallbackReasonId,
			a_observation.servedBackendId,
			SaturateBytes(a_observation.inputBytesAvailable),
			SaturateBytes(a_observation.outputBytesAvailable),
			SaturateBytes(a_observation.inputBytesConsumed),
			SaturateBytes(a_observation.outputBytesProduced),
			a_observation.primaryInputBytesConsumed,
			a_observation.primaryOutputBytesProduced,
			a_observation.nominalOutputBytes,
			a_observation.threadId,
			a_observation.zlibResult,
			a_shardIndex,
			a_observation.observationSiteId,
			a_observation.callerId,
			a_observation.chunkIndex,
			a_observation.chunkCount,
			a_check.flags,
			static_cast<uint8_t>(a_observation.primaryAttempted ? 1 : 0),
			a_observation.evidenceFlags
		};
	}

	inline constexpr size_t kBackendTableCapacity{ 8 };
	inline constexpr size_t kOutputSizeBucketCount{ 8 };
	inline constexpr std::array<std::string_view, kOutputSizeBucketCount> kOutputSizeBucketNames{
		"0"sv,
		"1-255"sv,
		"256-1023"sv,
		"1024-4095"sv,
		"4096-16383"sv,
		"16384-65535"sv,
		"65536-262143"sv,
		"262144+"sv
	};

	[[nodiscard]] constexpr size_t OutputSizeBucket(uint64_t a_bytes) noexcept
	{
		if (!a_bytes)
			return 0;
		if (a_bytes <= 255)
			return 1;
		if (a_bytes <= 1023)
			return 2;
		if (a_bytes <= 4095)
			return 3;
		if (a_bytes <= 16383)
			return 4;
		if (a_bytes <= 65535)
			return 5;
		if (a_bytes <= 262143)
			return 6;
		return 7;
	}

	struct BackendAggregate
	{
		BackendId id{ kBackendNone };
		uint64_t selectedCalls{ 0 };
		uint64_t primaryCalls{ 0 };
		uint64_t primaryQpc{ 0 };
		uint64_t fallbackCalls{ 0 };
		uint64_t fallbackQpc{ 0 };
		uint64_t servedCalls{ 0 };
		uint64_t servedQpc{ 0 };
		uint64_t inputBytesConsumed{ 0 };
		uint64_t outputBytesProduced{ 0 };
		std::array<uint64_t, kOutputSizeBucketCount> servedBucketCalls{};
		std::array<uint64_t, kOutputSizeBucketCount> servedBucketQpc{};
		std::array<uint64_t, kOutputSizeBucketCount> servedBucketBytes{};

		constexpr void Merge(const BackendAggregate& a_other) noexcept
		{
			selectedCalls += a_other.selectedCalls;
			primaryCalls += a_other.primaryCalls;
			primaryQpc += a_other.primaryQpc;
			fallbackCalls += a_other.fallbackCalls;
			fallbackQpc += a_other.fallbackQpc;
			servedCalls += a_other.servedCalls;
			servedQpc += a_other.servedQpc;
			inputBytesConsumed += a_other.inputBytesConsumed;
			outputBytesProduced += a_other.outputBytesProduced;
			for (size_t index = 0; index < kOutputSizeBucketCount; ++index)
			{
				servedBucketCalls[index] += a_other.servedBucketCalls[index];
				servedBucketQpc[index] += a_other.servedBucketQpc[index];
				servedBucketBytes[index] += a_other.servedBucketBytes[index];
			}
		}
	};

	struct BackendTable
	{
		std::array<BackendAggregate, kBackendTableCapacity> entries{};

		[[nodiscard]] constexpr const BackendAggregate* Find(BackendId a_backend) const noexcept
		{
			if (a_backend == kBackendNone)
				return nullptr;
			for (const auto& entry : entries)
			{
				if (entry.id == a_backend)
					return &entry;
			}
			return nullptr;
		}

		[[nodiscard]] constexpr BackendAggregate* Get(BackendId a_backend) noexcept
		{
			if (a_backend == kBackendNone)
				return nullptr;
			const auto start = static_cast<size_t>(a_backend) % kBackendTableCapacity;
			for (size_t probe = 0; probe < kBackendTableCapacity; ++probe)
			{
				auto& entry = entries[(start + probe) % kBackendTableCapacity];
				if (entry.id == kBackendNone)
				{
					entry.id = a_backend;
					return &entry;
				}
				if (entry.id == a_backend)
					return &entry;
			}
			return nullptr;
		}

		constexpr void Reset() noexcept
		{
			entries = {};
		}

		[[nodiscard]] constexpr uint64_t ServedTotal() const noexcept
		{
			uint64_t total{ 0 };
			for (const auto& entry : entries)
				total += entry.servedCalls;
			return total;
		}
	};

	// Fed identically from admitted observations and from persisted rows.
	struct RequestEvidence
	{
		std::array<uint64_t, kKnownSiteCount> siteCounts{};
		std::array<uint64_t, kKnownCallerCount> callerCounts{};
		uint64_t unknownSiteCalls{ 0 };
		uint64_t unknownCallerCalls{ 0 };
		uint64_t chunkRows{ 0 };
		uint64_t leaderRows{ 0 };
		uint64_t requestWallQpc{ 0 };
		uint64_t requestSequenceTotal{ 0 };
		uint64_t streamAddressTotal{ 0 };
		uint64_t threadIdTotal{ 0 };
		uint64_t chunkIndexTotal{ 0 };
		uint64_t chunkCountTotal{ 0 };
		uint64_t nominalOutputBytes{ 0 };
		uint64_t primaryInputBytesConsumed{ 0 };
		uint64_t primaryOutputBytesProduced{ 0 };
		uint64_t sizeDeltaSamples{ 0 };
		uint64_t sizeMismatchChunks{ 0 };
		uint64_t nominalDescMismatches{ 0 };
		uint64_t capacityFailures{ 0 };
		int64_t minSizeDelta{ 0 };
		int64_t maxSizeDelta{ 0 };

		constexpr void Account(
			SiteId a_site,
			CallerId a_caller,
			uint8_t a_evidenceFlags,
			uint16_t a_chunkIndex,
			uint16_t a_chunkCount,
			uint64_t a_requestSequence,
			uint64_t a_streamAddress,
			uint32_t a_threadId,
			uint64_t a_requestWallQpc,
			uint32_t a_primaryInputBytesConsumed,
			uint32_t a_primaryOutputBytesProduced,
			uint32_t a_nominalOutputBytes) noexcept
		{
			if (a_site < kKnownSiteCount)
				++siteCounts[a_site];
			else
				++unknownSiteCalls;

			if (a_caller < kKnownCallerCount)
				++callerCounts[a_caller];
			else
				++unknownCallerCalls;

			requestSequenceTotal += a_requestSequence;
			streamAddressTotal += a_streamAddress;
			threadIdTotal += a_threadId;
			requestWallQpc += a_requestWallQpc;
			chunkIndexTotal += a_chunkIndex;
			chunkCountTotal += a_chunkCount;
			nominalOutputBytes += a_nominalOutputBytes;
			primaryInputBytesConsumed += a_primaryInputBytesConsumed;
			primaryOutputBytesProduced += a_primaryOutputBytesProduced;

			if (a_evidenceFlags & kEvidenceChunkRow)
				++chunkRows;
			if (a_evidenceFlags & kEvidenceRequestLeader)
				++leaderRows;
			if (a_evidenceFlags & kEvidenceNominalDescMismatch)
				++nominalDescMismatches;
			if (a_evidenceFlags & kEvidenceCapacity)
				++capacityFailures;

			// Derived from the persisted sizes, not the row's own flag.
			if (a_evidenceFlags & kEvidenceSizeMeasured)
			{
				++sizeDeltaSamples;
				const auto delta = ObservedSizeDelta(
					a_nominalOutputBytes, a_primaryOutputBytesProduced);
				if (delta)
				{
					if (!sizeMismatchChunks || delta < minSizeDelta)
						minSizeDelta = delta;
					if (!sizeMismatchChunks || delta > maxSizeDelta)
						maxSizeDelta = delta;
					++sizeMismatchChunks;
				}
			}
		}

		constexpr void Account(const CallObservation& a_observation) noexcept
		{
			Account(
				a_observation.observationSiteId,
				a_observation.callerId,
				a_observation.evidenceFlags,
				a_observation.chunkIndex,
				a_observation.chunkCount,
				a_observation.requestSequence,
				a_observation.streamAddress,
				a_observation.threadId,
				a_observation.requestWallQpc,
				a_observation.primaryInputBytesConsumed,
				a_observation.primaryOutputBytesProduced,
				a_observation.nominalOutputBytes);
		}

		constexpr void Account(const CallRecord& a_record) noexcept
		{
			Account(
				a_record.observationSiteId,
				a_record.callerId,
				a_record.evidenceFlags,
				a_record.chunkIndex,
				a_record.chunkCount,
				a_record.requestSequence,
				a_record.streamAddress,
				a_record.threadId,
				a_record.requestWallQpc,
				a_record.primaryInputBytesConsumed,
				a_record.primaryOutputBytesProduced,
				a_record.nominalOutputBytes);
		}

		constexpr void Merge(const RequestEvidence& a_other) noexcept
		{
			for (size_t index = 0; index < kKnownSiteCount; ++index)
				siteCounts[index] += a_other.siteCounts[index];
			for (size_t index = 0; index < kKnownCallerCount; ++index)
				callerCounts[index] += a_other.callerCounts[index];
			unknownSiteCalls += a_other.unknownSiteCalls;
			unknownCallerCalls += a_other.unknownCallerCalls;
			chunkRows += a_other.chunkRows;
			leaderRows += a_other.leaderRows;
			requestWallQpc += a_other.requestWallQpc;
			requestSequenceTotal += a_other.requestSequenceTotal;
			streamAddressTotal += a_other.streamAddressTotal;
			threadIdTotal += a_other.threadIdTotal;
			chunkIndexTotal += a_other.chunkIndexTotal;
			chunkCountTotal += a_other.chunkCountTotal;
			nominalOutputBytes += a_other.nominalOutputBytes;
			primaryInputBytesConsumed += a_other.primaryInputBytesConsumed;
			primaryOutputBytesProduced += a_other.primaryOutputBytesProduced;
			nominalDescMismatches += a_other.nominalDescMismatches;
			capacityFailures += a_other.capacityFailures;
			sizeDeltaSamples += a_other.sizeDeltaSamples;
			const auto hadMismatches = sizeMismatchChunks != 0;
			sizeMismatchChunks += a_other.sizeMismatchChunks;
			if (a_other.sizeMismatchChunks)
			{
				if (!hadMismatches || a_other.minSizeDelta < minSizeDelta)
					minSizeDelta = a_other.minSizeDelta;
				if (!hadMismatches || a_other.maxSizeDelta > maxSizeDelta)
					maxSizeDelta = a_other.maxSizeDelta;
			}
		}

		[[nodiscard]] constexpr uint64_t SiteTotal() const noexcept
		{
			uint64_t total{ unknownSiteCalls };
			for (const auto count : siteCounts)
				total += count;
			return total;
		}

		[[nodiscard]] constexpr uint64_t CallerTotal() const noexcept
		{
			uint64_t total{ unknownCallerCalls };
			for (const auto count : callerCounts)
				total += count;
			return total;
		}

		[[nodiscard]] constexpr bool operator==(const RequestEvidence&) const noexcept = default;
	};

	struct ShardAggregate
	{
		uint64_t callsSeen{ 0 };
		uint64_t rowsWritten{ 0 };
		uint64_t rowsDropped{ 0 };
		uint64_t rowsDisabled{ 0 };
		uint64_t totalQpc{ 0 };
		uint64_t rowTotalQpc{ 0 };
		uint64_t inputBytesConsumed{ 0 };
		uint64_t outputBytesProduced{ 0 };
		std::array<uint64_t, kKnownReasonCount> reasonCounts{};
		std::array<uint64_t, kKnownReasonCount> reasonPrimaryQpc{};
		std::array<uint64_t, kKnownReasonCount> reasonFallbackQpc{};
		std::array<uint64_t, kKnownReasonCount> reasonTotalQpc{};
		uint64_t unknownReasonCalls{ 0 };
		uint64_t unknownReasonPrimaryQpc{ 0 };
		uint64_t unknownReasonFallbackQpc{ 0 };
		uint64_t unknownReasonTotalQpc{ 0 };
		uint64_t unservedCalls{ 0 };
		uint64_t malformedObservations{ 0 };
		uint64_t overflowedThreads{ 0 };
		uint64_t backendTableOverflowCalls{ 0 };
		uint64_t servedBackendOverflowCalls{ 0 };
		uint64_t oversizedBatches{ 0 };
		FallbackReasonId firstUnknownReasonId{ 0 };
		BackendTable backends{};
		RequestEvidence requests{};

		constexpr void Account(
			const CallObservation& a_observation,
			const ObservationCheck& a_check,
			bool a_rowsEnabled,
			bool a_rowWritten) noexcept
		{
			++callsSeen;
			totalQpc += a_observation.totalQpc;
			inputBytesConsumed += a_observation.inputBytesConsumed;
			outputBytesProduced += a_observation.outputBytesProduced;
			requests.Account(a_observation);
			if (!a_rowsEnabled)
			{
				++rowsDisabled;
			}
			else if (a_rowWritten)
			{
				++rowsWritten;
				rowTotalQpc += a_observation.totalQpc;
			}
			else
			{
				++rowsDropped;
			}

			if (a_observation.fallbackReasonId < kKnownReasonCount)
			{
				const auto reason = a_observation.fallbackReasonId;
				++reasonCounts[reason];
				reasonPrimaryQpc[reason] += a_observation.primaryQpc;
				reasonFallbackQpc[reason] += a_observation.fallbackQpc;
				reasonTotalQpc[reason] += a_observation.totalQpc;
			}
			else
			{
				if (!unknownReasonCalls)
					firstUnknownReasonId = a_observation.fallbackReasonId;
				++unknownReasonCalls;
				unknownReasonPrimaryQpc += a_observation.primaryQpc;
				unknownReasonFallbackQpc += a_observation.fallbackQpc;
				unknownReasonTotalQpc += a_observation.totalQpc;
			}

			bool backendOverflow{ false };
			if (a_observation.primaryBackendId != kBackendNone)
			{
				if (auto* backend = backends.Get(a_observation.primaryBackendId))
				{
					++backend->selectedCalls;
					if (a_observation.primaryAttempted)
					{
						++backend->primaryCalls;
						backend->primaryQpc += a_observation.primaryQpc;
					}
				}
				else
				{
					backendOverflow = true;
				}
			}

			if (a_observation.fallbackBackendId != kBackendNone)
			{
				if (auto* backend = backends.Get(a_observation.fallbackBackendId))
				{
					++backend->fallbackCalls;
					backend->fallbackQpc += a_observation.fallbackQpc;
				}
				else
				{
					backendOverflow = true;
				}
			}

			if (a_observation.servedBackendId == kBackendNone)
			{
				++unservedCalls;
			}
			else
			{
				if (auto* backend = backends.Get(a_observation.servedBackendId))
				{
					++backend->servedCalls;
					backend->servedQpc +=
						a_observation.fallbackBackendId == a_observation.servedBackendId ?
						a_observation.fallbackQpc :
						a_observation.primaryQpc;
					backend->inputBytesConsumed += a_observation.inputBytesConsumed;
					backend->outputBytesProduced += a_observation.outputBytesProduced;
					const auto bucket = OutputSizeBucket(a_observation.outputBytesProduced);
					++backend->servedBucketCalls[bucket];
					backend->servedBucketQpc[bucket] +=
						a_observation.fallbackBackendId == a_observation.servedBackendId ?
						a_observation.fallbackQpc :
						a_observation.primaryQpc;
					backend->servedBucketBytes[bucket] += a_observation.outputBytesProduced;
				}
				else
				{
					backendOverflow = true;
					++servedBackendOverflowCalls;
				}
			}

			if (backendOverflow)
				++backendTableOverflowCalls;

			if (!a_check.WellFormed())
				++malformedObservations;
		}

		constexpr void Merge(const ShardAggregate& a_other) noexcept
		{
			callsSeen += a_other.callsSeen;
			rowsWritten += a_other.rowsWritten;
			rowsDropped += a_other.rowsDropped;
			rowsDisabled += a_other.rowsDisabled;
			totalQpc += a_other.totalQpc;
			rowTotalQpc += a_other.rowTotalQpc;
			inputBytesConsumed += a_other.inputBytesConsumed;
			outputBytesProduced += a_other.outputBytesProduced;
			for (size_t index = 0; index < kKnownReasonCount; ++index)
			{
				reasonCounts[index] += a_other.reasonCounts[index];
				reasonPrimaryQpc[index] += a_other.reasonPrimaryQpc[index];
				reasonFallbackQpc[index] += a_other.reasonFallbackQpc[index];
				reasonTotalQpc[index] += a_other.reasonTotalQpc[index];
			}
			if (!unknownReasonCalls && a_other.unknownReasonCalls)
				firstUnknownReasonId = a_other.firstUnknownReasonId;
			unknownReasonCalls += a_other.unknownReasonCalls;
			unknownReasonPrimaryQpc += a_other.unknownReasonPrimaryQpc;
			unknownReasonFallbackQpc += a_other.unknownReasonFallbackQpc;
			unknownReasonTotalQpc += a_other.unknownReasonTotalQpc;
			unservedCalls += a_other.unservedCalls;
			malformedObservations += a_other.malformedObservations;
			overflowedThreads += a_other.overflowedThreads;
			backendTableOverflowCalls += a_other.backendTableOverflowCalls;
			servedBackendOverflowCalls += a_other.servedBackendOverflowCalls;
			oversizedBatches += a_other.oversizedBatches;
			requests.Merge(a_other.requests);
			for (const auto& otherBackend : a_other.backends.entries)
			{
				if (otherBackend.id == kBackendNone)
					continue;
				if (auto* backend = backends.Get(otherBackend.id))
				{
					backend->Merge(otherBackend);
				}
				else
				{
					backendTableOverflowCalls += std::max({
						otherBackend.selectedCalls,
						otherBackend.fallbackCalls,
						otherBackend.servedCalls
					});
					servedBackendOverflowCalls += otherBackend.servedCalls;
				}
			}
		}

		constexpr void Reset() noexcept { *this = ShardAggregate{}; }
	};

	[[nodiscard]] constexpr ShardAggregate MergeShards(std::span<const ShardAggregate> a_shards) noexcept
	{
		ShardAggregate merged{};
		for (const auto& shard : a_shards)
			merged.Merge(shard);
		return merged;
	}

	struct Reconciliation
	{
		bool reasonPartitionOk{ false };
		bool backendPartitionOk{ false };
		bool sitePartitionOk{ false };
		bool callerPartitionOk{ false };
		bool rowPartitionOk{ false };
		bool tickIdentityOk{ false };
		bool requestEvidenceOk{ false };
		bool rowEvidenceOk{ false };
		bool contractOk{ false };
		bool rowsTruncated{ false };

		[[nodiscard]] constexpr bool Ok() const noexcept
		{
			return reasonPartitionOk && backendPartitionOk && sitePartitionOk &&
				callerPartitionOk && rowPartitionOk && tickIdentityOk &&
				requestEvidenceOk && rowEvidenceOk && contractOk;
		}
	};

	struct RowEvidence
	{
		uint64_t rowsSeen{ 0 };
		uint64_t totalQpc{ 0 };
		std::array<uint64_t, kKnownReasonCount> reasonCounts{};
		uint64_t unknownReasonCalls{ 0 };
		uint64_t unservedCalls{ 0 };
		uint64_t malformedRows{ 0 };
		uint64_t shardMismatchRows{ 0 };
		BackendTable servedBackends{};
		RequestEvidence requests{};

		constexpr void Account(
			const CallRecord& a_record,
			uint16_t a_expectedShardIndex) noexcept
		{
			++rowsSeen;
			totalQpc += a_record.totalQpc;
			requests.Account(a_record);
			if (a_record.shardIndex != a_expectedShardIndex)
				++shardMismatchRows;
			if (a_record.fallbackReasonId < kKnownReasonCount)
				++reasonCounts[a_record.fallbackReasonId];
			else
				++unknownReasonCalls;

			if (a_record.servedBackendId == kBackendNone)
			{
				++unservedCalls;
			}
			else if (auto* backend = servedBackends.Get(a_record.servedBackendId))
			{
				++backend->servedCalls;
			}

			if (a_record.observationFlags & kMalformedFlags)
				++malformedRows;
		}

		constexpr void Merge(const RowEvidence& a_other) noexcept
		{
			rowsSeen += a_other.rowsSeen;
			totalQpc += a_other.totalQpc;
			requests.Merge(a_other.requests);
			for (size_t index = 0; index < kKnownReasonCount; ++index)
				reasonCounts[index] += a_other.reasonCounts[index];
			unknownReasonCalls += a_other.unknownReasonCalls;
			unservedCalls += a_other.unservedCalls;
			malformedRows += a_other.malformedRows;
			shardMismatchRows += a_other.shardMismatchRows;
			for (const auto& otherBackend : a_other.servedBackends.entries)
			{
				if (otherBackend.id == kBackendNone)
					continue;
				if (auto* backend = servedBackends.Get(otherBackend.id))
					backend->servedCalls += otherBackend.servedCalls;
			}
		}
	};

	[[nodiscard]] constexpr bool MatchesRetainedRows(
		const ShardAggregate& a_totals,
		const RowEvidence& a_rows) noexcept
	{
		if (a_totals.rowsWritten != a_rows.rowsSeen ||
			a_totals.rowTotalQpc != a_rows.totalQpc ||
			a_rows.shardMismatchRows)
			return false;

		if (a_totals.rowsDropped || a_totals.rowsDisabled)
			return true;

		if (a_totals.reasonCounts != a_rows.reasonCounts ||
			a_totals.unknownReasonCalls != a_rows.unknownReasonCalls ||
			a_totals.unservedCalls != a_rows.unservedCalls ||
			a_totals.malformedObservations != a_rows.malformedRows ||
			!(a_totals.requests == a_rows.requests))
			return false;

		for (const auto& backend : a_totals.backends.entries)
		{
			if (backend.id == kBackendNone)
				continue;
			const auto* rowBackend = a_rows.servedBackends.Find(backend.id);
			if (!rowBackend || rowBackend->servedCalls != backend.servedCalls)
				return false;
		}
		return a_rows.servedBackends.ServedTotal() == a_totals.backends.ServedTotal();
	}

	[[nodiscard]] constexpr Reconciliation Reconcile(
		const ShardAggregate& a_totals,
		const RowEvidence* a_rows = nullptr) noexcept
	{
		uint64_t reasonSum{ a_totals.unknownReasonCalls };
		for (const auto count : a_totals.reasonCounts)
			reasonSum += count;

		Reconciliation result;
		result.reasonPartitionOk = a_totals.callsSeen == reasonSum;
		result.backendPartitionOk =
			a_totals.callsSeen ==
			a_totals.backends.ServedTotal() +
				a_totals.servedBackendOverflowCalls +
				a_totals.unservedCalls;
		result.sitePartitionOk = a_totals.callsSeen == a_totals.requests.SiteTotal();
		result.callerPartitionOk = a_totals.callsSeen == a_totals.requests.CallerTotal();
		result.rowPartitionOk =
			a_totals.callsSeen ==
			a_totals.rowsWritten + a_totals.rowsDropped + a_totals.rowsDisabled;
		result.tickIdentityOk =
			a_totals.rowsDropped != 0 ||
			a_totals.rowsDisabled != 0 ||
			a_totals.totalQpc == a_totals.rowTotalQpc;
		result.requestEvidenceOk =
			a_totals.requests.leaderRows <= a_totals.requests.chunkRows &&
			(a_totals.requests.chunkRows != 0 || a_totals.requests.leaderRows == 0) &&
			a_totals.requests.chunkRows <= a_totals.callsSeen &&
			a_totals.requests.chunkRows == a_totals.requests.siteCounts[kSiteTextureChunk] &&
			a_totals.requests.sizeMismatchChunks <= a_totals.requests.sizeDeltaSamples &&
			a_totals.requests.sizeDeltaSamples <= a_totals.requests.chunkRows &&
			a_totals.requests.capacityFailures <= a_totals.requests.chunkRows &&
			a_totals.requests.nominalDescMismatches <= a_totals.requests.chunkRows;
		result.rowEvidenceOk =
			a_rows ?
			MatchesRetainedRows(a_totals, *a_rows) :
			true;
		result.contractOk = a_totals.malformedObservations == 0 &&
			a_totals.backendTableOverflowCalls == 0 &&
			a_totals.unknownReasonCalls == 0 &&
			a_totals.oversizedBatches == 0;
		result.rowsTruncated = a_totals.rowsDropped != 0;
		return result;
	}

	inline constexpr size_t kCallsColumnCount{ 37 };
	inline constexpr std::string_view kCallsColumns{
		"SchemaVersion,SessionId,QpcFrequency,PublishSequence,PublishReason,"
		"SaveLoadEpoch,MonotonicUs,ShardIndex,ShardSequence,"
		"ObservationSiteId,CallerId,ThreadId,RequestSequence,StreamAddress,ChunkIndex,ChunkCount,"
		"PrimaryBackendId,PrimaryAttempted,PrimaryQpc,FallbackBackendId,FallbackReasonId,FallbackQpc,"
		"ServedBackendId,ZlibResult,TotalQpc,RequestWallQpc,InputBytesAvailable,OutputBytesAvailable,"
		"InputBytesConsumed,OutputBytesProduced,PrimaryInputBytesConsumed,PrimaryOutputBytesProduced,"
		"NominalOutputBytes,ObservationFlags,EvidenceFlags,ShutdownPublishEnabled,AdmissionClosed"sv
	};

	struct FileContext
	{
		std::string_view sessionID;
		uint64_t qpcFrequency{ 0 };
		uint64_t publishSequence{ 0 };
		std::string_view publishReason;
		bool shutdownPublishEnabled{ false };
		bool admissionClosed{ false };
	};

	inline void WriteCallsHeader(std::ostream& a_file)
	{
		a_file << "# ba2 calls v"sv << kSchemaVersion
			<< "; durations are raw QPC ticks; a row is identified by ShardIndex+ShardSequence\n"sv
			<< "# a texture request is ThreadId+RequestSequence; RequestWallQpc is carried by its leader chunk row only\n"sv
			<< kCallsColumns << "\n"sv;
	}

	inline void WriteCallRow(
		std::ostream& a_file,
		const FileContext& a_context,
		const CallRecord& a_record)
	{
		a_file << kSchemaVersion << ","sv
			<< a_context.sessionID << ","sv
			<< a_context.qpcFrequency << ","sv
			<< a_context.publishSequence << ","sv
			<< a_context.publishReason << ","sv
			<< a_record.saveLoadEpoch << ","sv
			<< a_record.monotonicUs << ","sv
			<< a_record.shardIndex << ","sv
			<< a_record.shardSequence << ","sv
			<< a_record.observationSiteId << ","sv
			<< a_record.callerId << ","sv
			<< a_record.threadId << ","sv
			<< a_record.requestSequence << ","sv
			<< a_record.streamAddress << ","sv
			<< a_record.chunkIndex << ","sv
			<< a_record.chunkCount << ","sv
			<< a_record.primaryBackendId << ","sv
			<< static_cast<unsigned>(a_record.primaryAttempted) << ","sv
			<< a_record.primaryQpc << ","sv
			<< a_record.fallbackBackendId << ","sv
			<< a_record.fallbackReasonId << ","sv
			<< a_record.fallbackQpc << ","sv
			<< a_record.servedBackendId << ","sv
			<< a_record.zlibResult << ","sv
			<< a_record.totalQpc << ","sv
			<< a_record.requestWallQpc << ","sv
			<< a_record.inputBytesAvailable << ","sv
			<< a_record.outputBytesAvailable << ","sv
			<< a_record.inputBytesConsumed << ","sv
			<< a_record.outputBytesProduced << ","sv
			<< a_record.primaryInputBytesConsumed << ","sv
			<< a_record.primaryOutputBytesProduced << ","sv
			<< a_record.nominalOutputBytes << ","sv
			<< a_record.observationFlags << ","sv
			<< static_cast<unsigned>(a_record.evidenceFlags) << ","sv
			<< static_cast<unsigned>(a_context.shutdownPublishEnabled) << ","sv
			<< static_cast<unsigned>(a_context.admissionClosed) << "\n"sv;
	}

	inline constexpr size_t kSummaryColumnCount{ 61 };
	inline constexpr std::string_view kSummaryColumns{
		"SchemaVersion,SessionId,QpcFrequency,PublishSequence,PublishReason,SaveLoadEpoch,"
		"IntervalStartMonotonicUs,IntervalEndMonotonicUs,Scope,ScopeId,ScopeLabel,BackendId,OutputSizeBucket,"
		"ShutdownPublishEnabled,AdmissionClosed,"
		"CallsSeen,RowsWritten,RowsDropped,TotalQpc,RowTotalQpc,InputBytesConsumed,OutputBytesProduced,"
		"SelectedCalls,PrimaryCalls,PrimaryQpc,FallbackCalls,FallbackQpc,ServedCalls,ServedQpc,"
		"UnservedCalls,MalformedObservations,"
		"UnknownReasonCalls,FirstUnknownReasonId,BackendTableOverflowCalls,LeasedShards,"
		"OverflowedThreads,SpillCalls,OversizedBatches,"
		"ChunkRows,RequestLeaderRows,RequestWallQpc,NominalOutputBytes,"
		"PrimaryInputBytesConsumed,PrimaryOutputBytesProduced,"
		"SizeDeltaSamples,SizeMismatchChunks,MinSizeDelta,MaxSizeDelta,"
		"NominalDescMismatches,CapacityFailures,"
		"RowsTruncated,ReconciliationOk,ReasonPartitionOk,"
		"BackendPartitionOk,SitePartitionOk,CallerPartitionOk,RowPartitionOk,TickIdentityOk,"
		"RequestEvidenceOk,RowEvidenceOk,ContractOk"sv
	};

	struct SummaryContext
	{
		std::string_view sessionID;
		std::string_view publishReason;
		uint64_t qpcFrequency{ 0 };
		uint64_t publishSequence{ 0 };
		uint64_t saveLoadEpoch{ 0 };
		uint64_t intervalStartMonotonicUs{ 0 };
		uint64_t intervalEndMonotonicUs{ 0 };
		bool shutdownPublishEnabled{ false };
		bool admissionClosed{ false };
	};

	struct SummaryRow
	{
		std::string_view scope;
		std::string_view scopeLabel;
		uint64_t scopeID{ 0 };
		uint64_t backendID{ 0 };
		uint64_t outputSizeBucket{ 0 };
		uint64_t callsSeen{ 0 };
		uint64_t rowsWritten{ 0 };
		uint64_t rowsDropped{ 0 };
		uint64_t totalQpc{ 0 };
		uint64_t rowTotalQpc{ 0 };
		uint64_t inputBytesConsumed{ 0 };
		uint64_t outputBytesProduced{ 0 };
		uint64_t selectedCalls{ 0 };
		uint64_t primaryCalls{ 0 };
		uint64_t primaryQpc{ 0 };
		uint64_t fallbackCalls{ 0 };
		uint64_t fallbackQpc{ 0 };
		uint64_t servedCalls{ 0 };
		uint64_t servedQpc{ 0 };
		uint64_t unservedCalls{ 0 };
		uint64_t malformedObservations{ 0 };
		uint64_t unknownReasonCalls{ 0 };
		uint64_t firstUnknownReasonId{ 0 };
		uint64_t backendTableOverflowCalls{ 0 };
		uint64_t leasedShards{ 0 };
		uint64_t overflowedThreads{ 0 };
		uint64_t spillCalls{ 0 };
		uint64_t oversizedBatches{ 0 };
		RequestEvidence requests{};
		Reconciliation reconciliation{};
	};

	inline void WriteSummaryHeader(std::ostream& a_file)
	{
		a_file << "# ba2 summary v"sv << kSchemaVersion
			<< "; an interval edge is a per-shard aggregation boundary, not a global instant\n"sv
			<< "# retained rows are a biased union of per-shard prefixes when RowsTruncated=1; the aggregates stay exact\n"sv
			<< "# MinSizeDelta/MaxSizeDelta are nominal-minus-decoded bytes over mismatching chunks only and are only meaningful when SizeMismatchChunks>0\n"sv
			<< kSummaryColumns << "\n"sv;
	}

	inline void WriteSummaryRow(
		std::ostream& a_file,
		const SummaryContext& a_context,
		const SummaryRow& a_row)
	{
		const auto flag = [](bool a_value) { return a_value ? 1 : 0; };
		a_file << kSchemaVersion << ","sv
			<< a_context.sessionID << ","sv
			<< a_context.qpcFrequency << ","sv
			<< a_context.publishSequence << ","sv
			<< a_context.publishReason << ","sv
			<< a_context.saveLoadEpoch << ","sv
			<< a_context.intervalStartMonotonicUs << ","sv
			<< a_context.intervalEndMonotonicUs << ","sv
			<< a_row.scope << ","sv
			<< a_row.scopeID << ","sv
			<< a_row.scopeLabel << ","sv
			<< a_row.backendID << ","sv
			<< a_row.outputSizeBucket << ","sv
			<< flag(a_context.shutdownPublishEnabled) << ","sv
			<< flag(a_context.admissionClosed) << ","sv
			<< a_row.callsSeen << ","sv
			<< a_row.rowsWritten << ","sv
			<< a_row.rowsDropped << ","sv
			<< a_row.totalQpc << ","sv
			<< a_row.rowTotalQpc << ","sv
			<< a_row.inputBytesConsumed << ","sv
			<< a_row.outputBytesProduced << ","sv
			<< a_row.selectedCalls << ","sv
			<< a_row.primaryCalls << ","sv
			<< a_row.primaryQpc << ","sv
			<< a_row.fallbackCalls << ","sv
			<< a_row.fallbackQpc << ","sv
			<< a_row.servedCalls << ","sv
			<< a_row.servedQpc << ","sv
			<< a_row.unservedCalls << ","sv
			<< a_row.malformedObservations << ","sv
			<< a_row.unknownReasonCalls << ","sv
			<< a_row.firstUnknownReasonId << ","sv
			<< a_row.backendTableOverflowCalls << ","sv
			<< a_row.leasedShards << ","sv
			<< a_row.overflowedThreads << ","sv
			<< a_row.spillCalls << ","sv
			<< a_row.oversizedBatches << ","sv
			<< a_row.requests.chunkRows << ","sv
			<< a_row.requests.leaderRows << ","sv
			<< a_row.requests.requestWallQpc << ","sv
			<< a_row.requests.nominalOutputBytes << ","sv
			<< a_row.requests.primaryInputBytesConsumed << ","sv
			<< a_row.requests.primaryOutputBytesProduced << ","sv
			<< a_row.requests.sizeDeltaSamples << ","sv
			<< a_row.requests.sizeMismatchChunks << ","sv
			<< a_row.requests.minSizeDelta << ","sv
			<< a_row.requests.maxSizeDelta << ","sv
			<< a_row.requests.nominalDescMismatches << ","sv
			<< a_row.requests.capacityFailures << ","sv
			<< flag(a_row.reconciliation.rowsTruncated) << ","sv
			<< flag(a_row.reconciliation.Ok()) << ","sv
			<< flag(a_row.reconciliation.reasonPartitionOk) << ","sv
			<< flag(a_row.reconciliation.backendPartitionOk) << ","sv
			<< flag(a_row.reconciliation.sitePartitionOk) << ","sv
			<< flag(a_row.reconciliation.callerPartitionOk) << ","sv
			<< flag(a_row.reconciliation.rowPartitionOk) << ","sv
			<< flag(a_row.reconciliation.tickIdentityOk) << ","sv
			<< flag(a_row.reconciliation.requestEvidenceOk) << ","sv
			<< flag(a_row.reconciliation.rowEvidenceOk) << ","sv
			<< flag(a_row.reconciliation.contractOk) << "\n"sv;
	}
}

namespace Addictol
{
	[[nodiscard]] constexpr bool ShouldPublishBA2Interval(
		uint64_t a_callsSeen,
		bool a_admissionClosed) noexcept
	{
		return a_callsSeen != 0 || a_admissionClosed;
	}

	// The publish point retains this fixed-size copy so a reader never touches live shards or arenas.
	struct BA2PublishedSnapshot
	{
		static constexpr size_t kReasonCapacity{ 32 };

		bool valid{ false };
		std::array<char, kReasonCapacity> reason{};
		uint64_t publishSequence{ 0 };
		uint64_t saveLoadEpoch{ 0 };
		uint64_t qpcFrequency{ 0 };
		uint64_t intervalStartMonotonicUs{ 0 };
		uint64_t intervalEndMonotonicUs{ 0 };
		uint64_t leasedShards{ 0 };
		uint64_t overflowedThreads{ 0 };
		uint64_t spillCalls{ 0 };
		bool shutdownPublishEnabled{ false };
		bool admissionClosed{ false };
		BA2Profile::ShardAggregate totals{};
		BA2Profile::Reconciliation reconciliation{};

		[[nodiscard]] constexpr std::string_view Reason() const noexcept
		{
			return std::string_view{ reason.data() };
		}

		[[nodiscard]] constexpr uint64_t IntervalMicroseconds() const noexcept
		{
			return intervalEndMonotonicUs > intervalStartMonotonicUs ?
				intervalEndMonotonicUs - intervalStartMonotonicUs :
				0;
		}
	};

	// The reason is copied into fixed storage because the caller's view does not outlive the publish.
	[[nodiscard]] constexpr BA2PublishedSnapshot MakeBA2PublishedSnapshot(
		const BA2Profile::SummaryContext& a_context,
		const BA2Profile::ShardAggregate& a_totals,
		const BA2Profile::Reconciliation& a_reconciliation,
		uint64_t a_leasedShards,
		uint64_t a_overflowedThreads,
		uint64_t a_spillCalls) noexcept
	{
		BA2PublishedSnapshot snapshot;
		snapshot.valid = true;
		const auto length = std::min(
			a_context.publishReason.size(),
			BA2PublishedSnapshot::kReasonCapacity - 1);
		for (size_t index = 0; index < length; ++index)
			snapshot.reason[index] = a_context.publishReason[index];
		snapshot.publishSequence = a_context.publishSequence;
		snapshot.saveLoadEpoch = a_context.saveLoadEpoch;
		snapshot.qpcFrequency = a_context.qpcFrequency;
		snapshot.intervalStartMonotonicUs = a_context.intervalStartMonotonicUs;
		snapshot.intervalEndMonotonicUs = a_context.intervalEndMonotonicUs;
		snapshot.shutdownPublishEnabled = a_context.shutdownPublishEnabled;
		snapshot.admissionClosed = a_context.admissionClosed;
		snapshot.leasedShards = a_leasedShards;
		snapshot.overflowedThreads = a_overflowedThreads;
		snapshot.spillCalls = a_spillCalls;
		snapshot.totals = a_totals;
		snapshot.reconciliation = a_reconciliation;
		return snapshot;
	}

	class BA2PublishedStore
	{
	public:
		void Retain(const BA2PublishedSnapshot& a_snapshot) noexcept
		{
			std::lock_guard lock(m_lock);
			m_published = a_snapshot;
		}

		[[nodiscard]] bool CopyLatest(BA2PublishedSnapshot& a_out) const noexcept
		{
			std::lock_guard lock(m_lock);
			if (!m_published.valid)
				return false;

			a_out = m_published;
			return true;
		}

	private:
		mutable std::mutex m_lock;
		BA2PublishedSnapshot m_published{};
	};
}