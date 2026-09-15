# Changelog

## 1.6.0

- Added the "AIProcess 3DUpdateFlag" module.
- Added the "Armor Penetration" module.
- Added the "Attach Light Crash" module.
- Added the "CosaveIO" module.
- Added the "Crafting Menu Fix" module.
- Added the "Full Precision Decals" module.
- Added the "High Res Local Maps" module.
- Added the "Magic Keyword Crash" module.
- Added the "Menu" module.
- Added the "Moon Rotation" module.
- Added the "NPC Health Overflow Fix" module.
- Added the "Process Icon" module.
- Added the "Sprint Stutter" module.
- Added the "Water Jetpack Fix" module.
- Added support for DearModdingUI.
- Added and improved existing compatibility checks.
- Removed the "RobCo Patcher Cache" module.
- ArchiveLimits: Fixed the LoadChunks patch on OG.
- ArchiveLimits: Added 1.11.240.0 support.
- AudioSwitch: Reworked AudioProxy into AudioSwitch (XAudio2.7).
- ControlSamplers: Fixed a rare startup crash.
- CrashLogger: Added the .240 PDB (~43K symbols).
- CrashLogger: Updated the .221 PDB (~43K symbols).
- CrashLogger: Read AddictolCustom.toml overrides.
- EscapeFreeze: Only release locks orphaned by a terminated owner thread.
- FasterWorkshop: Fixed ClearBuiltMap never being called.
- LoadOrder: Further fixes to prevent plugin shuffling.
- LoadOrder: Prevent a kGameDataReady freeze for larger load orders.
- MagicEffectConditions: Cache the game setting.
- MemoryManager: Decreased memory usage.
- MemoryManager: Fixes and improvements.
- MaxPapyrusOps: Fixed the script page allocation guard.
- MaxStdIO: Set the proper limits for OG.
- PackageAllocateLocation: Fixed an AE-specific issue.
- Telemetry: Reworked the Profiler into Telemetry.
- Tints: Removed the IsDLCMasterOrCCFile patch.
- Other internal fixes and improvements.

## 1.5.2

- Various Audio Proxy improvements.
- Disabled bAudioProxy by default for now.
- Added a check for Companion Shoots At Player Fix.

## 1.5.1

- Fixed the MusicOverlap module if the user removes combat music.
- Added XAPO support for the AudioProxy module.

## 1.5.0

- Added the "Audio Proxy" module.
- Added the "Climate Load" module.
- Added the "Companion Stray Bullet" module.
- Added the "Crash Remove Ref" module.
- Added the "HUD Message Queue" module.
- Added the "Load Order" module.
- Added the "Magic Effect Conditions" module.
- Added the "Music Overlap" module.
- Added the "Puddle Cubemaps" module.
- Added the "RobCo Patcher Cache" module.
- Added the "Save Compression" module.
- Added the "String Pool Release" module.
- CrashLogger: Improved and expanded introspection.
- CrashLogger: Log __fastfail and fail-fast exceptions via VEH.
- CrashLogger: Recover the caller chain for null function-pointer calls.
- CrashLogger: Survive stack-overflow crashes.
- CrashLogger: Updated the .221 PDB (~41K symbols).
- AltTabFullscreen: Fixed black screen issues.
- ArchiveLimits: Fixed the LOD issue when over the limits.
- ReferenceHandleLimit: Improved and remade to save and quit.
- SmallBlockAllocator: Improved and upgraded.
- Other internal fixes and improvements.

## 1.4.1

- Fixed bullets disappearing.
- Scaleform: Allow more than a 2 GB heap size.

## 1.4.0

