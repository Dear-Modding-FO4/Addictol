#include <Menu/AdMenu.h>
#include <Core/AdClock.h>
#include <Core/AdModuleManager.h>
#include <Telemetry/AdTelemetryHub.h>
#include <Core/AdUtils.h>
#include <Menu/AdMenuTelemetry.h>

#include <REX/REX.h>
#include <Windows.h>
#include <resource_version2.h>

#undef ERROR

namespace Addictol
{
	namespace
	{
		std::atomic<FrameMetricSource*> s_frameSource{ nullptr };
		std::atomic<dmui::Client*> s_dmuiClient{ nullptr };
		const ModuleManager* s_moduleManager{ nullptr };

		[[nodiscard]] uint32_t CurrentThreadId() noexcept
		{
			return GetCurrentThreadId();
		}

		[[nodiscard]] bool ReadModuleOutcomes(
			ModuleOutcomeMetricSource::Values& a_values) noexcept
		{
			if (!s_moduleManager)
				return false;
			a_values = ModuleOutcomeMetricValues(s_moduleManager->ModuleOutcomeCounts());
			return true;
		}

		[[nodiscard]] bool ReadVideoMemory(
			uint64_t& a_used,
			uint64_t& a_budget) noexcept
		{
			const auto client = s_dmuiClient.load(std::memory_order_acquire);
			if (!client)
				return false;
			const auto info = client->QueryVideoMemory();
			if (!info)
				return false;
			a_used = info->used;
			a_budget = info->budget;
			return true;
		}

		[[nodiscard]] std::string_view RuntimeLabel() noexcept
		{
			if (RELEX::IsRuntimeOG())
				return "OG";
			if (RELEX::IsRuntimeNG())
				return "NG";
			if (RELEX::IsRuntimeAE())
				return "AE";
			return "unknown";
		}
	}

	void Telemetry::Initialize(const ModuleManager& a_modules) noexcept
	{
		static std::once_flag once;
		std::call_once(once, [&a_modules] {
			auto& hub = Hub();
			const auto telemetryEnabled = bTelemetryEnabled.GetValue();
			const auto profilingEnabled = bTelemetryOperationProfiling.GetValue();
			s_moduleManager = &a_modules;
			auto frameSource = std::make_shared<FrameMetricSource>(
				hub, Addictol::GetQpcFrequency(),
				(std::max)(uTelemetryFrameRecordMs.GetValue(), 1u));
			const auto processMemoryRegistration =
				hub.Register(std::make_shared<ProcessMemoryMetricSource>());
			const auto gpuMemoryRegistration = hub.Register(
				std::make_shared<GpuVideoMemoryMetricSource>(&ReadVideoMemory));
			const auto systemMemoryRegistration =
				hub.Register(std::make_shared<SystemMemoryMetricSource>());
			const auto moduleOutcomeRegistration = hub.Register(
				std::make_shared<ModuleOutcomeMetricSource>(
					kModuleOutcomeMetricSchema, &ReadModuleOutcomes));
			const auto frameRegistration = hub.Register(frameSource);
			if (processMemoryRegistration != TelemetryRegistration::kAccepted ||
				gpuMemoryRegistration != TelemetryRegistration::kAccepted ||
				systemMemoryRegistration != TelemetryRegistration::kAccepted ||
				moduleOutcomeRegistration != TelemetryRegistration::kAccepted ||
				frameRegistration != TelemetryRegistration::kAccepted)
			{
				REX::ERROR("Telemetry: source registration failed; collection is disabled"sv);
				return;
			}
			s_frameSource.store(frameSource.get(), std::memory_order_release);
			if (!hub.Freeze())
			{
				REX::ERROR("Telemetry: freeze failed; collection is disabled"sv);
				return;
			}

			auto panelsRegistered = true;
			if (telemetryEnabled || profilingEnabled)
			{
				for (const auto& panel : kTelemetryPanels)
				{
					const auto registered = Menu::RegisterPanel({
						panel.page,
						&DrawMenuTelemetryPanel,
						nullptr,
						const_cast<TelemetryPanelDefinition*>(&panel)
					});
					panelsRegistered = registered && panelsRegistered;
				}
			}
			if (!panelsRegistered)
				REX::ERROR("Telemetry: one or more menu panels could not be registered."sv);
			if (!telemetryEnabled && !profilingEnabled)
				return;

			TelemetryStartOptions options{};
			options.cadenceMs =
				(std::max)(uTelemetrySampleMs.GetValue(), 1u);
			options.ordinaryTelemetryEnabled = telemetryEnabled;
			if (telemetryEnabled && bTelemetryCsv.GetValue())
			{
				options.csvPath =
					AdGetRuntimeDirectory() + "Data\\F4SE\\Plugins\\AddictolTelemetry.csv";
				options.seriesCsvPath =
					AdGetRuntimeDirectory() + "Data\\F4SE\\Plugins\\AddictolSeries.csv";
			}
			if (profilingEnabled)
			{
				options.captureRoot =
					AdGetRuntimeDirectory() +
					"Data\\F4SE\\Plugins\\Addictol\\Captures";
				options.productVersion = VER_PRODUCT_VERSION_STR;
				options.runtime = RuntimeLabel();
			}
			if (!hub.Start(std::move(options)))
				REX::ERROR("Telemetry: worker failed to start"sv);
		});
	}

	bool Telemetry::ConnectDearModdingUI(dmui::Client& a_client) noexcept
	{
		if (!bTelemetryEnabled.GetValue() &&
			!bTelemetryOperationProfiling.GetValue())
			return true;
		const auto observer = a_client.AddFrameObserver([] {
			ObserveFrame();
		});
		if (!observer)
			return false;
		s_dmuiClient.store(&a_client, std::memory_order_release);
		return true;
	}

	void Telemetry::ObserveFrame() noexcept
	{
		TelemetryDetail::CaptureRenderThread(&CurrentThreadId);
		TelemetryDetail::ObserveFrame(
			s_frameSource.load(std::memory_order_acquire),
			&Addictol::ReadQpc);
	}
}
