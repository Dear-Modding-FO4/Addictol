#include <AdTextureOneShot.h>

#include <AdProfilerBA2.h>
#include <AdUtils.h>
#include <AdZlibBackend.h>
#include <AdZlibServeContext.h>

#include <RE/B/BSTextureStreamer.h>

#include <Windows.h>
#ifdef ERROR
#	undef ERROR
#endif
#include <intrin.h>

#include <atomic>
#include <limits>
#include <memory>
#include <new>

namespace Addictol::TextureOneShot
{
	using namespace BA2Profile;

	static_assert(sizeof(RE::BSTextureStreamer::ChunkDesc) == sizeof(TextureStream::ChunkDesc));
	static_assert(
		offsetof(RE::BSTextureStreamer::ChunkDesc, size) ==
		offsetof(TextureStream::ChunkDesc, compressedSize));
	static_assert(
		offsetof(RE::BSTextureStreamer::ChunkDesc, uncompressedSize) ==
		offsetof(TextureStream::ChunkDesc, uncompressedSize));

	namespace
	{
		using TSeam = bool(__fastcall*)(TextureStream::Stream*, std::byte*) noexcept;
		using TInner = bool(__fastcall*)(void*, TextureStream::Stream*, std::byte*) noexcept;

		constexpr size_t kCallerCodeSize = 0x160;

		struct Counters
		{
			std::atomic<uint64_t> requests{ 0 };
			std::atomic<uint64_t> oneShotRequests{ 0 };
			std::atomic<uint64_t> fallbackRequests{ 0 };
			std::atomic<uint64_t> fallbackFailures{ 0 };
			std::atomic<uint64_t> nestedDelegations{ 0 };
			std::atomic<uint64_t> unknownCallerDelegations{ 0 };
			std::atomic<uint64_t> delegations{ 0 };
			std::atomic<uint64_t> chunksObserved{ 0 };
			std::atomic<uint64_t> chunksDecoded{ 0 };
			std::atomic<uint64_t> decodedBytes{ 0 };
			std::atomic<uint64_t> sizeSamples{ 0 };
			std::atomic<uint64_t> sizeMismatches{ 0 };
			std::atomic<int64_t> minSizeDelta{ std::numeric_limits<int64_t>::max() };
			std::atomic<int64_t> maxSizeDelta{ std::numeric_limits<int64_t>::min() };
			std::atomic<uint64_t> attributionFailures{ 0 };
			std::atomic<bool> attributionWarned{ false };
			std::atomic<bool> firstMismatchClaimed{ false };
			std::atomic<bool> firstMismatchPublished{ false };
			std::atomic<int64_t> firstMismatchDelta{ 0 };
			std::atomic<uint32_t> firstMismatchNominal{ 0 };
			std::atomic<uint32_t> firstMismatchActual{ 0 };
			std::atomic<uint32_t> firstMismatchChunk{ 0 };
			std::atomic<uint32_t> firstMismatchChunkCount{ 0 };
			std::atomic<uint64_t> nominalDescMismatches{ 0 };
			std::atomic<uint64_t> zeroCompressedChunks{ 0 };
			std::atomic<uint64_t> badChunkHeaders{ 0 };
			std::atomic<uint64_t> capacityFailures{ 0 };
			std::atomic<uint64_t> rowBufferUnavailable{ 0 };
			std::array<std::atomic<uint64_t>, 16> delegateReasons{};
			std::array<std::atomic<uint64_t>, kKnownReasonCount> fallbackReasons{};
		};

		Counters& GetCounters() noexcept
		{
			static auto* counters = new Counters();
			return *counters;
		}

		// Sentinel seeded extrema need no first-writer special case, so there is no overwrite race.
		void SampleSizeDelta(Counters& a_counters, int64_t a_delta) noexcept
		{
			auto minimum = a_counters.minSizeDelta.load(std::memory_order_relaxed);
			while (a_delta < minimum &&
				!a_counters.minSizeDelta.compare_exchange_weak(
					minimum, a_delta, std::memory_order_relaxed))
			{
			}

			auto maximum = a_counters.maxSizeDelta.load(std::memory_order_relaxed);
			while (a_delta > maximum &&
				!a_counters.maxSizeDelta.compare_exchange_weak(
					maximum, a_delta, std::memory_order_relaxed))
			{
			}
		}

