#include <AdProfilerBA2.h>
#include <AdProfilerCore.h>
#include <AdProfilerRuntimeChannel.h>
#include <Modules/AdModuleSafeExit.h>

#include <Windows.h>
#ifdef ERROR
#undef ERROR
#endif

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <new>
#include <limits>
#include <ostream>
#include <span>
#include <string_view>
#include <utility>

namespace Addictol
{
	namespace ba2ProfilerDetail
	{
		using namespace BA2Profile;

		constexpr std::size_t kLeasedShardCount{ 64 };
		constexpr std::size_t kSpillShardIndex{ kLeasedShardCount };
		constexpr std::size_t kShardCount{ kLeasedShardCount + 1 };
		constexpr std::size_t kBankCount{ 2 };
		constexpr std::size_t kChunkRows{ 256 };
		constexpr std::size_t kArenaChunkCount{ 1024 };
		constexpr std::size_t kArenaRowCapacity{ kChunkRows * kArenaChunkCount };
		constexpr std::uint16_t kNoChunk{ 0xFFFF };

		static_assert(kArenaRowCapacity == 262144);
		static_assert(kArenaChunkCount < kNoChunk);
		static_assert(kBankCount == 2);
		static_assert(kSchemaVersion == 2);
		static_assert(sizeof(CallRecord) * kArenaRowCapacity == 22ull * 1024 * 1024);

		struct Arena
		{
			CallRecord* rows{ nullptr };
			std::array<std::uint16_t, kArenaChunkCount> chunkNext{};
			std::atomic<std::uint32_t> nextChunk{ 0 };
		};

		struct ShardBank
		{
			ShardAggregate aggregate{};
			std::uint16_t firstChunk{ kNoChunk };
			std::uint16_t currentChunk{ kNoChunk };
			std::uint32_t rowsInCurrentChunk{ 0 };
			bool exhausted{ false };
		};

		struct alignas(64) Shard
		{
			std::mutex lock;
			std::array<ShardBank, kBankCount> banks{};
			std::uint64_t sequence{ 0 };
			std::uint64_t generation{ 0 };
			std::uint32_t activeBank{ 0 };
			std::uint16_t index{ 0 };
		};

		struct ShardRows
		{
			std::uint16_t firstChunk{ kNoChunk };
			std::uint32_t rowsInCurrentChunk{ 0 };
			std::uint32_t bank{ 0 };
		};

		struct RecorderState
		{
			RecorderState(
				RuntimeSessionContext& a_session,
				std::uint64_t a_qpcFrequency,
				bool a_exportCSV,
				bool a_shutdownPublishEnabled) :
				calls(a_session, "ba2_calls_v2"sv, WriteCallsHeader),
				summary(a_session, "ba2_summary_v2"sv, WriteSummaryHeader),
				session(a_session),
				qpcFrequency(a_qpcFrequency),
				exportCSV(a_exportCSV),
				shutdownPublishEnabled(a_shutdownPublishEnabled)
			{
				for (std::size_t index = 0; index < kShardCount; ++index)
					shards[index].index = static_cast<std::uint16_t>(index);
			}

			std::array<Arena, kBankCount> arenas{};
			std::array<Shard, kShardCount> shards{};
			std::mutex publishLock;
			RuntimeCsvFile calls;
			RuntimeCsvFile summary;
			RuntimeSessionContext& session;
			std::atomic<std::uint64_t> freeShardMask{
				std::numeric_limits<std::uint64_t>::max()
			};
			std::atomic<std::size_t> activeDedicatedLeases{ 0 };
			std::atomic<bool> spillLogged{ false };
			std::uint64_t qpcFrequency{ 0 };
			std::uint64_t publishSequence{ 0 };
			std::uint64_t intervalStartMonotonicUs{ 0 };
			bool exportCSV{ false };
			bool shutdownPublishEnabled{ false };
			std::atomic<bool> accepting{ true };
		};

		static std::atomic<RecorderState*> g_state{ nullptr };
		struct ThreadLease
		{
			RecorderState* state{ nullptr };
			Shard* shard{ nullptr };
			std::uint16_t shardIndex{ kNoChunk };
			std::uint64_t countedGeneration{ std::numeric_limits<std::uint64_t>::max() };

			~ThreadLease()
			{
				Release();
			}

