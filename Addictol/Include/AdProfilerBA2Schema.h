#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <span>
#include <string_view>
#include <type_traits>

namespace Addictol::BA2Profile
{
	using namespace std::literals;

	inline constexpr std::uint32_t kSchemaVersion{ 2 };

	using BackendId = std::uint32_t;

	inline constexpr BackendId kBackendNone{ 0 };
	inline constexpr BackendId kBackendStockZlib{ 1 };
	inline constexpr BackendId kBackendLibDeflate{ 2 };
	inline constexpr std::size_t kKnownBackendCount{ 3 };

	inline constexpr std::array<std::string_view, kKnownBackendCount> kBackendNames{
		"None"sv, "StockZlib"sv, "LibDeflate"sv
	};

	using FallbackReasonId = std::uint32_t;

	inline constexpr FallbackReasonId kReasonNone{ 0 };
	inline constexpr FallbackReasonId kReasonState{ 1 };
	inline constexpr FallbackReasonId kReasonAllocation{ 2 };
	inline constexpr FallbackReasonId kReasonDecode{ 3 };
	inline constexpr FallbackReasonId kReasonCommit{ 4 };
	inline constexpr std::size_t kKnownReasonCount{ 5 };

	inline constexpr std::array<std::string_view, kKnownReasonCount> kReasonNames{
		"None"sv, "State"sv, "Allocation"sv, "Decode"sv, "Commit"sv
	};

	[[nodiscard]] constexpr std::string_view BackendName(BackendId a_backend) noexcept
	{
		return a_backend < kKnownBackendCount ? kBackendNames[a_backend] : "Unknown"sv;
	}

	[[nodiscard]] constexpr std::string_view ReasonName(FallbackReasonId a_reason) noexcept
	{
		return a_reason < kKnownReasonCount ? kReasonNames[a_reason] : "Unknown"sv;
	}

	// Malformed bits invalidate the contract; unknown-id bits only mark the row for analysis.
	inline constexpr std::uint8_t kFlagByteRangeOverflow{ 1u << 0 };
	inline constexpr std::uint8_t kFlagByteAccounting{ 1u << 1 };
	inline constexpr std::uint8_t kFlagTickAccounting{ 1u << 2 };
	inline constexpr std::uint8_t kFlagServedMismatch{ 1u << 3 };
	inline constexpr std::uint8_t kFlagReasonMismatch{ 1u << 4 };
	inline constexpr std::uint8_t kFlagUnknownReason{ 1u << 5 };
	inline constexpr std::uint8_t kFlagUnknownBackend{ 1u << 6 };
	inline constexpr std::uint8_t kFlagQpcFrequency{ 1u << 7 };
	inline constexpr std::uint8_t kMalformedFlags{
		kFlagByteRangeOverflow | kFlagByteAccounting | kFlagTickAccounting |
		kFlagServedMismatch | kFlagReasonMismatch | kFlagQpcFrequency
	};

	struct CallObservation
	{
		BackendId primaryBackendId{ kBackendNone };
		BackendId fallbackBackendId{ kBackendNone };
		BackendId servedBackendId{ kBackendNone };
		FallbackReasonId fallbackReasonId{ kReasonNone };
		std::uint64_t primaryQpc{ 0 };
		std::uint64_t fallbackQpc{ 0 };
		std::uint64_t totalQpc{ 0 };
		std::uint64_t qpcFrequency{ 0 };
		std::uint64_t inputBytesAvailable{ 0 };
		std::uint64_t outputBytesAvailable{ 0 };
		std::uint64_t inputBytesConsumed{ 0 };
		std::uint64_t outputBytesProduced{ 0 };
		std::int32_t zlibResult{ 0 };
		bool primaryAttempted{ false };
	};

	struct ObservationCheck
	{
		std::uint8_t flags{ 0 };

		[[nodiscard]] constexpr bool WellFormed() const noexcept { return (flags & kMalformedFlags) == 0; }
	};

	[[nodiscard]] constexpr ObservationCheck ValidateObservation(
		const CallObservation& a_observation,
		std::uint64_t a_expectedQpcFrequency = 0) noexcept
	{
		constexpr std::uint64_t byteLimit{ 0xFFFFFFFFull };
		std::uint8_t flags{ 0 };

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
			std::numeric_limits<std::uint64_t>::max() - a_observation.fallbackQpc;
		if (tickSumOverflows ||
			a_observation.totalQpc < a_observation.primaryQpc + a_observation.fallbackQpc)
			flags |= kFlagTickAccounting;

		if (!a_observation.primaryAttempted && a_observation.primaryQpc)
			flags |= kFlagTickAccounting;

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

		if (!a_observation.qpcFrequency ||
			(a_expectedQpcFrequency && a_observation.qpcFrequency != a_expectedQpcFrequency))
			flags |= kFlagQpcFrequency;

		return { flags };
	}

