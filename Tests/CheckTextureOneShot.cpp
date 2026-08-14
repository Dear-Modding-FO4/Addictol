#include "../Addictol/Include/AdTextureOneShot.h"
#include "Harness.h"

#include <array>
#include <utility>
#include <vector>

namespace
{
	using namespace Addictol;
	using namespace Addictol::TextureOneShot;

	constexpr uint8_t chunk_pattern(uint16_t a_chunk, size_t a_offset)
	{
		return static_cast<uint8_t>((a_chunk * 31 + a_offset * 7 + 1) & 0xFF);
	}

	enum class FaultKind
	{
		None,
		ShortInput,
		LongOutput,
		Capacity,
		BadData
	};

	// Writes the same deterministic bytes the engine loop would, so placement is checkable.
	struct FakeCodec
	{
		bool prepared{ true };
		int prepareCalls{ 0 };
		int decodeCalls{ 0 };
		uint16_t faultChunk{ 0xFFFF };
		FaultKind fault{ FaultKind::None };
		std::vector<uint32_t> expectedNominal;
		uint16_t nextChunk{ 0 };
		uint64_t* clock{ nullptr };
		uint64_t decodeTicks{ 0 };

		bool Prepare() noexcept
		{
			++prepareCalls;
			return prepared;
		}

		ZlibExactDecode Decode(
			std::span<const uint8_t> a_input,
			std::span<uint8_t> a_output) noexcept
		{
			++decodeCalls;
			const auto chunk = nextChunk++;
			const auto nominal = expectedNominal[chunk];
			if (clock)
				*clock += decodeTicks;

			if (chunk == faultChunk)
			{
				switch (fault)
				{
				case FaultKind::BadData:
					return { ZLIB_CODEC_BAD_DATA, 0, 0 };
				case FaultKind::Capacity:
					return { ZLIB_CODEC_INSUFFICIENT_SPACE, 0, 0 };
				case FaultKind::ShortInput:
					for (size_t offset = 0; offset < nominal; ++offset)
						a_output[offset] = chunk_pattern(chunk, offset);
					return { ZLIB_CODEC_SUCCESS, a_input.size() - 1, nominal };
				case FaultKind::LongOutput:
					// The real codec never writes past its bound; it reports insufficient space.
					if (a_output.size() < nominal + 1u)
						return { ZLIB_CODEC_INSUFFICIENT_SPACE, 0, 0 };
					for (size_t offset = 0; offset < nominal + 1u; ++offset)
						a_output[offset] = chunk_pattern(chunk, offset);
					return { ZLIB_CODEC_SUCCESS, a_input.size(), nominal + 1u };
				default:
					break;
				}
			}

			for (size_t offset = 0; offset < nominal; ++offset)
				a_output[offset] = chunk_pattern(chunk, offset);
			return { ZLIB_CODEC_SUCCESS, a_input.size(), nominal };
		}
	};

	struct RequestFixture
	{
		static constexpr uint16_t kChunks = 4;

		std::array<TextureStream::ChunkDesc, kChunks> chunks{};
		std::array<uint32_t, kChunks> nominal{};
		std::vector<uint8_t> compressed;
		std::vector<std::byte> destination;
		std::array<uint32_t, 2> engineState{ ZlibInflate::MODE_HEAD, 0 };
		uintptr_t vtable{ 0x140FEED0 };
		TextureStream::Detail detail{};
		TextureStream::ResidentState resident{};
		TextureStream::Stream stream{};
		RequestRows rows{};
		FakeCodec codec{};
		uint64_t clock{ 0 };
		int clockReads{ 0 };
		int fallbackCalls{ 0 };
		bool fallbackResult{ true };
		bool breakAttribution{ false };
		bool replayTimingEnabled{ true };
		uint16_t fallbackFirstServed{ 0 };

		RequestFixture()
		{
			uint32_t inputTotal = 0;
			uint32_t outputTotal = 0;
			for (uint16_t index = 0; index < kChunks; ++index)
			{
				chunks[index].compressedSize = static_cast<uint32_t>(32 + index * 8);
				chunks[index].uncompressedSize = static_cast<uint32_t>(128 + index * 64);
				nominal[index] = chunks[index].uncompressedSize;
				inputTotal += chunks[index].compressedSize;
				outputTotal += nominal[index];
				codec.expectedNominal.push_back(nominal[index]);
			}

			compressed.assign(inputTotal, 0);
			destination.assign(outputTotal, std::byte{ 0 });

			detail.vtable = reinterpret_cast<const void*>(vtable);
			detail.directFlag = TextureStream::DETAIL_DIRECT;
			detail.stream.state = engineState.data();
			resident.compressedBase = compressed.data();
			resident.compressedCursor = compressed.data();
			stream.detail = &detail;
			stream.chunks = chunks.data();
			stream.nominalSizes = nominal.data();
			stream.resident = &resident;
			stream.count = kChunks;
			stream.first = 0;
			stream.last = kChunks - 1;
			stream.index = 99;
			LayoutMembers();
		}

