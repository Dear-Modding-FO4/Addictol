<div align="center">
  <img src="Pics/logo.png" alt="Addictol" width="640">
</div>

# Addictol

An F4SE plugin that combines engine fixes, crash fixes, and performance patches for Fallout 4 into
a single DLL.

Consolidates patches from Buffout 4, X-Cell, Mentats, Escape Freeze Fix, Baka MaxPapyrusOps,
Interior NavCut Fix, and Faster Workshop, alongside fixes written for this project.

Crash logging is not included here. It ships separately as
[AddictolCrashLogger](https://github.com/Dear-Modding-FO4/AddictolCrashLogger).

## Supported runtimes

One DLL covers all three; there is no separate download per version.

| Runtime | Version |
| --- | --- |
| OG | 1.10.163 |
| NG | 1.10.984 |
| AE | 1.11.240 |

## Features

- **Memory Manager** - Replaces the game's allocator with a selectable backend
- **Faster Workshop** - O(1) keyword lookups instead of scanning all constructible objects
- **Zlib Decompression** - Selectable stock/libdeflate backend
- **Facegen** - Validates NPC face textures before using preprocessed data
- **Input Switch** - Proper keyboard/gamepad device switching
- **Scaleform Allocator** - Replaces Scaleform's memory mapper with configurable page/heap sizes
- **Archive Limits** - Increases max BA2 archives the game can load
- **Menu** - In-game diagnostics window drawn over the game on a configurable key
- **Papyrus GC Bug** - Fixes a critical bug in garbage collection that causes premature loop termination
- ~80 modules in total, covering crash fixes, engine bug fixes and stability patches

## Requirements

- Fallout 4, on one of the runtimes above
- [F4SE](https://f4se.silverlock.org/)
- The **Address Library** for your runtime. Addictol will refuse to load without it.

## Installation

Install with a mod manager, or extract the release archive into your Fallout 4 `Data` directory so
that the files land as:

```
Data/F4SE/Plugins/Addictol.dll
Data/F4SE/Plugins/Addictol.toml
Data/F4SE/Plugins/Addictol_FacegenExceptions.ini
Data/F4SE/Plugins/Addictol_SNCT.ini
Data/F4SE/Plugins/DearModdingUI/Fonts/Jost/Jost-Regular.ttf
Data/F4SE/Plugins/DearModdingUI/Fonts/Jost/Jost-SemiBold.ttf
Data/F4SE/Plugins/DearModdingUI/Icons/LICENSE
Data/F4SE/Plugins/DearModdingUI/Icons/Actions/**/*.png
Data/F4SE/Plugins/DearModdingUI/Icons/Categories/**/*.png
Data/F4SE/Plugins/DearModdingUI/Icons/Clients/*.png
Data/F4SE/Plugins/DearModdingUI/Shaders/BackgroundBlur*.hlsl
Data/Scripts/Addictol.pex
Data/Scripts/XCELL.pex
```

The archive also contains `Addictol.pdb` and the Papyrus script sources; neither is needed to play.

## Configuration

Nearly every module can be individually toggled; a small number are mandatory and always run.
`Addictol.toml` is the shipped configuration and documents each option inline.

**Do not edit `Addictol.toml` directly**, because it is overwritten on update. Instead create
`AddictolCustom.toml` next to it, containing only the sections and keys you want to change:

```toml
[Fixes]
bUnalignedLoad = false
```

Options are grouped into `[Patches]` (subsystem replacements), `[Fixes]` (bug and crash fixes),
`[Warnings]` (diagnostics for problems in your load order) and `[Additional]` (tunables belonging
to another option).

Addictol writes `Addictol.log` to `Documents\My Games\Fallout4\F4SE\`, listing which modules loaded
and any that disabled themselves. Check it first if something is not working.

### Menu

`[Additional] bMenu` adds an in-game diagnostics window drawn over the game.
`sMenuToggleKey` accepts F1-F12, Home, End, Insert, and Delete, and falls back to F11 when it does
not recognize the name. `uMenuRefreshMs` sets how often the open page copies fresh data, clamped to
100-2000 ms.

The Evil Modding window selects a registered mod from a dropdown, then shows its categorized settings
pages. Client and category icons resolve by convention from the shared `DearModdingUI\Icons` folder;
missing icons fall back to text without reserving empty space.
Addictol telemetry pages retain their configured order and Log Control remains last.
Its default style, typography, scaling, search, navigation, header, footer, blur, and cursor
match the current Fallout 4 Community Shaders menu while keeping DearModdingUI's neutral mod registry.

The menu always starts closed. The ImGui host installs its hook for external F4SE plugins regardless
of `bMenu`; a disabled menu registers no setup, draw, or toggle sinks. Window geometry is kept in
`Data\F4SE\Plugins\DearModdingUI\imgui.ini`; the open state is not persisted. The modal host opens a
registered, hidden Fallout 4 menu so the engine releases its gameplay cursor confinement, then maps
absolute client coordinates into the active backbuffer. ImGui draws the only visible pointer while the
operating-system cursor and carrier movie remain hidden. Normal game cursor handling is restored on
close, and overlay-only frames never take cursor ownership or draw a cursor. Missing fonts or blur
shaders fall back to a usable unblurred menu.

Log Control changes the record and flush levels for the current session and shows the live output
rate. The record level decides which lines are kept; the flush level forces a synchronous disk
write at that level or higher. These overrides are not persisted; `[Additional] sLogLevel` and
`sLogFlushLevel` are the persistent controls.

## Building

Requires Visual Studio 2022 Build Tools (or VS 2022) with the v143 toolset.

```powershell
git clone --recurse-submodules https://github.com/Dear-Modding-FO4/Addictol.git
cd Addictol
MSBuild VC/Addictol.sln -p:Configuration=Release -p:Platform=x64
```

Output: `.Build/F4SE/Plugins/Addictol.dll`

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the development setup, the module model, the rules for
handling game addresses across the three runtimes, and what a pull request needs to include.

## License

GPL-3.0 with a Modding Exception. See [LICENSE](LICENSE) and [EXCEPTIONS](EXCEPTIONS).

The shared menu shell, theme, font roles, cursor behavior, and blur are ported from Fallout 4
Community Shaders (`src/Menu/FeatureListRenderer.*`, `Menu.*`, `ThemeManager.*`, `Fonts.*`,
`CursorLoader.*`, `BackgroundBlur.*`, `src/Utils/UI.*`, and `ImGuiRecovery.h`), which is GPL-3.0.
Its Gaussian blur credits Unrimp by Christian Ofenberg under MIT. The bundled Jost fonts under
`Data\F4SE\Plugins\DearModdingUI\Fonts` ship under the SIL Open Font License; their license text sits
next to them. Shared menu icons under `Data\F4SE\Plugins\DearModdingUI\Icons` come from Phosphor Icons
under MIT; their license text sits at the icon root.