			void Release() noexcept
			{
				if (!state)
				return;
				if (shardIndex < kLeasedShardCount)
				{
					state->freeShardMask.fetch_or(
						std::uint64_t{ 1 } << shardIndex,
						std::memory_order_release);
					state->activeDedicatedLeases.fetch_sub(1, std::memory_order_release);
				}
				state = nullptr;
				shard = nullptr;
				shardIndex = kNoChunk;
				countedGeneration = std::numeric_limits<std::uint64_t>::max();
			}
		};
		static thread_local ThreadLease g_threadLease;

		[[nodiscard]] Shard& LeaseShard(RecorderState& a_state) noexcept
		{
			if (g_threadLease.state == &a_state)
				return *g_threadLease.shard;

			g_threadLease.Release();
			g_threadLease.state = &a_state;
			auto freeMask = a_state.freeShardMask.load(std::memory_order_acquire);
			while (freeMask)
			{
				const auto index = static_cast<std::uint16_t>(std::countr_zero(freeMask));
				const auto claimedMask = freeMask & ~(std::uint64_t{ 1 } << index);
				if (a_state.freeShardMask.compare_exchange_weak(
						freeMask,
						claimedMask,
						std::memory_order_acq_rel,
						std::memory_order_acquire))
				{
					g_threadLease.shardIndex = index;
					a_state.activeDedicatedLeases.fetch_add(1, std::memory_order_release);
					break;
				}
			}
			if (!freeMask && g_threadLease.shardIndex == kNoChunk)
				g_threadLease.shardIndex = kSpillShardIndex;
			g_threadLease.shard = &a_state.shards[g_threadLease.shardIndex];
			if (g_threadLease.shardIndex == kSpillShardIndex &&
				!a_state.spillLogged.exchange(true, std::memory_order_acq_rel))
			{
				REX::WARN(
					"BA2 profiler: {} shards leased; further decompression threads share the locked spill shard."sv,
					kLeasedShardCount);
			}
			return *g_threadLease.shard;
		}

		void DestroyState(RecorderState* a_state) noexcept
		{
			if (!a_state)
				return;
			for (auto& arena : a_state->arenas)
				delete[] arena.rows;
			delete a_state;
		}

		[[nodiscard]] CallRecord* ReserveRow(Arena& a_arena, ShardBank& a_bank) noexcept
		{
			if (a_bank.exhausted)
				return nullptr;

			if (a_bank.currentChunk == kNoChunk || a_bank.rowsInCurrentChunk == kChunkRows)
			{
				const auto reserved = a_arena.nextChunk.fetch_add(1, std::memory_order_relaxed);
				if (reserved >= kArenaChunkCount)
				{
					a_bank.exhausted = true;
					return nullptr;
				}

				const auto chunk = static_cast<std::uint16_t>(reserved);
				a_arena.chunkNext[chunk] = kNoChunk;
				if (a_bank.currentChunk == kNoChunk)
					a_bank.firstChunk = chunk;
				else
					a_arena.chunkNext[a_bank.currentChunk] = chunk;
				a_bank.currentChunk = chunk;
				a_bank.rowsInCurrentChunk = 0;
			}

			return &a_arena.rows[static_cast<std::size_t>(a_bank.currentChunk) * kChunkRows +
				a_bank.rowsInCurrentChunk++];
		}

		[[nodiscard]] RowEvidence SerializeShardRows(
			RecorderState& a_state,
			std::ostream& a_file,
			const FileContext& a_context,
			const ShardRows& a_rows,
			std::uint16_t a_shardIndex) noexcept
		{
			const auto& arena = a_state.arenas[a_rows.bank];
			RowEvidence evidence;
			auto chunk = a_rows.firstChunk;
			while (chunk != kNoChunk)
			{
				const auto next = arena.chunkNext[chunk];
				const auto rows = next == kNoChunk ?
					static_cast<std::size_t>(a_rows.rowsInCurrentChunk) :
					kChunkRows;
				const auto* base = &arena.rows[static_cast<std::size_t>(chunk) * kChunkRows];
				for (std::size_t row = 0; row < rows; ++row)
				{
					evidence.Account(base[row], a_shardIndex);
					WriteCallRow(a_file, a_context, base[row]);
				}
				chunk = next;
			}
			return evidence;
		}

		void ResetBank(Shard& a_shard, std::uint32_t a_bank) noexcept
		{
			auto& bank = a_shard.banks[a_bank];
			bank.aggregate.Reset();
			bank.firstChunk = kNoChunk;
			bank.currentChunk = kNoChunk;
			bank.rowsInCurrentChunk = 0;
			bank.exhausted = false;
		}

