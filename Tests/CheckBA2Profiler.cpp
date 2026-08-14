#include "../Addictol/Include/AdProfilerBA2Rows.h"
#include "../Addictol/Include/AdProfilerBA2Schema.h"
#include "Harness.h"

#include <array>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace vmm_tests
{
	namespace
	{
		using namespace Addictol::BA2Profile;

		[[nodiscard]] std::vector<std::string> split_columns(std::string_view line)
		{
			std::vector<std::string> columns;
			size_t start = 0;
			while (true)
			{
				const auto comma = line.find(',', start);
				if (comma == std::string_view::npos)
				{
					columns.emplace_back(line.substr(start));
					break;
				}
				columns.emplace_back(line.substr(start, comma - start));
				start = comma + 1;
			}
			return columns;
		}

		[[nodiscard]] std::vector<std::string> data_lines(const std::string& text)
		{
			std::vector<std::string> lines;
			std::istringstream stream(text);
			std::string line;
			while (std::getline(stream, line))
			{
				if (!line.empty() && line.front() != '#')
					lines.push_back(line);
			}
			return lines;
		}

		[[nodiscard]] CallObservation served_call(
			BackendId served,
			FallbackReasonId reason,
			uint64_t ticks)
		{
			CallObservation observation;
			observation.primaryBackendId = kBackendLibDeflate;
			observation.primaryAttempted = reason != kReasonState;
			observation.primaryQpc = observation.primaryAttempted ? ticks / 2 : 0;
			observation.servedBackendId = served;
			observation.fallbackReasonId = reason;
			observation.qpcFrequency = 10000000;
			observation.observationSiteId = kSiteInflate;
			if (reason != kReasonNone)
			{
				observation.fallbackBackendId = kBackendStockZlib;
				observation.fallbackQpc = ticks - observation.primaryQpc;
			}
			observation.totalQpc = ticks;
			observation.inputBytesAvailable = 4096;
			observation.outputBytesAvailable = 65536;
			observation.inputBytesConsumed = 4096;
			observation.outputBytesProduced = 65536;
			observation.zlibResult = 1;
			return observation;
		}

		[[nodiscard]] CallObservation chunk_call(
			uint16_t chunk,
			uint16_t count,
			uint64_t sequence,
			bool leader)
		{
			CallObservation observation;
			observation.primaryBackendId = kBackendLibDeflate;
			observation.servedBackendId = kBackendLibDeflate;
			observation.primaryAttempted = true;
			observation.primaryQpc = 100;
			observation.totalQpc = 100;
			observation.qpcFrequency = 10000000;
			observation.observationSiteId = kSiteTextureChunk;
			observation.callerId = kCallerStreamingTexture;
			observation.threadId = 4321;
			observation.requestSequence = sequence;
			observation.streamAddress = 0x1400ull + chunk;
			observation.chunkIndex = chunk;
			observation.chunkCount = count;
			observation.nominalOutputBytes = 65536;
			observation.inputBytesAvailable = 4096;
			observation.outputBytesAvailable = 65536;
			observation.inputBytesConsumed = 4096;
			observation.outputBytesProduced = 65536;
			observation.primaryInputBytesConsumed = 4096;
			observation.primaryOutputBytesProduced = 65536;
			observation.zlibResult = 1;
			observation.evidenceFlags = kEvidenceChunkRow | kEvidenceSizeMeasured;
			if (leader)
			{
				observation.evidenceFlags |= kEvidenceRequestLeader;
				observation.requestWallQpc = 5000;
			}
			return observation;
		}

		void account(
			ShardAggregate& aggregate,
			const CallObservation& observation,
			bool rowWritten,
			bool rowsEnabled = true)
		{
			aggregate.Account(
				observation,
				ValidateObservation(observation, observation.qpcFrequency),
				rowsEnabled,
				rowWritten);
		}

		struct BatchAdmission
		{
			std::vector<CallRecord> storage;
			RowArena arena{};
			BankCursor cursor{};
			ShardAggregate aggregate{};

			BatchAdmission()
			{
				storage.resize(2 * kChunkRows);
				arena.rows = storage.data();
				arena.chunkBudget = storage.size() / kChunkRows;
			}

			// Mirrors ProfilerBA2::RecordBatch: one reservation, then exact per-row admission.
			std::span<CallRecord> Admit(std::span<const CallObservation> observations)
			{
				const auto oversized = observations.size() > kMaxBatchRows;
				if (oversized)
					++aggregate.oversizedBatches;
				const auto rows = oversized ?
					std::span<CallRecord>{} :
					ReserveRows(arena, cursor, observations.size());
				for (size_t index = 0; index < observations.size(); ++index)
				{
					const auto check = ValidateObservation(
						observations[index], observations[index].qpcFrequency);
					const auto written = index < rows.size();
					if (written)
						rows[index] = MakeCallRecord(observations[index], check, 0, index, 0, 0);
					aggregate.Account(observations[index], check, true, written);
				}
				return rows;
			}

			[[nodiscard]] size_t ChunkOf(const CallRecord& record) const
			{
				return static_cast<size_t>(&record - arena.rows) / kChunkRows;
			}

			[[nodiscard]] RowEvidence Evidence() const
			{
				RowEvidence evidence;
				ForEachRow(arena, cursor.firstChunk, [&](const CallRecord& record) noexcept {
					evidence.Account(record, 0);
				});
				return evidence;
			}
		};
	}

	void run_ba2_profiler_checks(Runner& runner)
	{
		runner.test("BA2 record layout matches the documented memory budget", [] {
			require(sizeof(CallRecord) == 136, "CallRecord size changed");
			require(alignof(CallRecord) == 8, "CallRecord alignment changed");
			require(sizeof(CallRecord) * kArenaRowCapacity == 34ull * 1024 * 1024,
				"arena row budget changed");
			require(kSchemaVersion == 3, "schema version changed");
		});

		runner.test("BA2 enum and label tables stay pinned", [] {
			require(kBackendNames.size() == kKnownBackendCount, "backend label table size changed");
			require(kReasonNames.size() == kKnownReasonCount, "reason label table size changed");
			require(kSiteNames.size() == kKnownSiteCount, "site label table size changed");
			require(kCallerNames.size() == kKnownCallerCount, "caller label table size changed");
			require(kKnownBackendCount == 3, "known backend count changed");
			require(kKnownReasonCount == 8, "known reason count changed");
			require(kKnownSiteCount == 3 && kKnownCallerCount == 3, "site or caller count changed");
			require(kBackendNone == 0 && kBackendStockZlib == 1 && kBackendLibDeflate == 2,
				"backend id registry changed");
			require(kReasonNone == 0 && kReasonState == 1 && kReasonAllocation == 2 &&
				kReasonDecode == 3 && kReasonCommit == 4 && kReasonCapacity == 5 &&
				kReasonSizeMismatch == 6 && kReasonRequestRestart == 7,
				"fallback reason ids changed");
			require(kSiteNone == 0 && kSiteInflate == 1 && kSiteTextureChunk == 2,
				"observation site ids changed");
			require(kCallerNone == 0 && kCallerStreamingTexture == 1 && kCallerArraySlice == 2,
				"caller ids changed");
			require(kReasonNames[kReasonState] == "State", "reason label mismatch");
			require(kReasonNames[kReasonSizeMismatch] == "SizeMismatch", "reason label mismatch");
			require(kSiteNames[kSiteTextureChunk] == "TextureChunk", "site label mismatch");
			require(kCallerNames[kCallerArraySlice] == "ArraySlice", "caller label mismatch");
			require(BackendName(7) == "Unknown", "unknown backend must not borrow a known label");
			require(ReasonName(9) == "Unknown", "unknown reason must not borrow a known label");
			require(SiteName(9) == "Unknown", "unknown site must not borrow a known label");
			require(CallerName(9) == "Unknown", "unknown caller must not borrow a known label");
			require(kBackendTableCapacity == 8, "backend table capacity changed");
			require(kOutputSizeBucketNames.size() == kOutputSizeBucketCount, "size bucket labels changed");
			require(
				OutputSizeBucket(0) == 0 &&
					OutputSizeBucket(255) == 1 &&
					OutputSizeBucket(256) == 2 &&
					OutputSizeBucket(262143) == 6 &&
					OutputSizeBucket(262144) == 7,
				"output-size bucket boundaries changed");
		});

		runner.test("BA2 exact partitions reconcile", [] {
			ShardAggregate first;
			ShardAggregate second;
			account(first, served_call(kBackendLibDeflate, kReasonNone, 100), true);
			account(first, served_call(kBackendStockZlib, kReasonDecode, 200), true);
			account(second, served_call(kBackendStockZlib, kReasonState, 300), true);
			account(second, served_call(kBackendLibDeflate, kReasonNone, 400), true);

			const std::array shards{ first, second };
			const auto totals = MergeShards(shards);
			const auto result = Reconcile(totals);
			require(totals.callsSeen == 4, "merged call count wrong");
			require(totals.totalQpc == 1000, "merged tick total wrong");
			require(totals.rowTotalQpc == 1000, "row tick total wrong");
			require(totals.reasonCounts[kReasonNone] == 2, "reason partition wrong");
			require(totals.reasonCounts[kReasonDecode] == 1, "reason partition wrong");
			require(totals.reasonCounts[kReasonState] == 1, "reason partition wrong");
			require(totals.reasonPrimaryQpc[kReasonDecode] == 100, "reason primary ticks wrong");
			require(totals.reasonFallbackQpc[kReasonState] == 300, "reason fallback ticks wrong");
			require(totals.reasonTotalQpc[kReasonNone] == 500, "reason total ticks wrong");
			require(totals.inputBytesConsumed == 4 * 4096, "input byte total wrong");
			require(totals.outputBytesProduced == 4 * 65536, "output byte total wrong");
			require(totals.backends.ServedTotal() == 4, "served backend partition wrong");
			const auto* libdeflate = totals.backends.Find(kBackendLibDeflate);
			const auto* stock = totals.backends.Find(kBackendStockZlib);
			require(libdeflate && libdeflate->primaryQpc == 350,
				"primary backend ticks were not aggregated");
			require(stock && stock->fallbackQpc == 400,
				"fallback backend ticks were not aggregated");
			require(stock && stock->servedBucketCalls[6] == 2 &&
				stock->servedBucketQpc[6] == 400 &&
				stock->servedBucketBytes[6] == 2 * 65536,
				"served output-size bucket was not aggregated");
			require(result.Ok(), "valid interval failed reconciliation");
			require(!result.rowsTruncated, "valid interval reported truncation");
		});

		runner.test("BA2 reconciliation rejects a missing reason count", [] {
			ShardAggregate aggregate;
			account(aggregate, served_call(kBackendLibDeflate, kReasonNone, 100), true);
			account(aggregate, served_call(kBackendStockZlib, kReasonCommit, 100), true);
			require(Reconcile(aggregate).Ok(), "control interval failed reconciliation");

			aggregate.reasonCounts[kReasonCommit] = 0;
			const auto result = Reconcile(aggregate);
			require(!result.reasonPartitionOk, "missing reason count was not detected");
			require(!result.Ok(), "missing reason count did not invalidate the interval");
			require(result.backendPartitionOk, "unrelated partition was invalidated");
		});

		runner.test("BA2 reconciliation rejects a mismatched served backend count", [] {
			ShardAggregate aggregate;
			account(aggregate, served_call(kBackendLibDeflate, kReasonNone, 10), true);
			account(aggregate, served_call(kBackendNone, kReasonNone, 10), true);
			require(aggregate.unservedCalls == 1, "unserved call was not partitioned");
			require(Reconcile(aggregate).Ok(), "control interval failed reconciliation");

			aggregate.unservedCalls = 0;
			const auto result = Reconcile(aggregate);
			require(!result.backendPartitionOk, "mismatched backend count was not detected");
			require(!result.Ok(), "mismatched backend count did not invalidate the interval");
		});

		runner.test("BA2 dropped rows keep aggregates authoritative", [] {
			ShardAggregate aggregate;
			account(aggregate, served_call(kBackendLibDeflate, kReasonNone, 100), true);
			account(aggregate, served_call(kBackendLibDeflate, kReasonNone, 250), false);

			const auto result = Reconcile(aggregate);
			require(aggregate.callsSeen == 2, "dropped call was not counted");
			require(aggregate.rowsWritten == 1 && aggregate.rowsDropped == 1, "row partition wrong");
			require(aggregate.totalQpc == 350, "dropped call ticks were lost");
			require(aggregate.rowTotalQpc == 100, "dropped call ticks leaked into the row total");
			require(result.rowsTruncated, "truncation was not disclosed");
			require(result.tickIdentityOk, "tick identity must not apply once rows are dropped");
			require(result.Ok(), "truncation must not invalidate exact aggregates");

			aggregate.rowsDropped = 0;
			const auto broken = Reconcile(aggregate);
			require(!broken.rowPartitionOk, "row partition mismatch was not detected");
			require(!broken.tickIdentityOk, "tick identity was not enforced with zero drops");
			require(!broken.Ok(), "row partition mismatch did not invalidate the interval");

			ShardAggregate disabled;
			account(disabled, served_call(kBackendLibDeflate, kReasonNone, 100), false, false);
			const auto disabledResult = Reconcile(disabled);
			require(disabled.rowsDisabled == 1, "disabled row collection was not counted");
			require(!disabledResult.rowsTruncated, "disabled row collection reported truncation");
			require(disabledResult.Ok(), "disabled row collection invalidated exact aggregates");
		});

		runner.test("BA2 malformed observations are counted and invalidate the contract", [] {
			auto valid = served_call(kBackendLibDeflate, kReasonNone, 100);
			require(ValidateObservation(valid).WellFormed(), "valid observation was rejected");

			auto consumedTooMuch = valid;
			consumedTooMuch.inputBytesConsumed = consumedTooMuch.inputBytesAvailable + 1;
			require(!ValidateObservation(consumedTooMuch).WellFormed(), "byte accounting was not checked");

			auto shortTotal = valid;
			shortTotal.totalQpc = shortTotal.primaryQpc - 1;
			require(!ValidateObservation(shortTotal).WellFormed(), "tick accounting was not checked");

			auto servedByNobody = valid;
			servedByNobody.primaryAttempted = false;
			servedByNobody.primaryQpc = 0;
			require(!ValidateObservation(servedByNobody).WellFormed(), "served identity was not checked");

			auto reasonWithoutFallback = valid;
			reasonWithoutFallback.fallbackReasonId = kReasonDecode;
			require(!ValidateObservation(reasonWithoutFallback).WellFormed(), "reason identity was not checked");

			auto oversizedBytes = valid;
			oversizedBytes.inputBytesAvailable = 0x1'0000'0000ull;
			oversizedBytes.inputBytesConsumed = 0x1'0000'0000ull;
			const auto oversizedCheck = ValidateObservation(oversizedBytes);
			require(!oversizedCheck.WellFormed(), "32-bit byte range was not enforced");
			const auto record = MakeCallRecord(oversizedBytes, oversizedCheck, 0, 0, 0, 0);
			require(record.inputBytesAvailable == 0xFFFFFFFFu, "oversized bytes did not saturate");
			require((record.observationFlags & kFlagByteRangeOverflow) != 0, "record lost the overflow flag");

			ShardAggregate aggregate;
			account(aggregate, consumedTooMuch, true);
			require(aggregate.malformedObservations == 1, "malformed observation was not counted");
			require(!Reconcile(aggregate).contractOk, "malformed observation did not invalidate the contract");

			auto wrongFrequency = valid;
			wrongFrequency.qpcFrequency = 10000001;
			require(!ValidateObservation(wrongFrequency, 10000000).WellFormed(),
				"QPC frequency mismatch was not checked");
		});

		runner.test("BA2 unknown ids stay distinct and only reasons break the contract", [] {
			constexpr BackendId futureBackend{ 7 };
			auto future = served_call(futureBackend, kReasonNone, 500);
			future.primaryBackendId = futureBackend;
			const auto check = ValidateObservation(future);
			require(check.WellFormed(), "an additive backend id must stay well formed");
			require((check.flags & kFlagUnknownBackend) != 0, "unknown backend was not flagged");

			ShardAggregate aggregate;
			account(aggregate, future, true);
			account(aggregate, served_call(kBackendLibDeflate, kReasonNone, 500), true);
			require(aggregate.backends.ServedTotal() == 2, "unknown backend was not partitioned");
			bool foundFuture = false;
			bool foundKnown = false;
			for (const auto& backend : aggregate.backends.entries)
			{
				foundFuture = foundFuture ||
					(backend.id == futureBackend && backend.servedCalls == 1);
				foundKnown = foundKnown ||
					(backend.id == kBackendLibDeflate && backend.servedCalls == 1);
			}
			require(foundFuture, "unknown backend id was folded into another bucket");
			require(foundKnown, "known backend id lost its bucket");
			require(Reconcile(aggregate).Ok(), "an additive backend id must not invalidate the interval");

			auto unknownReason = served_call(kBackendStockZlib, 9, 100);
			const auto reasonCheck = ValidateObservation(unknownReason);
			require((reasonCheck.flags & kFlagUnknownReason) != 0, "unknown reason was not flagged");
			ShardAggregate reasonAggregate;
			account(reasonAggregate, unknownReason, true);
			require(reasonAggregate.unknownReasonCalls == 1, "unknown reason was not counted");
			require(reasonAggregate.firstUnknownReasonId == 9, "unknown reason id was not retained");
			const auto reasonResult = Reconcile(reasonAggregate);
			require(reasonResult.reasonPartitionOk, "unknown reason bucket broke the partition");
			require(!reasonResult.contractOk, "a reason id outside the pinned table must break the contract");
		});

		runner.test("BA2 backend table overflow is counted and loud", [] {
			ShardAggregate aggregate;
			for (BackendId backend = 1; backend <= kBackendTableCapacity + 2; ++backend)
			{
				auto observation = served_call(backend, kReasonNone, 10);
				observation.primaryBackendId = backend;
				account(aggregate, observation, true);
			}

			require(aggregate.callsSeen == kBackendTableCapacity + 2, "call count wrong");
			require(aggregate.backendTableOverflowCalls == 2, "overflow calls were not counted");
			require(aggregate.servedBackendOverflowCalls == 2, "served overflow calls were not counted");
			require(aggregate.backends.ServedTotal() == kBackendTableCapacity, "table exceeded its capacity");
			const auto result = Reconcile(aggregate);
			require(result.backendPartitionOk, "served overflow calls did not preserve the backend partition");
			require(!result.contractOk, "overflow did not invalidate the contract");
			require(result.rowPartitionOk, "overflow must not disturb the row partition");
			require(aggregate.totalQpc == 10ull * (kBackendTableCapacity + 2), "overflow lost exact ticks");
		});

		runner.test("BA2 row evidence detects admitted-row corruption", [] {
			const auto observation = served_call(kBackendLibDeflate, kReasonNone, 500);
			const auto check = ValidateObservation(observation, observation.qpcFrequency);
			ShardAggregate aggregate;
			aggregate.Account(observation, check, true, true);

			auto record = MakeCallRecord(observation, check, 0, 1, 2, 3);
			RowEvidence control;
			control.Account(record, 0);
			require(Reconcile(aggregate, &control).Ok(), "uncorrupted row evidence failed reconciliation");

			++record.totalQpc;
			RowEvidence corrupted;
			corrupted.Account(record, 0);
			const auto result = Reconcile(aggregate, &corrupted);
			require(!result.rowEvidenceOk, "corrupted row ticks matched admission aggregates");
			require(!result.Ok(), "corrupted admitted row did not fail reconciliation");

			record = MakeCallRecord(observation, check, 1, 1, 2, 3);
			RowEvidence wrongShard;
			wrongShard.Account(record, 0);
			require(!Reconcile(aggregate, &wrongShard).rowEvidenceOk,
				"row attributed to the wrong shard passed reconciliation");
		});

		runner.test("BA2 call rows serialize raw integers at full precision", [] {
			CallObservation observation;
			observation.primaryBackendId = 4294967295u;
			observation.primaryAttempted = true;
			observation.primaryQpc = 18446744073709551615ull;
			observation.fallbackBackendId = kBackendStockZlib;
			observation.fallbackReasonId = kReasonCommit;
			observation.fallbackQpc = 0;
			observation.servedBackendId = kBackendStockZlib;
			observation.totalQpc = 18446744073709551615ull;
			observation.qpcFrequency = 10000000;
			observation.inputBytesAvailable = 4294967295u;
			observation.outputBytesAvailable = 4294967295u;
			observation.inputBytesConsumed = 4294967295u;
			observation.outputBytesProduced = 4294967295u;
			observation.observationSiteId = kSiteInflate;
			observation.zlibResult = -2;

			const auto record = MakeCallRecord(
				observation, ValidateObservation(observation), 64, 1234567890123456789ull, 7, 99);

			std::ostringstream stream;
			WriteCallsHeader(stream);
			WriteCallRow(
				stream,
				FileContext{ "SESSION", 10000000, 3, "PostLoadGame", true, false },
				record);

			const auto lines = data_lines(stream.str());
			require(lines.size() == 2, "expected a header row and one data row");
			const auto header = split_columns(lines[0]);
			const auto row = split_columns(lines[1]);
			require(header.size() == kCallsColumnCount, "calls header column count changed");
			require(row.size() == kCallsColumnCount, "calls row column count changed");
			require(header[0] == "SchemaVersion" && header.back() == "AdmissionClosed",
				"calls column order changed");
			require(row[1] == "SESSION", "session id was not written");
			require(row[2] == "10000000", "qpc frequency was not written");
			require(row[3] == "3" && row[4] == "PostLoadGame", "publish identity was not written");
			require(row[7] == "64", "shard index was not written");
			require(row[8] == "1234567890123456789", "shard sequence lost precision");
			require(row[9] == "1", "observation site was not written");
			require(row[16] == "4294967295", "raw primary backend id was rewritten");
			require(row[18] == "18446744073709551615", "primary ticks lost precision");
			require(row[23] == "-2", "raw zlib result was rewritten");
			require(row[24] == "18446744073709551615", "total ticks lost precision");
			require(row[29] == "4294967295", "output bytes lost precision");
			require(row[35] == "1", "shutdown publish setting was not written");
			require(row[36] == "0", "normal publish closed admission");
			require(stream.str().find('.') == std::string::npos, "a row emitted a decimal duration");
		});

		runner.test("BA2 chunk rows carry request and size evidence", [] {
			const auto observation = chunk_call(3, 8, 42, true);
			const auto record = MakeCallRecord(
				observation, ValidateObservation(observation, observation.qpcFrequency), 1, 2, 3, 4);

			std::ostringstream stream;
			WriteCallRow(
				stream,
				FileContext{ "SESSION", 10000000, 0, "Interval", false, false },
				record);

			const auto row = split_columns(data_lines(stream.str())[0]);
			require(row.size() == kCallsColumnCount, "chunk row column count changed");
			require(row[9] == "2" && row[10] == "1", "chunk row lost its site or caller identity");
			require(row[11] == "4321" && row[12] == "42", "chunk row lost its request identity");
			require(row[13] == std::to_string(0x1400ull + 3), "chunk row lost the stream address");
			require(row[14] == "3" && row[15] == "8", "chunk row lost its chunk identity");
			require(row[25] == "5000", "leader row lost the request wall clock");
			require(row[30] == "4096" && row[31] == "65536", "primary byte columns were not written");
			require(row[32] == "65536", "nominal output bytes were not written");
			require(
				row[34] == std::to_string(
					kEvidenceChunkRow | kEvidenceSizeMeasured | kEvidenceRequestLeader),
				"evidence flags were not written");
		});

		runner.test("BA2 summary rows share one fixed width", [] {
			ShardAggregate aggregate;
			account(aggregate, served_call(kBackendLibDeflate, kReasonNone, 100), true);
			account(aggregate, served_call(kBackendStockZlib, kReasonDecode, 100), false);

			const SummaryContext context{
				"SESSION", "PostLoadGame", 10000000, 3, 2, 10, 20, true, false
			};
			const auto reconciliation = Reconcile(aggregate);

			std::ostringstream stream;
			WriteSummaryHeader(stream);
			SummaryRow interval;
			interval.scope = "Interval";
			interval.scopeLabel = "All";
			interval.callsSeen = aggregate.callsSeen;
			interval.rowsWritten = aggregate.rowsWritten;
			interval.rowsDropped = aggregate.rowsDropped;
			interval.totalQpc = aggregate.totalQpc;
			interval.rowTotalQpc = aggregate.rowTotalQpc;
			interval.reconciliation = reconciliation;
			WriteSummaryRow(stream, context, interval);

			SummaryRow shard;
			shard.scope = "Shard";
			shard.scopeLabel = "Spill";
			shard.scopeID = 64;
			shard.callsSeen = aggregate.callsSeen;
			shard.spillCalls = aggregate.callsSeen;
			shard.reconciliation = reconciliation;
			WriteSummaryRow(stream, context, shard);

			SummaryRow backend;
			backend.scope = "Backend";
			backend.scopeLabel = BackendName(kBackendStockZlib);
			backend.scopeID = kBackendStockZlib;
			backend.callsSeen = 1;
			backend.selectedCalls = 2;
			backend.primaryCalls = 3;
			backend.primaryQpc = 4;
			backend.fallbackCalls = 5;
			backend.fallbackQpc = 6;
			backend.servedCalls = 7;
			backend.servedQpc = 8;
			backend.reconciliation = reconciliation;
			WriteSummaryRow(stream, context, backend);

			const auto lines = data_lines(stream.str());
			require(lines.size() == 4, "expected a header row and three scope rows");
			const auto header = split_columns(lines[0]);
			require(header.size() == kSummaryColumnCount, "summary header column count changed");
			for (size_t index = 1; index < lines.size(); ++index)
				require(split_columns(lines[index]).size() == kSummaryColumnCount,
					"a summary row was ragged");

			const auto intervalRow = split_columns(lines[1]);
			require(intervalRow[8] == "Interval" && intervalRow[10] == "All", "interval scope mislabeled");
			require(intervalRow[13] == "1", "shutdown publish setting was not published");
			require(intervalRow[14] == "0", "normal summary closed admission");
			require(intervalRow[15] == "2" && intervalRow[16] == "1" && intervalRow[17] == "1",
				"interval counts wrong");
			require(intervalRow[18] == "200" && intervalRow[19] == "100", "interval ticks wrong");
			require(intervalRow[50] == "1", "RowsTruncated was not published");
			require(intervalRow[51] == "1", "ReconciliationOk was not published");
			require(split_columns(lines[2])[9] == "64", "shard scope id wrong");
			const auto backendRow = split_columns(lines[3]);
			require(backendRow[10] == "StockZlib", "backend scope label wrong");
			require(
				backendRow[22] == "2" && backendRow[23] == "3" && backendRow[24] == "4" &&
					backendRow[25] == "5" && backendRow[26] == "6" &&
					backendRow[27] == "7" && backendRow[28] == "8",
				"backend aggregate fields shifted or lost precision");
			for (size_t index = 1; index < lines.size(); ++index)
				require(split_columns(lines[index])[51] == "1",
					"healthy summary scope reported ReconciliationOk=false");
		});

		runner.test("BA2 batch admission keeps one request contiguous in one bank", [] {
			BatchAdmission admission;
			std::vector<CallObservation> filler;
			for (uint16_t chunk = 0; chunk < 100; ++chunk)
				filler.push_back(chunk_call(chunk, 100, 6, chunk == 0));
			std::vector<CallObservation> request;
			for (uint16_t chunk = 0; chunk < 200; ++chunk)
				request.push_back(chunk_call(chunk, 200, 7, chunk == 0));

			admission.Admit(filler);
			const auto rows = admission.Admit(request);
			require(rows.size() == request.size(), "the request did not get one contiguous block");
			require(
				admission.ChunkOf(rows.front()) == admission.ChunkOf(rows.back()),
				"a request straddled two arena chunks");
			require(
				admission.ChunkOf(rows.front()) == 1 && admission.arena.nextChunk.load() == 2,
				"the request did not start a fresh chunk when the current one was too short");
			require(admission.aggregate.requests.chunkRows == 300, "chunk rows were not counted");
			require(admission.aggregate.requests.leaderRows == 2, "leader rows were not counted");

			const auto evidence = admission.Evidence();
			require(evidence.rowsSeen == 300, "serialized rows disagreed with admission");
			require(Reconcile(admission.aggregate, &evidence).Ok(),
				"a healthy batch failed reconciliation");

			BatchAdmission bounded;
			std::vector<CallObservation> full;
			for (size_t index = 0; index < kMaxBatchRows; ++index)
				full.push_back(chunk_call(static_cast<uint16_t>(index), 256, 9, index == 0));
			const auto boundedRows = bounded.Admit(full);
			require(boundedRows.size() == kMaxBatchRows, "a full batch was not admitted contiguously");
			require(bounded.arena.nextChunk.load() == 1, "a full batch used more than one chunk");
			require(bounded.aggregate.oversizedBatches == 0, "a full batch was called oversized");

			BatchAdmission rejected;
			full.push_back(chunk_call(0, 257, 11, false));
			require(rejected.Admit(full).empty(), "an over-long batch reserved rows");
			require(rejected.aggregate.oversizedBatches == 1, "an over-long batch was not disclosed");
			require(rejected.aggregate.callsSeen == full.size(), "an over-long batch lost aggregates");
			require(rejected.aggregate.rowsDropped == full.size(), "dropped rows were not counted");
			const auto rejectedResult = Reconcile(rejected.aggregate);
			require(rejectedResult.rowPartitionOk, "an over-long batch broke the row partition");
			require(!rejectedResult.contractOk, "an over-long batch did not break the contract");
		});

		runner.test("BA2 reconciliation rejects post-admission row identity edits", [] {
			BatchAdmission admission;
			std::vector<CallObservation> request;
			for (uint16_t chunk = 0; chunk < 4; ++chunk)
				request.push_back(chunk_call(chunk, 4, 21, chunk == 0));
			auto rows = admission.Admit(request);
			const auto control = admission.Evidence();
			require(Reconcile(admission.aggregate, &control).Ok(),
				"unmutated persisted rows failed reconciliation");

			const auto rejects = [&](const char* a_what) {
				const auto evidence = admission.Evidence();
				const auto result = Reconcile(admission.aggregate, &evidence);
				require(!result.rowEvidenceOk, a_what);
				require(!result.Ok(), a_what);
			};

			const auto site = rows[1].observationSiteId;
			rows[1].observationSiteId = kSiteInflate;
			rejects("a rewritten observation site passed reconciliation");
			rows[1].observationSiteId = site;

			const auto caller = rows[1].callerId;
			rows[1].callerId = kCallerArraySlice;
			rejects("a rewritten caller id passed reconciliation");
			rows[1].callerId = caller;

			++rows[2].requestSequence;
			rejects("a rewritten request sequence passed reconciliation");
			--rows[2].requestSequence;

			++rows[2].chunkIndex;
			rejects("a rewritten chunk index passed reconciliation");
			--rows[2].chunkIndex;

			++rows[3].chunkCount;
			rejects("a rewritten chunk count passed reconciliation");
			--rows[3].chunkCount;

			rows[0].requestWallQpc += 7;
			rejects("a rewritten request wall clock passed reconciliation");
			rows[0].requestWallQpc -= 7;

			++rows[0].threadId;
			rejects("a rewritten thread id passed reconciliation");
			--rows[0].threadId;

			rows[1].evidenceFlags |= kEvidenceRequestLeader;
			rejects("a forged leader row passed reconciliation");
			rows[1].evidenceFlags &= static_cast<uint8_t>(~kEvidenceRequestLeader);

			rows[1].primaryOutputBytesProduced += 1;
			rejects("a rewritten decoded size passed reconciliation");
			rows[1].primaryOutputBytesProduced -= 1;

			const auto restored = admission.Evidence();
			require(Reconcile(admission.aggregate, &restored).Ok(),
				"restoring the rows did not restore reconciliation");
		});

		runner.test("BA2 size evidence is derived from persisted sizes, not the row flag", [] {
			ShardAggregate exact;
			account(exact, chunk_call(0, 2, 5, true), true);
			require(exact.requests.sizeDeltaSamples == 1, "a measured chunk was not sampled");
			require(exact.requests.sizeMismatchChunks == 0, "an exact chunk was called a mismatch");
			require(exact.requests.minSizeDelta == 0 && exact.requests.maxSizeDelta == 0,
				"an exact chunk moved the signed extremes");

			auto longDecode = chunk_call(1, 2, 5, false);
			longDecode.primaryOutputBytesProduced += 6;
			longDecode.outputBytesProduced += 6;
			longDecode.evidenceFlags |= kEvidenceSizeMismatch;
			account(exact, longDecode, true);
			require(exact.requests.sizeMismatchChunks == 1, "a longer decode was not counted");
			require(exact.requests.minSizeDelta == -6 && exact.requests.maxSizeDelta == -6,
				"a decode longer than fullSize must read as a negative delta");

			ShardAggregate forged;
			auto liar = chunk_call(0, 2, 7, true);
			liar.evidenceFlags |= kEvidenceSizeMismatch;
			account(forged, liar, true);
			require(forged.requests.sizeMismatchChunks == 0,
				"a row flag alone must not create a size mismatch");

			ShardAggregate shortDecode;
			auto shrunk = chunk_call(0, 2, 9, true);
			shrunk.primaryOutputBytesProduced -= 2;
			account(shortDecode, shrunk, true);
			require(shortDecode.requests.minSizeDelta == 2 && shortDecode.requests.maxSizeDelta == 2,
				"padded fullSize must read as a positive delta");

			ShardAggregate merged{ exact };
			merged.Merge(shortDecode);
			require(merged.requests.sizeMismatchChunks == 2, "merged mismatch counts were lost");
			require(merged.requests.minSizeDelta == -6 && merged.requests.maxSizeDelta == 2,
				"merging did not keep the widest signed extremes");
			require(merged.requests.sizeDeltaSamples == 3, "merged measured chunks were lost");
		});

		runner.test("BA2 site and caller partitions are exact", [] {
			ShardAggregate aggregate;
			account(aggregate, chunk_call(0, 2, 5, true), true);
			account(aggregate, chunk_call(1, 2, 5, false), true);
			account(aggregate, served_call(kBackendStockZlib, kReasonState, 100), true);
			require(aggregate.requests.siteCounts[kSiteTextureChunk] == 2, "texture site was not counted");
			require(aggregate.requests.siteCounts[kSiteInflate] == 1, "inflate site was not counted");
			require(aggregate.requests.callerCounts[kCallerStreamingTexture] == 2,
				"caller partition was not counted");
			require(aggregate.requests.callerCounts[kCallerNone] == 1,
				"an unattributed call left the caller partition");
			require(Reconcile(aggregate).Ok(), "an exact site partition failed reconciliation");

			aggregate.requests.siteCounts[kSiteTextureChunk] = 1;
			const auto broken = Reconcile(aggregate);
			require(!broken.sitePartitionOk, "a missing site count was not detected");
			require(!broken.requestEvidenceOk, "chunk rows must match the texture site count");
			require(!broken.Ok(), "a broken site partition did not invalidate the interval");
		});

		runner.test("BA2 chunk identity is required on chunk rows", [] {
			auto leaderWithoutChunk = served_call(kBackendLibDeflate, kReasonNone, 100);
			leaderWithoutChunk.evidenceFlags = kEvidenceRequestLeader;
			require(
				(ValidateObservation(leaderWithoutChunk).flags & kFlagRequestIdentity) != 0,
				"a leader row without a chunk row was accepted");

			auto wallWithoutLeader = served_call(kBackendLibDeflate, kReasonNone, 100);
			wallWithoutLeader.requestWallQpc = 10;
			require(
				(ValidateObservation(wallWithoutLeader).flags & kFlagRequestIdentity) != 0,
				"a non-leader row carrying the request wall clock was accepted");

			auto shortWall = chunk_call(0, 2, 3, true);
			shortWall.requestWallQpc = shortWall.totalQpc - 1;
			require(
				(ValidateObservation(shortWall).flags & kFlagTickAccounting) != 0,
				"a request wall shorter than its own codec time was accepted");

			auto strayChunk = served_call(kBackendLibDeflate, kReasonNone, 100);
			strayChunk.chunkCount = 4;
			require(
				(ValidateObservation(strayChunk).flags & kFlagChunkIdentity) != 0,
				"a non-chunk row carrying chunk identity was accepted");

			auto outOfRange = chunk_call(4, 4, 9, false);
			require(
				(ValidateObservation(outOfRange).flags & kFlagChunkIdentity) != 0,
				"a chunk index outside the chunk count was accepted");

			auto anonymous = chunk_call(1, 4, 0, false);
			require(
				(ValidateObservation(anonymous).flags & kFlagChunkIdentity) != 0,
				"a chunk row without a request sequence was accepted");

			auto unattempted = chunk_call(1, 4, 9, false);
			unattempted.primaryAttempted = false;
			unattempted.primaryQpc = 0;
			unattempted.servedBackendId = kBackendNone;
			require(
				(ValidateObservation(unattempted).flags & kFlagPrimaryBytes) != 0,
				"primary bytes without a primary attempt were accepted");

			auto future = chunk_call(1, 4, 9, false);
			future.observationSiteId = 9;
			future.callerId = 9;
			const auto check = ValidateObservation(future);
			require(check.WellFormed(), "an additive site or caller id must stay well formed");
			require((check.flags & kFlagUnknownSite) != 0, "unknown site was not flagged");
			require((check.flags & kFlagUnknownCaller) != 0, "unknown caller was not flagged");
		});
	}
}