		// Claimed before publishing so a reader never sees a half-written first mismatch.
		void CaptureFirstMismatch(Counters& a_counters, const SizeEvidence& a_evidence) noexcept
		{
			auto claimed = false;
			if (!a_counters.firstMismatchClaimed.compare_exchange_strong(
					claimed, true, std::memory_order_acq_rel))
				return;

			a_counters.firstMismatchDelta.store(a_evidence.firstDelta, std::memory_order_relaxed);
			a_counters.firstMismatchNominal.store(a_evidence.firstNominal, std::memory_order_relaxed);
			a_counters.firstMismatchActual.store(a_evidence.firstActual, std::memory_order_relaxed);
			a_counters.firstMismatchChunk.store(a_evidence.firstChunk, std::memory_order_relaxed);
			a_counters.firstMismatchChunkCount.store(
				a_evidence.firstChunkCount, std::memory_order_relaxed);
			a_counters.firstMismatchPublished.store(true, std::memory_order_release);
		}

		[[nodiscard]] uint64_t PerTenThousand(uint64_t a_part, uint64_t a_whole) noexcept
		{
			return a_whole ? (a_part * 10000 + a_whole / 2) / a_whole : 0;
		}

		struct ThreadRowBuffer
		{
			std::unique_ptr<RequestRows> rows;
			bool unavailable{ false };

			[[nodiscard]] RequestRows* Acquire() noexcept
			{
				if (!rows && !unavailable)
				{
					rows.reset(new (std::nothrow) RequestRows());
					if (!rows)
					{
						unavailable = true;
						GetCounters().rowBufferUnavailable.fetch_add(1, std::memory_order_relaxed);
					}
				}
				return rows.get();
			}
		};

		thread_local ThreadRowBuffer g_threadRows;
		thread_local uint64_t g_threadRequestSequence{ 0 };

		InstallState g_installState{ InstallState::NotAttempted };
		TSeam g_originalSeam{ nullptr };
		TInner g_ogResident{ nullptr };
		TInner g_ogAlternate{ nullptr };
		uintptr_t g_seam{ 0 };
		uintptr_t g_detailVtable{ 0 };
		uintptr_t g_streamingReturn{ 0 };
		uintptr_t g_arraySliceReturn{ 0 };
		std::string_view g_runtime;
		bool g_runtimeIsOG{ false };

		[[nodiscard]] REL::ID MakeId(const TextureStream::RuntimeIds& a_ids) noexcept
		{
			return REL::ID{ a_ids.og, a_ids.ng, a_ids.ae };
		}

		struct LibDeflateCodec
		{
			static bool Prepare() noexcept { return LibDeflateZlibBackend::Prepare(); }

			static ZlibExactDecode Decode(
				std::span<const uint8_t> a_input,
				std::span<uint8_t> a_output) noexcept
			{
				return LibDeflateZlibBackend::DecodeExact(a_input, a_output);
			}
		};

		[[nodiscard]] uint64_t ReadQpc() noexcept
		{
			LARGE_INTEGER value{};
			QueryPerformanceCounter(&value);
			return static_cast<uint64_t>(value.QuadPart);
		}

		[[nodiscard]] uint64_t GetQpcFrequency() noexcept
		{
			static const auto frequency = []() noexcept {
				LARGE_INTEGER value{};
				return QueryPerformanceFrequency(&value) && value.QuadPart > 0 ?
					static_cast<uint64_t>(value.QuadPart) :
					uint64_t{ 0 };
			}();
			return frequency;
		}

		[[nodiscard]] CallerId ClassifyCaller(uintptr_t a_returnAddress) noexcept
		{
			if (a_returnAddress == g_streamingReturn)
				return kCallerStreamingTexture;
			if (a_returnAddress == g_arraySliceReturn)
				return kCallerArraySlice;
			return kCallerNone;
		}