	struct CallRecord
	{
		std::uint64_t shardSequence;
		std::uint64_t monotonicUs;
		std::uint64_t primaryQpc;
		std::uint64_t fallbackQpc;
		std::uint64_t totalQpc;
		std::uint32_t saveLoadEpoch;
		BackendId primaryBackendId;
		BackendId fallbackBackendId;
		FallbackReasonId fallbackReasonId;
		BackendId servedBackendId;
		std::uint32_t inputBytesAvailable;
		std::uint32_t outputBytesAvailable;
		std::uint32_t inputBytesConsumed;
		std::uint32_t outputBytesProduced;
		std::int32_t zlibResult;
		std::uint16_t shardIndex;
		std::uint8_t primaryAttempted;
		std::uint8_t observationFlags;
	};

	static_assert(sizeof(CallRecord) == 88);
	static_assert(alignof(CallRecord) == 8);
	static_assert(std::is_standard_layout_v<CallRecord>);
	static_assert(std::is_trivially_copyable_v<CallRecord>);

	[[nodiscard]] constexpr std::uint32_t SaturateBytes(std::uint64_t a_value) noexcept
	{
		constexpr std::uint64_t byteLimit{ 0xFFFFFFFFull };
		return static_cast<std::uint32_t>(a_value > byteLimit ? byteLimit : a_value);
	}

	[[nodiscard]] constexpr CallRecord MakeCallRecord(
		const CallObservation& a_observation,
		const ObservationCheck& a_check,
		std::uint16_t a_shardIndex,
		std::uint64_t a_shardSequence,
		std::uint32_t a_saveLoadEpoch,
		std::uint64_t a_monotonicUs) noexcept
	{
		return {
			a_shardSequence,
			a_monotonicUs,
			a_observation.primaryQpc,
			a_observation.fallbackQpc,
			a_observation.totalQpc,
			a_saveLoadEpoch,
			a_observation.primaryBackendId,
			a_observation.fallbackBackendId,
			a_observation.fallbackReasonId,
			a_observation.servedBackendId,
			SaturateBytes(a_observation.inputBytesAvailable),
			SaturateBytes(a_observation.outputBytesAvailable),
			SaturateBytes(a_observation.inputBytesConsumed),
			SaturateBytes(a_observation.outputBytesProduced),
			a_observation.zlibResult,
			a_shardIndex,
			static_cast<std::uint8_t>(a_observation.primaryAttempted ? 1 : 0),
			a_check.flags
		};
	}

	inline constexpr std::size_t kBackendTableCapacity{ 8 };
	inline constexpr std::size_t kOutputSizeBucketCount{ 8 };
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

	[[nodiscard]] constexpr std::size_t OutputSizeBucket(std::uint64_t a_bytes) noexcept
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
		std::uint64_t selectedCalls{ 0 };
		std::uint64_t primaryCalls{ 0 };
		std::uint64_t primaryQpc{ 0 };
		std::uint64_t fallbackCalls{ 0 };
		std::uint64_t fallbackQpc{ 0 };
		std::uint64_t servedCalls{ 0 };
		std::uint64_t servedQpc{ 0 };
		std::uint64_t inputBytesConsumed{ 0 };
		std::uint64_t outputBytesProduced{ 0 };
		std::array<std::uint64_t, kOutputSizeBucketCount> servedBucketCalls{};
		std::array<std::uint64_t, kOutputSizeBucketCount> servedBucketQpc{};
		std::array<std::uint64_t, kOutputSizeBucketCount> servedBucketBytes{};

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
			for (std::size_t index = 0; index < kOutputSizeBucketCount; ++index)
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
			const auto start = static_cast<std::size_t>(a_backend) % kBackendTableCapacity;
			for (std::size_t probe = 0; probe < kBackendTableCapacity; ++probe)
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

