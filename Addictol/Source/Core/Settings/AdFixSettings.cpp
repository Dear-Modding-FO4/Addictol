#include <Core/Settings/AdSettings.h>

namespace Addictol
{
	using namespace std::literals;

	static dmui::localize::LocalizeString lsFixesGreyMovie(
		"$Setting_FixesGreyMovie",
		"Fixes a bug where movies that don't define \"BackgroundAlpha\" on their movie root could load with a grey background."
	);
	static dmui::localize::LocalizeString lsFixesPackageAllocateLocation(
		"$Setting_FixesPackageAllocateLocation",
		"Fixes a crash when allocating the location for a package."
	);
	static dmui::localize::LocalizeString lsFixesInitTints(
		"$Setting_FixesInitTints",
		"Removes a block when loading NPC tints from Fallout4.esm, as well as for NPCs with the IsChargenPresent flag set."
	);
	static dmui::localize::LocalizeString lsFixesLODDistance(
		"$Setting_FixesLODDistance",
		"Fixes a loaded cell's distant object LOD briefly reappearing when a reference with distant LOD is enabled or built."
	);
	static dmui::localize::LocalizeString lsFixesActorIsHostileToActor(
		"$Setting_FixesActorIsHostileToActor",
		"Fixes a crash when invoking Actor.IsHostileToActor with a none form."
	);
	static dmui::localize::LocalizeString lsFixesBGSAIWorldLocationRefRadius(
		"$Setting_FixesBGSAIWorldLocationRefRadius",
		"Fixes a crash with BGSAIWorldLocationRefRadius when the target ref is null."
	);
	static dmui::localize::LocalizeString lsFixesSafeExit(
		"$Setting_FixesSafeExit",
		"Fixes crashes/freezes related to exiting the game. Covers both F4SE-plugin shutdown crashes and the quit-to-desktop freeze."
	);
	static dmui::localize::LocalizeString lsFixesUnalignedLoad(
		"$Setting_FixesUnalignedLoad",
		"Fixes a crash related to SIMD intrinsics with an aligned move on unaligned memory."
	);
	static dmui::localize::LocalizeString lsFixesCellInit(
		"$Setting_FixesCellInit",
		"Fixes a crash where a form does not get converted to a form pointer on unloaded cells."
	);
	static dmui::localize::LocalizeString lsFixesWorkbenchSwap(
		"$Setting_FixesWorkbenchSwap",
		"Fixes a crash when you are scrapping or swapping many inventory items in the workbench."
	);
	static dmui::localize::LocalizeString lsFixesMaxStdIO(
		"$Setting_FixesMaxStdIO",
		"Sets the maximum stdio handles. -1 selects 512; the runtime cap is 2048 on OG and 8192 on NG/AE."
	);
	static dmui::localize::LocalizeString lsFixesMovementPlanner(
		"$Setting_FixesMovementPlanner",
		"Fixes a bug where the Movement Planner crashes with non-actors."
	);
	static dmui::localize::LocalizeString lsFixesCompanionStrayBullet(
		"$Setting_FixesCompanionStrayBullet",
		"Stops companions/teammates from shooting the player during and shortly after combat (re-equips their weapons on combat exit)."
	);
	static dmui::localize::LocalizeString lsFixesEscapeFreeze(
		"$Setting_FixesEscapeFreeze",
		"Fixes a combat freeze (deadlock) in perk condition checks by evaluating them without the global condition lock."
	);
	static dmui::localize::LocalizeString lsFixesIOChacher(
		"$Setting_FixesIOChacher",
		"Use disk cache for IO for less disk activity."
	);
	static dmui::localize::LocalizeString lsFixesCosaveIO(
		"$Setting_FixesCosaveIO",
		"Buffers F4SE co-save reads into memory so loading a save issues one read instead of thousands of syscalls."
	);
	static dmui::localize::LocalizeString lsFixesTESObjectREFRGetEncounterZone(
		"$Setting_FixesTESObjectREFRGetEncounterZone",
		"Fixes a crash when looking up the Encounter Zone on a reference that has not yet been initialized."
	);
	static dmui::localize::LocalizeString lsFixesPipBoyLightInv(
		"$Setting_FixesPipBoyLightInv",
		"Fixes a crash when you have the PipBoy light on and are checking the inventory."
	);
	static dmui::localize::LocalizeString lsFixesInteriorNavCut(
		"$Setting_FixesInteriorNavCut",
		"Fixes the engine bug that causes Workshop Navmesh cuts to persist throughout all interior cells."
	);
	static dmui::localize::LocalizeString lsFixesMagicEffectApplyEvent(
		"$Setting_FixesMagicEffectApplyEvent",
		"Fixes a crash when Magic Effect Apply events are dispatched on null references."
	);
	static dmui::localize::LocalizeString lsFixesMagicEffectConditions(
		"$Setting_FixesMagicEffectConditions",
		"Fixes a floating-point precision bug where magic-effect conditions eventually stop being re-evaluated during long sessions."
	);
	static dmui::localize::LocalizeString lsFixesEncounterZoneReset(
		"$Setting_FixesEncounterZoneReset",
		"Fixes encounter zones resetting immediately once you leave them on foot."
	);
	static dmui::localize::LocalizeString lsFixesLeveledListCrash(
		"$Setting_FixesLeveledListCrash",
		"Prevents an issue where leveled lists can have over 255 entries, which will cause them to crash the game when they are resolved."
	);
	static dmui::localize::LocalizeString lsFixesBakaMaxPapyrusOps(
		"$Setting_FixesBakaMaxPapyrusOps",
		"Fixes Script Page Allocation and the Toggle Scripts Command, adjusts the maximum papyrus operations per frame via nMaxPapyrusOpsPerFrame."
	);
	static dmui::localize::LocalizeString lsFixesPapyrusGCBug(
		"$Setting_FixesPapyrusGCBug",
		"Fixes a critical bug in garbage collection that causes premature loop termination, preventing incremental GC starvation and \"Long Save Bug\"."
	);
	static dmui::localize::LocalizeString lsFixesCreateD3DAndSwapchain(
		"$Setting_FixesCreateD3DAndSwapchain",
		"Fixes a crash on startup when enumerating certain monitor display modes."
	);
	static dmui::localize::LocalizeString lsFixesCheckInternetAccess(
		"$Setting_FixesCheckInternetAccess",
		"Eliminates memory leaks if you have lost internet access."
	);
	static dmui::localize::LocalizeString lsFixesStolenPowerArmorOwnership(
		"$Setting_FixesStolenPowerArmorOwnership",
		"Fixes an issue where the Player is never given proper ownership of stolen Power Armor Frames."
	);
	static dmui::localize::LocalizeString lsFixesManyItems(
		"$Setting_FixesManyItems",
		"Fixes the Item Stack Limit, allowing you to add / drop more than 32767 items (stacked generation), similar to the Add/Drop7FFFPatch mods."
	);
	static dmui::localize::LocalizeString lsFixesCombatMusic(
		"$Setting_FixesCombatMusic",
		"Fixes an issue where combat music can continue to loop when combat ends or when you load a save."
	);
	static dmui::localize::LocalizeString lsFixesWorkbenchSound(
		"$Setting_FixesWorkbenchSound",
		"Fixes an issue where workbench sound effects loop infinitely on actors. This is commonly referred to as the \"Sewing Machine Sound Bug\"."
	);
	static dmui::localize::LocalizeString lsFixesActorCauseSaveBloat(
		"$Setting_FixesActorCauseSaveBloat",
		"Removes unused ActorCause data from projectiles when a cell is unloaded to reduce save size."
	);
	static dmui::localize::LocalizeString lsFixesAnimSignedCrash(
		"$Setting_FixesAnimSignedCrash",
		"Fixes a CTD when loading animations with high-bit-set 16-bit event IDs."
	);
	static dmui::localize::LocalizeString lsFixesBethesdaNetCrash(
		"$Setting_FixesBethesdaNetCrash",
		"Fixes a startup CTD on non-English Windows installs caused by Bethesda.net response headers containing non-ASCII characters."
	);
	static dmui::localize::LocalizeString lsFixesUtilityShader(
		"$Setting_FixesUtilityShader",
		"Fixes a crash when a shader can't be found for a given technique id."
	);
	static dmui::localize::LocalizeString lsFixesPipBoyCursorConstraints(
		"$Setting_FixesPipBoyCursorConstraints",
		"Automatically sets PipBoy Cursor Constraints based on PipBoy resolution for controllers."
	);
	static dmui::localize::LocalizeString lsFixesMuzzleFlashLight(
		"$Setting_FixesMuzzleFlashLight",
		"Fixes a bug where the muzzle-flash light keeps illuminating the scene after the flash ends."
	);
	static dmui::localize::LocalizeString lsFixesPowerGridScrap(
		"$Setting_FixesPowerGridScrap",
		"Fixes a CTD when scrapping or wiring after a settlement mod has been removed, by cleaning up orphan power-grid entries left behind by deleted references."
	);
	static dmui::localize::LocalizeString lsFixesViewmodelShading(
		"$Setting_FixesViewmodelShading",
		"Fixes wrong specular lighting on the first-person viewmodel caused by the eye-position vector missing the engine's per-frame light offset."
	);
	static dmui::localize::LocalizeString lsFixesDofFix(
		"$Setting_FixesDofFix",
		"Fixes the first-person viewmodel getting blurred by depth-of-field (iron-sight ADS, dialogue camera) by re-rasterizing it after the DoF pass."
	);
	static dmui::localize::LocalizeString lsFixesToggleGrassCommand(
		"$Setting_FixesToggleGrassCommand",
		"Fixes the \"ToggleGrass\" / \"tg\" command being a reference function in the AE versions of the game."
	);
	static dmui::localize::LocalizeString lsFixesTextureLoadCrash(
		"$Setting_FixesTextureLoadCrash",
		"Fixes a crash if NiTexture is nullptr when created, fixes degrade level and also logs texture load errors."
	);
	static dmui::localize::LocalizeString lsFixesTextureCreateCrash(
		"$Setting_FixesTextureCreateCrash",
		"Fixes a crash when the renderer registers a texture that D3D11 refused to create, such as a block-compressed texture whose top mip is not a multiple of 4."
	);
	static dmui::localize::LocalizeString lsFixesFullPrecisionDecals(
		"$Setting_FixesFullPrecisionDecals",
		"Enables fixes for decal projection and effect-shader particles on full-precision meshes."
	);
	static dmui::localize::LocalizeString lsFixesShaderReferenceEffectLifetime(
		"$Setting_FixesShaderReferenceEffectLifetime",
		"Prevents crashes when an actor with an active effect shader unloads by detaching the shader when its magic effect is destroyed."
	);
	static dmui::localize::LocalizeString lsFixesMagicKeywordCrash(
		"$Setting_FixesMagicKeywordCrash",
		"Fixes a crash when a magic effect item has no effect setting."
	);
	static dmui::localize::LocalizeString lsFixesEffectEnvMapCrash(
		"$Setting_FixesEffectEnvMapCrash",
		"Fixes a crash when an effect shader material has environment mapping enabled but no environment texture."
	);
	static dmui::localize::LocalizeString lsFixesNavmeshLocationCrash(
		"$Setting_FixesNavmeshLocationCrash",
		"Fixes a crash when combat AI searches the navmesh for a valid location to attack from or return to."
	);
	static dmui::localize::LocalizeString lsFixesCreatedRefBaseCrash(
		"$Setting_FixesCreatedRefBaseCrash",
		"Fixes a crash while loading a save when a created reference has lost its base object."
	);
	static dmui::localize::LocalizeString lsFixesAttachLightCrash(
		"$Setting_FixesAttachLightCrash",
		"Fixes a crash when a light hit effect has no attach root."
	);
	static dmui::localize::LocalizeString lsFixesDownwardAiming(
		"$Setting_FixesDownwardAiming",
		"Fixes a bug where shots don't fire properly if you're aiming downward while crouching on a ridge."
	);
	static dmui::localize::LocalizeString lsFixesThirdPersonSpeedMult(
		"$Setting_FixesThirdPersonSpeedMult",
		"Makes the player's SpeedMult actor value affect third-person sprint and sneak speeds, matching first person."
	);
	static dmui::localize::LocalizeString lsFixesSprintStutter(
		"$Setting_FixesSprintStutter",
		"Removes the first-person sprint camera micro-stutter by raising the position-delta snap threshold."
	);
	static dmui::localize::LocalizeString lsFixesMoonRotation(
		"$Setting_FixesMoonRotation",
		"Corrects the lunar disc orientation during moon initialization."
	);
	static dmui::localize::LocalizeString lsFixesCrashRemoveRefFix(
		"$Setting_FixesCrashRemoveRefFix",
		"Fixes a crash where remove reference function BGSObjectVisibilityManager class send no TESObjectREFR."
	);
	static dmui::localize::LocalizeString lsFixesClimateLoad(
		"$Setting_FixesClimateLoad",
		"Fixes a bug where the game fails to properly apply sunrise and sunset data from Climate records if you load a saved game in an interior."
	);
	static dmui::localize::LocalizeString lsFixesMusicOverlap(
		"$Setting_FixesMusicOverlap",
		"Fixes a bug where multiple music tracks are playing at the same time."
	);
	static dmui::localize::LocalizeString lsFixesPuddleCubemaps(
		"$Setting_FixesPuddleCubemaps",
		"Blanks each worldspace's water environment-map cubemap to remove flicker on water puddles / blood pools (also removes water cubemap reflections)."
	);
	static dmui::localize::LocalizeString lsFixesCubemapArray(
		"$Setting_FixesCubemapArray",
		"Fixes material cubemap reflections turning off when one cubemap has an unexpected size, format or mip count, by converting every cubemap into a fixed 128x128 array (replaces the Fix Cubemaps xEdit script)."
	);
	static dmui::localize::LocalizeString lsFixesStringPoolRelease(
		"$Setting_FixesStringPoolRelease",
		"Catches the crash when the game releases an invalid string and logs which code released it."
	);
	static dmui::localize::LocalizeString lsFixesLoadOrder(
		"$Setting_FixesLoadOrder",
		"Fixes multiple AE Load Order issues when the Creations Platform is enabled, including plugins being disabled or moved, inability to toggle some plugins in-game, and a freeze while loading."
	);
	static dmui::localize::LocalizeString lsFixesHUDMessageQueue(
		"$Setting_FixesHUDMessageQueue",
		"Fixes a crash related to queued HUD messages on NG & AE."
	);
	static dmui::localize::LocalizeString lsFixesWaterJetpackFix(
		"$Setting_FixesWaterJetpackFix",
		"Fixes a bug where entering water does not reset the Jetpack's state, allowing the Player to Jump / Fly again."
	);
	static dmui::localize::LocalizeString lsFixesCraftingMenuFix(
		"$Setting_FixesCraftingMenuFix",
		"Reduces the lag when browsing mod slots in the weapon/armor crafting menu. The menu stops walking the whole COBJ/OMOD form arrays on every slot switch."
	);
	static dmui::localize::LocalizeString lsFixesAIProcess3DUpdateFlag(
		"$Setting_FixesAIProcess3DUpdateFlag",
		"Fixes a bug where there is no check for nullptr when AI for a character is disabled."
	);
	static dmui::localize::LocalizeString lsFixesNPCHealthOverflowFix(
		"$Setting_FixesNPCHealthOverflowFix",
		"Fixes auto-calculated NPC health and action points wrapping negative on high level NPCs, which could make them drop dead instantly."
	);
	static dmui::localize::LocalizeString lsFixesArmorPenetration(
		"$Setting_FixesArmorPenetration",
		"Fixes an issue where Armor Penetration does not get reset when unequipping or changing Weapons."
	);
	static dmui::localize::LocalizeString lsFixesLocalMapRotation(
		"$Setting_FixesLocalMapRotation",
		"Fixes an issue where Player Map Markers in rotated cells are not placed where you expect them to be."
	);
	static dmui::localize::LocalizeString lsFixesNeverFadesSpotlight(
		"$Setting_FixesNeverFadesSpotlight",
		"Fixes shadow-casting spotlights marked Never Fades being culled instead of skipping the distance check."
	);
	static dmui::localize::LocalizeString lsFixesLightOrder(
		"$Setting_FixesLightOrder",
		"Fixes previs discarding object light lists, which left glass and other objects lit only by the sun."
	);
	static dmui::localize::LocalizeString lsFixesLightPriority(
		"$Setting_FixesLightPriority",
		"Fixes objects lit by more than four lights receiving the four weakest instead of the four strongest."
	);
	static dmui::localize::LocalizeString lsFixesRadioSilence(
		"$Setting_FixesRadioSilence",
		"[EXPERIMENTAL] Fixes an issue where Radio Signals can go silent after some time."
	);
	static dmui::localize::LocalizeString lsFixesQuestLogBloat(
		"$Setting_FixesQuestLogBloat",
		"Prevents repeatable quest stages from appending duplicate quest log nodes and deduplicates bloated quest logs on save load."
	);
	static dmui::localize::LocalizeString lsFixesCustomScale(
		"$Setting_FixesCustomScale",
		"Fixes ADS issues that occur when the player scale is changed."
	);
	static dmui::localize::LocalizeString lsFixesArchiveStreamClose(
		"$Setting_FixesArchiveStreamClose",
		"Waits for in-flight BA2 streamer reads to finish before closing compressed archive streams, preventing heap corruption."
	);
	static dmui::localize::LocalizeString lsFixesPapyrusLeaks(
		"$Setting_FixesPapyrusLeaks",
		"Releases Papyrus scripts the engine leaves bound to destroyed inventory items and unused plugin-form wrappers; existing saves are cleaned on load."
	);