		// The engine loop for the whole request: the validated seam on AE/NG, the inner loop on OG.
		[[nodiscard]] bool CallEngineLoop(
			TextureStream::Stream* a_stream,
			std::byte* a_destination) noexcept
		{
			if (!g_runtimeIsOG)
				return g_originalSeam ? g_originalSeam(a_stream, a_destination) : false;

			if (a_stream && a_stream->resident && g_ogResident)
				return g_ogResident(a_stream->resident, a_stream, a_destination);
			if (a_stream && a_stream->alternate && g_ogAlternate)
				return g_ogAlternate(a_stream->alternate, a_stream, a_destination);
			return false;
		}

		[[nodiscard]] bool DelegateObserved(
			TextureStream::Stream* a_stream,
			std::byte* a_destination,
			CallerId a_caller) noexcept
		{
			ZlibServeScope scope(
				ZlibServeMode::ForceStockObserved,
				a_caller,
				++g_threadRequestSequence,
				reinterpret_cast<uint64_t>(a_stream));
			return CallEngineLoop(a_stream, a_destination);
		}

		void CountDelegation(TextureStream::DelegateReason a_reason) noexcept
		{
			auto& counters = GetCounters();
			counters.delegations.fetch_add(1, std::memory_order_relaxed);
			const auto index = static_cast<size_t>(a_reason);
			if (index < counters.delegateReasons.size())
				counters.delegateReasons[index].fetch_add(1, std::memory_order_relaxed);
		}

		void CountOutcome(const RequestOutcome& a_outcome) noexcept
		{
			auto& counters = GetCounters();
			counters.requests.fetch_add(1, std::memory_order_relaxed);
			counters.chunksObserved.fetch_add(a_outcome.chunkRows, std::memory_order_relaxed);
			counters.decodedBytes.fetch_add(a_outcome.decodedBytes, std::memory_order_relaxed);
			counters.nominalDescMismatches.fetch_add(
				a_outcome.evidence.descMismatches, std::memory_order_relaxed);
			counters.capacityFailures.fetch_add(
				a_outcome.evidence.capacityFailures, std::memory_order_relaxed);
			counters.sizeSamples.fetch_add(
				a_outcome.evidence.samples, std::memory_order_relaxed);

			if (a_outcome.evidence.mismatches)
			{
				counters.sizeMismatches.fetch_add(
					a_outcome.evidence.mismatches, std::memory_order_relaxed);
				SampleSizeDelta(counters, a_outcome.evidence.minDelta);
				SampleSizeDelta(counters, a_outcome.evidence.maxDelta);
				CaptureFirstMismatch(counters, a_outcome.evidence);
			}

			if (!a_outcome.attributionOk)
			{
				counters.attributionFailures.fetch_add(1, std::memory_order_relaxed);
				if (!counters.attributionWarned.exchange(true, std::memory_order_acq_rel))
				{
					REX::WARN(
						"Texture one-shot: a stock replay could not be attributed to its chunks; those request rows were dropped and decompression continued through the engine loop."sv);
				}
			}

			if (a_outcome.oneShot)
			{
				counters.oneShotRequests.fetch_add(1, std::memory_order_relaxed);
				counters.chunksDecoded.fetch_add(a_outcome.chunkRows, std::memory_order_relaxed);
				return;
			}

			counters.fallbackRequests.fetch_add(1, std::memory_order_relaxed);
			if (!a_outcome.served)
				counters.fallbackFailures.fetch_add(1, std::memory_order_relaxed);
			const auto reason = ZlibFallbackReasonRegistryId(a_outcome.reason);
			if (reason < counters.fallbackReasons.size())
				counters.fallbackReasons[reason].fetch_add(1, std::memory_order_relaxed);
		}

