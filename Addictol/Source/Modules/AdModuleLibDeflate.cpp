#include <Modules/AdModuleLibDeflate.h>
#include <AdProfilerBA2.h>
#include <AdProfilerCore.h>
#include <AdUtils.h>
#include <AdZlibBackend.h>
#include <AdZlibInflate.h>
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

		using TInflate = int32_t(*)(ZlibInflate::Stream*, int32_t) noexcept;
		TInflate OriginalInflate;

		std::uint64_t ReadQpc() noexcept
		{
			LARGE_INTEGER value{};
			QueryPerformanceCounter(&value);
			return static_cast<std::uint64_t>(value.QuadPart);
		}

		std::uint64_t GetQpcFrequency() noexcept
		{
			static const auto frequency = []() noexcept {
				LARGE_INTEGER value{};
				return QueryPerformanceFrequency(&value) && value.QuadPart > 0 ?
					static_cast<std::uint64_t>(value.QuadPart) :
					std::uint64_t{ 0 };
			}();
			return frequency;
		}

		struct AtomicCounters
		{
			std::atomic<std::uint64_t> attempted{ 0 };
			std::atomic<std::uint64_t> succeeded{ 0 };
			std::atomic<std::uint64_t> rejectedByState{ 0 };
			std::atomic<std::uint64_t> codecFailed{ 0 };
			std::atomic<std::uint64_t> commitRejected{ 0 };
			std::atomic<std::uint64_t> allocationFailed{ 0 };
			std::atomic<std::uint64_t> servedStock{ 0 };
			std::atomic<std::uint64_t> servedLibDeflate{ 0 };
			std::atomic<std::uint64_t> servedUnknown{ 0 };
		};

		AtomicCounters& GetAtomicCounters() noexcept
		{
			static auto* counters = new AtomicCounters();
			return *counters;
		}

		struct ThreadCounters
		{
			static constexpr std::uint32_t FLUSH_THRESHOLD = 256;

			std::uint64_t attempted{ 0 };
			std::uint64_t succeeded{ 0 };
			std::uint64_t rejectedByState{ 0 };
			std::uint64_t codecFailed{ 0 };
			std::uint64_t commitRejected{ 0 };
			std::uint64_t allocationFailed{ 0 };
			std::uint64_t servedStock{ 0 };
			std::uint64_t servedLibDeflate{ 0 };
			std::uint64_t servedUnknown{ 0 };
			std::uint32_t pending{ 0 };

			~ThreadCounters() noexcept
			{
				Flush();
			}

			void Count(std::uint64_t& a_counter) noexcept
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
			template<class Backend>
			struct Selected
			{
				static int32_t Inflate(ZlibInflate::Stream* a_stream, int32_t a_flush) noexcept
				{
					if (!a_stream)
						return Z_STREAM_ERROR;
					if (!OriginalInflate)
						return Z_STREAM_ERROR;

					const auto timingRequested =
						ProfilerCore::GetSingleton()->IsActive() &&
						ProfilerCore::IsBA2TimingEnabled();
					const auto qpcFrequency = timingRequested ? GetQpcFrequency() : 0;
					const auto timingEnabled = timingRequested && qpcFrequency != 0;
					const auto outcome = ServeZlib<Backend>(
						a_stream,
						a_flush,
						[](ZlibInflate::Stream* a_stockStream, std::int32_t a_stockFlush) noexcept {
							return OriginalInflate(a_stockStream, a_stockFlush);
						},
						timingEnabled,
						qpcFrequency,
						[]() noexcept { return ReadQpc(); });
					CountOutcome(outcome);

					if (timingEnabled &&
						outcome.servedBackendId == ZlibBackendRegistryId(ZlibBackendKind::LibDeflate))
					{
						// Transitional v1 sink; P2 profiler wiring removes this millisecond view.
						const auto elapsedMs =
							static_cast<double>(outcome.primaryQpc) * 1000.0 /
							static_cast<double>(outcome.qpcFrequency);
						ProfilerBA2::GetSingleton()->RecordDecompression(
							outcome.consumed, outcome.produced, elapsedMs);
					}

					return outcome.zlibResult;
				}
			};
		}
	}

	void InitializeZlibBackendConfig() noexcept
	{
		ResolveZlibBackendSelection(sAdditionalZlibBackend.GetValue());
		if (!bPatchesLibDeflate.GetValue())
			REX::INFO("Zlib decompression backend: stock (bLibDeflate is disabled; hook not installed)."sv);
	}

	ModuleLibDeflate::ModuleLibDeflate() :
		Module("LibDeflate", &bPatchesLibDeflate, {
			F4SE::MessagingInterface::kPostLoadGame,
			F4SE::MessagingInterface::kPostSaveGame })
	{}

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
		const auto target = REL::ID{ 224011, 2168026 }.address();
		const auto code = std::span{
			reinterpret_cast<const std::uint8_t*>(target),
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

		const auto hook = VisitSelectedZlibBackend([]<class Backend>() {
			return reinterpret_cast<std::uintptr_t>(&Decompression::Selected<Backend>::Inflate);
		});
		OriginalInflate = reinterpret_cast<TInflate>(RELEX::DetourJump(target, hook));
		if (!OriginalInflate)
		{
			REX::ERROR(
				"LibDeflate: {} detour failed for selected backend {}; inflate may be left in an indeterminate state."sv,
				runtime,
				ZlibBackendKindName(GetSelectedZlibBackendKind()));
			return false;
		}

		REX::INFO(
			"LibDeflate: {} zlib contract validated; backend {} enabled (mode +0x0, HEAD 0, DONE 28)."sv,
			runtime,
			ZlibBackendKindName(GetSelectedZlibBackendKind()));
		return true;
	}

	bool ModuleLibDeflate::DoListener(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg &&
			(a_msg->type == F4SE::MessagingInterface::kPostLoadGame ||
				a_msg->type == F4SE::MessagingInterface::kPostSaveGame))
			zlibDetail::LogCounters();

		return true;
	}

	bool ModuleLibDeflate::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}