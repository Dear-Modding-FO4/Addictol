#include <Core/Settings/AdSettings.h>

namespace Addictol
{
	using namespace std::literals;

	BoolSetting bPatchesThreads{
		"Patches"sv,
		"bThreads"sv,
		SettingDisplayCategory::kPerformance,
		true,
		"All threads have priority above idle, process above normal, for adequate latency, prohibition on changing processor cores."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesLibDeflate{
		"Patches"sv,
		"bLibDeflate"sv,
		SettingDisplayCategory::kPerformance,
		true,
		"Enables the validated zlib decompression hook. Backend selected by sZlibBackend. With libdeflate selected, a validated texture seam decodes complete chunks directly; ineligible or failed requests replay through stock."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesLoadScreen{
		"Patches"sv,
		"bLoadScreen"sv,
		SettingDisplayCategory::kInterface,
		true,
		"Black loading screen with fixes for 21:9 display. Need High FPS Physics Fix mod, otherwise slowly loads game."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesProfile{
		"Patches"sv,
		"bProfile"sv,
		SettingDisplayCategory::kInterface,
		true,
		"Added settings Pref.ini for default .ini, This provides an opportunity mods change settings Pref.ini."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesAchievements{
		"Patches"sv,
		"bAchievements"sv,
		SettingDisplayCategory::kGameplay,
		true,
		"Enables achievements on modded saves."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesFacegen{
		"Patches"sv,
		"bFacegen"sv,
		SettingDisplayCategory::kVisuals,
		true,
		"Enables facegen support and removes freezes."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesMemoryManager{
		"Patches"sv,
		"bMemoryManager"sv,
		SettingDisplayCategory::kPerformance,
		true,
		"Replaces the global memory manager with vmm allocator."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesSmallBlockAllocator{
		"Patches"sv,
		"bSmallBlockAllocator"sv,
		SettingDisplayCategory::kPerformance,
		true,
		"Replaces the small block memory allocators with visper allocator."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesSmallBlockAllocatorUseSelectedHeap{
		"Patches"sv,
		"bSmallBlockAllocatorUseSelectedHeap"sv,
		SettingDisplayCategory::kPerformance,
		false,
		"Routes small block allocations to the selected heap instead of visper."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesScaleformAllocator{
		"Patches"sv,
		"bScaleformAllocator"sv,
		SettingDisplayCategory::kPerformance,
		true,
		"Replaces the scaleform memory allocator with os allocator."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesBSMTAManager{
		"Patches"sv,
		"bBSMTAManager"sv,
		SettingDisplayCategory::kPerformance,
		true,
		"General rendering performance improvement."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesBSPreCulledObjects{
		"Patches"sv,
		"bBSPreCulledObjects"sv,
		SettingDisplayCategory::kPerformance,
		true,
		"General rendering performance improvement."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesINISettingCollection{
		"Patches"sv,
		"bINISettingCollection"sv,
		SettingDisplayCategory::kPerformance,
		true,
		"Improves startup times for large load orders by optimizing INI setting loading."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesArchiveLimits{
		"Patches"sv,
		"bArchiveLimits"sv,
		SettingDisplayCategory::kStability,
		false,
		"Increases the allowed number of archives from 255 GNRL and from 254 DX10 to 65355."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesInputSwitch{
		"Patches"sv,
		"bInputSwitch"sv,
		SettingDisplayCategory::kInterface,
		true,
		"Automatically swaps inputs between Keyboard + Mouse / Controller."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesFasterWorkshop{
		"Patches"sv,
		"bFasterWorkshop"sv,
		SettingDisplayCategory::kPerformance,
		true,
		"Alleviates lag while opening the Workshop menu."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesSaveAddedSoundCategories{
		"Patches"sv,
		"bSaveAddedSoundCategories"sv,
		SettingDisplayCategory::kAudio,
		true,
		"Saves the volume of Sound Categories added by mods."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesCOMInit{
		"Patches"sv,
		"bCOMInit"sv,
		SettingDisplayCategory::kStability,
		true,
		"Blocks the use of incorrect COM interface initialization settings for mods."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesHighResBloom{
		"Patches"sv,
		"bHighResBloom"sv,
		SettingDisplayCategory::kVisuals,
		false,
		"Raises the bloom render-target resolution to reduce flicker on bright pixels (sun glints, neon, fire). Cost scales with nBloomScale."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesDpiScaling{
		"Patches"sv,
		"bDpiScaling"sv,
		SettingDisplayCategory::kInterface,
		true,
		"Marks the process as DPI-aware so menus and cursor track correctly on high-DPI desktops."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesSaveCompression{
		"Patches"sv,
		"bSaveCompression"sv,
		SettingDisplayCategory::kPerformance,
		true,
		"Swaps the engine's save compression to libdeflate (zlib level 6) for faster, slightly smaller saves."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesAudioSwitch{
		"Patches"sv,
		"bAudioSwitch"sv,
		SettingDisplayCategory::kAudio,
		true,
		"Allows seamless audio device switching for XAudio 2.7. Automatically switches to a newly selected default audio device."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesHighResLocalMaps{
		"Patches"sv,
		"bHighResLocalMaps"sv,
		SettingDisplayCategory::kVisuals,
		false,
		"Raises the Local Maps resolution, scales with fLocalMapScaleFactor. Affects the Companion app as well, can increase performance & VRAM cost."sv,
		SettingApplyTiming::kNextLaunch
	};
}