		[[nodiscard]] SummaryRow MakeShardRow(
			const ShardAggregate& a_aggregate,
			std::size_t a_index,
			const Reconciliation& a_reconciliation) noexcept
		{
			SummaryRow row;
			row.scope = "Shard"sv;
			row.scopeLabel = a_index == kSpillShardIndex ? "Spill"sv : "Leased"sv;
			row.scopeID = a_index;
			row.callsSeen = a_aggregate.callsSeen;
			row.rowsWritten = a_aggregate.rowsWritten;
			row.rowsDropped = a_aggregate.rowsDropped;
			row.totalQpc = a_aggregate.totalQpc;
			row.rowTotalQpc = a_aggregate.rowTotalQpc;
			row.inputBytesConsumed = a_aggregate.inputBytesConsumed;
			row.outputBytesProduced = a_aggregate.outputBytesProduced;
			row.unservedCalls = a_aggregate.unservedCalls;
			row.malformedObservations = a_aggregate.malformedObservations;
			row.unknownReasonCalls = a_aggregate.unknownReasonCalls;
			row.firstUnknownReasonId = a_aggregate.firstUnknownReasonId;
			row.backendTableOverflowCalls = a_aggregate.backendTableOverflowCalls;
			row.overflowedThreads = a_aggregate.overflowedThreads;
			row.spillCalls = a_index == kSpillShardIndex ? a_aggregate.callsSeen : 0;
			for (const auto& backend : a_aggregate.backends.entries)
			{
				row.selectedCalls += backend.selectedCalls;
				row.primaryCalls += backend.primaryCalls;
				row.primaryQpc += backend.primaryQpc;
				row.fallbackCalls += backend.fallbackCalls;
				row.fallbackQpc += backend.fallbackQpc;
				row.servedCalls += backend.servedCalls;
				row.servedQpc += backend.servedQpc;
			}
			row.reconciliation = a_reconciliation;
			return row;
		}

		struct IntervalReport
		{
			SummaryContext context;
			ShardAggregate totals;
			Reconciliation reconciliation;
			std::uint64_t leasedShards{ 0 };
			std::uint64_t overflowedThreads{ 0 };
			std::uint64_t spillCalls{ 0 };
		};

