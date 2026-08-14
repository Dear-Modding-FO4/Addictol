#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "AdProfilerBA2Rows.h"
#include "AdProfilerBA2Schema.h"
#include "AdTextureStream.h"
#include "AdZlibBackend.h"
#include "AdZlibServeContext.h"

namespace Addictol::TextureOneShot
{
	using BA2Profile::CallObservation;

	struct RequestIdentity
	{
		BA2Profile::CallerId caller{ BA2Profile::kCallerNone };
		std::uint32_t threadId{ 0 };
		std::uint64_t sequence{ 0 };
		std::uint64_t streamAddress{ 0 };
		std::uint64_t qpcFrequency{ 0 };
	};

	// One row per physical chunk, buffered until the request outcome is known.
	struct RequestRows
	{
		std::array<CallObservation, BA2Profile::kMaxBatchRows> rows{};
		std::uint16_t count{ 0 };

		[[nodiscard]] std::span<const CallObservation> Admitted() const noexcept
		{
			return { rows.data(), count };
		}
	};

	struct SizeEvidence
	{
		std::uint32_t samples{ 0 };
		std::uint32_t mismatches{ 0 };
		std::uint32_t descMismatches{ 0 };
		std::uint32_t capacityFailures{ 0 };
		std::int64_t minDelta{ 0 };
		std::int64_t maxDelta{ 0 };
		std::int64_t firstDelta{ 0 };
		std::uint32_t firstNominal{ 0 };
		std::uint32_t firstActual{ 0 };
		std::uint16_t firstChunk{ 0 };
		std::uint16_t firstChunkCount{ 0 };

		// Positive delta means the archive claimed more than the member decoded to.
		void Sample(
			std::uint16_t a_chunk,
			std::uint16_t a_chunkCount,
			std::uint32_t a_nominal,
			std::uint32_t a_actual) noexcept
		{
			++samples;
			const auto delta = BA2Profile::ObservedSizeDelta(a_nominal, a_actual);
			if (!delta)
				return;

			// Exact chunks carry no signal, so the extremes span mismatches only.
			if (!mismatches || delta < minDelta)
				minDelta = delta;
			if (!mismatches || delta > maxDelta)
				maxDelta = delta;
			if (!mismatches)
			{
				firstDelta = delta;
				firstNominal = a_nominal;
				firstActual = a_actual;
				firstChunk = a_chunk;
				firstChunkCount = a_chunkCount;
			}
			++mismatches;
		}
	};

	struct RequestOutcome
	{
		bool served{ false };
		bool oneShot{ false };
		bool attributionOk{ true };
		ZlibFallbackReason reason{ ZlibFallbackReason::None };
		std::uint16_t failingChunk{ 0 };
		std::uint16_t chunkRows{ 0 };
		std::uint64_t decodedBytes{ 0 };
		SizeEvidence evidence{};
	};

	namespace detail
	{
		inline void SeedRow(
			CallObservation& a_row,
			const RequestIdentity& a_identity,
			std::uint16_t a_chunk,
			std::uint16_t a_count,
			std::uint32_t a_compressedSize,
			std::uint32_t a_nominal,
			std::uint32_t a_capacity,
			bool a_leader,
			bool a_descMismatch) noexcept
		{
			a_row = {};
			a_row.primaryBackendId = BA2Profile::kBackendLibDeflate;
			a_row.qpcFrequency = a_identity.qpcFrequency;
			a_row.observationSiteId = BA2Profile::kSiteTextureChunk;
			a_row.callerId = a_identity.caller;
			a_row.threadId = a_identity.threadId;
			a_row.requestSequence = a_identity.sequence;
			a_row.streamAddress = a_identity.streamAddress;
			a_row.chunkIndex = a_chunk;
			a_row.chunkCount = a_count;
			a_row.nominalOutputBytes = a_nominal;
			a_row.inputBytesAvailable = a_compressedSize;
			a_row.outputBytesAvailable = a_capacity;
			a_row.evidenceFlags = BA2Profile::kEvidenceChunkRow;
			if (a_leader)
				a_row.evidenceFlags |= BA2Profile::kEvidenceRequestLeader;
			if (a_descMismatch)
				a_row.evidenceFlags |= BA2Profile::kEvidenceNominalDescMismatch;
		}

