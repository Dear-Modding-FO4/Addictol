#include <Core/Settings/AdSettings.h>

namespace Addictol
{
	using namespace std::literals;

	BoolSetting bTelemetryEnabled{
		"Telemetry"sv,
		"bEnabled"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		"Enables low-overhead sampled telemetry."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bTelemetryOperationProfiling{
		"Telemetry"sv,
		"bOperationProfiling"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		"Enables sampled operation profiling captures on the next launch."sv,
		SettingApplyTiming::kNextLaunch
	};

	U32Setting uTelemetrySampleMs{
		"Telemetry"sv,
		"uSampleMs"sv,
		SettingDisplayCategory::kDiagnostics,
		1000,
		"Sets the telemetry sampling cadence in milliseconds."sv,
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ 1.0, std::nullopt }
	};

	U32Setting uTelemetryFrameRecordMs{
		"Telemetry"sv,
		"uFrameRecordMs"sv,
		SettingDisplayCategory::kDiagnostics,
		50,
		"Sets the threshold above which individual frames are recorded."sv,
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ 1.0, std::nullopt }
	};

	BoolSetting bTelemetryCsv{
		"Telemetry"sv,
		"bCsv"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		"Exports sampled telemetry to AddictolTelemetry.csv and AddictolSeries.csv."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bTelemetryPluginTiming{
		"Telemetry"sv,
		"bPluginTiming"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		"Times F4SE plugin exports and callbacks"sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bTelemetryFormLoadTiming{
		"Telemetry"sv,
		"bFormLoadTiming"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		"Times form compilation and construction"sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bTelemetryImageMemory{
		"Telemetry"sv,
		"bImageMemory"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		"Attributes allocation flow and live blocks of at least 64 KiB to non-system DLLs."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bTelemetryImageSampling{
		"Telemetry"sv,
		"bImageSampling"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		"Samples active thread stacks to estimate CPU time by DLL; unavailable under Wine."sv,
		SettingApplyTiming::kNextLaunch
	};

	U32Setting uTelemetryImageSampleHz{
		"Telemetry"sv,
		"uImageSampleHz"sv,
		SettingDisplayCategory::kDiagnostics,
		100,
		"Sets image CPU samples per second; higher rates increase diagnostic overhead (needs bImageSampling)."sv,
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ 10.0, 1000.0 }
	};
}
