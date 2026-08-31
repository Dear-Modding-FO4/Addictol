<div align="center">

<img src="Pics/logo.png" alt="Addictol" width="640">

# Addictol

**Engine fixes, crash fixes, and performance patches for Fallout 4 in one F4SE plugin.**

Addictol consolidates proven work from Buffout 4, X-Cell, Mentats, Escape Freeze Fix,
Baka MaxPapyrusOps, Interior NavCut Fix, and Faster Workshop alongside fixes developed here.

<br>

[![CI](https://img.shields.io/github/actions/workflow/status/Dear-Modding-FO4/Addictol/xmake.yml?branch=master&style=for-the-badge&label=CI&logo=githubactions&logoColor=white)](https://github.com/Dear-Modding-FO4/Addictol/actions/workflows/xmake.yml)
[![Version](https://img.shields.io/badge/version-1.6.x-orange?style=for-the-badge)](Version/resource_version2.h)
[![License](https://img.shields.io/badge/license-GPL--3.0%20with%20exception-blue?style=for-the-badge)](LICENSE)

[![Fallout 4](https://img.shields.io/badge/Fallout%204-OG%20%C2%B7%20NG%20%C2%B7%20AE-3a7d44?style=for-the-badge)](#requirements)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](xmake.lua)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6?style=for-the-badge&logo=windows&logoColor=white)](#building)

<sub>[Requirements](#requirements) · [Features](#features) · [Installation](#installation) · [Configuration](#configuration) · [Menu](#menu) · [Building](#building) · [Contributing](#contributing) · [License](#license)</sub>

</div>

---

## Requirements

| | |
|---|---|
| **Fallout 4** | OG **1.10.163**, NG **1.10.984**, or AE **1.11.240**. One DLL supports all three. |
| **[Fallout 4 Script Extender (F4SE)](https://f4se.silverlock.org/)** | Required for the matching game runtime. |
| **[Address Library for F4SE](https://www.nexusmods.com/fallout4/mods/47327)** | Required for the matching game runtime. Addictol will refuse to load without it. |

---

## Features

More than 90 modules cover the following areas. Most can be toggled independently; a small set of
core modules is mandatory.

| Capability | Implementation |
|---|---|
| **Stability and crash fixes** | Guards invalid engine state, repairs shutdown and loading faults, and fixes known crashes across gameplay, rendering, animation, and UI paths. |
| **Memory, I/O, and compression** | Replaces game allocators, accelerates zlib and save compression, and reduces file and co-save overhead. |
| **Performance and limits** | Optimizes workshop and configuration lookups, raises archive and handle limits, and removes avoidable engine bottlenecks. |
| **Engine and gameplay correctness** | Fixes Papyrus GC, navmesh cuts, input switching, audio state, encounter zones, crafting, and other engine behavior. |
| **Visuals and display** | Corrects facegen, viewmodel shading and depth of field, high-DPI behavior, bloom, local maps, and fullscreen transitions. |
| **Diagnostics and telemetry** | Reports load-order hazards and module outcomes, with optional sampled memory, frame, decompression, stability, and audio telemetry. |

> [!NOTE]
> Crash logging is separate: use [AddictolCrashLogger](https://github.com/Dear-Modding-FO4/AddictolCrashLogger).

---

## Installation

Install with a mod manager, or extract the release archive into the Fallout 4 `Data` directory:

```text
Data\
├─ F4SE\Plugins\
│  ├─ Addictol.dll, Addictol.toml, Addictol_*.ini
│  └─ DearModdingUI\{Fonts,Shaders}\...
└─ Scripts\{Addictol,XCELL}.pex
```

The archive also contains `Addictol.pdb` and Papyrus source files; neither is required to play.

---

## Configuration

The central registry exposes 121 settings through `[Patches]`, `[Fixes]`, `[Warnings]`,
`[Telemetry]`, and `[Additional]`. The shipped `Addictol.toml` documents every option inline.

> [!WARNING]
> Do not edit `Addictol.toml`; updates overwrite it. Put only your overrides in
> `AddictolCustom.toml` beside it.

```toml
[Fixes]
bUnalignedLoad = false
```

Addictol writes `Addictol.log` to `Documents\My Games\Fallout4\F4SE\`. It records which modules
loaded, were disabled, or skipped; check it first when something is not working.

---

## Menu

The menu is enabled by default. Press **F11** to open it, or set `[Additional] sMenuToggleKey` to
F1-F12, Home, End, Insert, or Delete. It can also display pages registered by other loaded mods.

| Page | Contents |
|---|---|
| **Home** | Runtime, live module summary, project links, and common answers. |
| **Settings** | All 121 settings under Stability, Performance, Visuals, Audio, Gameplay, Interface, and Diagnostics. |
| **Modules** | Every registration outcome, with search, outcome filters, skip reasons, and the config key for disabled modules. |
| **Telemetry** | Overview, Memory, Decompression, Stability, and Audio panels. |
| **Log Control** | Session-only record and flush levels with the live output rate. |

> [!NOTE]
> Telemetry panels appear only when `[Telemetry] bEnabled` is `true`; it defaults to `false`.

Shared appearance and accessibility controls live behind the footer gear. Addictol's menu enablement,
toggle key, and refresh interval are under **Settings > Interface**.

---

## Building

Use Visual Studio 2022 or its Build Tools with the pinned v143 toolset; newer Visual Studio releases
ship v145, so install v143 or override `PlatformToolset` on the command line as described in
[CONTRIBUTING.md](CONTRIBUTING.md).

```powershell
git clone --recurse-submodules https://github.com/Dear-Modding-FO4/Addictol.git
cd Addictol
MSBuild VC/Addictol.sln -p:Configuration=Release -p:Platform=x64
```

Output: `.Build/F4SE/Plugins/Addictol.dll`

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, the module model, cross-runtime
address rules, and pull request requirements.

---

## License

GPL-3.0 with a Modding Exception. See [LICENSE](LICENSE) and [EXCEPTIONS](EXCEPTIONS).

The DearModdingUI shell, theme, font roles, cursor behavior, and blur are ported from
[Fallout 4 Community Shaders](https://github.com/northaxosky/fallout4-community-shaders)
(`src/Menu/FeatureListRenderer.*`, `Menu.*`, `ThemeManager.*`, `Fonts.*`, `CursorLoader.*`,
`BackgroundBlur.*`, `src/Utils/UI.*`, and `ImGuiRecovery.h`), which is GPL-3.0. Its Gaussian blur
credits Unrimp by Christian Ofenberg under MIT.

The bundled Jost and Atkinson Hyperlegible fonts under
`Data\F4SE\Plugins\DearModdingUI\Fonts` use the SIL Open Font License; their license text sits next
to them, and Atkinson Hyperlegible's pinned official source is recorded in its provenance note. The
bundled Phosphor icon font uses MIT; its license and upstream pin sit next to the font.