		// The resident buffer holds only the selected members, starting at the base.
		void LayoutMembers() noexcept
		{
			std::fill(compressed.begin(), compressed.end(), uint8_t{ 0 });
			uint32_t offset = 0;
			for (uint32_t index = stream.first; index <= stream.last; ++index)
			{
				compressed[offset] = 0x78;
				compressed[offset + 1] = 0x9C;
				offset += chunks[index].compressedSize;
			}
		}

		void SetFirst(uint32_t a_first) noexcept
		{
			stream.first = a_first;
			LayoutMembers();
		}

		void SetLast(uint32_t a_last) noexcept
		{
			stream.last = a_last;
			LayoutMembers();
		}

		[[nodiscard]] RequestIdentity Identity() const noexcept
		{
			RequestIdentity identity;
			identity.caller = BA2Profile::kCallerStreamingTexture;
			identity.threadId = 77;
			identity.sequence = 5;
			identity.streamAddress = 0xABCD;
			identity.qpcFrequency = 10'000'000;
			return identity;
		}

		// Stands in for the engine loop: it rewrites the whole request from chunk zero.
		bool Replay(ZlibReplayCapture& a_capture) noexcept
		{
			++fallbackCalls;
			replayTimingEnabled = a_capture.timingEnabled;
			stream.index = 0;
			resident.compressedCursor = resident.compressedBase;
			uint32_t outputOffset = 0;
			for (uint16_t index = 0; index < kChunks; ++index)
			{
				if (index >= fallbackFirstServed)
				{
					clock += 3;
					for (size_t offset = 0; offset < nominal[index]; ++offset)
						destination[outputOffset + offset] =
							static_cast<std::byte>(chunk_pattern(index, offset));
					if (breakAttribution)
						stream.index = 0xFFFF;
					a_capture.Account(3, chunks[index].compressedSize, nominal[index], 1);
				}
				outputOffset += nominal[index];
				stream.index = index + 1u;
			}
			return fallbackResult;
		}

		[[nodiscard]] RequestOutcome Run(bool a_recordRows = true, bool a_timing = true)
		{
			const auto preflight = TextureStream::Preflight(&stream, destination.data(), vtable);
			vmm_tests::require(
				static_cast<bool>(preflight), "the fixture request failed preflight");
			codec.clock = &clock;
			return RunRequest(
				stream,
				destination.data(),
				preflight.bounds,
				Identity(),
				codec,
				[this]() noexcept {
					++clockReads;
					return clock += 1;
				},
				a_timing,
				[this](ZlibReplayCapture& a_capture) noexcept { return Replay(a_capture); },
				a_recordRows ? &rows : nullptr);
		}

		[[nodiscard]] bool OutputMatches() const noexcept
		{
			uint32_t outputOffset = 0;
			for (uint16_t index = 0; index < kChunks; ++index)
			{
				for (size_t offset = 0; offset < nominal[index]; ++offset)
				{
					if (destination[outputOffset + offset] !=
						static_cast<std::byte>(chunk_pattern(index, offset)))
						return false;
				}
				outputOffset += nominal[index];
			}
			return true;
		}

		[[nodiscard]] uint32_t InputTotal() const noexcept
		{
			uint32_t total = 0;
			for (uint16_t index = stream.first; index <= stream.last; ++index)
				total += chunks[index].compressedSize;
			return total;
		}
	};
}

