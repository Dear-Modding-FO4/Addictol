#include <Core/Settings/AdSettings.h>

namespace Addictol
{
	using namespace std::literals;

	dmui::localize::LocalizeString lsTelemetryEnabled(
		"$Setting_TelemetryEnabled",
		"Enables low-overhead sampled telemetry."
	);
	dmui::localize::LocalizeString lsTelemetryOperationProfiling(
		"$Setting_TelemetryOperationProfiling",
		"Enables sampled operation profiling captures on the next launch."
	);
	dmui::localize::LocalizeString lsTelemetrySampleMs(
		"$Setting_TelemetrySampleMs",
		"Sets the telemetry sampling cadence in milliseconds."
	);
	dmui::localize::LocalizeString lsTelemetryFrameRecordMs(
		"$Setting_TelemetryFrameRecordMs",
		"Sets the threshold above which individual frames are recorded."
	);
	dmui::localize::LocalizeString lsTelemetryCsv(
		"$Setting_TelemetryCsv",
		"Exports sampled telemetry to AddictolTelemetry.csv and AddictolSeries.csv."
	);
	dmui::localize::LocalizeString lsTelemetryPluginTiming(
		"$Setting_TelemetryPluginTiming",
		"Times F4SE plugin exports and callbacks."
	);
	dmui::localize::LocalizeString lsTelemetryFormLoadTiming(
		"$Setting_TelemetryFormLoadTiming",
		"Times form compilation and construction."
	);
	dmui::localize::LocalizeString lsTelemetrySaveLoadTiming(
		"$Setting_TelemetrySaveLoadTiming",
		"Times save loading: the native load steps, Papyrus restore, and the loading screen."
	);
	dmui::localize::LocalizeString lsTelemetryImageMemory(
		"$Setting_TelemetryImageMemory",
		"Attributes allocation flow and live blocks of at least 64 KiB to non-system DLLs."
	);
	dmui::localize::LocalizeString lsTelemetryImageSampling(
		"$Setting_TelemetryImageSampling",
		"Samples active thread stacks to estimate CPU time by DLL; unavailable under Wine."
	);
	dmui::localize::LocalizeString lsTelemetryImageSampleHz(
		"$Setting_TelemetryImageSampleHz",
		"Sets image CPU samples per second; higher rates increase diagnostic overhead (needs bImageSampling)."
	);

	BoolSetting bTelemetryEnabled{
		"Telemetry"sv,
		"bEnabled"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		std::addressof(lsTelemetryEnabled),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bTelemetryOperationProfiling{
		"Telemetry"sv,
		"bOperationProfiling"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		std::addressof(lsTelemetryOperationProfiling),
		SettingApplyTiming::kNextLaunch
	};

	U32Setting uTelemetrySampleMs{
		"Telemetry"sv,
		"uSampleMs"sv,
		SettingDisplayCategory::kDiagnostics,
		1000,
		std::addressof(lsTelemetrySampleMs),
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ 1.0, std::nullopt }
	};

	U32Setting uTelemetryFrameRecordMs{
		"Telemetry"sv,
		"uFrameRecordMs"sv,
		SettingDisplayCategory::kDiagnostics,
		50,
		std::addressof(lsTelemetryFrameRecordMs),
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ 1.0, std::nullopt }
	};

	BoolSetting bTelemetryCsv{
		"Telemetry"sv,
		"bCsv"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		std::addressof(lsTelemetryCsv),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bTelemetryPluginTiming{
		"Telemetry"sv,
		"bPluginTiming"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		std::addressof(lsTelemetryPluginTiming),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bTelemetryFormLoadTiming{
		"Telemetry"sv,
		"bFormLoadTiming"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		std::addressof(lsTelemetryFormLoadTiming),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bTelemetrySaveLoadTiming{
		"Telemetry"sv,
		"bSaveLoadTiming"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		std::addressof(lsTelemetrySaveLoadTiming),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bTelemetryImageMemory{
		"Telemetry"sv,
		"bImageMemory"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		std::addressof(lsTelemetryImageMemory),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bTelemetryImageSampling{
		"Telemetry"sv,
		"bImageSampling"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		std::addressof(lsTelemetryImageSampling),
		SettingApplyTiming::kNextLaunch
	};

	U32Setting uTelemetryImageSampleHz{
		"Telemetry"sv,
		"uImageSampleHz"sv,
		SettingDisplayCategory::kDiagnostics,
		100,
		std::addressof(lsTelemetryImageSampleHz),
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ 10.0, 1000.0 }
	};
}