		inline void ApplyReplay(
			CallObservation& a_row,
			const ZlibReplayChunk& a_replay,
			ZlibFallbackReason a_reason) noexcept
		{
			a_row.fallbackBackendId = BA2Profile::kBackendStockZlib;
			a_row.fallbackReasonId = ZlibFallbackReasonRegistryId(a_reason);
			a_row.fallbackQpc = a_replay.qpc;
			a_row.totalQpc = a_row.primaryQpc + a_row.fallbackQpc;
			if (a_replay.calls)
			{
				a_row.servedBackendId = BA2Profile::kBackendStockZlib;
				a_row.inputBytesConsumed = a_replay.consumed;
				a_row.outputBytesProduced = a_replay.produced;
				a_row.zlibResult = a_replay.zlibResult;
				a_row.evidenceFlags |= BA2Profile::kEvidenceReplayed;
			}
			else
			{
				// Nothing rewrote this chunk, so no codec result or bytes may survive on the row.
				a_row.servedBackendId = BA2Profile::kBackendNone;
				a_row.inputBytesConsumed = 0;
				a_row.outputBytesProduced = 0;
				a_row.zlibResult = 0;
			}
		}
	}

	// Decodes one whole request into the caller buffer, or hands it back to the engine loop.
	template <class Codec, class Clock, class Fallback>
	RequestOutcome RunRequest(
		TextureStream::Stream& a_stream,
		std::byte* a_destination,
		const TextureStream::RequestBounds& a_bounds,
		const RequestIdentity& a_identity,
		Codec&& a_codec,
		Clock&& a_clock,
		bool a_timingEnabled,
		Fallback&& a_fallback,
		RequestRows* a_rows) noexcept
	{
		RequestOutcome outcome;
		outcome.chunkRows = a_bounds.ChunkRows();
		outcome.evidence.descMismatches = a_bounds.descMismatches;
		outcome.failingChunk = a_bounds.first;

		auto&& codec = a_codec;
		auto&& clock = a_clock;
		auto&& fallback = a_fallback;
		const auto readQpc = [&]() noexcept -> std::uint64_t {
			return a_timingEnabled ? clock() : 0;
		};

		auto* base = a_stream.resident->compressedBase;
		auto* destination = reinterpret_cast<std::uint8_t*>(a_destination);
		const auto requestStart = readQpc();

		if (a_rows)
		{
			a_rows->count = outcome.chunkRows;
			std::uint32_t outputOffset = a_bounds.outputStart;
			for (std::uint16_t chunk = a_bounds.first; chunk <= a_bounds.last; ++chunk)
			{
				const auto& desc = a_stream.chunks[chunk];
				const auto nominal = a_stream.nominalSizes[chunk];
				detail::SeedRow(
					a_rows->rows[chunk - a_bounds.first],
					a_identity,
					chunk,
					a_bounds.count,
					desc.compressedSize,
					nominal,
					a_bounds.outputEnd - outputOffset,
					chunk == a_bounds.first,
					nominal != desc.uncompressedSize);
				outputOffset += nominal;
			}
		}

		auto reason = ZlibFallbackReason::None;
		// Two disagreeing size sources leave no trustworthy exact target, so nothing is attempted.
		const auto ineligible = a_bounds.descMismatches != 0;
		if (ineligible)
		{
			reason = ZlibFallbackReason::SizeMismatch;
			for (std::uint16_t chunk = a_bounds.first; chunk <= a_bounds.last; ++chunk)
			{
				if (a_stream.nominalSizes[chunk] != a_stream.chunks[chunk].uncompressedSize)
				{
					outcome.failingChunk = chunk;
					break;
				}
			}
		}
		else if (!codec.Prepare())
		{
			reason = ZlibFallbackReason::Allocation;
		}
		else
		{
			std::uint32_t inputOffset = 0;
			std::uint32_t outputOffset = a_bounds.outputStart;
			for (std::uint16_t chunk = a_bounds.first; chunk <= a_bounds.last; ++chunk)
			{
				const auto& desc = a_stream.chunks[chunk];
				const auto nominal = a_stream.nominalSizes[chunk];
				// The engine loop only rewrites the selected mips, so nothing may decode past them.
				const auto capacity = a_bounds.outputEnd - outputOffset;

				const auto start = readQpc();
				const auto decoded = codec.Decode(
					std::span<const std::uint8_t>{ base + inputOffset, desc.compressedSize },
					std::span<std::uint8_t>{ destination + outputOffset, capacity });
				const auto end = readQpc();
				const auto status = ClassifyExactDecode(decoded, desc.compressedSize, nominal);
				const auto measured = decoded.codecResult == ZLIB_CODEC_SUCCESS;

				if (measured)
					outcome.evidence.Sample(
						chunk,
						a_bounds.count,
						nominal,
						static_cast<std::uint32_t>(decoded.produced));
				if (status == ZlibExactStatus::Capacity)
					++outcome.evidence.capacityFailures;

				if (a_rows)
				{
					auto& row = a_rows->rows[chunk - a_bounds.first];
					row.primaryAttempted = true;
					row.primaryQpc = end - start;
					row.totalQpc = row.primaryQpc;
					if (measured)
					{
						row.primaryInputBytesConsumed = static_cast<std::uint32_t>(decoded.consumed);
						row.primaryOutputBytesProduced = static_cast<std::uint32_t>(decoded.produced);
						row.evidenceFlags |= BA2Profile::kEvidenceSizeMeasured;
						if (decoded.produced != nominal)
							row.evidenceFlags |= BA2Profile::kEvidenceSizeMismatch;
					}
					if (status == ZlibExactStatus::Capacity)
						row.evidenceFlags |= BA2Profile::kEvidenceCapacity;
				}

				if (status != ZlibExactStatus::Success)
				{
					reason = ExactStatusFallbackReason(status);
					outcome.failingChunk = chunk;
					break;
				}

				if (a_rows)
				{
					auto& row = a_rows->rows[chunk - a_bounds.first];
					row.servedBackendId = BA2Profile::kBackendLibDeflate;
					row.inputBytesConsumed = desc.compressedSize;
					row.outputBytesProduced = nominal;
					row.zlibResult = ZlibInflate::Z_STREAM_END;
				}

				outcome.decodedBytes += nominal;
				inputOffset += desc.compressedSize;
				outputOffset += nominal;
			}
		}

		if (reason == ZlibFallbackReason::None)
		{
			a_stream.index = a_bounds.last + 1u;
			a_stream.resident->compressedCursor = base + a_bounds.inputTotal;
			outcome.served = true;
			outcome.oneShot = true;
			if (a_rows && a_rows->count)
				a_rows->rows[0].requestWallQpc = readQpc() - requestStart;
			return outcome;
		}

		outcome.reason = reason;
		ZlibReplayCapture capture;
		capture.liveChunkIndex = &a_stream.index;
		capture.timingEnabled = a_timingEnabled;
		outcome.served = fallback(capture);
		outcome.attributionOk = capture.AttributionOk();

		// An unattributable replay would produce well formed but wrong rows, so admit none.
		if (a_rows && !outcome.attributionOk)
			a_rows->count = 0;

		if (a_rows && a_rows->count)
		{
			for (std::uint16_t chunk = a_bounds.first; chunk <= a_bounds.last; ++chunk)
			{
				auto& row = a_rows->rows[chunk - a_bounds.first];
				const auto rowReason = [&]() noexcept {
					if (reason == ZlibFallbackReason::Allocation)
						return ZlibFallbackReason::Allocation;
					if (ineligible)
						return (row.evidenceFlags & BA2Profile::kEvidenceNominalDescMismatch) ?
							ZlibFallbackReason::SizeMismatch :
							ZlibFallbackReason::RequestRestart;
					return chunk == outcome.failingChunk ?
						reason :
						ZlibFallbackReason::RequestRestart;
				}();
				detail::ApplyReplay(row, capture.chunks[chunk], rowReason);
			}
			a_rows->rows[0].requestWallQpc = readQpc() - requestStart;
		}

		return outcome;
	}

	enum class InstallState : std::uint8_t
	{
		NotAttempted,
		Rejected,
		Validated,
		Attempted,
		Installed,
		Indeterminate
	};

	// A pre-write rejection stays retryable; anything past a Detours attempt is terminal.
	[[nodiscard]] constexpr bool MayValidate(InstallState a_state) noexcept
	{
		return a_state == InstallState::NotAttempted || a_state == InstallState::Rejected;
	}

	// Patching is only ever reachable from a completed validation.
	[[nodiscard]] constexpr bool MayPatch(InstallState a_state) noexcept
	{
		return a_state == InstallState::Validated;
	}

	// Resolves and proves every target; it never writes, so a rejection stays retryable.
	InstallState Validate(std::string_view a_runtime) noexcept;
	InstallState InstallValidated() noexcept;
	[[nodiscard]] InstallState GetInstallState() noexcept;
	void LogCounters() noexcept;
}