		void WriteSummary(
			RecorderState& a_state,
			const IntervalReport& a_report,
			std::span<const ShardAggregate> a_shards,
			std::span<const RowEvidence> a_rowEvidence) noexcept
		{
			auto* file = a_state.summary.Begin();
			if (!file)
			{
				REX::ERROR("BA2 profiler: summary CSV unavailable; {} write failures so far."sv,
					a_state.summary.GetFailureCount());
				return;
			}

			const auto& totals = a_report.totals;
			SummaryRow interval;
			interval.scope = "Interval"sv;
			interval.scopeLabel = "All"sv;
			interval.callsSeen = totals.callsSeen;
			interval.rowsWritten = totals.rowsWritten;
			interval.rowsDropped = totals.rowsDropped;
			interval.totalQpc = totals.totalQpc;
			interval.rowTotalQpc = totals.rowTotalQpc;
			interval.inputBytesConsumed = totals.inputBytesConsumed;
			interval.outputBytesProduced = totals.outputBytesProduced;
			interval.unservedCalls = totals.unservedCalls;
			interval.malformedObservations = totals.malformedObservations;
			interval.unknownReasonCalls = totals.unknownReasonCalls;
			interval.firstUnknownReasonId = totals.firstUnknownReasonId;
			interval.backendTableOverflowCalls = totals.backendTableOverflowCalls;
			interval.leasedShards = a_report.leasedShards;
			interval.overflowedThreads = a_report.overflowedThreads;
			interval.spillCalls = a_report.spillCalls;
			for (const auto& backend : totals.backends.entries)
			{
				interval.selectedCalls += backend.selectedCalls;
				interval.primaryCalls += backend.primaryCalls;
				interval.primaryQpc += backend.primaryQpc;
				interval.fallbackCalls += backend.fallbackCalls;
				interval.fallbackQpc += backend.fallbackQpc;
				interval.servedCalls += backend.servedCalls;
				interval.servedQpc += backend.servedQpc;
			}
			interval.reconciliation = a_report.reconciliation;
			WriteSummaryRow(*file, a_report.context, interval);

			for (std::size_t index = 0; index < a_shards.size(); ++index)
			{
				if (a_shards[index].callsSeen)
					WriteSummaryRow(
						*file,
						a_report.context,
						MakeShardRow(
							a_shards[index],
							index,
							Reconcile(a_shards[index], &a_rowEvidence[index])));
			}

			for (const auto& backend : totals.backends.entries)
			{
				if (backend.id == kBackendNone)
					continue;
				SummaryRow row;
				row.scope = "Backend"sv;
				row.scopeLabel = BackendName(backend.id);
				row.scopeID = backend.id;
				row.backendID = backend.id;
				row.callsSeen = backend.servedCalls;
				row.inputBytesConsumed = backend.inputBytesConsumed;
				row.outputBytesProduced = backend.outputBytesProduced;
				row.selectedCalls = backend.selectedCalls;
				row.primaryCalls = backend.primaryCalls;
				row.primaryQpc = backend.primaryQpc;
				row.fallbackCalls = backend.fallbackCalls;
				row.fallbackQpc = backend.fallbackQpc;
				row.servedCalls = backend.servedCalls;
				row.servedQpc = backend.servedQpc;
				row.reconciliation = a_report.reconciliation;
				WriteSummaryRow(*file, a_report.context, row);

				for (std::size_t bucket = 0; bucket < kOutputSizeBucketCount; ++bucket)
				{
					if (!backend.servedBucketCalls[bucket])
						continue;
					SummaryRow bucketRow;
					bucketRow.scope = "BackendBucket"sv;
					bucketRow.scopeID = (static_cast<std::uint64_t>(backend.id) << 8) | bucket;
					bucketRow.scopeLabel = kOutputSizeBucketNames[bucket];
					bucketRow.backendID = backend.id;
					bucketRow.outputSizeBucket = bucket;
					bucketRow.callsSeen = backend.servedBucketCalls[bucket];
					bucketRow.servedCalls = backend.servedBucketCalls[bucket];
					bucketRow.servedQpc = backend.servedBucketQpc[bucket];
					bucketRow.outputBytesProduced = backend.servedBucketBytes[bucket];
					bucketRow.reconciliation = a_report.reconciliation;
					WriteSummaryRow(*file, a_report.context, bucketRow);
				}
			}

			SummaryRow unserved;
			unserved.scope = "Backend"sv;
			unserved.scopeLabel = BackendName(kBackendNone);
			unserved.scopeID = kBackendNone;
			unserved.callsSeen = totals.unservedCalls;
			unserved.reconciliation = a_report.reconciliation;
			WriteSummaryRow(*file, a_report.context, unserved);

			for (std::size_t reason = 0; reason < kKnownReasonCount; ++reason)
			{
				SummaryRow row;
				row.scope = "Reason"sv;
				row.scopeLabel = kReasonNames[reason];
				row.scopeID = reason;
				row.callsSeen = totals.reasonCounts[reason];
				row.primaryQpc = totals.reasonPrimaryQpc[reason];
				row.fallbackQpc = totals.reasonFallbackQpc[reason];
				row.totalQpc = totals.reasonTotalQpc[reason];
				row.reconciliation = a_report.reconciliation;
				WriteSummaryRow(*file, a_report.context, row);
			}

			if (totals.unknownReasonCalls)
			{
				SummaryRow row;
				row.scope = "Reason"sv;
				row.scopeLabel = "Unknown"sv;
				row.scopeID = totals.firstUnknownReasonId;
				row.callsSeen = totals.unknownReasonCalls;
				row.primaryQpc = totals.unknownReasonPrimaryQpc;
				row.fallbackQpc = totals.unknownReasonFallbackQpc;
				row.totalQpc = totals.unknownReasonTotalQpc;
				row.reconciliation = a_report.reconciliation;
				WriteSummaryRow(*file, a_report.context, row);
			}

			a_state.summary.End();
		}

