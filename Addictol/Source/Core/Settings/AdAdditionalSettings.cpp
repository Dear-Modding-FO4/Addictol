#include <Core/Settings/AdSettings.h>
#include <Zlib/AdZlibBackend.h>

namespace Addictol
{
	using namespace std::literals;

	static dmui::localize::LocalizeString lsAdditionalDbgFacegenOutput(
		"$Setting_AdditionalDbgFacegenOutput",
		"Debugging messages about the presence of facegen in the NPC in console and log (needs bFacegen)."
	);
	static dmui::localize::LocalizeString lsAdditionalUseNewRedistributable(
		"$Setting_AdditionalUseNewRedistributable",
		"Redirects OG's imported memory routines to Addictol's CRT; memory faults may be attributed to Addictol (needs bMemoryManager)."
	);
	static dmui::localize::LocalizeString lsAdditionalAllocator(
		"$Setting_AdditionalAllocator",
		"Selects the allocator backend: stock (the game's own allocators), voltek, mimalloc, or rpmalloc."
	);
	static dmui::localize::LocalizeString lsAdditionalZlibBackend(
		"$Setting_AdditionalZlibBackend",
		"Selects the zlib decompression backend: stock, zlib, zlib-ng, isa-l, hybrid-zlib-ng, or hybrid-isa-l. Hybrid modes try libdeflate first when a stream fits in one call (needs bLibDeflate)."
	);
	static dmui::localize::LocalizeString lsAdditionalLogLevel(
		"$Setting_AdditionalLogLevel",
		"Sets the runtime log level: trace, debug, info, warn, error, critical, or off."
	);
	static dmui::localize::LocalizeString lsAdditionalLogFlushLevel(
		"$Setting_AdditionalLogFlushLevel",
		"Flushes the log after messages at this level or higher: trace, debug, info, warn, error, critical, or off."
	);
	static dmui::localize::LocalizeString lsAdditionalScaleformPageSize(
		"$Setting_AdditionalScaleformPageSize",
		"The page size (in KB), vanilla size is 64. More, better, but the higher the memory consumption. Limit 2Mb (2048), rounded down to a power of 2 (needs bScaleformAllocator)."
	);
	static dmui::localize::LocalizeString lsAdditionalScaleformHeapSize(
		"$Setting_AdditionalScaleformHeapSize",
		"The heap size (in MB), vanilla size is 2048. This is all the available memory, out of memory = CTD. Limit 8Gb (8192), number must be a multiple of 8 (needs bScaleformAllocator)."
	);
	static dmui::localize::LocalizeString lsAdditionalFullPrecisionDecalsEffectShaders(
		"$Setting_AdditionalFullPrecisionDecalsEffectShaders",
		"Fixes effect-shader particles on full-precision meshes (needs bFullPrecisionDecals)."
	);
	static dmui::localize::LocalizeString lsAdditionalMaxPapyrusOpsPerFrame(
		"$Setting_AdditionalMaxPapyrusOpsPerFrame",
		"Maximum papyrus operations per frame. Higher number means better script performance on average."
	);
	static dmui::localize::LocalizeString lsAdditionalBloomScale(
		"$Setting_AdditionalBloomScale",
		"Bloom render-target downsample factor (needs bHighResBloom). 1 = full screen (highest quality, highest GPU cost), 2 = half (recommended), 4 = vanilla quarter, 8 = eighth."
	);
	static dmui::localize::LocalizeString lsAdditionalIgnoreCompatibilityChecks(
		"$Setting_AdditionalIgnoreCompatibilityChecks",
		"Ignore Mod Compatibility Checks."
	);
	static dmui::localize::LocalizeString lsAdditionalLocalMapScaleFactor(
		"$Setting_AdditionalLocalMapScaleFactor",
		"Local Map Scale Factor (needs bHighResLocalMaps)."
	);
	static dmui::localize::LocalizeString lsAdditionalMenuRefreshMs(
		"$Setting_AdditionalMenuRefreshMs",
		"How often an open Addictol tab refreshes its copy of plugin data, in milliseconds. Clamped to 100-2000."
	);
	static dmui::localize::LocalizeString lsAdditionalUseCameraOverrideScale(
		"$Setting_AdditionalUseCameraOverrideScale",
		"Fix the screen issue when using Terminals (needs bCustomScale)."
	);
	static dmui::localize::LocalizeString lsAdditionalUseCustomFurniturePosition(
		"$Setting_AdditionalUseCustomFurniturePosition",
		"Change the position of the actor based on the scale of the actor when using furnitures (needs bCustomScale)."
	);

