#include <Core/Settings/AdSettings.h>

namespace Addictol
{
	using namespace std::literals;

	dmui::localize::LocalizeString lsPatchesThreads(
		"$Setting_PatchesThreads",
		"All threads have priority above idle, process above normal, for adequate latency, prohibition on changing processor cores."
	);
	dmui::localize::LocalizeString lsPatchesLibDeflate(
		"$Setting_PatchesLibDeflate",
		"Replaces the game's zlib decompression with the backend selected by sZlibBackend. If another mod already patches zlib, the game's zlib is left untouched."
	);
	dmui::localize::LocalizeString lsPatchesLoadScreen(
		"$Setting_PatchesLoadScreen",
		"Black loading screen with fixes for 21:9 display.Need High FPS Physics Fix mod, otherwise slowly loads game."
	);
	dmui::localize::LocalizeString lsPatchesProfile(
		"$Setting_PatchesProfile",
		"Added settings Pref.ini for default.ini, This provides an opportunity mods change settings Pref.ini."
	);
	dmui::localize::LocalizeString lsPatchesAchievements(
		"$Setting_PatchesAchievements",
		"Enables achievements on modded saves."
	);
	dmui::localize::LocalizeString lsPatchesFacegen(
		"$Setting_PatchesFacegen",
		"Enables facegen support and removes freezes."
	);
	dmui::localize::LocalizeString lsPatchesMemoryManager(
		"$Setting_PatchesMemoryManager",
		"Replaces the global memory manager with vmm allocator."
	);
	dmui::localize::LocalizeString lsPatchesSmallBlockAllocator(
		"$Setting_PatchesSmallBlockAllocator",
		"Replaces the small block memory allocators with visper allocator."
	);
	dmui::localize::LocalizeString lsPatchesSmallBlockAllocatorUseSelectedHeap(
		"$Setting_PatchesSmallBlockAllocatorUseSelectedHeap",
		"Routes small block allocations to the selected heap instead of visper."
	);
	dmui::localize::LocalizeString lsPatchesScaleformAllocator(
		"$Setting_PatchesScaleformAllocator",
		"Replaces the scaleform memory allocator with os allocator."
	);
	dmui::localize::LocalizeString lsPatchesBSMTAManager(
		"$Setting_PatchesBSMTAManager",
		"General rendering performance improvement."
	);
	dmui::localize::LocalizeString lsPatchesBSPreCulledObjects(
		"$Setting_PatchesBSPreCulledObjects",
		"General rendering performance improvement."
	);
	dmui::localize::LocalizeString lsPatchesINISettingCollection(
		"$Setting_PatchesINISettingCollection",
		"Improves startup times for large load orders by optimizing INI setting loading."
	);
	dmui::localize::LocalizeString lsPatchesArchiveLimits(
		"$Setting_PatchesArchiveLimits",
		"Increases the allowed number of archives from 255 GNRL and from 254 DX10 to 65355."
	);
	dmui::localize::LocalizeString lsPatchesInputSwitch(
		"$Setting_PatchesInputSwitch",
		"Automatically swaps inputs between Keyboard + Mouse / Controller."
	);
	dmui::localize::LocalizeString lsPatchesFasterWorkshop(
		"$Setting_PatchesFasterWorkshop",
		"Alleviates lag while opening the Workshop menu."
	);
	dmui::localize::LocalizeString lsPatchesSaveAddedSoundCategories(
		"$Setting_PatchesSaveAddedSoundCategories",
		"Saves the volume of Sound Categories added by mods."
	);
	dmui::localize::LocalizeString lsPatchesCOMInit(
		"$Setting_PatchesCOMInit",
		"Blocks the use of incorrect COM interface initialization settings for mods."
	);
	dmui::localize::LocalizeString lsPatchesHighResBloom(
		"$Setting_PatchesHighResBloom",
		"Raises the bloom render - target resolution to reduce flicker on bright pixels(sun glints, neon, fire).Cost scales with nBloomScale."
	);
	dmui::localize::LocalizeString lsPatchesDpiScaling(
		"$Setting_PatchesDpiScaling",
		"Marks the process as DPI - aware so menus and cursor track correctly on high - DPI desktops."
	);
	dmui::localize::LocalizeString lsPatchesSaveCompression(
		"$Setting_PatchesSaveCompression",
		"Swaps the engine's save compression to libdeflate (zlib level 6) for faster, slightly smaller saves."
	);
	dmui::localize::LocalizeString lsPatchesAudioSwitch(
		"$Setting_PatchesAudioSwitch",
		"Allows seamless audio device switching for XAudio 2.7.Automatically switches to a newly selected default audio device."
	);
	dmui::localize::LocalizeString lsPatchesHighResLocalMaps(
		"$Setting_PatchesHighResLocalMaps",
		"Raises the Local Maps resolution, scales with fLocalMapScaleFactor.Affects the Companion app as well, can increase performance& VRAM cost."
	);
	dmui::localize::LocalizeString lsPatchesRefExtended(
		"$Setting_PatchesRefExtended",
		"[Experimental] [AE latest only] Increase from 2^21 to 2^23 available BSPointerHandle's by reducing the internal QRefCount counter from 2^10 to 2^8. "
		"For a large LO so you can play for more than 2 hours. Enable only if \"OUT OF HANDLE ARRAY ENTRIES.\" is triggered. "
		"Use at your own risk! Mass change of the entire .exe, more than 15 thousand changes."
	);