		void LogInterval(RecorderState& a_state, const IntervalReport& a_report) noexcept
		{
			const auto& context = a_report.context;
			const auto& totals = a_report.totals;
			REX::INFO(
				"BA2 profiler [{} #{}]: calls {}, rows {}, dropped {}, disabled {}, total {} ticks, row total {} ticks, qpc {} Hz."sv,
				context.publishReason,
				context.publishSequence,
				totals.callsSeen,
				totals.rowsWritten,
				totals.rowsDropped,
				totals.rowsDisabled,
				totals.totalQpc,
				totals.rowTotalQpc,
				context.qpcFrequency);
			REX::INFO(
				"BA2 profiler [{} #{}]: unserved {}, reasons none {}, state {}, allocation {}, decode {}, commit {}, unknown {}."sv,
				context.publishReason,
				context.publishSequence,
				totals.unservedCalls,
				totals.reasonCounts[kReasonNone],
				totals.reasonCounts[kReasonState],
				totals.reasonCounts[kReasonAllocation],
				totals.reasonCounts[kReasonDecode],
				totals.reasonCounts[kReasonCommit],
				totals.unknownReasonCalls);
			REX::INFO(
				"BA2 profiler [{} #{}]: leased shards {}, overflowed threads {}, spill calls {}; an interval edge is a per-shard aggregation boundary, not one global instant."sv,
				context.publishReason,
				context.publishSequence,
				a_report.leasedShards,
				a_report.overflowedThreads,
				a_report.spillCalls);
			if (a_report.spillCalls)
			{
				REX::WARN(
					"BA2 profiler [{} #{}]: {} calls from {} overflowed threads used the locked spill shard; timing may include recorder contention."sv,
					context.publishReason,
					context.publishSequence,
					a_report.spillCalls,
					a_report.overflowedThreads);
			}

			for (const auto& backend : totals.backends.entries)
			{
				if (backend.id != kBackendNone)
				{
					REX::INFO(
						"BA2 profiler [{} #{}]: backend {} ({}) selected {}, primary {}/{} ticks, fallback {}/{} ticks, served {}/{} ticks."sv,
						context.publishReason,
						context.publishSequence,
						backend.id,
						BackendName(backend.id),
						backend.selectedCalls,
						backend.primaryCalls,
						backend.primaryQpc,
						backend.fallbackCalls,
						backend.fallbackQpc,
						backend.servedCalls,
						backend.servedQpc);
				}
			}

			if (a_report.reconciliation.rowsTruncated)
			{
				REX::WARN(
					"BA2 profiler [{} #{}]: {} rows dropped; the retained rows are a biased union of per-shard prefixes, not a sample, and the aggregates above stay exact."sv,
					context.publishReason,
					context.publishSequence,
					totals.rowsDropped);
			}

			if (totals.unknownReasonCalls)
			{
				REX::ERROR(
					"BA2 profiler [{} #{}]: {} calls carried unknown fallback reason ids (first {}); the backend reason registry is inconsistent."sv,
					context.publishReason,
					context.publishSequence,
					totals.unknownReasonCalls,
					totals.firstUnknownReasonId);
			}

			if (totals.malformedObservations)
			{
				REX::ERROR("BA2 profiler [{} #{}]: {} malformed observations violated the call contract."sv,
					context.publishReason,
					context.publishSequence,
					totals.malformedObservations);
			}

			if (totals.backendTableOverflowCalls)
			{
				REX::ERROR(
					"BA2 profiler [{} #{}]: {} calls exceeded the {}-entry backend table; the backend partition is incomplete."sv,
					context.publishReason,
					context.publishSequence,
					totals.backendTableOverflowCalls,
					kBackendTableCapacity);
			}

			if (!a_report.reconciliation.Ok())
			{
				REX::ERROR(
					"BA2 profiler [{} #{}]: ReconciliationOk=false (reason {}, backend {}, row {}, tick {}, evidence {}, contract {})."sv,
					context.publishReason,
					context.publishSequence,
					a_report.reconciliation.reasonPartitionOk,
					a_report.reconciliation.backendPartitionOk,
					a_report.reconciliation.rowPartitionOk,
					a_report.reconciliation.tickIdentityOk,
					a_report.reconciliation.rowEvidenceOk,
					a_report.reconciliation.contractOk);
			}

			const auto failures = a_state.calls.GetFailureCount() + a_state.summary.GetFailureCount();
			if (failures)
			{
				REX::ERROR("BA2 profiler [{} #{}]: {} CSV write failures."sv,
					context.publishReason,
					context.publishSequence,
					failures);
			}
		}
	}