namespace vmm_tests
{
	void run_texture_one_shot_checks(Runner& runner)
	{
		using namespace Addictol::BA2Profile;

		runner.test("install state machine never patches twice or without validation", [] {
			require(MayValidate(InstallState::NotAttempted), "a fresh install must validate");
			require(MayValidate(InstallState::Rejected),
				"a rejection happens before any write and must stay retryable");
			require(!MayValidate(InstallState::Validated), "a validated target must not revalidate");
			require(!MayValidate(InstallState::Attempted),
				"validation must not re-enter after a detour attempt");
			require(!MayValidate(InstallState::Installed),
				"validation must not re-enter after a detour attempt");
			require(!MayValidate(InstallState::Indeterminate),
				"validation must not re-enter an indeterminate patch");

			require(MayPatch(InstallState::Validated), "a validated target must be patchable");
			require(!MayPatch(InstallState::NotAttempted), "patching must require validation first");
			require(!MayPatch(InstallState::Rejected), "a rejected target must never be patched");
			require(!MayPatch(InstallState::Attempted), "a detour attempt must not be repeated");
			require(!MayPatch(InstallState::Installed), "an installed detour must not be repeated");
			require(!MayPatch(InstallState::Indeterminate),
				"an indeterminate detour must never be retried");
		});

		runner.test("one-shot decodes every chunk into the caller buffer", [] {
			RequestFixture fixture;
			const auto outcome = fixture.Run();

			require(outcome.served && outcome.oneShot, "a complete decode did not serve the request");
			require(fixture.codec.prepareCalls == 1, "the decompressor was prepared more than once");
			require(fixture.codec.decodeCalls == RequestFixture::kChunks, "a chunk was not decoded");
			require(fixture.fallbackCalls == 0, "a complete decode called the engine loop");
			require(fixture.OutputMatches(), "chunk output landed at the wrong offset");
			require(fixture.stream.index == fixture.stream.last + 1u,
				"the mutable chunk index was not advanced past the request");
			require(
				fixture.resident.compressedCursor ==
					fixture.resident.compressedBase + fixture.InputTotal(),
				"the resident cursor was not advanced by the consumed members");
			require(fixture.detail.stream.total_in == 0 && fixture.detail.stream.total_out == 0,
				"the engine z_stream was touched");
			require(fixture.engineState[0] == ZlibInflate::MODE_HEAD,
				"the engine z_stream left HEAD");
			require(outcome.decodedBytes == 128 + 192 + 256 + 320, "decoded bytes were miscounted");
			require(outcome.evidence.samples == 4 && outcome.evidence.mismatches == 0,
				"size evidence was not sampled");

			const auto& rows = fixture.rows;
			require(rows.count == RequestFixture::kChunks, "one row per chunk was not buffered");
			for (uint16_t index = 0; index < rows.count; ++index)
			{
				const auto& row = rows.rows[index];
				require(row.observationSiteId == kSiteTextureChunk, "chunk row lost its site");
				require(row.callerId == kCallerStreamingTexture, "chunk row lost its caller");
				require(row.chunkIndex == index && row.chunkCount == RequestFixture::kChunks,
					"chunk identity was not recorded");
				require(row.requestSequence == 5 && row.threadId == 77,
					"request identity was not recorded");
				require(row.servedBackendId == kBackendLibDeflate, "libdeflate did not serve the row");
				require(row.fallbackReasonId == kReasonNone, "a served chunk reported a fallback");
				require(row.primaryAttempted && row.primaryQpc, "codec timing was not recorded");
				require(row.totalQpc == row.primaryQpc, "per-chunk ticks must be codec only");
				require(row.nominalOutputBytes == fixture.nominal[index], "nominal bytes were lost");
				require(row.outputBytesProduced == fixture.nominal[index], "served bytes were lost");
				require(
					row.primaryOutputBytesProduced == fixture.nominal[index] &&
						row.primaryInputBytesConsumed == fixture.chunks[index].compressedSize,
					"primary bytes were not separated from served bytes");
				require((row.evidenceFlags & kEvidenceChunkRow) != 0, "chunk evidence was not set");
				require((row.evidenceFlags & kEvidenceSizeMeasured) != 0, "size was not measured");
				require((row.evidenceFlags & kEvidenceSizeMismatch) == 0, "an exact size was flagged");
				require(ValidateObservation(row, 10'000'000).WellFormed(), "a chunk row was malformed");
			}
			require(
				(rows.rows[0].evidenceFlags & kEvidenceRequestLeader) != 0,
				"the first chunk row is the request leader");
			require(rows.rows[0].requestWallQpc >= rows.rows[0].totalQpc,
				"the request wall clock must cover the leader codec time");
			for (uint16_t index = 1; index < rows.count; ++index)
				require(rows.rows[index].requestWallQpc == 0,
					"only the leader row carries the request wall clock");
		});

		runner.test("one-shot decodes a partial mip run from the resident base", [] {
			RequestFixture fixture;
			fixture.SetFirst(2);
			fixture.codec.nextChunk = 2;
			const auto outcome = fixture.Run();

			require(outcome.served && outcome.oneShot, "a partial request was not served");
			require(fixture.codec.decodeCalls == 2, "a partial request decoded the wrong chunk count");
			require(fixture.rows.count == 2, "a partial request buffered the wrong row count");
			require(fixture.rows.rows[0].chunkIndex == 2, "the leader row is the first mip decoded");
			require(
				fixture.resident.compressedCursor ==
					fixture.resident.compressedBase + fixture.InputTotal(),
				"the resident cursor skipped members that were never read");
			const auto skipped = fixture.nominal[0] + fixture.nominal[1];
			for (size_t offset = 0; offset < skipped; ++offset)
				require(fixture.destination[offset] == std::byte{ 0 },
					"a partial request wrote over earlier mips");
			for (size_t offset = 0; offset < fixture.nominal[2]; ++offset)
				require(
					fixture.destination[skipped + offset] ==
						static_cast<std::byte>(chunk_pattern(2, offset)),
					"a partial request wrote its first mip at the wrong offset");
		});

		runner.test("one-shot rejects an inexact decode and replays the whole request", [] {
			const auto run = [](FaultKind a_fault, uint16_t a_chunk) {
				RequestFixture fixture;
				fixture.codec.fault = a_fault;
				fixture.codec.faultChunk = a_chunk;
				const auto outcome = fixture.Run();
				return std::pair{ outcome, std::move(fixture.rows) };
			};

			const auto [shortInput, shortRows] = run(FaultKind::ShortInput, 1);
			require(shortInput.served && !shortInput.oneShot, "the stock replay did not serve");
			require(shortInput.reason == ZlibFallbackReason::SizeMismatch,
				"a consumed-byte disagreement was not a size mismatch");
			require(shortInput.failingChunk == 1, "the failing chunk was misattributed");
			require(
				shortRows.rows[1].fallbackReasonId == kReasonSizeMismatch,
				"the failing row lost its specific reason");

			const auto [longOutput, longRows] = run(FaultKind::LongOutput, 0);
			require(longOutput.reason == ZlibFallbackReason::SizeMismatch,
				"a produced-byte disagreement was not a size mismatch");
			require(longOutput.evidence.mismatches == 1, "the size mismatch was not counted");
			require(longOutput.evidence.minDelta == -1,
				"a decode longer than fullSize must read as a negative delta");
			require(
				(longRows.rows[0].evidenceFlags & kEvidenceSizeMismatch) != 0,
				"the row did not carry the size mismatch evidence");

			const auto [capacity, capacityRows] = run(FaultKind::Capacity, 2);
			require(capacity.reason == ZlibFallbackReason::Capacity,
				"an insufficient output bound was not a capacity failure");
			require(capacity.evidence.capacityFailures == 1, "the capacity failure was not counted");
			require(capacity.evidence.samples == 2, "an unmeasured decode was sampled");
			require(
				(capacityRows.rows[2].evidenceFlags & kEvidenceCapacity) != 0,
				"the row did not carry the capacity evidence");
			require(capacityRows.rows[2].fallbackReasonId == kReasonCapacity,
				"the failing row lost the capacity reason");

			const auto [badData, badRows] = run(FaultKind::BadData, 3);
			require(badData.reason == ZlibFallbackReason::Decode, "corrupt data was not a decode failure");
			require(badRows.rows[3].fallbackReasonId == kReasonDecode,
				"the failing row lost the decode reason");
			require(badData.evidence.samples == 3, "a failed decode was measured");
		});

		runner.test("one-shot fallback rows attribute the replay per chunk", [] {
			RequestFixture fixture;
			fixture.codec.fault = FaultKind::BadData;
			fixture.codec.faultChunk = 2;
			const auto outcome = fixture.Run();

			require(fixture.fallbackCalls == 1, "the engine loop ran more than once");
			require(outcome.served && !outcome.oneShot, "the replayed request did not serve");
			require(fixture.OutputMatches(), "the stock replay did not rewrite the whole request");

			const auto& rows = fixture.rows;
			require(rows.count == RequestFixture::kChunks, "the replay changed the row count");
			for (uint16_t index = 0; index < rows.count; ++index)
			{
				const auto& row = rows.rows[index];
				require(row.fallbackBackendId == kBackendStockZlib, "a replayed row lost the fallback");
				require(row.servedBackendId == kBackendStockZlib, "the replay did not serve the row");
				require(row.inputBytesConsumed == fixture.chunks[index].compressedSize,
					"replay input bytes were misattributed");
				require(row.outputBytesProduced == fixture.nominal[index],
					"replay output bytes were misattributed");
				require(row.fallbackQpc == 3, "replay ticks were misattributed");
				require(row.totalQpc == row.primaryQpc + row.fallbackQpc,
					"a replayed row lost the tick identity");
				require(ValidateObservation(row, 10'000'000).WellFormed(), "a replayed row was malformed");
			}

			require(
				rows.rows[0].fallbackReasonId == kReasonRequestRestart &&
					rows.rows[1].fallbackReasonId == kReasonRequestRestart,
				"chunks decoded before the failure must report a request restart");
			require(
				(rows.rows[0].evidenceFlags & kEvidenceReplayed) != 0 &&
					(rows.rows[3].evidenceFlags & kEvidenceReplayed) != 0,
				"every chunk the stock replay rewrote must be marked replayed");
			require(rows.rows[0].primaryAttempted && rows.rows[0].primaryQpc,
				"an earlier libdeflate success lost its primary timing");
			require(rows.rows[2].fallbackReasonId == kReasonDecode,
				"the failing chunk lost its specific reason");
			require(rows.rows[3].fallbackReasonId == kReasonRequestRestart,
				"chunks after the failure must report a request restart");
			require(!rows.rows[3].primaryAttempted && rows.rows[3].primaryQpc == 0 &&
					rows.rows[3].primaryOutputBytesProduced == 0,
				"a chunk after the failure must not claim a codec attempt");
			require(rows.rows[0].requestWallQpc >= rows.rows[0].totalQpc,
				"the request wall clock must cover the replay");
		});

		runner.test("one-shot clears codec results the stock replay never reached", [] {
			RequestFixture fixture;
			fixture.codec.fault = FaultKind::BadData;
			fixture.codec.faultChunk = 2;
			fixture.fallbackFirstServed = RequestFixture::kChunks;
			fixture.fallbackResult = false;
			const auto outcome = fixture.Run();

			require(!outcome.served, "a failed engine loop was reported as served");
			require(!outcome.oneShot, "a failed request claimed a one-shot decode");
			require(fixture.rows.count == RequestFixture::kChunks, "the request lost its rows");

			for (uint16_t index = 0; index < 2; ++index)
			{
				const auto& row = fixture.rows.rows[index];
				require(row.primaryAttempted &&
						row.primaryOutputBytesProduced == fixture.nominal[index],
					"the discarded codec attempt lost its own evidence");
				require(row.servedBackendId == kBackendNone,
					"a chunk nothing rewrote must stay unserved");
				require(row.inputBytesConsumed == 0 && row.outputBytesProduced == 0,
					"an unserved chunk reported served bytes");
				require(row.zlibResult == 0,
					"a discarded one-shot result survived a replay that never reached the chunk");
				require((row.evidenceFlags & kEvidenceReplayed) == 0,
					"a chunk the stock replay never rewrote was marked replayed");
				require(ValidateObservation(row, 10'000'000).WellFormed(),
					"an unreached chunk row was malformed");
			}
		});

		runner.test("one-shot reports an unavailable decompressor without decoding", [] {
			RequestFixture fixture;
			fixture.codec.prepared = false;
			const auto outcome = fixture.Run();

			require(fixture.codec.decodeCalls == 0, "a failed preparation still decoded");
			require(outcome.served && !outcome.oneShot, "the engine loop did not serve");
			require(outcome.reason == ZlibFallbackReason::Allocation, "the allocation failure was lost");
			require(outcome.evidence.samples == 0, "an unattempted request sampled sizes");
			for (uint16_t index = 0; index < fixture.rows.count; ++index)
			{
				const auto& row = fixture.rows.rows[index];
				require(row.fallbackReasonId == kReasonAllocation,
					"every row of an unattempted request reports the allocation failure");
				require(!row.primaryAttempted, "an unattempted request claimed a codec attempt");
				require(row.servedBackendId == kBackendStockZlib, "the replay did not serve the row");
			}
		});

		runner.test("one-shot preserves a failed stock replay instead of synthesizing success", [] {
			RequestFixture fixture;
			fixture.codec.fault = FaultKind::BadData;
			fixture.codec.faultChunk = 0;
			fixture.fallbackResult = false;
			fixture.fallbackFirstServed = 2;
			const auto outcome = fixture.Run();

			require(!outcome.served, "a failed engine loop was reported as served");
			require(!outcome.oneShot, "a failed request claimed a one-shot decode");
			require(fixture.rows.rows[0].servedBackendId == kBackendNone,
				"a chunk the replay never wrote must stay unserved");
			require(fixture.rows.rows[0].outputBytesProduced == 0,
				"an unserved chunk reported produced bytes");
			require((fixture.rows.rows[0].evidenceFlags & kEvidenceReplayed) == 0,
				"a chunk the replay never wrote was marked replayed");
			require(fixture.rows.rows[2].servedBackendId == kBackendStockZlib,
				"a chunk the replay did write must be attributed to stock");
			require((fixture.rows.rows[2].evidenceFlags & kEvidenceReplayed) != 0,
				"a chunk the replay rewrote was not marked replayed");
			require(ValidateObservation(fixture.rows.rows[0], 10'000'000).WellFormed(),
				"an unserved chunk row was malformed");
		});

		runner.test("one-shot request rows are admissible as one profiler batch", [] {
			RequestFixture fixture;
			const auto outcome = fixture.Run();
			require(outcome.oneShot, "the fixture request did not decode");

			ShardAggregate aggregate;
			const auto admitted = fixture.rows.Admitted();
			require(admitted.size() <= kMaxBatchRows, "a request cannot exceed one batch");
			for (const auto& row : admitted)
				aggregate.Account(row, ValidateObservation(row, 10'000'000), true, true);

			require(aggregate.requests.chunkRows == RequestFixture::kChunks,
				"chunk rows were not counted");
			require(aggregate.requests.leaderRows == 1, "a request must have exactly one leader row");
			require(aggregate.requests.sizeDeltaSamples == RequestFixture::kChunks,
				"size evidence was not admitted");
			require(aggregate.requests.sizeMismatchChunks == 0, "an exact request reported mismatches");
			require(Reconcile(aggregate).Ok(), "an admitted request failed reconciliation");
		});

		runner.test("size evidence keeps the first mismatch and signed extremes", [] {
			SizeEvidence evidence;
			evidence.Sample(0, 6, 1024, 1024);
			require(evidence.samples == 1 && evidence.mismatches == 0, "an exact chunk was called a mismatch");
			require(evidence.minDelta == 0 && evidence.maxDelta == 0, "an exact chunk moved the extremes");

			// Nominal minus decoded: a padded fullSize decodes short and reads positive.
			evidence.Sample(2, 6, 4096, 4090);
			require(evidence.mismatches == 1, "a short decode was not a mismatch");
			require(evidence.firstDelta == 6, "padded fullSize must read as a positive delta");
			require(evidence.firstChunk == 2 && evidence.firstChunkCount == 6,
				"the first mismatch lost its chunk coordinates");
			require(evidence.firstNominal == 4096 && evidence.firstActual == 4090,
				"the first mismatch lost its byte counts");
			require(evidence.minDelta == 6 && evidence.maxDelta == 6,
				"exact chunks must not drag the extremes toward zero");

			evidence.Sample(4, 6, 2048, 2048);
			require(evidence.minDelta == 6 && evidence.maxDelta == 6,
				"a later exact chunk moved the extremes");

			evidence.Sample(3, 6, 8192, 8195);
			require(evidence.mismatches == 2, "a long decode was not a mismatch");
			require(evidence.minDelta == -3 && evidence.maxDelta == 6,
				"mixed-sign deltas were not tracked as signed extremes");
			require(evidence.firstDelta == 6 && evidence.firstChunk == 2,
				"a later mismatch overwrote the first mismatch");
			require(evidence.samples == 4, "measured chunks were not counted");
		});

		runner.test("one-shot publishes first-mismatch evidence without archive identity", [] {
			RequestFixture fixture;
			fixture.codec.fault = FaultKind::LongOutput;
			fixture.codec.faultChunk = 2;
			const auto outcome = fixture.Run();

			require(outcome.reason == ZlibFallbackReason::SizeMismatch, "the mismatch was not detected");
			require(outcome.evidence.mismatches == 1, "the mismatch was not counted");
			require(outcome.evidence.samples == 3, "measured chunks were miscounted");
			require(outcome.evidence.firstChunk == 2, "the first mismatch lost its chunk index");
			require(outcome.evidence.firstChunkCount == RequestFixture::kChunks,
				"the first mismatch lost the request chunk count");
			require(outcome.evidence.firstNominal == fixture.nominal[2],
				"the first mismatch lost the nominal size");
			require(outcome.evidence.firstActual == fixture.nominal[2] + 1,
				"the first mismatch lost the decoded size");
			require(outcome.evidence.firstDelta == -1,
				"a decode longer than fullSize must read as a negative delta");
			require(outcome.evidence.minDelta == -1 && outcome.evidence.maxDelta == -1,
				"the exact chunks before the mismatch dragged the extremes to zero");
		});

		runner.test("one-shot never decodes past the replay-covered range", [] {
			RequestFixture fixture;
			fixture.SetLast(2);
			fixture.codec.fault = FaultKind::LongOutput;
			fixture.codec.faultChunk = 2;
			const auto trailingStart = fixture.nominal[0] + fixture.nominal[1] + fixture.nominal[2];
			fixture.fallbackFirstServed = RequestFixture::kChunks;
			const auto outcome = fixture.Run();

			require(!outcome.oneShot, "an overproducing chunk was accepted");
			require(outcome.reason == ZlibFallbackReason::Capacity,
				"overproduction past the selected range must be a capacity failure, not a write");
			require(outcome.evidence.capacityFailures == 1, "the capacity failure was not counted");
			for (size_t offset = trailingStart; offset < fixture.destination.size(); ++offset)
				require(fixture.destination[offset] == std::byte{ 0 },
					"a chunk wrote past the last selected mip, which the engine loop never rewrites");

			RequestFixture bounded;
			bounded.SetLast(2);
			const auto served = bounded.Run();
			require(served.oneShot, "a well behaved partial request was rejected");
			require(bounded.rows.rows[2].outputBytesAvailable == bounded.nominal[2],
				"the last selected chunk was offered more room than the engine loop covers");
			for (size_t offset = trailingStart; offset < bounded.destination.size(); ++offset)
				require(bounded.destination[offset] == std::byte{ 0 },
					"a served request touched mips outside its range");
		});

		runner.test("one-shot reads no clock when profiling is disabled", [] {
			RequestFixture fixture;
			fixture.codec.fault = FaultKind::BadData;
			fixture.codec.faultChunk = 1;
			const auto outcome = fixture.Run(false, false);

			require(outcome.served, "the engine loop did not serve with profiling disabled");
			require(!outcome.oneShot, "the fault was not detected with profiling disabled");
			require(fixture.clockReads == 0, "the request read the clock with profiling disabled");
			require(!fixture.replayTimingEnabled,
				"the replay context must tell the inflate hook not to read the clock");
			require(fixture.rows.count == 0, "rows were buffered with profiling disabled");

			RequestFixture timed;
			timed.codec.fault = FaultKind::BadData;
			timed.codec.faultChunk = 1;
			const auto profiled = timed.Run(true, true);
			require(profiled.served, "the timed request did not serve");
			require(timed.clockReads != 0, "the timed request did not read the clock");
			require(timed.replayTimingEnabled, "the timed replay context disabled the clock");
		});

		runner.test("one-shot withholds rows when a replay cannot be attributed", [] {
			RequestFixture fixture;
			fixture.codec.fault = FaultKind::BadData;
			fixture.codec.faultChunk = 1;
			fixture.breakAttribution = true;
			const auto outcome = fixture.Run();

			require(outcome.served, "an unattributable replay changed the decode result");
			require(fixture.OutputMatches(), "an unattributable replay changed the decoded bytes");
			require(!outcome.attributionOk, "the attribution failure was not reported");
			require(fixture.rows.count == 0,
				"rows that cannot be attributed must not be admitted as a batch");

			RequestFixture control;
			control.codec.fault = FaultKind::BadData;
			control.codec.faultChunk = 1;
			const auto served = control.Run();
			require(served.attributionOk, "a well formed replay was called unattributable");
			require(control.rows.count == RequestFixture::kChunks,
				"a well formed replay lost its rows");
		});

		runner.test("replay capture refuses to fold calls it cannot attribute", [] {
			ZlibReplayCapture capture;
			uint32_t liveIndex = 0;
			capture.liveChunkIndex = &liveIndex;
			capture.Account(1, 1, 1, 1);
			require(capture.AttributionOk(), "a valid call was called unattributable");

			liveIndex = static_cast<uint32_t>(capture.chunks.size());
			capture.Account(1, 1, 1, 1);
			require(!capture.AttributionOk(), "an out-of-range chunk index was tolerated");

			ZlibReplayCapture overflow;
			overflow.liveChunkIndex = &liveIndex;
			liveIndex = 0;
			overflow.Account(1, 0xFFFFFFFFu, 1, 1);
			require(overflow.AttributionOk(), "the first saturating call was rejected");
			overflow.Account(1, 1, 1, 1);
			require(!overflow.AttributionOk(), "a byte sum that would wrap was tolerated");

			ZlibReplayCapture unbound;
			unbound.Account(1, 1, 1, 1);
			require(!unbound.AttributionOk(), "a capture without a live index accepted a call");
		});

		runner.test("one-shot refuses a chunk whose two size sources disagree", [] {
			RequestFixture fixture;
			fixture.chunks[2].uncompressedSize += 4;
			const auto outcome = fixture.Run();

			require(fixture.codec.prepareCalls == 0,
				"a request with disagreeing sizes prepared the codec");
			require(fixture.codec.decodeCalls == 0,
				"a request with disagreeing sizes attempted a decode");
			require(fixture.fallbackCalls == 1, "the engine loop was not asked to serve the request");
			require(!outcome.oneShot, "a request with disagreeing sizes claimed a one-shot decode");
			require(outcome.served, "the engine loop result was lost");
			require(outcome.reason == ZlibFallbackReason::SizeMismatch,
				"the size disagreement was not the fallback reason");
			require(outcome.failingChunk == 2, "the disagreeing chunk was misidentified");
			require(outcome.evidence.descMismatches == 1,
				"the nominal-vs-descriptor mismatch was not counted");
			require(outcome.evidence.samples == 0 && outcome.evidence.mismatches == 0,
				"an unattempted request reported decoded size evidence");
			require(outcome.decodedBytes == 0, "an unattempted request reported decoded bytes");
			require(fixture.OutputMatches(), "the engine loop did not write the request");

			const auto& rows = fixture.rows;
			require(rows.count == RequestFixture::kChunks, "the request lost its rows");
			for (uint16_t index = 0; index < rows.count; ++index)
			{
				const auto& row = rows.rows[index];
				require(!row.primaryAttempted && row.primaryQpc == 0 &&
						row.primaryOutputBytesProduced == 0,
					"an unattempted chunk claimed a codec attempt");
				require(row.servedBackendId == kBackendStockZlib, "the replay did not serve the row");
				require((row.evidenceFlags & kEvidenceReplayed) != 0,
					"a chunk the replay rewrote was not marked replayed");
				require(ValidateObservation(row, 10'000'000).WellFormed(),
					"an ineligible chunk row was malformed");
			}

			require(rows.rows[2].fallbackReasonId == kReasonSizeMismatch,
				"the disagreeing chunk row must name the size mismatch");
			require((rows.rows[2].evidenceFlags & kEvidenceNominalDescMismatch) != 0,
				"the disagreeing chunk row lost its descriptor evidence");
			for (const uint16_t sibling : { 0, 1, 3 })
			{
				require(rows.rows[sibling].fallbackReasonId == kReasonRequestRestart,
					"a chunk that agreed with its descriptor must report a request restart");
				require((rows.rows[sibling].evidenceFlags & kEvidenceNominalDescMismatch) == 0,
					"a chunk that agreed with its descriptor was flagged");
			}
		});

		runner.test("nested serve scopes restore the previous mode", [] {
			auto& state = CurrentZlibServe();
			require(!state.Active(), "the serve context leaked from an earlier test");
			require(state.mode == ZlibServeMode::Normal, "the serve context did not start normal");

			{
				ZlibServeScope outer(
					ZlibServeMode::ForceStockObserved, BA2Profile::kCallerArraySlice, 11, 0x2000);
				require(state.Active() && state.depth == 1, "the outer scope was not entered");
				require(state.mode == ZlibServeMode::ForceStockObserved, "the outer mode was not set");
				require(state.callerId == BA2Profile::kCallerArraySlice, "the caller was not published");
				require(state.requestSequence == 11, "the request identity was not published");

				ZlibReplayCapture capture;
				{
					ZlibServeScope inner(
						ZlibServeMode::CaptureReplay,
						BA2Profile::kCallerStreamingTexture,
						12,
						0x3000,
						&capture);
					require(state.depth == 2, "the inner scope did not deepen the context");
					require(state.mode == ZlibServeMode::CaptureReplay, "the inner mode was not set");
					require(state.capture == &capture, "the replay capture was not published");
				}

				require(state.depth == 1, "leaving the inner scope did not restore the depth");
				require(state.mode == ZlibServeMode::ForceStockObserved,
					"leaving the inner scope did not restore the mode");
				require(state.capture == nullptr, "a stale replay capture survived the inner scope");
				require(state.requestSequence == 11, "the outer request identity was not restored");
			}

			require(!state.Active(), "the outer scope did not clear the context");
			require(state.mode == ZlibServeMode::Normal, "the context did not return to normal");
			require(state.callerId == BA2Profile::kCallerNone, "a stale caller survived the scope");
		});

		runner.test("replay capture groups stock calls by the live chunk index", [] {
			ZlibReplayCapture capture;
			uint32_t liveIndex = 0;
			capture.liveChunkIndex = &liveIndex;

			capture.Account(10, 4, 40, 1);
			capture.Account(5, 6, 60, 1);
			liveIndex = 3;
			capture.Account(7, 8, 80, 1);
			liveIndex = kMaxBatchRows;
			capture.Account(9, 1, 1, 1);

			require(capture.chunks[0].calls == 2, "calls for one chunk were not grouped");
			require(capture.chunks[0].qpc == 15, "grouped ticks were not summed");
			require(capture.chunks[0].consumed == 10 && capture.chunks[0].produced == 100,
				"grouped bytes were not summed");
			require(capture.chunks[3].calls == 1 && capture.chunks[3].qpc == 7,
				"a later chunk was misattributed");
			require(capture.chunks[1].calls == 0, "an untouched chunk gained a call");
			require(capture.unattributedCalls == 1, "an out-of-range call was silently dropped");
		});
	}
}
