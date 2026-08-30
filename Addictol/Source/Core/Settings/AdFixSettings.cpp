#include <Core/Settings/AdSettings.h>

namespace Addictol
{
	using namespace std::literals;

	BoolSetting bFixesGreyMovie{
		"Fixes"sv,
		"bGreyMovie"sv,
		true,
		"Fixes a bug where movies that don't define \"BackgroundAlpha\" on their movie root could load with a grey background."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesPackageAllocateLocation{
		"Fixes"sv,
		"bPackageAllocateLocation"sv,
		true,
		"Fixes a crash when allocating the location for a package."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesInitTints{
		"Fixes"sv,
		"bInitTints"sv,
		true,
		"Removes the block on loading NPCs tints of the Fallout4.esm file, as well as for NPCs with set the IsChargenPresent flag."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesLODDistance{
		"Fixes"sv,
		"bLODDistance"sv,
		true,
		"Fixes bugs when toggling references with LOD causing LOD to briefly enable and disable by removing the \"Has Distant LOD\" and \"Visible When Distant\" flag checks: https://www.youtube.com/watch?v=hgMm9Z8lHfU."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesActorIsHostileToActor{
		"Fixes"sv,
		"bActorIsHostileToActor"sv,
		true,
		"Fixes a crash when invoking Actor.IsHostileToActor with a none form."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesBGSAIWorldLocationRefRadius{
		"Fixes"sv,
		"bBGSAIWorldLocationRefRadius"sv,
		true,
		"Fixes a crash with BGSAIWorldLocationRefRadius when the target ref is null."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesSafeExit{
		"Fixes"sv,
		"bSafeExit"sv,
		true,
		"Fixes crashes/freezes related to exiting the game. Covers both F4SE-plugin shutdown crashes and the quit-to-desktop freeze."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesUnalignedLoad{
		"Fixes"sv,
		"bUnalignedLoad"sv,
		true,
		"Fixes a crash related to SIMD intrinsics with an aligned move on unaligned memory."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCellInit{
		"Fixes"sv,
		"bCellInit"sv,
		true,
		"Fixes a crash where a form does not get converted to a form pointer on unloaded cells."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesWorkbenchSwap{
		"Fixes"sv,
		"bWorkbenchSwap"sv,
		true,
		"Fixes a crash when you are scrapping or swapping many inventory items in the workbench."sv,
		SettingApplyTiming::kNextLaunch
	};

	I32Setting nFixesMaxStdIO{
		"Fixes"sv,
		"nMaxStdIO"sv,
		-1,
		"Replaces the maximum stdio handles (max 8192, but not for everyone OS)."sv,
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ std::nullopt, 8192.0 }
	};

	BoolSetting bFixesMovementPlanner{
		"Fixes"sv,
		"bMovementPlanner"sv,
		true,
		"Fixes a bug where the the Movement Planner crashes with non-actors."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCompanionStrayBullet{
		"Fixes"sv,
		"bCompanionStrayBullet"sv,
		true,
		"Stops companions/teammates from shooting the player during and shortly after combat (re-equips their weapons on combat exit)."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesEscapeFreeze{
		"Fixes"sv,
		"bEscapeFreeze"sv,
		true,
		"Samples condition-lock ownership and renderer frame progress, releasing only locks orphaned by a terminated owner thread."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesIOChacher{
		"Fixes"sv,
		"bIOCacher"sv,
		true,
		"Use disk cache for IO for less disk activity."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bCosaveIO{
		"Fixes"sv,
		"bCosaveIO"sv,
		true,
		"Buffers F4SE co-save reads into memory so loading a save issues one read instead of thousands of syscalls."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesTESObjectREFRGetEncounterZone{
		"Fixes"sv,
		"bTESObjectREFRGetEncounterZone"sv,
		true,
		"Fixes a crash when looking up the Encounter Zone on a reference that has not yet been initialized."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bPatchesPipBoyLightInv{
		"Fixes"sv,
		"bPipBoyLightInv"sv,
		true,
		"Fixes a crash when you have the PipBoy light on and are checking the inventory."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesInteriorNavCut{
		"Fixes"sv,
		"bInteriorNavCut"sv,
		true,
		"Fixes the engine bug that causes Workshop Navmesh cuts to persist throughout all interior cells."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesMagicEffectApplyEvent{
		"Fixes"sv,
		"bMagicEffectApplyEvent"sv,
		true,
		"Fixes a crash when Magic Effect Apply events are dispatched on null references."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesMagicEffectConditions{
		"Fixes"sv,
		"bMagicEffectConditions"sv,
		true,
		"Fixes a floating-point precision bug where magic-effect conditions eventually stop being re-evaluated during long sessions."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesEncounterZoneReset{
		"Fixes"sv,
		"bEncounterZoneReset"sv,
		true,
		"Fixes encounter zones resetting immediately once you leave them on foot."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bLeveledListCrash{
		"Fixes"sv,
		"bLeveledListCrash"sv,
		true,
		"Prevents an issue where leveled lists can have over 255 entries, which will cause them to crash the game when they are resolved."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesBakaMaxPapyrusOps{
		"Fixes"sv,
		"bBakaMaxPapyrusOps"sv,
		true,
		"Fixes Script Page Allocation and the Toggle Scripts Command, adjusts the maximum papyrus operations per frame via nMaxPapyrusOpsPerFrame."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesPapyrusGCBug{
		"Fixes"sv,
		"bPapyrusGCBug"sv,
		true,
		"Fixes a critical bug in garbage collection that causes premature loop termination, preventing incremental GC starvation and \"Long Save Bug\"."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCreateD3DAndSwapchain{
		"Fixes"sv,
		"bCreateD3DAndSwapchain"sv,
		true,
		"Fixes a crash on startup when enumerating certain monitor display modes."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCheckInternetAccess{
		"Fixes"sv,
		"bCheckInternetAccess"sv,
		true,
		"Eliminates memory leaks if you have lost internet access."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesStolenPowerArmorOwnership{
		"Fixes"sv,
		"bStolenPowerArmorOwnership"sv,
		true,
		"Fixes an issue where the Player is never given proper ownership of stolen Power Armor Frames."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesManyItems{
		"Fixes"sv,
		"bManyItems"sv,
		true,
		"Fixes drop items (now can drops more than 32.767, but generation stacks), similar with Drop7FFFPatch mods, supported 32-bit for containers."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCombatMusic{
		"Fixes"sv,
		"bCombatMusic"sv,
		true,
		"Fixes an issue where combat music can continue to loop when combat ends or when you load a save."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesWorkbenchSound{
		"Fixes"sv,
		"bWorkbenchSound"sv,
		true,
		"Fixes an issue where workbench sound effects loop infinitely on actors. This is commonly referred to as the \"Sewing Machine Sound Bug\"."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesActorCauseSaveBloat{
		"Fixes"sv,
		"bActorCauseSaveBloat"sv,
		true,
		"Removes unused ActorCause data from projectiles when a cell is unloaded to reduce save size."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesAnimSignedCrash{
		"Fixes"sv,
		"bAnimSignedCrash"sv,
		true,
		"Fixes a CTD when loading animations with high-bit-set 16-bit event IDs."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesBethesdaNetCrash{
		"Fixes"sv,
		"bBethesdaNetCrash"sv,
		true,
		"Fixes a startup CTD on non-English Windows installs caused by Bethesda.net response headers containing non-ASCII characters."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesUtilityShader{
		"Fixes"sv,
		"bUtilityShader"sv,
		true,
		"Fixes a crash when a shader can't be found for a given technique id."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesPipBoyCursorConstraints{
		"Fixes"sv,
		"bPipBoyCursorConstraints"sv,
		true,
		"Automatically sets PipBoy Cursor Constraints based on PipBoy resolution for controllers."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesMuzzleFlashLight{
		"Fixes"sv,
		"bMuzzleFlashLight"sv,
		true,
		"Fixes a bug where the muzzle-flash light keeps illuminating the scene after the flash ends."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesAltTabFullscreen{
		"Fixes"sv,
		"bAltTabFullscreen"sv,
		true,
		"Fixes the exclusive-fullscreen Alt-Tab hang by forcing the swap chain to borderless-windowed at creation and blocking DXGI's auto Alt+Enter handler."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesPowerGridScrap{
		"Fixes"sv,
		"bPowerGridScrap"sv,
		true,
		"Fixes a CTD when scrapping or wiring after a settlement mod has been removed, by cleaning up orphan power-grid entries left behind by deleted references."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesViewmodelShading{
		"Fixes"sv,
		"bViewmodelShading"sv,
		true,
		"Fixes wrong specular lighting on the first-person viewmodel caused by the eye-position vector missing the engine's per-frame light offset."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesDofFix{
		"Fixes"sv,
		"bDofFix"sv,
		true,
		"Fixes the first-person viewmodel getting blurred by depth-of-field (iron-sight ADS, dialogue camera) by re-rasterizing it after the DoF pass."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesToggleGrassCommand{
		"Fixes"sv,
		"bToggleGrassCommand"sv,
		true,
		"Fixes the \"ToggleGrass\" / \"tg\" command being a reference function in the AE versions of the game."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesTextureLoadCrash{
		"Fixes"sv,
		"bTextureLoadCrash"sv,
		true,
		"Fixes a crash if NiTexture is nullptr when created, fixes degrade level and also logs texture load errors."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesFullPrecisionDecals{
		"Fixes"sv,
		"bFullPrecisionDecals"sv,
		true,
		"Enables fixes for decal projection, effect-shader particles, and membrane shaders on full-precision meshes."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesMagicKeywordCrash{
		"Fixes"sv,
		"bMagicKeywordCrash"sv,
		true,
		"Fixes a crash when a magic effect item has no effect setting."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesAttachLightCrash{
		"Fixes"sv,
		"bAttachLightCrash"sv,
		true,
		"Fixes a crash when a light hit effect has no attach root."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesDownwardAiming{
		"Fixes"sv,
		"bDownwardAiming"sv,
		true,
		"Fixes a bug where shots don't fire properly if you're aiming downward while crouching on a ridge."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesSprintStutter{
		"Fixes"sv,
		"bSprintStutter"sv,
		true,
		"Removes the first-person sprint camera micro-stutter by raising the position-delta snap threshold."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesMoonRotation{
		"Fixes"sv,
		"bMoonRotation"sv,
		true,
		"Corrects the lunar disc orientation during moon initialization."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesWeaponDebrisCrash{
		"Fixes"sv,
		"bWeaponDebrisCrash"sv,
		true,
		"Prevents the NVIDIA Weapon Debris CTD on Turing+ / deprecated-FleX drivers by skipping the crashing collision path."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bCrashRemoveRefFix{
		"Fixes"sv,
		"bCrashRemoveRefFix"sv,
		true,
		"Fixes a crash where remove reference function BGSObjectVisibilityManager class send no TESObjectREFR."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bClimateLoad{
		"Fixes"sv,
		"bClimateLoad"sv,
		true,
		"Fixes a bug where the game fails to properly apply sunrise and sunset data from Climate records if you load a saved game in an interior."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesMusicOverlap{
		"Fixes"sv,
		"bMusicOverlap"sv,
		true,
		"Fixes a bug where multiple music tracks are playing at the same time."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesPuddleCubemaps{
		"Fixes"sv,
		"bPuddleCubemaps"sv,
		false,
		"Blanks each worldspace's water environment-map cubemap to remove flicker on water puddles / blood pools (also removes water cubemap reflections)."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesStringPoolRelease{
		"Fixes"sv,
		"bStringPoolRelease"sv,
		true,
		"An attempt to not shutdown the game due to a garbage Entry, although I'm sure it's caused by pool overflow (fix really is rude)."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesLoadOrder{
		"Fixes"sv,
		"bLoadOrder"sv,
		true,
		"Fixes multiple AE Load Order issues when the Creations Platform is enabled. Including Plugins getting disabled, moved around, and inability to toggle certain Plugins in-game."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesHUDMessageQueue{
		"Fixes"sv,
		"bHUDMessageQueue"sv,
		true,
		"Fixes a crash related to queued HUD messages on NG & AE."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesWaterJetpackFix{
		"Fixes"sv,
		"bWaterJetpackFix"sv,
		true,
		"Fixes a bug where entering water does not reset the Jetpack's state, allowing the Player to Jump / Fly again."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCraftingMenuFix{
		"Fixes"sv,
		"bCraftingMenuFix"sv,
		true,
		"Reduces the lag when browsing mod slots in the weapon/armor crafting menu. The menu stops walking the whole COBJ/OMOD form arrays on every slot switch."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesAIProcess3DUpdateFlag{
		"Fixes"sv,
		"bAIProcess3DUpdateFlag"sv,
		true,
		"Fixes a bug where there is no check for nullptr when AI for a character is disabled."sv,
		SettingApplyTiming::kNextLaunch
	};
}
