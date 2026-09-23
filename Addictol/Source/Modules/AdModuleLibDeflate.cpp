#include <Modules/AdModuleLibDeflate.h>
#include <Core/AdUtils.h>
#include <Zlib/AdZlibBackendRegistry.h>
#include <Zlib/AdZlibHooks.h>

namespace Addictol
{
	void InitializeZlibBackendConfig() noexcept
	{
		ResolveZlibBackendSelection(bPatchesLibDeflate.GetValue() ? sAdditionalZlibBackend.GetValue() : "stock");
	}

	ModuleLibDeflate::ModuleLibDeflate() : Module("LibDeflate", &bPatchesLibDeflate) { s_instance = this; }
	const REX::TOML::Bool<>* ModuleLibDeflate::GetOption() const noexcept { return &bPatchesLibDeflate; }

	bool ModuleLibDeflate::DoInstall(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg) return false;
		if (GetSelectedZlibBackendKind() == ZlibBackendKind::Stock)
			return true;
		static bool attempted{};
		if (attempted)
		{
			REX::ERROR("Owned zlib: installation already attempted.");
			return false;
		}
		attempted = true;
		const auto fail = [](std::string_view a_reason) {
			REX::ERROR("Owned zlib: {}; nothing installed, effective backend stock.", a_reason);
			ResolveZlibBackendSelection("stock");
			return false;
		};
		const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
		if (version != REL::Version{ 1, 10, 163, 0 } && version != REL::Version{ 1, 10, 984, 0 } &&
			version != REL::Version{ 1, 11, 240, 0 })
			return fail("unsupported runtime");
		const auto anchor = REL::ID{ 224011, 2168026, 2168026 }.address();
		const auto image = std::span{ reinterpret_cast<const uint8_t*>(anchor), ZLIB_INSTALL_IMAGE_SIZE };
		for (size_t index = 0; index < ZLIB_ENTRIES.size(); ++index)
		{
			const auto& entry = ZLIB_ENTRIES[index];
			const auto address = entry.og ? REL::ID{ entry.og, entry.ng, entry.ae }.address() : anchor + entry.offset;
			if (address != anchor + entry.offset)
				return fail(entry.name);
			ZlibHooks::targets[index].original = reinterpret_cast<void*>(address);
			ZlibHooks::targets[index].expected = { entry.prologue.begin(), entry.prologue.size() };
		}
		const auto rejected = InstallValidatedZlib(image, [&] {
			const bool profiling = bTelemetryOperationProfiling.GetValue() &&
				InitializeZlibOperationProfile(Telemetry::Hub(), GetSelectedZlibBackendKind());
			if (bTelemetryOperationProfiling.GetValue() && !profiling)
				REX::WARN("Owned zlib: operation profiling registration failed; hooks remain uninstrumented.");
			VisitSelectedZlibBackend([&]<class Backend> {
				if constexpr (Backend::kind != ZlibBackendKind::Stock)
				{
					(void)InstallSelectedOperationProfileConsumer(profiling, []<bool Profile> {
						ZlibHooks::Selected<Backend, Profile>::Select();
						return true;
					});
				}
			});
			const auto result = RELEX::DetourBatch(ZlibHooks::targets);
			if (!result)
				REX::ERROR("Owned zlib: Detours transaction error {} at target {}.", result.error, result.target);
			return static_cast<bool>(result);
		});
		if (rejected)
			return fail(*rejected);
		m_active.store(true, std::memory_order_relaxed);
		REX::INFO("Owned zlib: all 15 entries installed transactionally; backend {}.", ZlibBackendKindName(GetSelectedZlibBackendKind()));
		return true;
	}

	std::span<const MetricDescriptor> ModuleLibDeflate::Schema() const noexcept { return ZlibIntervalCounters::Schema(); }
	size_t ModuleLibDeflate::SeriesCapacity() const noexcept { return kSeriesCapacity; }

	void ModuleLibDeflate::Record(const ZlibInflateOutcome& a_outcome, bool a_enabled, int32_t a_flush,
		uint32_t a_thread, uint64_t a_input, uint64_t a_output) noexcept
	{
		if (!a_enabled || !s_instance) return;
		Telemetry::ObserveZlibCall(s_instance->m_interval, a_enabled,
			static_cast<ZlibFallbackReason>(a_outcome.fallbackReasonId),
			a_outcome.policy == ZlibOwnedPolicy::Whole, a_flush, a_thread, a_input, a_output, a_outcome.totalQpc);
	}

	void ModuleLibDeflate::Drain(std::span<MetricValue> a_out) noexcept
	{
		if (a_out.size() != Schema().size()) return;
		const auto packed = m_interval.Drain();
		// Counters only record with ordinary telemetry; profiling-only captures must not report them as zero.
		const auto active = m_active.load(std::memory_order_relaxed) && Telemetry::EnabledRelaxed();
		a_out[0] = { static_cast<double>(static_cast<uint32_t>(packed)), active };
		a_out[1] = { static_cast<double>(packed >> 32), active };
		a_out[2] = { static_cast<double>(m_interval.DrainBytesOut()), active };
		a_out[3] = { static_cast<double>(m_interval.DrainBytesIn()), active };
		a_out[4] = { static_cast<double>(m_interval.DrainFallbackBytesOut()), active };
		for (size_t index = 0; index < ZlibIntervalCounters::kFallbackReasonCount; ++index)
			a_out[index + 5] = { static_cast<double>(m_interval.DrainFallbackReason(index)), active };
	}

	size_t ModuleLibDeflate::DrainSeries(std::span<SeriesSample> a_out) noexcept
	{
		if (a_out.size() != SeriesCapacity()) return 0;
		size_t offset{};
		const auto append = [&]<size_t N>(std::string_view a_series, const std::array<std::string_view, N>& a_labels,
			const std::array<HistogramBucket, N>& a_histogram) {
			for (size_t index = 0; index < N; ++index)
				if (!AppendSeriesSample(a_out, offset, { a_series, a_labels[index], a_histogram[index].calls, a_histogram[index].ticks, a_histogram[index].bytes }))
					break;
		};
		append("zlib.served.whole", kSizeBucketLabels, m_interval.servedWholeOutput.Drain());
		append("zlib.served.streaming", kSizeBucketLabels, m_interval.servedStreamingOutput.Drain());
		append("zlib.input.whole", kSizeBucketLabels, m_interval.servedWholeInput.Drain());
		append("zlib.input.streaming", kSizeBucketLabels, m_interval.servedStreamingInput.Drain());
		append("zlib.streaming.thread", kThreadBucketLabels, m_interval.fallbackThread.Drain());
		append("zlib.served.thread", kThreadBucketLabels, m_interval.servedThread.Drain());
		append("zlib.flush", kFlushBucketLabels, m_interval.flush.Drain());
		return offset;
	}
}