		bool __fastcall TextureSeamHook(
			TextureStream::Stream* a_stream,
			std::byte* a_destination) noexcept
		{
			const auto caller = ClassifyCaller(
				reinterpret_cast<uintptr_t>(_ReturnAddress()));

			// Work reached through an already managed request keeps that request's serve rules.
			if (CurrentZlibServe().Active())
			{
				GetCounters().nestedDelegations.fetch_add(1, std::memory_order_relaxed);
				return CallEngineLoop(a_stream, a_destination);
			}

			if (caller == kCallerNone)
			{
				auto& counters = GetCounters();
				counters.unknownCallerDelegations.fetch_add(1, std::memory_order_relaxed);
				CountDelegation(TextureStream::DelegateReason::UnknownCaller);
				return DelegateObserved(a_stream, a_destination, caller);
			}

			const auto preflight = TextureStream::Preflight(a_stream, a_destination, g_detailVtable);
			if (!preflight)
			{
				auto& counters = GetCounters();
				if (preflight.zeroCompressed)
					counters.zeroCompressedChunks.fetch_add(1, std::memory_order_relaxed);
				if (preflight.badHeader)
					counters.badChunkHeaders.fetch_add(1, std::memory_order_relaxed);
				CountDelegation(preflight.reason);
				return DelegateObserved(a_stream, a_destination, caller);
			}

			const auto recording = ProfilerBA2::GetSingleton()->IsRecording();
			const auto qpcFrequency = recording ? GetQpcFrequency() : 0;
			const auto timingEnabled = recording && qpcFrequency != 0;

			RequestIdentity identity;
			identity.caller = caller;
			identity.threadId = static_cast<uint32_t>(GetCurrentThreadId());
			identity.sequence = ++g_threadRequestSequence;
			identity.streamAddress = reinterpret_cast<uint64_t>(a_stream);
			identity.qpcFrequency = qpcFrequency;

			auto* rows = timingEnabled ? g_threadRows.Acquire() : nullptr;
			LibDeflateCodec codec;
			const auto outcome = RunRequest(
				*a_stream,
				a_destination,
				preflight.bounds,
				identity,
				codec,
				[]() noexcept { return ReadQpc(); },
				timingEnabled,
				[&](ZlibReplayCapture& a_capture) noexcept {
					ZlibServeScope scope(
						ZlibServeMode::CaptureReplay,
						caller,
						identity.sequence,
						identity.streamAddress,
						&a_capture);
					return CallEngineLoop(a_stream, a_destination);
				},
				rows);

			CountOutcome(outcome);
			if (rows && rows->count)
				ProfilerBA2::GetSingleton()->RecordBatch(rows->Admitted());
			return outcome.served;
		}

		struct SiteValidation
		{
			bool entry{ false };
			bool streamingCaller{ false };
			bool arraySliceCaller{ false };
			bool residentInner{ true };
			bool alternateInner{ true };

			[[nodiscard]] explicit operator bool() const noexcept
			{
				return entry && streamingCaller && arraySliceCaller &&
					residentInner && alternateInner;
			}
		};

		[[nodiscard]] std::span<const uint8_t> CodeAt(
			uintptr_t a_address,
			size_t a_size) noexcept
		{
			return { reinterpret_cast<const uint8_t*>(a_address), a_size };
		}

		[[nodiscard]] bool ValidateCaller(
			uintptr_t a_root,
			const TextureStream::CallerSignature& a_signature,
			uintptr_t a_seam,
			uintptr_t& a_returnAddress,
			std::string_view a_runtime) noexcept
		{
			const auto check = TextureStream::ValidateCallerSite(
				CodeAt(a_root, kCallerCodeSize), a_signature, a_root, a_seam);
			if (!check)
			{
				REX::ERROR(
					"Texture one-shot: {} caller {} rejected: pre={}, call={}, target={}, post={}."sv,
					a_runtime,
					CallerName(a_signature.caller),
					check.preOk,
					check.callOk,
					check.targetOk,
					check.postOk);
				return false;
			}

			a_returnAddress = a_root + a_signature.returnOffset;
			return true;
		}
	}

	InstallState GetInstallState() noexcept
	{
		return g_installState;
	}

