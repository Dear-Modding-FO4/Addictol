#include "../Addictol/Include/AdProfilerBA2Schema.h"
#include "Harness.h"

#include <array>
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
			std::size_t start = 0;
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
			std::uint64_t ticks)
		{
			CallObservation observation;
			observation.primaryBackendId = kBackendLibDeflate;
			observation.primaryAttempted = reason != kReasonState;
			observation.primaryQpc = observation.primaryAttempted ? ticks / 2 : 0;
			observation.servedBackendId = served;
			observation.fallbackReasonId = reason;
			observation.qpcFrequency = 10000000;
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
	}

	void run_ba2_profiler_checks(Runner& runner)
	{
		runner.test("BA2 record layout matches the documented memory budget", [] {
			require(sizeof(CallRecord) == 88, "CallRecord size changed");
			require(alignof(CallRecord) == 8, "CallRecord alignment changed");
			require(sizeof(CallRecord) * 262144 == 22ull * 1024 * 1024, "arena row budget changed");
			require(kSchemaVersion == 2, "schema version changed");
		});

		runner.test("BA2 enum and label tables stay pinned", [] {
			require(kBackendNames.size() == kKnownBackendCount, "backend label table size changed");
			require(kReasonNames.size() == kKnownReasonCount, "reason label table size changed");
			require(kKnownBackendCount == 3, "known backend count changed");
			require(kKnownReasonCount == 5, "known reason count changed");
			require(kBackendNone == 0 && kBackendStockZlib == 1 && kBackendLibDeflate == 2,
				"backend id registry changed");
			require(kReasonNone == 0 && kReasonState == 1 && kReasonAllocation == 2 &&
				kReasonDecode == 3 && kReasonCommit == 4, "fallback reason ids changed");
			require(kReasonNames[kReasonState] == "State", "reason label mismatch");
			require(BackendName(7) == "Unknown", "unknown backend must not borrow a known label");
			require(ReasonName(9) == "Unknown", "unknown reason must not borrow a known label");
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
			require(row[9] == "4294967295", "raw primary backend id was rewritten");
			require(row[11] == "18446744073709551615", "primary ticks lost precision");
			require(row[16] == "-2", "raw zlib result was rewritten");
			require(row[17] == "18446744073709551615", "total ticks lost precision");
			require(row[21] == "4294967295", "output bytes lost precision");
			require(row[23] == "1", "shutdown publish setting was not written");
			require(row[24] == "0", "normal publish closed admission");
			require(stream.str().find('.') == std::string::npos, "a row emitted a decimal duration");
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
			for (std::size_t index = 1; index < lines.size(); ++index)
				require(split_columns(lines[index]).size() == kSummaryColumnCount,
					"a summary row was ragged");

			const auto intervalRow = split_columns(lines[1]);
			require(intervalRow[8] == "Interval" && intervalRow[10] == "All", "interval scope mislabeled");
			require(intervalRow[13] == "1", "shutdown publish setting was not published");
			require(intervalRow[14] == "0", "normal summary closed admission");
			require(intervalRow[15] == "2" && intervalRow[16] == "1" && intervalRow[17] == "1",
				"interval counts wrong");
			require(intervalRow[18] == "200" && intervalRow[19] == "100", "interval ticks wrong");
			require(intervalRow[37] == "1", "RowsTruncated was not published");
			require(intervalRow[38] == "1", "ReconciliationOk was not published");
			require(split_columns(lines[2])[9] == "64", "shard scope id wrong");
			const auto backendRow = split_columns(lines[3]);
			require(backendRow[10] == "StockZlib", "backend scope label wrong");
			require(
				backendRow[22] == "2" && backendRow[23] == "3" && backendRow[24] == "4" &&
					backendRow[25] == "5" && backendRow[26] == "6" &&
					backendRow[27] == "7" && backendRow[28] == "8",
				"backend aggregate fields shifted or lost precision");
			for (std::size_t index = 1; index < lines.size(); ++index)
				require(split_columns(lines[index])[38] == "1",
					"healthy summary scope reported ReconciliationOk=false");
		});
	}
}