	bool ProfilerBA2::Start() noexcept
	{
		using namespace ba2ProfilerDetail;

		if (g_state.load(std::memory_order_acquire))
			return true;

		if (!ProfilerCore::IsEnabledInConfig() || !ProfilerCore::IsBA2TimingEnabled())
			return false;

		if (!ProfilerCore::GetSingleton()->IsActive())
		{
			REX::WARN("BA2 profiler: shared profiler session is inactive; recording was not started."sv);
			return false;
		}

		LARGE_INTEGER frequency{};
		if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
		{
			REX::WARN("BA2 profiler: QueryPerformanceFrequency failed; recording was not started."sv);
			return false;
		}

		RecorderState* state = nullptr;
		try
		{
			const auto shutdownPublishEnabled = ModuleSafeExit::IsEnabledInConfig();
			state = new RecorderState(
				ProfilerCore::GetRuntimeSession(),
				static_cast<std::uint64_t>(frequency.QuadPart),
				ProfilerCore::IsCSVExportEnabled(),
				shutdownPublishEnabled);
		}
		catch (const std::exception& error)
		{
			REX::WARN("BA2 profiler: recorder state creation failed: {}; recording is disabled."sv, error.what());
			return false;
		}
		catch (...)
		{
			REX::WARN("BA2 profiler: recorder state creation failed; recording is disabled."sv);
			return false;
		}

		constexpr auto arenaMiB = (sizeof(CallRecord) * kArenaRowCapacity * kBankCount) / (1024 * 1024);
		if (state->exportCSV)
		{
			for (auto& arena : state->arenas)
			{
				arena.rows = new (std::nothrow) CallRecord[kArenaRowCapacity];
				if (!arena.rows)
				{
					DestroyState(state);
					REX::WARN(
						"BA2 profiler: {} MiB of row arenas could not be allocated; BA2 recording is disabled and the rest of the profiler continues."sv,
						arenaMiB);
					return false;
				}
			}
		}

		state->intervalStartMonotonicUs = state->session.Capture().monotonicUs;
		RecorderState* expected{ nullptr };
		if (!g_state.compare_exchange_strong(
				expected,
				state,
				std::memory_order_release,
				std::memory_order_acquire))
		{
			DestroyState(state);
			return true;
		}

		if (state->exportCSV)
		{
			REX::INFO(
				"BA2 profiler: recording with {} leased shards, one spill shard, and {} rows per arena ({} MiB of row storage)."sv,
				kLeasedShardCount,
				kArenaRowCapacity,
				arenaMiB);
		}
		else
		{
			REX::INFO(
				"BA2 profiler: aggregate recording with {} leased shards and one spill shard; per-call row export is disabled."sv,
				kLeasedShardCount);
		}

		if (!state->shutdownPublishEnabled)
		{
			REX::WARN(
				"BA2 profiler: bSafeExit is disabled; the final interval cannot be published at shutdown and output may end at the last lifecycle boundary."sv);
		}
		return true;
	}

	bool ProfilerBA2::IsRecording() const noexcept
	{
		return ba2ProfilerDetail::g_state.load(std::memory_order_acquire) != nullptr;
	}

	void ProfilerBA2::Record(const BA2Profile::CallObservation& a_observation) noexcept
	{
		using namespace ba2ProfilerDetail;

		auto* state = g_state.load(std::memory_order_acquire);
		if (!state)
			return;

		const auto check = ValidateObservation(a_observation, state->qpcFrequency);
		RuntimeRowMetadata metadata{};
		if (state->exportCSV)
			metadata = state->session.Capture();
		auto& shard = LeaseShard(*state);

		std::lock_guard lock(shard.lock);
		if (!state->accepting.load(std::memory_order_acquire))
			return;
		const auto bankIndex = shard.activeBank;
		auto& bank = shard.banks[bankIndex];
		if (g_threadLease.shardIndex == kSpillShardIndex &&
			g_threadLease.countedGeneration != shard.generation)
		{
			++bank.aggregate.overflowedThreads;
			g_threadLease.countedGeneration = shard.generation;
		}
		auto* row = state->exportCSV ? ReserveRow(state->arenas[bankIndex], bank) : nullptr;
		if (row)
		{
			*row = MakeCallRecord(
				a_observation,
				check,
				shard.index,
				shard.sequence,
				static_cast<std::uint32_t>(metadata.saveLoadEpoch),
				metadata.monotonicUs);
		}
		++shard.sequence;
		bank.aggregate.Account(a_observation, check, state->exportCSV, row != nullptr);
	}

