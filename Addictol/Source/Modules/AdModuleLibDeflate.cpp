#include <Modules/AdModuleLibDeflate.h>
#include <Core/AdUtils.h>
#include <Zlib/AdZlibBackend.h>
#include <Zlib/AdZlibInflate.h>
#include <Zlib/AdZlibTelemetry.h>
#include <Windows.h>

#ifdef ERROR
#	undef ERROR
#endif

namespace Addictol
{


	namespace zlibDetail
	{
		constexpr static int32_t Z_STREAM_ERROR		= -2;

		using TInflate = int32_t(*)(ZlibInflate::Stream*, int32_t) noexcept;
		TInflate OriginalInflate;

		// Set before the first Detours call, so a retry cannot re-enter.
		enum class PatchState : uint8_t
		{
			NotAttempted,
			Attempted
		};

		PatchState g_patchState{ PatchState::NotAttempted };

		namespace Decompression
		{
			template<class Backend>
			struct Selected
			{
				static int32_t Inflate(ZlibInflate::Stream* a_stream, int32_t a_flush) noexcept
				{
					if (!a_stream || !OriginalInflate)
						return Z_STREAM_ERROR;

					const auto original = [](ZlibInflate::Stream* a_stockStream, int32_t a_stockFlush) noexcept {
						return OriginalInflate(a_stockStream, a_stockFlush);
					};
					const auto clock = []() noexcept {
						return TelemetryDetail::ReadQpc();
					};
					const auto bytesInBefore = a_stream->total_in;
					const auto bytesOutBefore = a_stream->total_out;
					const auto outcome = [&] {
						if constexpr (Backend::kind == ZlibBackendKind::LibDeflate)
						{
							return TelemetryDetail::ServeTelemetryZlib<Backend>(
								a_stream,
								a_flush,
								original,
								clock,
								[]() noexcept { return GetCurrentThreadId(); },
								[&](const ZlibInflateOutcome& a_outcome,
									bool a_enabled,
									uint32_t a_threadId) noexcept {
									ModuleLibDeflate::Record(
										a_outcome,
										a_enabled,
										a_flush,
										a_threadId,
										static_cast<uint32_t>(
											a_stream->total_in - bytesInBefore),
										static_cast<uint32_t>(
											a_stream->total_out - bytesOutBefore));
								});
						}
						else
							return ServeZlib<Backend>(
								a_stream, a_flush, original, false, 0, clock);
					}();
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
			ResolveZlibBackendSelection(sAdditionalZlibBackend.GetValue());
	}

	ModuleLibDeflate::ModuleLibDeflate() :
		Module("LibDeflate", &bPatchesLibDeflate)
	{
		s_instance = this;
	}

	const REX::TOML::Bool<>* ModuleLibDeflate::GetOption() const noexcept
	{
		return &bPatchesLibDeflate;
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

		const auto hook = VisitSelectedZlibBackend([]<class Backend>() {
			return reinterpret_cast<uintptr_t>(&Decompression::Selected<Backend>::Inflate);
		});
		g_patchState = PatchState::Attempted;
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

		m_active.store(
			GetSelectedZlibBackendKind() == ZlibBackendKind::LibDeflate,
			std::memory_order_relaxed);
		return true;
	}

	std::span<const MetricDescriptor> ModuleLibDeflate::Schema() const noexcept
	{
		return ZlibIntervalCounters::Schema();
	}

	size_t ModuleLibDeflate::SeriesCapacity() const noexcept
	{
		return kSeriesCapacity;
	}

	void ModuleLibDeflate::Record(
		const ZlibInflateOutcome& a_outcome,
		bool a_telemetryEnabled,
		int32_t a_flush,
		uint32_t a_currentThreadId,
		uint64_t a_bytesIn,
		uint64_t a_bytesOut) noexcept
	{
		if (!a_telemetryEnabled || !s_instance)
			return;
		const auto fallbackReason =
			static_cast<ZlibFallbackReason>(a_outcome.fallbackReasonId);
		Telemetry::ObserveZlibCall(
			s_instance->m_interval,
			a_telemetryEnabled,
			fallbackReason,
			a_outcome.servedBackendId ==
				ZlibBackendRegistryId(ZlibBackendKind::LibDeflate),
			a_flush,
			a_currentThreadId,
			a_bytesIn,
			a_bytesOut,
			a_outcome.totalQpc);
	}

	void ModuleLibDeflate::Drain(std::span<MetricValue> a_out) noexcept
	{
		if (a_out.size() != Schema().size())
			return;
		const auto packed = m_interval.Drain();
		const auto bytesIn = m_interval.DrainBytesIn();
		const auto bytesOut = m_interval.DrainBytesOut();
		const auto fallbackBytesOut = m_interval.DrainFallbackBytesOut();
		const auto active = m_active.load(std::memory_order_relaxed);
		a_out[0] = { static_cast<double>(static_cast<uint32_t>(packed)), active };
		a_out[1] = { static_cast<double>(packed >> 32), active };
		a_out[2] = { static_cast<double>(bytesOut), active };
		a_out[3] = { static_cast<double>(bytesIn), active };
		a_out[4] = { static_cast<double>(fallbackBytesOut), active };
		for (size_t index = 0; index < ZlibIntervalCounters::kFallbackReasonCount; ++index)
		{
			a_out[index + 5] = {
				static_cast<double>(m_interval.DrainFallbackReason(index)),
				active
			};
		}
	}

	size_t ModuleLibDeflate::DrainSeries(
		std::span<SeriesSample> a_out) noexcept
	{
		if (a_out.size() != SeriesCapacity())
			return 0;
		size_t offset{ 0 };
		const auto append = [&]<size_t N>(
			std::string_view a_series,
			const std::array<std::string_view, N>& a_labels,
			const std::array<HistogramBucket, N>& a_histogram) noexcept {
			for (size_t index = 0; index < N; ++index)
			{
				if (!AppendSeriesSample(a_out, offset, {
					a_series,
					a_labels[index],
					a_histogram[index].calls,
					a_histogram[index].ticks,
					a_histogram[index].bytes
				}))
					break;
			}
		};

		append(
			"zlib.served.libdeflate",
			kSizeBucketLabels,
			m_interval.servedLibDeflateOutput.Drain());
		append(
			"zlib.served.stock",
			kSizeBucketLabels,
			m_interval.servedStockOutput.Drain());
		append(
			"zlib.input.libdeflate",
			kSizeBucketLabels,
			m_interval.servedLibDeflateInput.Drain());
		append(
			"zlib.input.stock",
			kSizeBucketLabels,
			m_interval.servedStockInput.Drain());
		append(
			"zlib.fallback.thread",
			kThreadBucketLabels,
			m_interval.fallbackThread.Drain());
		append(
			"zlib.served.thread",
			kThreadBucketLabels,
			m_interval.servedThread.Drain());
		append("zlib.flush", kFlushBucketLabels, m_interval.flush.Drain());
		return std::min(offset, a_out.size());
	}

}