	BoolSetting bAdditionalDbgFacegenOutput{
		"Additional"sv,
		"bDbgFacegenOutput"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		std::addressof(lsAdditionalDbgFacegenOutput),
		SettingApplyTiming::kImmediate
	};

	BoolSetting bAdditionalUseNewRedistributable{
		"Additional"sv,
		"bUseNewRedistributable"sv,
		SettingDisplayCategory::kStability,
		false,
		std::addressof(lsAdditionalUseNewRedistributable),
		SettingApplyTiming::kNextLaunch
	};

	StrSetting sAdditionalAllocator{
		"Additional"sv,
		"sAllocator"sv,
		SettingDisplayCategory::kPerformance,
		"voltek",
		std::addressof(lsAdditionalAllocator),
		SettingApplyTiming::kNextLaunch
	};

	StrSetting sAdditionalZlibBackend{
		"Additional"sv,
		"sZlibBackend"sv,
		SettingDisplayCategory::kPerformance,
		std::string{ ZlibBackendKindName(DEFAULT_ZLIB_BACKEND) },
		std::addressof(lsAdditionalZlibBackend),
		SettingApplyTiming::kNextLaunch
	};

	StrSetting sAdditionalLogLevel{
		"Additional"sv,
		"sLogLevel"sv,
		SettingDisplayCategory::kDiagnostics,
		"info",
		std::addressof(lsAdditionalLogLevel),
		SettingApplyTiming::kNextLaunch
	};

	StrSetting sAdditionalLogFlushLevel{
		"Additional"sv,
		"sLogFlushLevel"sv,
		SettingDisplayCategory::kDiagnostics,
		"info",
		std::addressof(lsAdditionalLogFlushLevel),
		SettingApplyTiming::kNextLaunch
	};

	U32Setting uAdditionalScaleformPageSize{
		"Additional"sv,
		"uScaleformPageSize"sv,
		SettingDisplayCategory::kPerformance,
		64ul,
		std::addressof(lsAdditionalScaleformPageSize),
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ 64.0, 2048.0 }
	};

	U32Setting uAdditionalScaleformHeapSize{
		"Additional"sv,
		"uScaleformHeapSize"sv,
		SettingDisplayCategory::kPerformance,
		2048ul,
		std::addressof(lsAdditionalScaleformHeapSize),
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ 2048.0, 8192.0 }
	};

	BoolSetting bAdditionalFullPrecisionDecalsEffectShaders{
		"Additional"sv,
		"bFullPrecisionDecalsEffectShaders"sv,
		SettingDisplayCategory::kVisuals,
		true,
		std::addressof(lsAdditionalFullPrecisionDecalsEffectShaders),
		SettingApplyTiming::kNextLaunch
	};

	I32Setting nAdditionalMaxPapyrusOpsPerFrame{
		"Additional"sv,
		"nMaxPapyrusOpsPerFrame"sv,
		SettingDisplayCategory::kPerformance,
		500,
		std::addressof(lsAdditionalMaxPapyrusOpsPerFrame),
		SettingApplyTiming::kNextLaunch
	};

	I32Setting nAdditionalBloomScale{
		"Additional"sv,
		"nBloomScale"sv,
		SettingDisplayCategory::kVisuals,
		2,
		std::addressof(lsAdditionalBloomScale),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bAdditionalIgnoreCompatibilityChecks{
		"Additional"sv,
		"bIgnoreCompatibilityChecks"sv,
		SettingDisplayCategory::kDiagnostics,
		false,
		std::addressof(lsAdditionalIgnoreCompatibilityChecks),
		SettingApplyTiming::kNextLaunch
	};

	F32Setting fAdditionalLocalMapScaleFactor{
		"Additional"sv,
		"fLocalMapScaleFactor"sv,
		SettingDisplayCategory::kVisuals,
		1.5f,
		std::addressof(lsAdditionalLocalMapScaleFactor),
		SettingApplyTiming::kImmediate
	};

	U32Setting uAdditionalMenuRefreshMs{
		"Additional"sv,
		"uMenuRefreshMs"sv,
		SettingDisplayCategory::kInterface,
		250,
		std::addressof(lsAdditionalMenuRefreshMs),
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ 100.0, 2000.0 }
	};

	BoolSetting bAdditionalUseCameraOverrideScale{
		"Additional"sv,
		"bUseCameraOverrideScaleFix"sv,
		SettingDisplayCategory::kGameplay,
		true,
		std::addressof(lsAdditionalUseCameraOverrideScale),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bAdditionalUseCustomFurniturePosition{
		"Additional"sv,
		"bUseCustomFurniturePosition"sv,
		SettingDisplayCategory::kGameplay,
		true,
		std::addressof(lsAdditionalUseCustomFurniturePosition),
		SettingApplyTiming::kNextLaunch
	};
}
