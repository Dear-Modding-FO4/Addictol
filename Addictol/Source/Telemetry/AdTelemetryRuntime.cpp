#include <Menu/AdMenu.h>
#include <Core/AdModuleManager.h>
#include <Platform/AdPlatformImgui.h>
#include <Telemetry/AdTelemetryHub.h>
#include <Core/AdUtils.h>
#include <Menu/AdMenuTelemetry.h>

#include <REX/REX.h>
#include <Windows.h>

#undef ERROR

namespace Addictol
{
	namespace
	{
		REX::TOML::Bool<> bTelemetryEnabled{ "Telemetry"sv, "bEnabled"sv, false };
		REX::TOML::U32<> uTelemetrySampleMs{ "Telemetry"sv, "uSampleMs"sv, 1000 };
		REX::TOML::U32<> uTelemetryFrameRecordMs{
			"Telemetry"sv, "uFrameRecordMs"sv, 50
		};
		REX::TOML::Bool<> bTelemetryCsv{ "Telemetry"sv, "bCsv"sv, false };

		std::atomic<FrameMetricSource*> s_frameSource{ nullptr };
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
	}

	TelemetryHub& Telemetry::Hub() noexcept
	{
		static TelemetryHub hub{ TelemetryDetail::QpcFrequency() };
		return hub;
	}

	void Telemetry::Initialize(const ModuleManager& a_modules) noexcept
	{
		static std::once_flag once;
		std::call_once(once, [&a_modules] {
			auto& hub = Hub();
			s_moduleManager = &a_modules;
			auto frameSource = std::make_shared<FrameMetricSource>(
				hub, TelemetryDetail::QpcFrequency(),
				(std::max)(uTelemetryFrameRecordMs.GetValue(), 1u));
			const auto processMemoryRegistration =
				hub.Register(std::make_shared<ProcessMemoryMetricSource>());
			const auto gpuMemoryRegistration = hub.Register(
				std::make_shared<GpuVideoMemoryMetricSource>(&PlatformImgui::QueryVideoMemory));
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
			for (const auto& panel : kTelemetryPanels)
			{
				const auto registered = Menu::RegisterPanel({
					panel.name,
					&DrawMenuTelemetryPanel,
					&bTelemetryEnabled,
					&panel
				});
				panelsRegistered = registered && panelsRegistered;
			}
			if (!panelsRegistered)
				REX::ERROR("Telemetry: one or more menu panels could not be registered."sv);
			if (!bTelemetryEnabled.GetValue())
				return;

			std::filesystem::path csvPath;
			std::filesystem::path seriesCsvPath;
			if (bTelemetryCsv.GetValue())
			{
				csvPath = AdGetRuntimeDirectory() + "Data\\F4SE\\Plugins\\AddictolTelemetry.csv";
				seriesCsvPath =
					AdGetRuntimeDirectory() + "Data\\F4SE\\Plugins\\AddictolSeries.csv";
			}
			if (!hub.Start(
				(std::max)(uTelemetrySampleMs.GetValue(), 1u),
				std::move(csvPath),
				std::move(seriesCsvPath)))
				REX::ERROR("Telemetry: worker failed to start"sv);
		});
	}

	void Telemetry::ObserveFrame() noexcept
	{
		TelemetryDetail::CaptureRenderThread(&CurrentThreadId);
		TelemetryDetail::ObserveFrame(
			s_frameSource.load(std::memory_order_acquire),
			&TelemetryDetail::ReadQpc);
	}
}
