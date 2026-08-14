#include <Modules/AdModuleLibDeflate.h>
#include <AdProfilerBA2.h>
#include <AdProfilerCore.h>
#include <AdTextureOneShot.h>
#include <AdUtils.h>
#include <AdZlibBackend.h>
#include <AdZlibInflate.h>
#include <AdZlibServeContext.h>
#include <Windows.h>
#ifdef ERROR
#	undef ERROR
#endif

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesLibDeflate{ "Patches"sv, "bLibDeflate"sv, true };
	static REX::TOML::Str<> sAdditionalZlibBackend{ "Additional"sv, "sZlibBackend"sv, "libdeflate" };

	namespace zlibDetail
	{
		constexpr static int32_t Z_STREAM_ERROR		= -2;

		static_assert(ZlibBackendRegistryId(ZlibBackendKind::Stock) == BA2Profile::kBackendStockZlib);
		static_assert(ZlibBackendRegistryId(ZlibBackendKind::LibDeflate) == BA2Profile::kBackendLibDeflate);
		static_assert(ZlibFallbackReasonRegistryId(ZlibFallbackReason::None) == BA2Profile::kReasonNone);
		static_assert(ZlibFallbackReasonRegistryId(ZlibFallbackReason::State) == BA2Profile::kReasonState);
		static_assert(ZlibFallbackReasonRegistryId(ZlibFallbackReason::Allocation) == BA2Profile::kReasonAllocation);
		static_assert(ZlibFallbackReasonRegistryId(ZlibFallbackReason::Decode) == BA2Profile::kReasonDecode);
		static_assert(ZlibFallbackReasonRegistryId(ZlibFallbackReason::Commit) == BA2Profile::kReasonCommit);
		static_assert(ZlibFallbackReasonRegistryId(ZlibFallbackReason::Capacity) == BA2Profile::kReasonCapacity);
		static_assert(ZlibFallbackReasonRegistryId(ZlibFallbackReason::SizeMismatch) == BA2Profile::kReasonSizeMismatch);
		static_assert(ZlibFallbackReasonRegistryId(ZlibFallbackReason::RequestRestart) == BA2Profile::kReasonRequestRestart);

		using TInflate = int32_t(*)(ZlibInflate::Stream*, int32_t) noexcept;
		TInflate OriginalInflate;

		// Set immediately before the first Detours call so a retry can never re-enter patching.
		enum class PatchState : uint8_t
		{
			NotAttempted,
			Attempted
		};

		PatchState g_patchState{ PatchState::NotAttempted };

		uint64_t ReadQpc() noexcept
		{
			LARGE_INTEGER value{};
			QueryPerformanceCounter(&value);
			return static_cast<uint64_t>(value.QuadPart);
		}

		uint64_t GetQpcFrequency() noexcept
		{
			static const auto frequency = []() noexcept {
				LARGE_INTEGER value{};
				return QueryPerformanceFrequency(&value) && value.QuadPart > 0 ?
					static_cast<uint64_t>(value.QuadPart) :
					uint64_t{ 0 };
			}();
			return frequency;
		}

		struct AtomicCounters
		{
			std::atomic<uint64_t> attempted{ 0 };
			std::atomic<uint64_t> succeeded{ 0 };
			std::atomic<uint64_t> rejectedByState{ 0 };
			std::atomic<uint64_t> codecFailed{ 0 };
			std::atomic<uint64_t> commitRejected{ 0 };
			std::atomic<uint64_t> allocationFailed{ 0 };
			std::atomic<uint64_t> servedStock{ 0 };
			std::atomic<uint64_t> servedLibDeflate{ 0 };
			std::atomic<uint64_t> servedUnknown{ 0 };
		};

		AtomicCounters& GetAtomicCounters() noexcept
		{
			static auto* counters = new AtomicCounters();
			return *counters;
		}

		struct ThreadCounters
		{
			static constexpr uint32_t FLUSH_THRESHOLD = 256;

			uint64_t attempted{ 0 };
			uint64_t succeeded{ 0 };
			uint64_t rejectedByState{ 0 };
			uint64_t codecFailed{ 0 };
			uint64_t commitRejected{ 0 };
			uint64_t allocationFailed{ 0 };
			uint64_t servedStock{ 0 };
			uint64_t servedLibDeflate{ 0 };
			uint64_t servedUnknown{ 0 };
			uint32_t pending{ 0 };

			~ThreadCounters() noexcept
			{
				Flush();
			}

			void Count(uint64_t& a_counter) noexcept
			{
				++a_counter;
				if (++pending == FLUSH_THRESHOLD)
					Flush();
			}

			void Flush() noexcept
			{
				if (!pending)
					return;

				auto& totals = GetAtomicCounters();
				if (attempted)
					totals.attempted.fetch_add(attempted, std::memory_order_relaxed);
				if (succeeded)
					totals.succeeded.fetch_add(succeeded, std::memory_order_relaxed);
				if (rejectedByState)
					totals.rejectedByState.fetch_add(rejectedByState, std::memory_order_relaxed);
				if (codecFailed)
					totals.codecFailed.fetch_add(codecFailed, std::memory_order_relaxed);
				if (commitRejected)
					totals.commitRejected.fetch_add(commitRejected, std::memory_order_relaxed);
				if (allocationFailed)
					totals.allocationFailed.fetch_add(allocationFailed, std::memory_order_relaxed);
				if (servedStock)
					totals.servedStock.fetch_add(servedStock, std::memory_order_relaxed);
				if (servedLibDeflate)
					totals.servedLibDeflate.fetch_add(servedLibDeflate, std::memory_order_relaxed);
				if (servedUnknown)
					totals.servedUnknown.fetch_add(servedUnknown, std::memory_order_relaxed);

				attempted = 0;
				succeeded = 0;
				rejectedByState = 0;
				codecFailed = 0;
				commitRejected = 0;
				allocationFailed = 0;
				servedStock = 0;
				servedLibDeflate = 0;
				servedUnknown = 0;
				pending = 0;
			}
		};

		thread_local ThreadCounters g_threadCounters;

		void CountOutcome(const ZlibInflateOutcome& a_outcome) noexcept
		{
			switch (a_outcome.servedBackendId)
			{
			case ZlibBackendRegistryId(ZlibBackendKind::Stock):
				g_threadCounters.Count(g_threadCounters.servedStock);
				break;
			case ZlibBackendRegistryId(ZlibBackendKind::LibDeflate):
				g_threadCounters.Count(g_threadCounters.servedLibDeflate);
				break;
			default:
				g_threadCounters.Count(g_threadCounters.servedUnknown);
				break;
			}

			if (a_outcome.primaryBackendId != ZlibBackendRegistryId(ZlibBackendKind::LibDeflate))
				return;

			switch (static_cast<ZlibFallbackReason>(a_outcome.fallbackReasonId))
			{
			case ZlibFallbackReason::None:
				g_threadCounters.Count(g_threadCounters.attempted);
				g_threadCounters.Count(g_threadCounters.succeeded);
				break;
			case ZlibFallbackReason::State:
				g_threadCounters.Count(g_threadCounters.rejectedByState);
				break;
			case ZlibFallbackReason::Allocation:
				g_threadCounters.Count(g_threadCounters.allocationFailed);
				break;
			case ZlibFallbackReason::Decode:
				g_threadCounters.Count(g_threadCounters.attempted);
				g_threadCounters.Count(g_threadCounters.codecFailed);
				break;
			case ZlibFallbackReason::Commit:
				g_threadCounters.Count(g_threadCounters.attempted);
				g_threadCounters.Count(g_threadCounters.commitRejected);
				break;
			default:
				break;
			}
		}

		void RecordOutcome(
			const ZlibInflateOutcome& a_outcome,
			uint64_t a_inputBytesAvailable,
			uint64_t a_outputBytesAvailable,
			const ZlibServeState& a_serve) noexcept
		{
			BA2Profile::CallObservation observation;
			observation.primaryBackendId = a_outcome.primaryBackendId;
			observation.fallbackBackendId = a_outcome.fallbackBackendId;
			observation.servedBackendId = a_outcome.servedBackendId;
			observation.fallbackReasonId = a_outcome.fallbackReasonId;
			observation.primaryQpc = a_outcome.primaryQpc;
			observation.fallbackQpc = a_outcome.fallbackQpc;
			observation.totalQpc = a_outcome.totalQpc;
			observation.qpcFrequency = a_outcome.qpcFrequency;
			observation.inputBytesAvailable = a_inputBytesAvailable;
			observation.outputBytesAvailable = a_outputBytesAvailable;
			observation.inputBytesConsumed = a_outcome.consumed;
			observation.outputBytesProduced = a_outcome.produced;
			observation.zlibResult = a_outcome.zlibResult;
			observation.primaryAttempted = a_outcome.primaryAttempted;
			observation.observationSiteId = BA2Profile::kSiteInflate;
			observation.callerId = a_serve.callerId;
			observation.threadId = static_cast<uint32_t>(GetCurrentThreadId());
			observation.requestSequence = a_serve.requestSequence;
			observation.streamAddress = a_serve.streamAddress;
			if (a_outcome.servedBackendId == a_outcome.primaryBackendId)
			{
				observation.primaryInputBytesConsumed =
					static_cast<uint32_t>(a_outcome.consumed);
				observation.primaryOutputBytesProduced =
					static_cast<uint32_t>(a_outcome.produced);
			}
			ProfilerBA2::GetSingleton()->Record(observation);
		}

		void LogCounters() noexcept
		{
			g_threadCounters.Flush();
			const auto& counters = GetAtomicCounters();
			REX::INFO(
				"Zlib backend counters (selected {}): served-stock {}, served-libdeflate {}, served-unknown {}, attempted {}, succeeded {}, rejected-by-state {}, codec-failed {}, commit-rejected {}, allocation-failed {}"sv,
				ZlibBackendKindName(GetSelectedZlibBackendKind()),
				counters.servedStock.load(std::memory_order_relaxed),
				counters.servedLibDeflate.load(std::memory_order_relaxed),
				counters.servedUnknown.load(std::memory_order_relaxed),
				counters.attempted.load(std::memory_order_relaxed),
				counters.succeeded.load(std::memory_order_relaxed),
				counters.rejectedByState.load(std::memory_order_relaxed),
				counters.codecFailed.load(std::memory_order_relaxed),
				counters.commitRejected.load(std::memory_order_relaxed),
				counters.allocationFailed.load(std::memory_order_relaxed));
		}

		namespace Decompression
		{
			// Inner calls of a stock replay are the request's own bytes; they are aggregated, not rowed.
			inline int32_t ServeReplay(
				ZlibInflate::Stream* a_stream,
				int32_t a_flush,
				ZlibReplayCapture& a_capture) noexcept
			{
				const auto inputBefore = a_stream->avail_in;
				const auto outputBefore = a_stream->avail_out;
				const auto start = a_capture.timingEnabled ? ReadQpc() : 0;
				const auto result = OriginalInflate(a_stream, a_flush);
				const auto end = a_capture.timingEnabled ? ReadQpc() : 0;
				const auto inputAfter = a_stream->avail_in;
				const auto outputAfter = a_stream->avail_out;
				a_capture.Account(
					end - start,
					inputAfter <= inputBefore ? inputBefore - inputAfter : 0,
					outputAfter <= outputBefore ? outputBefore - outputAfter : 0,
					result);
				return result;
			}

			template<class Backend>
			struct Selected
			{
				static int32_t Inflate(ZlibInflate::Stream* a_stream, int32_t a_flush) noexcept
				{
					auto& serve = CurrentZlibServe();
					if (serve.mode == ZlibServeMode::CaptureReplay && serve.capture &&
						a_stream && OriginalInflate)
						return ServeReplay(a_stream, a_flush, *serve.capture);

					const auto recording = ProfilerBA2::GetSingleton()->IsRecording();
					const auto qpcFrequency = recording ? GetQpcFrequency() : 0;
					const auto timingEnabled = recording && qpcFrequency != 0;
					const auto inputBytesAvailable = a_stream ? a_stream->avail_in : 0;
					const auto outputBytesAvailable = a_stream ? a_stream->avail_out : 0;
					if (!a_stream || !OriginalInflate)
					{
						if (timingEnabled)
						{
							ZlibInflateOutcome outcome;
							outcome.primaryBackendId = ZlibBackendRegistryId(Backend::kind);
							outcome.zlibResult = Z_STREAM_ERROR;
							outcome.qpcFrequency = qpcFrequency;
							const auto start = ReadQpc();
							const auto end = ReadQpc();
							outcome.totalQpc = end - start;
							RecordOutcome(outcome, inputBytesAvailable, outputBytesAvailable, serve);
						}
						return Z_STREAM_ERROR;
					}

					const auto original = [](ZlibInflate::Stream* a_stockStream, int32_t a_stockFlush) noexcept {
						return OriginalInflate(a_stockStream, a_stockFlush);
					};
					const auto clock = []() noexcept { return ReadQpc(); };
					// Delegated request work must not reattempt the codec, but is still observed.
					const auto outcome = serve.mode == ZlibServeMode::ForceStockObserved ?
						ServeZlib<StockZlibBackend>(
							a_stream, a_flush, original, timingEnabled, qpcFrequency, clock) :
						ServeZlib<Backend>(
							a_stream, a_flush, original, timingEnabled, qpcFrequency, clock);
					CountOutcome(outcome);
					if (timingEnabled)
						RecordOutcome(outcome, inputBytesAvailable, outputBytesAvailable, serve);

					return outcome.zlibResult;
				}
			};
		}
	}

	void InitializeZlibBackendConfig() noexcept
	{
		if (!bPatchesLibDeflate.GetValue())
		{
			ResolveZlibBackendSelection("stock"sv);
			REX::INFO("Zlib decompression backend: stock (bLibDeflate is disabled)."sv);
		}
		else
		{
			ResolveZlibBackendSelection(sAdditionalZlibBackend.GetValue());
		}
	}

	ModuleLibDeflate::ModuleLibDeflate() :
		Module("LibDeflate", &bPatchesLibDeflate, {
			F4SE::MessagingInterface::kPostLoadGame,
			F4SE::MessagingInterface::kPostSaveGame })
	{}

	const REX::TOML::Bool<>* ModuleLibDeflate::GetOption() const noexcept
	{
		if (ProfilerCore::IsEnabledInConfig() && ProfilerCore::IsBA2TimingEnabled())
			return nullptr;
		return &bPatchesLibDeflate;
	}

	bool ModuleLibDeflate::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleLibDeflate::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg) return false;
		using namespace zlibDetail;
		const auto runtime = RELEX::IsRuntimeOG() ? "OG"sv :
			(RELEX::IsRuntimeAE() ? "AE"sv : "NG"sv);

		if (g_patchState != PatchState::NotAttempted)
		{
			REX::ERROR(
				"LibDeflate: {} install re-entered after a detour attempt; the hooks are left exactly as they are."sv,
				runtime);
			return false;
		}

		const auto target = REL::ID{ 224011, 2168026 }.address();
		const auto code = std::span{
			reinterpret_cast<const uint8_t*>(target),
			ZlibInflate::Contract::VALIDATION_SIZE
		};
		const auto validation = ZlibInflate::ValidateContract(code);
		if (!validation)
		{
			REX::ERROR(
				"LibDeflate: {} zlib contract rejected: prologue={}, mode-load={}, mode-bounds={}, done-store={}, reset-zero={}, reset-store={}; effective backend stock (hook not installed)."sv,
				runtime,
				validation.prologue,
				validation.modeLoad,
				validation.modeBounds,
				validation.doneStore,
				validation.resetZero,
				validation.resetStore);
			return false;
		}

		const auto recording = ProfilerBA2::GetSingleton()->Start();
		if (!bPatchesLibDeflate.GetValue() && !recording)
		{
			Skip("BA2 profiler could not start"sv);
			return false;
		}

		// Every target is proven before the first write; a rejection here still leaves both untouched.
		const auto textureSelected = GetSelectedZlibBackendKind() == ZlibBackendKind::LibDeflate;
		const auto textureValidated = textureSelected &&
			TextureOneShot::Validate(runtime) == TextureOneShot::InstallState::Validated;
		if (!textureSelected)
		{
			REX::INFO(
				"LibDeflate: {} one-shot texture decompression stays off because the selected backend is {}."sv,
				runtime,
				ZlibBackendKindName(GetSelectedZlibBackendKind()));
		}

		const auto hook = VisitSelectedZlibBackend([]<class Backend>() {
			return reinterpret_cast<uintptr_t>(&Decompression::Selected<Backend>::Inflate);
		});
		g_patchState = PatchState::Attempted;
		OriginalInflate = reinterpret_cast<TInflate>(RELEX::DetourJump(target, hook));
		if (!OriginalInflate)
		{
			REX::ERROR(
				"LibDeflate: {} detour failed for selected backend {}; inflate may be left in an indeterminate state and the texture seam is not attempted."sv,
				runtime,
				ZlibBackendKindName(GetSelectedZlibBackendKind()));
			return false;
		}

		REX::INFO(
			"LibDeflate: {} zlib contract validated; backend {} enabled (mode +0x0, HEAD 0, DONE 28)."sv,
			runtime,
			ZlibBackendKindName(GetSelectedZlibBackendKind()));

		// The seam decodes whole requests through the same backend, so it is patched second by design.
		if (textureValidated &&
			TextureOneShot::InstallValidated() != TextureOneShot::InstallState::Installed)
		{
			REX::WARN(
				"LibDeflate: {} one-shot texture decompression is disabled; the validated inflate hook stays installed and textures decompress per chunk."sv,
				runtime);
		}
		else if (textureSelected && !textureValidated)
		{
			REX::WARN(
				"LibDeflate: {} one-shot texture decompression was not installed because its targets did not validate; the inflate hook stays installed and textures decompress per chunk."sv,
				runtime);
		}
		return true;
	}

	bool ModuleLibDeflate::DoListener(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg &&
			(a_msg->type == F4SE::MessagingInterface::kPostLoadGame ||
				a_msg->type == F4SE::MessagingInterface::kPostSaveGame))
		{
			zlibDetail::LogCounters();
			TextureOneShot::LogCounters();
		}

		return true;
	}

	bool ModuleLibDeflate::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}