#include <Core/Settings/AdSettings.h>
#include <DearModdingUI/HostSettings.h>

namespace Addictol
{
	using namespace std::literals;

	BoolSetting bAdditionalDbgFacegenOutput{
		"Additional"sv,
		"bDbgFacegenOutput"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		"Debugging messages about the presence of facegen in the NPC in console and log (needs bFacegen)."sv,
		SettingApplyTiming::kImmediate
	};

	BoolSetting bAdditionalUseNewRedistributable{
		"Additional"sv,
		"bUseNewRedistributable"sv,
		SettingDisplayCategory::kStability,
		true,
		"Replaces the old redistributable with a new one. If option is enabled, reports will include Addictol in case of errors related to copying or comparing memory (needs bMemoryManager)."sv,
		SettingApplyTiming::kNextLaunch
	};

	StrSetting sAdditionalAllocator{
		"Additional"sv,
		"sAllocator"sv,
		SettingDisplayCategory::kPerformance,
		"voltek",
		"Selects the allocator backend: voltek."sv,
		SettingApplyTiming::kNextLaunch
	};

	StrSetting sAdditionalZlibBackend{
		"Additional"sv,
		"sZlibBackend"sv,
		SettingDisplayCategory::kPerformance,
		"libdeflate",
		"Selects the zlib decompression backend: libdeflate or stock (needs bLibDeflate). One-shot texture decompression requires libdeflate."sv,
		SettingApplyTiming::kNextLaunch
	};

	StrSetting sAdditionalLogLevel{
		"Additional"sv,
		"sLogLevel"sv,
		SettingDisplayCategory::kDiagnostics,
		"info",
		"Sets the runtime log level: trace, debug, info, warn, error, critical, or off."sv,
		SettingApplyTiming::kNextLaunch
	};

	StrSetting sAdditionalLogFlushLevel{
		"Additional"sv,
		"sLogFlushLevel"sv,
		SettingDisplayCategory::kDiagnostics,
		"info",
		"Flushes the log after messages at this level or higher: trace, debug, info, warn, error, critical, or off."sv,
		SettingApplyTiming::kNextLaunch
	};