		[[nodiscard]] constexpr std::uint64_t ServedTotal() const noexcept
		{
			std::uint64_t total{ 0 };
			for (const auto& entry : entries)
				total += entry.servedCalls;
			return total;
		}
	};

	struct ShardAggregate
	{
		std::uint64_t callsSeen{ 0 };
		std::uint64_t rowsWritten{ 0 };
		std::uint64_t rowsDropped{ 0 };
		std::uint64_t rowsDisabled{ 0 };
		std::uint64_t totalQpc{ 0 };
		std::uint64_t rowTotalQpc{ 0 };
		std::uint64_t inputBytesConsumed{ 0 };
		std::uint64_t outputBytesProduced{ 0 };
		std::array<std::uint64_t, kKnownReasonCount> reasonCounts{};
		std::array<std::uint64_t, kKnownReasonCount> reasonPrimaryQpc{};
		std::array<std::uint64_t, kKnownReasonCount> reasonFallbackQpc{};
		std::array<std::uint64_t, kKnownReasonCount> reasonTotalQpc{};
		std::uint64_t unknownReasonCalls{ 0 };
		std::uint64_t unknownReasonPrimaryQpc{ 0 };
		std::uint64_t unknownReasonFallbackQpc{ 0 };
		std::uint64_t unknownReasonTotalQpc{ 0 };
		std::uint64_t unservedCalls{ 0 };
		std::uint64_t malformedObservations{ 0 };
		std::uint64_t overflowedThreads{ 0 };
		std::uint64_t backendTableOverflowCalls{ 0 };
		std::uint64_t servedBackendOverflowCalls{ 0 };
		FallbackReasonId firstUnknownReasonId{ 0 };
		BackendTable backends{};

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
			for (std::size_t index = 0; index < kKnownReasonCount; ++index)
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
		bool rowPartitionOk{ false };
		bool tickIdentityOk{ false };
		bool rowEvidenceOk{ false };
		bool contractOk{ false };
		bool rowsTruncated{ false };

		[[nodiscard]] constexpr bool Ok() const noexcept
		{
			return reasonPartitionOk && backendPartitionOk && rowPartitionOk &&
				tickIdentityOk && rowEvidenceOk && contractOk;
		}
	};

	struct RowEvidence
	{
		std::uint64_t rowsSeen{ 0 };
		std::uint64_t totalQpc{ 0 };
		std::array<std::uint64_t, kKnownReasonCount> reasonCounts{};
		std::uint64_t unknownReasonCalls{ 0 };
		std::uint64_t unservedCalls{ 0 };
		std::uint64_t malformedRows{ 0 };
		std::uint64_t shardMismatchRows{ 0 };
		BackendTable servedBackends{};

		constexpr void Account(
			const CallRecord& a_record,
			std::uint16_t a_expectedShardIndex) noexcept
		{
			++rowsSeen;
			totalQpc += a_record.totalQpc;
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
			for (std::size_t index = 0; index < kKnownReasonCount; ++index)
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
			a_totals.malformedObservations != a_rows.malformedRows)
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
		std::uint64_t reasonSum{ a_totals.unknownReasonCalls };
		for (const auto count : a_totals.reasonCounts)
			reasonSum += count;

		Reconciliation result;
		result.reasonPartitionOk = a_totals.callsSeen == reasonSum;
		result.backendPartitionOk =
			a_totals.callsSeen ==
			a_totals.backends.ServedTotal() +
				a_totals.servedBackendOverflowCalls +
				a_totals.unservedCalls;
		result.rowPartitionOk =
			a_totals.callsSeen ==
			a_totals.rowsWritten + a_totals.rowsDropped + a_totals.rowsDisabled;
		result.tickIdentityOk =
			a_totals.rowsDropped != 0 ||
			a_totals.rowsDisabled != 0 ||
			a_totals.totalQpc == a_totals.rowTotalQpc;
		result.rowEvidenceOk =
			a_rows ?
			MatchesRetainedRows(a_totals, *a_rows) :
			true;
		result.contractOk = a_totals.malformedObservations == 0 &&
			a_totals.backendTableOverflowCalls == 0 &&
			a_totals.unknownReasonCalls == 0;
		result.rowsTruncated = a_totals.rowsDropped != 0;
		return result;
	}

	inline constexpr std::size_t kCallsColumnCount{ 25 };
	inline constexpr std::string_view kCallsColumns{
		"SchemaVersion,SessionId,QpcFrequency,PublishSequence,PublishReason,"
		"SaveLoadEpoch,MonotonicUs,ShardIndex,ShardSequence,"
		"PrimaryBackendId,PrimaryAttempted,PrimaryQpc,FallbackBackendId,FallbackReasonId,FallbackQpc,"
		"ServedBackendId,ZlibResult,TotalQpc,InputBytesAvailable,OutputBytesAvailable,"
		"InputBytesConsumed,OutputBytesProduced,ObservationFlags,ShutdownPublishEnabled,AdmissionClosed"sv
	};

	struct FileContext
	{
		std::string_view sessionID;
		std::uint64_t qpcFrequency{ 0 };
		std::uint64_t publishSequence{ 0 };
		std::string_view publishReason;
		bool shutdownPublishEnabled{ false };
		bool admissionClosed{ false };
	};

	inline void WriteCallsHeader(std::ostream& a_file)
	{
		a_file << "# ba2 calls v"sv << kSchemaVersion
			<< "; durations are raw QPC ticks; a row is identified by ShardIndex+ShardSequence\n"sv
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
			<< a_record.primaryBackendId << ","sv
			<< static_cast<unsigned>(a_record.primaryAttempted) << ","sv
			<< a_record.primaryQpc << ","sv
			<< a_record.fallbackBackendId << ","sv
			<< a_record.fallbackReasonId << ","sv
			<< a_record.fallbackQpc << ","sv
			<< a_record.servedBackendId << ","sv
			<< a_record.zlibResult << ","sv
			<< a_record.totalQpc << ","sv
			<< a_record.inputBytesAvailable << ","sv
			<< a_record.outputBytesAvailable << ","sv
			<< a_record.inputBytesConsumed << ","sv
			<< a_record.outputBytesProduced << ","sv
			<< static_cast<unsigned>(a_record.observationFlags) << ","sv
			<< static_cast<unsigned>(a_context.shutdownPublishEnabled) << ","sv
			<< static_cast<unsigned>(a_context.admissionClosed) << "\n"sv;
	}

	inline constexpr std::size_t kSummaryColumnCount{ 45 };
	inline constexpr std::string_view kSummaryColumns{
		"SchemaVersion,SessionId,QpcFrequency,PublishSequence,PublishReason,SaveLoadEpoch,"
		"IntervalStartMonotonicUs,IntervalEndMonotonicUs,Scope,ScopeId,ScopeLabel,BackendId,OutputSizeBucket,"
		"ShutdownPublishEnabled,AdmissionClosed,"
		"CallsSeen,RowsWritten,RowsDropped,TotalQpc,RowTotalQpc,InputBytesConsumed,OutputBytesProduced,"
		"SelectedCalls,PrimaryCalls,PrimaryQpc,FallbackCalls,FallbackQpc,ServedCalls,ServedQpc,"
		"UnservedCalls,MalformedObservations,"
		"UnknownReasonCalls,FirstUnknownReasonId,BackendTableOverflowCalls,LeasedShards,"
		"OverflowedThreads,SpillCalls,RowsTruncated,ReconciliationOk,ReasonPartitionOk,"
		"BackendPartitionOk,RowPartitionOk,TickIdentityOk,RowEvidenceOk,ContractOk"sv
	};

	struct SummaryContext
	{
		std::string_view sessionID;
		std::string_view publishReason;
		std::uint64_t qpcFrequency{ 0 };
		std::uint64_t publishSequence{ 0 };
		std::uint64_t saveLoadEpoch{ 0 };
		std::uint64_t intervalStartMonotonicUs{ 0 };
		std::uint64_t intervalEndMonotonicUs{ 0 };
		bool shutdownPublishEnabled{ false };
		bool admissionClosed{ false };
	};

	struct SummaryRow
	{
		std::string_view scope;
		std::string_view scopeLabel;
		std::uint64_t scopeID{ 0 };
		std::uint64_t backendID{ 0 };
		std::uint64_t outputSizeBucket{ 0 };
		std::uint64_t callsSeen{ 0 };
		std::uint64_t rowsWritten{ 0 };
		std::uint64_t rowsDropped{ 0 };
		std::uint64_t totalQpc{ 0 };
		std::uint64_t rowTotalQpc{ 0 };
		std::uint64_t inputBytesConsumed{ 0 };
		std::uint64_t outputBytesProduced{ 0 };
		std::uint64_t selectedCalls{ 0 };
		std::uint64_t primaryCalls{ 0 };
		std::uint64_t primaryQpc{ 0 };
		std::uint64_t fallbackCalls{ 0 };
		std::uint64_t fallbackQpc{ 0 };
		std::uint64_t servedCalls{ 0 };
		std::uint64_t servedQpc{ 0 };
		std::uint64_t unservedCalls{ 0 };
		std::uint64_t malformedObservations{ 0 };
		std::uint64_t unknownReasonCalls{ 0 };
		std::uint64_t firstUnknownReasonId{ 0 };
		std::uint64_t backendTableOverflowCalls{ 0 };
		std::uint64_t leasedShards{ 0 };
		std::uint64_t overflowedThreads{ 0 };
		std::uint64_t spillCalls{ 0 };
		Reconciliation reconciliation{};
	};

	inline void WriteSummaryHeader(std::ostream& a_file)
	{
		a_file << "# ba2 summary v"sv << kSchemaVersion
			<< "; an interval edge is a per-shard aggregation boundary, not a global instant\n"sv
			<< "# retained rows are a biased union of per-shard prefixes when RowsTruncated=1; the aggregates stay exact\n"sv
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
			<< flag(a_row.reconciliation.rowsTruncated) << ","sv
			<< flag(a_row.reconciliation.Ok()) << ","sv
			<< flag(a_row.reconciliation.reasonPartitionOk) << ","sv
			<< flag(a_row.reconciliation.backendPartitionOk) << ","sv
			<< flag(a_row.reconciliation.rowPartitionOk) << ","sv
			<< flag(a_row.reconciliation.tickIdentityOk) << ","sv
			<< flag(a_row.reconciliation.rowEvidenceOk) << ","sv
			<< flag(a_row.reconciliation.contractOk) << "\n"sv;
	}
}