- Added the "Alt-Tab Fullscreen" module.
- Added the "Anim Signed Crash" module.
- Added the "Bethesda.net Crash" module.
- Added the "Depth of Field Fix" module.
- Added the "Downward Aiming" module.
- Added the "DPI Scaling" module.
- Added the "High Res Bloom" module.
- Added the "Muzzle Flash Light" module.
- Added the "PipBoy Cursor Constraints" module.
- Added the "Power Grid Scrap" module.
- Added the "Reference Handle Limit Warning" module.
- Added the "Texture Load Crash" module.
- Added the "Toggle Grass Command" module.
- Added the "Viewmodel Shading" module.
- CrashLogger: Added the .221 PDB (~34K symbols).
- CrashLogger: Restrict logging to AddictolCrashLogger.log.
- CreateD3DAndSwapchain: Fixed a faulty hook.
- PapyrusGCBug: Fixed NG ID issues.
- WorkbenchSoundFix: Fixed console log spam.
- Other internal fixes and improvements.

## 1.3.0

- Added the "Combat Music Fix" module.
- Added the "Workbench Sound Fix" module.
- Added the "Actor Save Bloat Fix" module.
- Added the "Utility Shader" module.
- Addictol: New config file layout.
- Addictol: Config validation and module load summary.
- CrashLogger: Added extra safeguards.
- CrashLogger: Improved stability under Wine.
- CrashLogger: Added a warning about AddictolCrashLogger.log.
- CrashLogger: Switched to a TOML config file format.
- CrashLogger: New config file layout.
- Facegen: Added support to change NPCs from save files.
- InitTints: Fixed an occasional error when loading the game.
- InputSwitch: Disabled by default.
- LoadScreen: Improved compatibility with High FPS Performance Fix.
- MaxPapyrusOps: Fixed a freeze with the ReloadScripts command.
- PapyrusGCFix: Fixed NG/AE issues.
- PapyrusGCFix: Enabled by default.
- SafeExit: Deferred to fix a deadlock that could occur.
- Other internal fixes and improvements.

## 1.2.0

- Added the "COM Init" module (compatibility with ALR).
- Added the "Papyrus GC Fix" module ("Long Save Bug Fix" mod).
- Added the "Performance Profiler" module.
- Added the "Save Added Sound Categories" module.
- ArchiveLimits: Fixed an incorrect register used for StartStreamingChunksPatch on old-gen.
- ArchiveLimits: Improved archive lookup to allow multiple concurrent thread access.
- ArchiveLimits: Optimized hashing operations.
- CheckInternetAccess: Fixed an issue when a custom WinHTTP.dll is present.
- CheckInternetAccess: This fixes the Creation Club being inaccessible.
- ControlSamplers: Fixed an incompatibility with "Fallout 4 Upscaler".
- Facegen: Fixed a potential crash when reading the config caused by a missing null check.
- Facegen: Improved exception lookup performance.
- FasterWorkshop: Eliminated double hash lookups.
- InitTints: Fixed the module on old-gen.
- InputSwitch: Fixed auto-device detection for F4SE's "GetMappedKey" function.
- LibDeflate: Various fixes and improvements.
- LoadScreen: Fixed an issue where the back buffer was never released.
- MemoryManager: Various fixes and improvements.
- CrashLogger: Various fixes and improvements.
- Other internal fixes and improvements.

## 1.1.0

- Added Faster Workshop (improvement for NG/AE).
- Added the Archive Limits patch (for any supported version).
- Fixed the Papyrus function and added function x-cell for GVCM compatibility.
- Fixed a crash on startup when enumerating certain monitor display modes.
- Added the BSMTAManager patch for rendering performance improvement.
- Added a Wine check for the InteriorNavCut patch, for multithread or not.
- Set the default mipbias to -1.0f and anisotropy to 16; fixed the patch for the upscaler, and Addictol now rehooks.
- Disable the load screen patch if high physics is not found; it mod needs for fast load save files, this mod supported 21:9 display, for 16:9 enough high physics.
- Added a DuplicateAddonNodeIndex warning.
- Added Stolen Power Armor Ownership.
- Added a patch fixing memory leaks if the user plays without internet.
- Fixed dropped items now renamed on many items.
- Removed a spam message that causes freezes if the user value condition constant == 1.

## 1.0.0

- Initial mod.