	void ProfilerBA2::Publish(std::string_view a_reason, bool a_closeAdmission) noexcept
	{
		using namespace ba2ProfilerDetail;

		auto* state = g_state.load(std::memory_order_acquire);
		if (!state)
			return;

		std::lock_guard publish(state->publishLock);
		if (a_closeAdmission)
			state->accepting.store(false, std::memory_order_release);

		std::array<ShardRows, kShardCount> rows{};
		std::array<ShardAggregate, kShardCount> aggregates{};
		for (std::size_t index = 0; index < kShardCount; ++index)
		{
			auto& shard = state->shards[index];
			std::lock_guard lock(shard.lock);
			const auto bankIndex = shard.activeBank;
			const auto& bank = shard.banks[bankIndex];
			rows[index] = { bank.firstChunk, bank.rowsInCurrentChunk, bankIndex };
			aggregates[index] = bank.aggregate;
			shard.activeBank = bankIndex ^ 1u;
			++shard.generation;
		}

		const auto metadata = state->session.Capture();
		const auto leased = state->activeDedicatedLeases.load(std::memory_order_acquire);
		IntervalReport report;
		report.context = {
			metadata.sessionID,
			a_reason,
			state->qpcFrequency,
			state->publishSequence,
			metadata.saveLoadEpoch,
			state->intervalStartMonotonicUs,
			metadata.monotonicUs,
			state->shutdownPublishEnabled,
			a_closeAdmission
		};
		report.totals = MergeShards(aggregates);
		report.reconciliation = Reconcile(report.totals);
		report.leasedShards = leased < kLeasedShardCount ? leased : kLeasedShardCount;
		report.overflowedThreads = report.totals.overflowedThreads;
		report.spillCalls = aggregates[kSpillShardIndex].callsSeen;

		if (state->exportCSV)
		{
			bool callsOutputOk = true;
			std::array<RowEvidence, kShardCount> shardEvidence{};
			if (report.totals.callsSeen)
			{
				const FileContext fileContext{
					metadata.sessionID,
					state->qpcFrequency,
					state->publishSequence,
					a_reason,
					state->shutdownPublishEnabled,
					a_closeAdmission
				};
				if (auto* file = state->calls.Begin())
				{
					const auto failuresBefore = state->calls.GetFailureCount();
					RowEvidence evidence;
					for (std::size_t index = 0; index < rows.size(); ++index)
					{
						shardEvidence[index] = SerializeShardRows(
							*state,
							*file,
							fileContext,
							rows[index],
							static_cast<std::uint16_t>(index));
						evidence.Merge(shardEvidence[index]);
					}
					state->calls.End();
					callsOutputOk = state->calls.GetFailureCount() == failuresBefore;
					report.reconciliation = Reconcile(report.totals, &evidence);
					if (!report.reconciliation.rowEvidenceOk)
					{
						callsOutputOk = false;
						REX::ERROR(
							"BA2 profiler: serialized rows disagree with admitted aggregates (rows {}/{}, ticks {}/{})."sv,
							evidence.rowsSeen,
							report.totals.rowsWritten,
							evidence.totalQpc,
							report.totals.rowTotalQpc);
					}
				}
				else
				{
					callsOutputOk = false;
					report.reconciliation.rowEvidenceOk = false;
					REX::ERROR("BA2 profiler: calls CSV unavailable; {} write failures so far."sv,
						state->calls.GetFailureCount());
				}
			}

			if (!callsOutputOk)
				report.reconciliation.contractOk = false;
			if (report.totals.callsSeen || a_closeAdmission)
			{
				std::array<RowEvidence, kShardCount> noEvidence{};
				WriteSummary(
					*state,
					report,
					aggregates,
					report.totals.callsSeen && callsOutputOk ?
						std::span<const RowEvidence>{ shardEvidence } :
						std::span<const RowEvidence>{ noEvidence });
			}
		}

		if (report.totals.callsSeen || a_closeAdmission)
			LogInterval(*state, report);

		for (std::size_t index = 0; index < kShardCount; ++index)
			ResetBank(state->shards[index], rows[index].bank);
		state->arenas[rows[0].bank].nextChunk.store(0, std::memory_order_relaxed);

		state->intervalStartMonotonicUs = metadata.monotonicUs;
		++state->publishSequence;
	}

	void ProfilerBA2::Publish(std::string_view a_reason) noexcept
	{
		Publish(a_reason, false);
	}

	void ProfilerBA2::PublishFinal(std::string_view a_reason) noexcept
	{
		Publish(a_reason, true);
	}

}