	U32Setting uAdditionalScaleformPageSize{
		"Additional"sv,
		"uScaleformPageSize"sv,
		SettingDisplayCategory::kPerformance,
		64ul,
		"The page size (in KB), vanilla size is 64. More, better, but the higher the memory consumption. Limit 2Mb (2048), number must be a multiple of 8 (needs bScaleformAllocator)."sv,
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ 64.0, 2048.0 }
	};

	U32Setting uAdditionalScaleformHeapSize{
		"Additional"sv,
		"uScaleformHeapSize"sv,
		SettingDisplayCategory::kPerformance,
		2048ul,
		"The heap size (in MB), vanilla size is 2048. This is all the available memory, out of memory = CTD. Limit 8Gb (8192), number must be a multiple of 8 (needs bScaleformAllocator)."sv,
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ 2048.0, 8192.0 }
	};

	I32Setting nAdditionalSleepTimer{
		"Additional"sv,
		"nSleepTimer"sv,
		SettingDisplayCategory::kStability,
		125,
		"Sampling interval in milliseconds for Escape Freeze (needs bEscapeFreeze)."sv,
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ 1.0, 60000.0 }
	};

	I32Setting nAdditionalMaxLockCount{
		"Additional"sv,
		"nMaxLockCount"sv,
		SettingDisplayCategory::kStability,
		8,
		"Sampling threshold multiplier; total threshold is nSleepTimer x nMaxLockCount (needs bEscapeFreeze)."sv,
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ 1.0, 1000000.0 }
	};

	BoolSetting bAdditionalMultiThreading{
		"Additional"sv,
		"bInteriorNavCutMultiThreading"sv,
		SettingDisplayCategory::kPerformance,
		true,
		"Enable InteriorNavCut MultiThreading, automatically disabled on Linux / Proton (needs bInteriorNavCut)."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bAdditionalFullPrecisionDecalsMembrane{
		"Additional"sv,
		"bFullPrecisionDecalsMembrane"sv,
		SettingDisplayCategory::kVisuals,
		true,
		"Fixes membrane shaders on full-precision meshes with a cached compact vertex buffer (needs bFullPrecisionDecals)."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bAdditionalFullPrecisionDecalsEffectShaders{
		"Additional"sv,
		"bFullPrecisionDecalsEffectShaders"sv,
		SettingDisplayCategory::kVisuals,
		true,
		"Fixes effect-shader particles on full-precision meshes (needs bFullPrecisionDecals)."sv,
		SettingApplyTiming::kNextLaunch
	};

	I32Setting nAdditionalMaxPapyrusOpsPerFrame{
		"Additional"sv,
		"nMaxPapyrusOpsPerFrame"sv,
		SettingDisplayCategory::kPerformance,
		500,
		"Maximum papyrus operations per frame. Higher number means better script performance on average."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bAdditionalIgnorePreInstallBias{
		"Additional"sv,
		"bIgnorePreInstallBias"sv,
		SettingDisplayCategory::kVisuals,
		false,
		"Ignore the previously preset value for texture quality distance, if this sets as false, should hopefully reduce the chance of causing rendering errors."sv,
		SettingApplyTiming::kNextLaunch
	};

	I32Setting nAdditionalQuitGameDelayMs{
		"Additional"sv,
		"nQuitGameDelayMs"sv,
		SettingDisplayCategory::kStability,
		2000,
		"Delay (ms) before the deferred quit-to-desktop flag is set. Lets the UI/menu unwind so cleanup can't deadlock (needs bSafeExit)."sv,
		SettingApplyTiming::kImmediate
	};

	I32Setting nAdditionalBloomScale{
		"Additional"sv,
		"nBloomScale"sv,
		SettingDisplayCategory::kVisuals,
		2,
		"Bloom render-target downsample factor (needs bHighResBloom). 1 = full screen (highest quality, highest GPU cost), 2 = half (recommended), 4 = vanilla quarter, 8 = eighth."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bAdditionalIgnoreCompatibilityChecks{
		"Additional"sv,
		"bIgnoreCompatibilityChecks"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		"Ignore Mod Compatibility Checks."sv,
		SettingApplyTiming::kNextLaunch
	};

	F32Setting fAdditionalLocalMapScaleFactor{
		"Additional"sv,
		"fLocalMapScaleFactor"sv,
		SettingDisplayCategory::kVisuals,
		1.5f,
		"Local Map Scale Factor (needs bHighResLocalMaps)."sv,
		SettingApplyTiming::kImmediate
	};

	BoolSetting bAdditionalMenu{
		"Additional"sv,
		"bMenu"sv,
		SettingDisplayCategory::kInterface,
		true,
		"In-game diagnostics window drawn over the game. Tabs appear in module name order; Log Control is always last and is always available."sv,
		SettingApplyTiming::kNextLaunch
	};

	StrSetting sAdditionalMenuToggleKey{
		"Additional"sv,
		"sMenuToggleKey"sv,
		SettingDisplayCategory::kInterface,
		"F11",
		"Key that opens and closes the menu: F1-F12, Home, End, Insert, or Delete (needs bMenu)."sv,
		SettingApplyTiming::kNextLaunch
	};

	U32Setting uAdditionalMenuRefreshMs{
		"Additional"sv,
		"uMenuRefreshMs"sv,
		SettingDisplayCategory::kInterface,
		250,
		"How often the open tab refreshes its copy of plugin data, in milliseconds. Clamped to 100-2000 (needs bMenu)."sv,
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ 100.0, 2000.0 }
	};

	BoolSetting bAdditionalMenuMonochromeIcons{
		"Additional"sv,
		"bMenuMonochromeIcons"sv,
		SettingDisplayCategory::kInterface,
		false,
		"Uses the theme text color for menu icons instead of colored accents (needs bMenu)."sv,
		SettingApplyTiming::kImmediate
	};

	StrSetting sAdditionalMenuAccentColor{
		"Additional"sv,
		"sMenuAccentColor"sv,
		SettingDisplayCategory::kInterface,
		"#42FA60",
		"Menu accent as a six-digit RGB hex color. Drives controls and colored menu icons (needs bMenu)."sv,
		SettingApplyTiming::kImmediate
	};

	F32Setting fAdditionalMenuWindowOpacity{
		"Additional"sv,
		"fMenuWindowOpacity"sv,
		SettingDisplayCategory::kInterface,
		DearModdingUI::kDefaultWindowBackgroundOpacity,
		"Host window background opacity from 0.20 to 1.00 (needs bMenu)."sv,
		SettingApplyTiming::kImmediate,
		SettingNumericRange{ 0.20, 1.0 }
	};

	BoolSetting bAdditionalMenuBackgroundBlur{
		"Additional"sv,
		"bMenuBackgroundBlur"sv,
		SettingDisplayCategory::kInterface,
		true,
		"Blurs the game behind the menu window (needs bMenu)."sv,
		SettingApplyTiming::kImmediate
	};

	F32Setting fAdditionalMenuBackgroundBlurStrength{
		"Additional"sv,
		"fMenuBackgroundBlurStrength"sv,
		SettingDisplayCategory::kInterface,
		DearModdingUI::kDefaultBackgroundBlurStrength,
		"Background blur sample spread from 0.10 to 1.00 (needs bMenuBackgroundBlur)."sv,
		SettingApplyTiming::kImmediate,
		SettingNumericRange{ 0.10, 1.0 }
	};

	F32Setting fAdditionalMenuUiScale{
		"Additional"sv,
		"fMenuUiScale"sv,
		SettingDisplayCategory::kInterface,
		DearModdingUI::Theme::kDefaultUserScale,
		"Accessibility scale applied on top of resolution-derived menu sizing, from 0.75 to 2.00 (needs bMenu)."sv,
		SettingApplyTiming::kImmediate,
		SettingNumericRange{ 0.75, 2.0 }
	};

	StrSetting sAdditionalMenuBodyFontFamily{
		"Additional"sv,
		"sMenuBodyFontFamily"sv,
		SettingDisplayCategory::kInterface,
		std::string{ DearModdingUI::kDefaultBodyFontFamily },
		"Body font family folder under Data/F4SE/Plugins/DearModdingUI/Fonts (needs bMenu)."sv,
		SettingApplyTiming::kImmediate
	};
}
