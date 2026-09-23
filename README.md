<div align="center">

<img src="Pics/logo.png" alt="Addictol" width="640">

# Addictol

**Engine fixes, crash fixes, and performance patches for Fallout 4 in one F4SE plugin.**

Addictol consolidates proven work from Buffout 4, X-Cell, Mentats, Escape Freeze Fix,
Baka MaxPapyrusOps, Interior NavCut Fix, and Faster Workshop alongside fixes developed here.

<br>

[![CI](https://img.shields.io/github/actions/workflow/status/Dear-Modding-FO4/Addictol/xmake.yml?branch=master&style=for-the-badge&label=CI&logo=githubactions&logoColor=white)](https://github.com/Dear-Modding-FO4/Addictol/actions/workflows/xmake.yml)
[![Release](https://img.shields.io/github/v/release/Dear-Modding-FO4/Addictol?style=for-the-badge&label=release&color=orange)](https://github.com/Dear-Modding-FO4/Addictol/releases/latest)
[![License](https://img.shields.io/badge/license-GPL--3.0%20with%20exception-blue?style=for-the-badge)](LICENSE)

[![Fallout 4](https://img.shields.io/badge/Fallout%204-OG%20%C2%B7%20NG%20%C2%B7%20AE-3a7d44?style=for-the-badge)](#requirements)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](xmake.lua)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6?style=for-the-badge&logo=windows&logoColor=white)](#building)

<sub>[Requirements](#requirements) · [Features](#features) · [Changelog](CHANGELOG.md) · [Installation](#installation) · [Configuration](#configuration) · [Menu](#menu) · [Building](#building) · [Contributing](#contributing) · [License](#license)</sub>

</div>

---

## Requirements

| | |
|---|---|
| **Fallout 4** | OG **1.10.163**, NG **1.10.984**, or AE **1.11.240**. One DLL supports all three. |
| **[Fallout 4 Script Extender (F4SE)](https://f4se.silverlock.org/)** | Required for the matching game runtime. |
| **[Address Library for F4SE](https://www.nexusmods.com/fallout4/mods/47327)** | Required for the matching game runtime. Addictol will refuse to load without it. |
| **[DearModdingUI](https://github.com/Dear-Modding-FO4/DearModdingUI)** | Install the standalone host separately (`1c5eb35` or later). Addictol continues without an in-game menu when the host is absent. |

---

## Features

The 91 modules cover the following areas. Most can be toggled independently; a small set of
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
│  └─ Addictol.dll, Addictol_*.ini
└─ Scripts\{Addictol,XCELL}.pex
```

The archive also contains `Addictol.pdb` and Papyrus source files; neither is required to play.

Use the [latest stable release](https://github.com/Dear-Modding-FO4/Addictol/releases/latest) for
normal installations. Development prereleases are published from `master` as
`vMAJOR.MINOR.PATCH-dev.RUN`; they contain newer unpromoted changes and are available from the
[full releases list](https://github.com/Dear-Modding-FO4/Addictol/releases).

---

## Configuration

The central registry exposes 116 settings through `[Patches]`, `[Fixes]`, `[Warnings]`,
`[Telemetry]`, and `[Additional]`. On first launch, Addictol creates
`Data\F4SE\Plugins\Addictol.toml` with every registry description and a commented factory value.
Addictol refreshes those managed help comments on later launches while preserving active values,
unknown entries, and personal notes placed outside the marked help blocks.
Uncomment a generated assignment or add an override to edit settings without DearModdingUI.
File edits are loaded on the next game launch.

> [!WARNING]
> Existing users must move the old shipped `Addictol.toml` aside, then rename
> `AddictolCustom.toml` to `Addictol.toml` if they used one. There is no automatic migration.
> Leaving the old full base file in place makes every active value in it an explicit override.

```toml
[Fixes]
bUnalignedLoad = false
```

Only active values that differ from factory defaults are retained when settings are applied through
the menu. Reset fills the menu draft with C++ factory defaults; Revert restores the last committed
values. Most changes take effect on the next launch, while settings marked immediate update when
Apply succeeds.

Addictol writes `Addictol.log` to `Documents\My Games\Fallout4\F4SE\`. It records which modules
loaded, were disabled, or skipped; check it first when something is not working.

### Operation profiling

`[Telemetry] bOperationProfiling = true` enables sampled operation profiling on the next launch,
independently of `[Telemetry] bEnabled`. Each run writes a unique capture under
`Data\F4SE\Plugins\Addictol\Captures\<capture-id>\` with `metadata.json`, `telemetry.csv`, and
`series.csv`. The legacy `AddictolTelemetry.csv` and `AddictolSeries.csv` paths remain controlled by
`bEnabled` and `bCsv`.

Installed allocator hooks share one source across MemoryManager, scrap, Havok, CRT, small-block,
and Scaleform sites. `ProfiledHeap<Heap, Site>` decorates the selected heap; disabled profiling
selects the original type, without per-call settings checks. Existing heap choices and module
gates are unchanged, including Visper small-block and the dormant Scaleform replacement.
Descriptors cover operation families and allocation/reallocation request sizes (≤64 B, ≤1 KiB,
≤64 KiB, larger), plus failure and realloc in-place/moved results. Free never probes allocation size.
Sampling is 1/256 with 262,144 records (roughly one second at 67 million heap operations/sec).

Zlib profiling surrounds backend dispatch, not codec implementations. It records total inflate
duration and output bytes for primary service or stock fallback by reason, including selected stock,
with decode failures split by bad header, bad data, insufficient space, short output, or other.
Window-limited and bad-data fallback streams also report lifetime/output and whether the first call held all input, input was refilled, or tracking ended by abandonment/eviction.
It samples every call with 131,072 records, sized for in-game load bursts. This works with ordinary telemetry off; ordinary
telemetry's existing counters and internal codec timing remain unchanged. Effective backend
labels and all sampling/storage limits are exported.

Consumers register immutable `OperationProfileSource` descriptors through `Telemetry::Hub().Register`
before collection starts. `Begin(admission)` counts calls; `End(std::move(token), bytes, result)`
publishes a coherent sampled record. Results must share the admission's nonempty `resultGroup`
(or be the admission itself); invalid classifications are rejected and counted. All-operation
counts remain on admission descriptors, while durations go to result descriptors. Metadata exports
these groups. Sources must outlive their move-only, generation-bound tokens.

After one-time TLS lane acquisition, unsampled calls use thread-local sampling and relaxed
loads/stores to exclusively owned, cache-line-padded counters: no shared atomic RMW, clock read,
or allocation. There are 256 concurrent producer lanes, returned at thread exit; an unassigned
thread rejects and counts operations for its remaining lifetime. Sampled admission/publication
is bounded and capture-safe. Collector/exporter work uses `ScopedOperationProfileSuppression`.

Percentiles are bucket-bounded estimates, sampled bytes are not exact heap accounting, and timings
are inclusive (nested codec/allocator durations must not be added). Counter snapshots have approximate
interval boundaries. Saturation/contention losses bias retained samples and must be inspected;
metadata also reports invalid results, producer-capacity loss, unfinished work and capture completion.
There is no thread-class/BSJobs dimension yet and no in-game performance or hook-safety proof.

---

## Menu

DearModdingUI owns the shared menu. Press **F11** to open it, or configure the toggle key and
appearance in the host settings page behind the footer gear (or via `DearModdingUI.toml`).

| Page | Contents |
|---|---|
| **Home** | Runtime, live module summary, project links, FAQ, and mod evaluation guide. |
| **Settings** | All 116 Addictol settings under Stability, Performance, Visuals, Audio, Gameplay, Interface, and Diagnostics. |
| **Modules** | Every registration outcome, with search, outcome filters, skip reasons, and the config key for disabled modules. |
| **Changelog** | Released versions and their notes from the canonical [changelog](CHANGELOG.md). |
| **Telemetry** | Overview, Memory, Decompression, Stability, and Audio panels when telemetry or operation profiling is enabled. |
| **Facegen Exceptions** | Facegen exception coverage, configuration state, and resolution failures. |
| **Log Control** | Session-only record and flush levels with the live output rate. |

Home, Settings, Modules, and Changelog appear under **General**. Telemetry, Facegen Exceptions, and
Log Control appear under **Diagnostics**. Home uses DmUI's icon-bearing section headers and project-link buttons;
GitHub and Nexus Mods open in your default browser through the host. Use the header action button to
copy a diagnostic summary to the clipboard.

Shared appearance, accessibility, and toggle-key controls live behind the footer gear. Addictol's
refresh interval remains under **Settings > Interface**.

---

## Building

Clone recursively to initialize dependencies:

```powershell
git clone --recurse-submodules https://github.com/Dear-Modding-FO4/Addictol.git
cd Addictol
```

### xmake (recommended)

```powershell
xmake build Addictol
```

To build and run tests:

```powershell
xmake build vmm-tests
.\.Build\Tests\vmm-tests.exe
```

### MSBuild / Visual Studio

Build with Visual Studio 2022 or standalone Build Tools using the pinned `v143` toolset:

```powershell
MSBuild VC/Addictol.sln -p:Configuration=Release -p:Platform=x64
```

Both build systems stage Addictol's DLL and authored payload under `.Build\`. Install DearModdingUI
separately for the in-game menu.

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, the module model, cross-runtime
address rules, and pull request requirements.

---

## License

GPL-3.0 with a Modding Exception. See [LICENSE](LICENSE) and [EXCEPTIONS](EXCEPTIONS).
