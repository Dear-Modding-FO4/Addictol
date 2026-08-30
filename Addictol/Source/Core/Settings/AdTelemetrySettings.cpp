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
		"times f4se plugin query and load exports"sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bTelemetryFormLoadTiming{
		"Telemetry"sv,
		"bFormLoadTiming"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		"times form compilation and construction"sv,
		SettingApplyTiming::kNextLaunch
	};
}