	BoolSetting bFixesGreyMovie{
		"Fixes"sv,
		"bGreyMovie"sv,
		SettingDisplayCategory::kVisuals,
		true,
		std::addressof(lsFixesGreyMovie),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesPackageAllocateLocation{
		"Fixes"sv,
		"bPackageAllocateLocation"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesPackageAllocateLocation),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesInitTints{
		"Fixes"sv,
		"bInitTints"sv,
		SettingDisplayCategory::kVisuals,
		true,
		std::addressof(lsFixesInitTints),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesLODDistance{
		"Fixes"sv,
		"bLODDistance"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsFixesLODDistance),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesActorIsHostileToActor{
		"Fixes"sv,
		"bActorIsHostileToActor"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesActorIsHostileToActor),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesBGSAIWorldLocationRefRadius{
		"Fixes"sv,
		"bBGSAIWorldLocationRefRadius"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesBGSAIWorldLocationRefRadius),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesSafeExit{
		"Fixes"sv,
		"bSafeExit"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesSafeExit),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesUnalignedLoad{
		"Fixes"sv,
		"bUnalignedLoad"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesUnalignedLoad),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCellInit{
		"Fixes"sv,
		"bCellInit"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesCellInit),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesWorkbenchSwap{
		"Fixes"sv,
		"bWorkbenchSwap"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesWorkbenchSwap),
		SettingApplyTiming::kNextLaunch
	};

	I32Setting nFixesMaxStdIO{
		"Fixes"sv,
		"nMaxStdIO"sv,
		SettingDisplayCategory::kPerformance,
		-1,
		std::addressof(lsFixesMaxStdIO),
		SettingApplyTiming::kNextLaunch,
		SettingNumericRange{ std::nullopt, 8192.0 }
	};

	BoolSetting bFixesMovementPlanner{
		"Fixes"sv,
		"bMovementPlanner"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesMovementPlanner),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCompanionStrayBullet{
		"Fixes"sv,
		"bCompanionStrayBullet"sv,
		SettingDisplayCategory::kGameplay,
		true,
		std::addressof(lsFixesCompanionStrayBullet),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesEscapeFreeze{
		"Fixes"sv,
		"bEscapeFreeze"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesEscapeFreeze),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesIOChacher{
		"Fixes"sv,
		"bIOCacher"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsFixesIOChacher),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCosaveIO{
		"Fixes"sv,
		"bCosaveIO"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsFixesCosaveIO),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesTESObjectREFRGetEncounterZone{
		"Fixes"sv,
		"bTESObjectREFRGetEncounterZone"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesTESObjectREFRGetEncounterZone),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesPipBoyLightInv{
		"Fixes"sv,
		"bPipBoyLightInv"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesPipBoyLightInv),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesInteriorNavCut{
		"Fixes"sv,
		"bInteriorNavCut"sv,
		SettingDisplayCategory::kGameplay,
		true,
		std::addressof(lsFixesInteriorNavCut),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesMagicEffectApplyEvent{
		"Fixes"sv,
		"bMagicEffectApplyEvent"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesMagicEffectApplyEvent),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesMagicEffectConditions{
		"Fixes"sv,
		"bMagicEffectConditions"sv,
		SettingDisplayCategory::kGameplay,
		true,
		std::addressof(lsFixesMagicEffectConditions),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesEncounterZoneReset{
		"Fixes"sv,
		"bEncounterZoneReset"sv,
		SettingDisplayCategory::kGameplay,
		true,
		std::addressof(lsFixesEncounterZoneReset),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesLeveledListCrash{
		"Fixes"sv,
		"bLeveledListCrash"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesLeveledListCrash),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesBakaMaxPapyrusOps{
		"Fixes"sv,
		"bBakaMaxPapyrusOps"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsFixesBakaMaxPapyrusOps),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesPapyrusGCBug{
		"Fixes"sv,
		"bPapyrusGCBug"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesPapyrusGCBug),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCreateD3DAndSwapchain{
		"Fixes"sv,
		"bCreateD3DAndSwapchain"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesCreateD3DAndSwapchain),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCheckInternetAccess{
		"Fixes"sv,
		"bCheckInternetAccess"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesCheckInternetAccess),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesStolenPowerArmorOwnership{
		"Fixes"sv,
		"bStolenPowerArmorOwnership"sv,
		SettingDisplayCategory::kGameplay,
		true,
		std::addressof(lsFixesStolenPowerArmorOwnership),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesManyItems{
		"Fixes"sv,
		"bManyItems"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesManyItems),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCombatMusic{
		"Fixes"sv,
		"bCombatMusic"sv,
		SettingDisplayCategory::kAudio,
		true,
		std::addressof(lsFixesCombatMusic),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesWorkbenchSound{
		"Fixes"sv,
		"bWorkbenchSound"sv,
		SettingDisplayCategory::kAudio,
		true,
		std::addressof(lsFixesWorkbenchSound),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesActorCauseSaveBloat{
		"Fixes"sv,
		"bActorCauseSaveBloat"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsFixesActorCauseSaveBloat),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesAnimSignedCrash{
		"Fixes"sv,
		"bAnimSignedCrash"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesAnimSignedCrash),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesBethesdaNetCrash{
		"Fixes"sv,
		"bBethesdaNetCrash"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesBethesdaNetCrash),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesUtilityShader{
		"Fixes"sv,
		"bUtilityShader"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesUtilityShader),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesPipBoyCursorConstraints{
		"Fixes"sv,
		"bPipBoyCursorConstraints"sv,
		SettingDisplayCategory::kInterface,
		true,
		std::addressof(lsFixesPipBoyCursorConstraints),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesMuzzleFlashLight{
		"Fixes"sv,
		"bMuzzleFlashLight"sv,
		SettingDisplayCategory::kVisuals,
		true,
		std::addressof(lsFixesMuzzleFlashLight),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesPowerGridScrap{
		"Fixes"sv,
		"bPowerGridScrap"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesPowerGridScrap),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesViewmodelShading{
		"Fixes"sv,
		"bViewmodelShading"sv,
		SettingDisplayCategory::kVisuals,
		true,
		std::addressof(lsFixesViewmodelShading),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesDofFix{
		"Fixes"sv,
		"bDofFix"sv,
		SettingDisplayCategory::kVisuals,
		true,
		std::addressof(lsFixesDofFix),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesToggleGrassCommand{
		"Fixes"sv,
		"bToggleGrassCommand"sv,
		SettingDisplayCategory::kInterface,
		true,
		std::addressof(lsFixesToggleGrassCommand),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesTextureLoadCrash{
		"Fixes"sv,
		"bTextureLoadCrash"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesTextureLoadCrash),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesTextureCreateCrash{
		"Fixes"sv,
		"bTextureCreateCrash"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesTextureCreateCrash),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesFullPrecisionDecals{
		"Fixes"sv,
		"bFullPrecisionDecals"sv,
		SettingDisplayCategory::kVisuals,
		true,
		std::addressof(lsFixesFullPrecisionDecals),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesShaderReferenceEffectLifetime{
		"Fixes"sv,
		"bShaderReferenceEffectLifetime"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesShaderReferenceEffectLifetime),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesMagicKeywordCrash{
		"Fixes"sv,
		"bMagicKeywordCrash"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesMagicKeywordCrash),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesEffectEnvMapCrash{
		"Fixes"sv,
		"bEffectEnvMapCrash"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesEffectEnvMapCrash),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesNavmeshLocationCrash{
		"Fixes"sv,
		"bNavmeshLocationCrash"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesNavmeshLocationCrash),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCreatedRefBaseCrash{
		"Fixes"sv,
		"bCreatedRefBaseCrash"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesCreatedRefBaseCrash),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesAttachLightCrash{
		"Fixes"sv,
		"bAttachLightCrash"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesAttachLightCrash),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesDownwardAiming{
		"Fixes"sv,
		"bDownwardAiming"sv,
		SettingDisplayCategory::kGameplay,
		true,
		std::addressof(lsFixesDownwardAiming),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesThirdPersonSpeedMult{
		"Fixes"sv,
		"bThirdPersonSpeedMult"sv,
		SettingDisplayCategory::kGameplay,
		true,
		std::addressof(lsFixesThirdPersonSpeedMult),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesSprintStutter{
		"Fixes"sv,
		"bSprintStutter"sv,
		SettingDisplayCategory::kGameplay,
		true,
		std::addressof(lsFixesSprintStutter),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesMoonRotation{
		"Fixes"sv,
		"bMoonRotation"sv,
		SettingDisplayCategory::kVisuals,
		true,
		std::addressof(lsFixesMoonRotation),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCrashRemoveRefFix{
		"Fixes"sv,
		"bCrashRemoveRefFix"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesCrashRemoveRefFix),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesClimateLoad{
		"Fixes"sv,
		"bClimateLoad"sv,
		SettingDisplayCategory::kVisuals,
		true,
		std::addressof(lsFixesClimateLoad),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesMusicOverlap{
		"Fixes"sv,
		"bMusicOverlap"sv,
		SettingDisplayCategory::kAudio,
		true,
		std::addressof(lsFixesMusicOverlap),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesPuddleCubemaps{
		"Fixes"sv,
		"bPuddleCubemaps"sv,
		SettingDisplayCategory::kVisuals,
		false,
		std::addressof(lsFixesPuddleCubemaps),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCubemapArray{
		"Fixes"sv,
		"bCubemapArray"sv,
		SettingDisplayCategory::kVisuals,
		true,
		std::addressof(lsFixesCubemapArray),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesStringPoolRelease{
		"Fixes"sv,
		"bStringPoolRelease"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesStringPoolRelease),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesLoadOrder{
		"Fixes"sv,
		"bLoadOrder"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesLoadOrder),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesHUDMessageQueue{
		"Fixes"sv,
		"bHUDMessageQueue"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesHUDMessageQueue),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesWaterJetpackFix{
		"Fixes"sv,
		"bWaterJetpackFix"sv,
		SettingDisplayCategory::kGameplay,
		true,
		std::addressof(lsFixesWaterJetpackFix),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCraftingMenuFix{
		"Fixes"sv,
		"bCraftingMenuFix"sv,
		SettingDisplayCategory::kInterface,
		true,
		std::addressof(lsFixesCraftingMenuFix),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesAIProcess3DUpdateFlag{
		"Fixes"sv,
		"bAIProcess3DUpdateFlag"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesAIProcess3DUpdateFlag),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesNPCHealthOverflowFix{
		"Fixes"sv,
		"bNPCHealthOverflowFix"sv,
		SettingDisplayCategory::kGameplay,
		true,
		std::addressof(lsFixesNPCHealthOverflowFix),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesArmorPenetration{
		"Fixes"sv,
		"bFixesArmorPenetration"sv,
		SettingDisplayCategory::kGameplay,
		false,
		std::addressof(lsFixesArmorPenetration),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesLocalMapRotation{
		"Fixes"sv,
		"bFixesLocalMapRotation"sv,
		SettingDisplayCategory::kGameplay,
		true,
		std::addressof(lsFixesLocalMapRotation),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesNeverFadesSpotlight{
		"Fixes"sv,
		"bNeverFadesSpotlight"sv,
		SettingDisplayCategory::kVisuals,
		true,
		std::addressof(lsFixesNeverFadesSpotlight),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesLightOrder{
		"Fixes"sv,
		"bLightOrder"sv,
		SettingDisplayCategory::kVisuals,
		true,
		std::addressof(lsFixesLightOrder),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesLightPriority{
		"Fixes"sv,
		"bLightPriority"sv,
		SettingDisplayCategory::kVisuals,
		true,
		std::addressof(lsFixesLightPriority),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesRadioSilence{
		"Fixes"sv,
		"bFixesRadioSilence"sv,
		SettingDisplayCategory::kGameplay,
		false,
		std::addressof(lsFixesRadioSilence),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesQuestLogBloat{
		"Fixes"sv,
		"bQuestLogBloat"sv,
		SettingDisplayCategory::kPerformance,
		true,
		std::addressof(lsFixesQuestLogBloat),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesCustomScale{
		"Fixes"sv,
		"bCustomScale"sv,
		SettingDisplayCategory::kGameplay,
		true,
		std::addressof(lsFixesCustomScale),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesArchiveStreamClose{
		"Fixes"sv,
		"bArchiveStreamClose"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesArchiveStreamClose),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bFixesPapyrusLeaks{
		"Fixes"sv,
		"bPapyrusLeaks"sv,
		SettingDisplayCategory::kStability,
		true,
		std::addressof(lsFixesPapyrusLeaks),
		SettingApplyTiming::kNextLaunch
	};
}