	InstallState Validate(std::string_view a_runtime) noexcept
	{
		if (!MayValidate(g_installState))
			return g_installState;

		g_runtimeIsOG = RELEX::IsRuntimeOG();
		const auto seam = MakeId(TextureStream::SEAM_ID).address();
		const auto detailVtable = MakeId(TextureStream::DETAIL_VTABLE_ID).address();
		const auto streamingRoot = MakeId(TextureStream::STREAMING_CALLER_ID).address();
		const auto arraySliceRoot = MakeId(TextureStream::ARRAY_SLICE_CALLER_ID).address();
		const auto residentInner = g_runtimeIsOG ?
			REL::ID{ TextureStream::OG_RESIDENT_INNER_ID.og }.address() :
			0;
		const auto alternateInner = g_runtimeIsOG ?
			REL::ID{ TextureStream::OG_ALTERNATE_INNER_ID.og }.address() :
			0;

		SiteValidation validation;
		const auto& callers = g_runtimeIsOG ?
			TextureStream::OG_CALLERS :
			TextureStream::MODERN_CALLERS;
		if (g_runtimeIsOG)
		{
			validation.entry = TextureStream::MatchesBytes(
				CodeAt(seam, TextureStream::Guards::OG_DISPATCHER.size()),
				0,
				TextureStream::Guards::OG_DISPATCHER);
			validation.residentInner = TextureStream::MatchesBytes(
				CodeAt(residentInner, TextureStream::Guards::OG_RESIDENT_INNER.size()),
				0,
				TextureStream::Guards::OG_RESIDENT_INNER);
			validation.alternateInner = TextureStream::MatchesBytes(
				CodeAt(alternateInner, TextureStream::Guards::OG_ALTERNATE_INNER.size()),
				0,
				TextureStream::Guards::OG_ALTERNATE_INNER);
		}
		else
		{
			validation.entry = TextureStream::MatchesBytes(
				CodeAt(seam, TextureStream::Guards::MODERN_ENTRY.size()),
				0,
				TextureStream::Guards::MODERN_ENTRY);
		}

		uintptr_t streamingReturn = 0;
		uintptr_t arraySliceReturn = 0;
		validation.streamingCaller = ValidateCaller(
			streamingRoot, callers[0], seam, streamingReturn, a_runtime);
		validation.arraySliceCaller = ValidateCaller(
			arraySliceRoot, callers[1], seam, arraySliceReturn, a_runtime);

		if (!validation)
		{
			g_installState = InstallState::Rejected;
			REX::ERROR(
				"Texture one-shot: {} signatures rejected (entry={}, streaming caller={}, array-slice caller={}, resident loop={}, alternate loop={}); the seam is left untouched and decompression stays per chunk."sv,
				a_runtime,
				validation.entry,
				validation.streamingCaller,
				validation.arraySliceCaller,
				validation.residentInner,
				validation.alternateInner);
			return g_installState;
		}

		g_seam = seam;
		g_detailVtable = detailVtable;
		g_streamingReturn = streamingReturn;
		g_arraySliceReturn = arraySliceReturn;
		g_ogResident = reinterpret_cast<TInner>(residentInner);
		g_ogAlternate = reinterpret_cast<TInner>(alternateInner);
		g_runtime = a_runtime;
		g_installState = InstallState::Validated;
		REX::INFO(
			"Texture one-shot: {} targets validated; seam {:X}, detail vtable {:X}, streaming return {:X}, array-slice return {:X}, resident loop {:X}, alternate loop {:X}."sv,
			a_runtime,
			seam,
			detailVtable,
			streamingReturn,
			arraySliceReturn,
			residentInner,
			alternateInner);
		return g_installState;
	}

	InstallState InstallValidated() noexcept
	{
		if (!MayPatch(g_installState))
			return g_installState;

		g_installState = InstallState::Attempted;
		const auto trampoline = RELEX::DetourJump(
			g_seam, reinterpret_cast<uintptr_t>(&TextureSeamHook));
		if (!trampoline)
		{
			g_installState = InstallState::Indeterminate;
			REX::ERROR(
				"Texture one-shot: {} detour failed at {:X}; the seam may be left in an indeterminate state and is not retried."sv,
				g_runtime,
				g_seam);
			return g_installState;
		}

		if (!g_runtimeIsOG)
			g_originalSeam = reinterpret_cast<TSeam>(trampoline);

		g_installState = InstallState::Installed;
		REX::INFO("Texture one-shot: {} enabled at {:X}."sv, g_runtime, g_seam);
		return g_installState;
	}