	BoolSetting bPatchesThreads{
		"Patches"sv,
		"bThreads"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsPatchesThreads),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesLibDeflate{
		"Patches"sv,
		"bLibDeflate"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsPatchesLibDeflate),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesLoadScreen{
		"Patches"sv,
		"bLoadScreen"sv,
		SettingDisplayCategory::kInterface,
		true,
		std::addressof(lsPatchesLoadScreen),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesProfile{
		"Patches"sv,
		"bProfile"sv,
		SettingDisplayCategory::kInterface,
		true,
		std::addressof(lsPatchesProfile),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesAchievements{
		"Patches"sv,
		"bAchievements"sv,
		SettingDisplayCategory::kGameplay,
		true,
		std::addressof(lsPatchesAchievements),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesFacegen{
		"Patches"sv,
		"bFacegen"sv,
		SettingDisplayCategory::kVisuals,
		true,
		std::addressof(lsPatchesFacegen),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesMemoryManager{
		"Patches"sv,
		"bMemoryManager"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsPatchesMemoryManager),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesSmallBlockAllocator{
		"Patches"sv,
		"bSmallBlockAllocator"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsPatchesSmallBlockAllocator),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesSmallBlockAllocatorUseSelectedHeap{
		"Patches"sv,
		"bSmallBlockAllocatorUseSelectedHeap"sv,
		SettingDisplayCategory::kPerformance,
		false,
		std::addressof(lsPatchesSmallBlockAllocatorUseSelectedHeap),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesScaleformAllocator{
		"Patches"sv,
		"bScaleformAllocator"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsPatchesScaleformAllocator),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesBSMTAManager{
		"Patches"sv,
		"bBSMTAManager"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsPatchesBSMTAManager),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesBSPreCulledObjects{
		"Patches"sv,
		"bBSPreCulledObjects"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsPatchesBSPreCulledObjects),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesINISettingCollection{
		"Patches"sv,
		"bINISettingCollection"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsPatchesINISettingCollection),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesArchiveLimits{
		"Patches"sv,
		"bArchiveLimits"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsPatchesArchiveLimits),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesInputSwitch{
		"Patches"sv,
		"bInputSwitch"sv,
		SettingDisplayCategory::kInterface,
		false,
		std::addressof(lsPatchesInputSwitch),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesFasterWorkshop{
		"Patches"sv,
		"bFasterWorkshop"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsPatchesFasterWorkshop),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesSaveAddedSoundCategories{
		"Patches"sv,
		"bSaveAddedSoundCategories"sv,
		SettingDisplayCategory::kAudio,
		true,
		std::addressof(lsPatchesSaveAddedSoundCategories),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesCOMInit{
		"Patches"sv,
		"bCOMInit"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsPatchesCOMInit),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesHighResBloom{
		"Patches"sv,
		"bHighResBloom"sv,
		SettingDisplayCategory::kVisuals,
		false,
		std::addressof(lsPatchesHighResBloom),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesDpiScaling{
		"Patches"sv,
		"bDpiScaling"sv,
		SettingDisplayCategory::kInterface,
		true,
		std::addressof(lsPatchesDpiScaling),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesSaveCompression{
		"Patches"sv,
		"bSaveCompression"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsPatchesSaveCompression),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesAudioSwitch{
		"Patches"sv,
		"bAudioSwitch"sv,
		SettingDisplayCategory::kAudio,
		true,
		std::addressof(lsPatchesAudioSwitch),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesHighResLocalMaps{
		"Patches"sv,
		"bHighResLocalMaps"sv,
		SettingDisplayCategory::kVisuals,
		false,
		std::addressof(lsPatchesHighResLocalMaps),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesRefExtended{
		"Patches"sv,
		"bRefExtended"sv,
		SettingDisplayCategory::kStability,
		false,
		std::addressof(lsPatchesRefExtended),
		SettingApplyTiming::kNextLaunch
	};
}