	void LogCounters() noexcept
	{
		if (g_installState != InstallState::Installed)
			return;

		const auto& counters = GetCounters();
		const auto observed = counters.chunksObserved.load(std::memory_order_relaxed);
		const auto samples = counters.sizeSamples.load(std::memory_order_relaxed);
		const auto mismatches = counters.sizeMismatches.load(std::memory_order_relaxed);
		const auto minDelta = mismatches ? counters.minSizeDelta.load(std::memory_order_relaxed) : 0;
		const auto maxDelta = mismatches ? counters.maxSizeDelta.load(std::memory_order_relaxed) : 0;
		REX::INFO(
			"Texture one-shot counters: requests {}, one-shot {}, fallback {} (stock failed {}), delegated {}, nested {}, unknown caller {}."sv,
			counters.requests.load(std::memory_order_relaxed),
			counters.oneShotRequests.load(std::memory_order_relaxed),
			counters.fallbackRequests.load(std::memory_order_relaxed),
			counters.fallbackFailures.load(std::memory_order_relaxed),
			counters.delegations.load(std::memory_order_relaxed),
			counters.nestedDelegations.load(std::memory_order_relaxed),
			counters.unknownCallerDelegations.load(std::memory_order_relaxed));
		REX::INFO(
			"Texture one-shot chunks: observed {}, decoded {} ({} bytes), size measured {}."sv,
			observed,
			counters.chunksDecoded.load(std::memory_order_relaxed),
			counters.decodedBytes.load(std::memory_order_relaxed),
			samples);
		REX::INFO(
			"Texture one-shot size evidence: fullSize mismatches {} of {} measured and {} observed chunks ({} per 10000 measured, {} per 10000 observed); nominal-minus-decoded delta over mismatching chunks min {}, max {}; nominal-vs-desc mismatches {}, zero compressed {}, bad headers {}, capacity failures {}."sv,
			mismatches,
			samples,
			observed,
			PerTenThousand(mismatches, samples),
			PerTenThousand(mismatches, observed),
			minDelta,
			maxDelta,
			counters.nominalDescMismatches.load(std::memory_order_relaxed),
			counters.zeroCompressedChunks.load(std::memory_order_relaxed),
			counters.badChunkHeaders.load(std::memory_order_relaxed),
			counters.capacityFailures.load(std::memory_order_relaxed));

		if (counters.firstMismatchPublished.load(std::memory_order_acquire))
		{
			REX::WARN(
				"Texture one-shot: first fullSize mismatch was chunk {} of {}: nominal {} bytes, decoded {} bytes, delta {}."sv,
				counters.firstMismatchChunk.load(std::memory_order_relaxed),
				counters.firstMismatchChunkCount.load(std::memory_order_relaxed),
				counters.firstMismatchNominal.load(std::memory_order_relaxed),
				counters.firstMismatchActual.load(std::memory_order_relaxed),
				counters.firstMismatchDelta.load(std::memory_order_relaxed));
		}

		if (mismatches)
		{
			// Uniform sign points at a packer padding fullSize; mixed signs do not.
			const auto reading = minDelta > 0 ?
				"every mismatch decoded shorter than fullSize, which is consistent with a third-party packer padding fullSize"sv :
				(maxDelta < 0 ?
					"every mismatch decoded longer than fullSize"sv :
					"mismatch deltas carry mixed signs, which points at corruption or a wrong assumption rather than padding"sv);
			REX::WARN(
				"Texture one-shot: the archive fullSize contract did not hold for every chunk; {}. The exact-size guard sent those requests back to the engine loop."sv,
				reading);
		}

		const auto attributionFailures = counters.attributionFailures.load(std::memory_order_relaxed);
		if (attributionFailures)
		{
			REX::ERROR(
				"Texture one-shot: {} stock replays could not be attributed to their chunks; those requests decompressed normally but were withheld from the profiler."sv,
				attributionFailures);
		}

		if (counters.nominalDescMismatches.load(std::memory_order_relaxed))
		{
			REX::WARN(
				"Texture one-shot: {} chunk sizes disagreed between the stream size array and the chunk descriptor; the caller allocation follows the size array."sv,
				counters.nominalDescMismatches.load(std::memory_order_relaxed));
		}

		const auto unavailable = counters.rowBufferUnavailable.load(std::memory_order_relaxed);
		if (unavailable)
		{
			REX::WARN(
				"Texture one-shot: {} threads could not allocate a request row buffer; those requests decompressed normally but were not profiled."sv,
				unavailable);
		}
	}
}
