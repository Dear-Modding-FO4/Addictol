# Contributing to Addictol

Addictol patches Fallout 4's engine in memory at runtime. A mistake does not produce a failed test;
it produces a crash in somebody's 200 hour save. Three rules shape everything else:

**One DLL, three runtimes.** OG 1.10.163, NG 1.10.984 and AE 1.11.240 load the same binary. Every
game address must resolve correctly on all three, or be gated to the runtimes where it is valid.

**Fail closed.** A module that cannot apply itself safely disables itself and logs why. Trading a
rare vanilla bug for a new crash is a regression, however correct the patch is in isolation.

**Automated tests cover only Addictol's own subsystems.** Module and runtime correctness comes from
reasoning about the engine and from running the game.

## Setting up

You need Visual Studio 2022 or the VS 2022 Build Tools with the C++ workload, and NASM on `PATH`
(or `NASM_PATH` set to its directory) for ISA-L.

```powershell
git clone --recurse-submodules https://github.com/Dear-Modding-FO4/Addictol.git
cd Addictol
MSBuild VC/Addictol.sln -p:Configuration=Release -p:Platform=x64
```

xmake works too: `xmake build -P . Addictol`. Set `FO4_DEV_MODS` to your mod manager's mods
directory and `xmake install Addictol` deploys to an `Addictol - Dev` folder there.

Both builds stage `.Build/F4SE/Plugins/Addictol.dll` plus the authored `data/` payload. Running it
needs [F4SE](https://f4se.silverlock.org/) and the Address Library for your runtime.

Things that catch everyone once:

- The project pins `PlatformToolset v143`. On newer Visual Studio, pass `-p:PlatformToolset=v145`
  instead of editing the tracked pin.
- There is no Debug configuration; every solution configuration builds `Release|x64`.
- Build the whole solution, not just the Addictol project, or linking cannot find dependencies.
- MSBuild does not glob. Every new `.cpp` needs a `<ClCompile>` entry in `VC/Addictol.vcxproj`
  (headers as `<ClInclude>`, both mirrored in `.filters`). xmake globs.
- `Addictol/Include/Core/AdPCH.h` is force-included. Never include it, and never put anything in it
  that changes between builds.
- Cloned without `--recurse-submodules`? Run `git submodule update --init --recursive`; the nested
  submodules matter.

## Repository layout

`Addictol/Include` and `Addictol/Source` mirror each other: `Core/` (lifecycle, hook utilities,
the `Settings/` registry), `Modules/` (one feature each), and `Memory/`, `Zlib/`, `Telemetry/`,
`Menu/`. `data/` is the authored mod payload, `Version/` the version header, `Tools/` the packaging
tools, and `Depends/` the submodules and vendored libraries. `commonlibf4` provides the `RE::`,
`REL::`, `REX::`, `F4SE::` and DearModdingUI client APIs.

Crash logging ships separately as
[AddictolCrashLogger](https://github.com/Dear-Modding-FO4/AddictolCrashLogger), and DearModdingUI
is a separate mod. The menu draws only through DearModdingUI's `dmui::ui` interface; it compiles no
Dear ImGui sources and requires an exact `DMUI_ABI_VERSION` match.

## The module model

Every feature is a subclass of `Addictol::Module` (`Addictol/Include/Core/AdModule.h`) that owns
exactly one concern:

```cpp
Module(const char* a_name, const REX::TOML::Bool<>* a_option = nullptr,
	std::initializer_list<uint32_t> a_listeners = {}, bool a_papyrusListener = false);
```

`a_name` must be unique; a collision drops the module with only an error in the log. `a_option` is
the toggle, and `nullptr` makes the module mandatory. `a_listeners` subscribes `DoListener` to F4SE
messages, and `a_papyrusListener` opts into `DoPapyrusListener`.

| Method | When | Return value |
| --- | --- | --- |
| `DoQuery()` | before anything is patched | `false` means "I cannot run here"; the module is dropped and logged. |
| `DoInstall(msg)` | only if `DoQuery()` returned true | `false` means install failed. Nothing is rolled back. |
| `DoListener(msg)` | per subscribed message | conventionally `true` |
| `DoPapyrusListener(vm)` | when the Papyrus VM binds | conventionally `true` |

Only `DoInstall` is pure virtual. Every call is wrapped in structured exception handling, so a fault
is logged as a `false` return instead of crashing. That is a safety net, not permission to be careless.

`modules.Register(sModule)` queries and installs at plugin load, before the game has loaded
anything; use it for pure code patches. Pass a `ModuleManager::Type` stage such as
`kGameDataReady` when the patch needs forms, the Papyrus VM, a save, or another mod's DLL.
Registering under several stages installs once per stage, so do it only when that is harmless.

## Adding a module

A configurable module touches six places:

1. `Addictol/Include/Modules/AdModule<Name>.h`: the class, with `[[nodiscard]] virtual ... noexcept
   override` on each `DoX`.
2. `Addictol/Source/Modules/AdModule<Name>.cpp`: the implementation.
3. Its constructor: name, option, listener stages and Papyrus flag.
4. `Addictol/Source/Core/AdRegisterModules.cpp`: the `#include`, a `static auto sModule<Name> =
   std::make_shared<...>()`, and the `modules.Register(...)` call.
5. `VC/Addictol.vcxproj` and `.filters`.
6. The setting in `Addictol/Include/Core/Settings/AdSettings.h` and its section source under
   `Addictol/Source/Core/Settings`.

`ModuleUnalignedLoad` is about as small as a real module gets:

```cpp
ModuleUnalignedLoad::ModuleUnalignedLoad() :
	Module("Unaligned Load", &bFixesUnalignedLoad)
{}

bool ModuleUnalignedLoad::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
{
	const auto target = REL::Relocation<uintptr_t>{ REL::ID{ 44611, 2277131 }, REL::Offset{ 0x174, 0x192 } }.address();

	if (RELEX::IsRuntimeOG())
	{
		// CreateCommandBuffer (not needed in NG/AE)
	}

	// ApplySkinningToGeometry
	const uint8_t value = 0x10;
	REL::WriteSafe(target, &value, sizeof(value));
	return true;
}
```

## Configuration

Each setting is declared once, in the registry under `Addictol/Source/Core/Settings`:

```cpp
BoolSetting bFixesUnalignedLoad{
	"Fixes"sv,
	"bUnalignedLoad"sv,
	SettingDisplayCategory::kStability,
	true,
	"Fixes a crash related to SIMD intrinsics with an aligned move on unaligned memory."sv,
	SettingApplyTiming::kNextLaunch
};
```

The types are `BoolSetting`, `F32Setting`, `I32Setting`, `U32Setting` and `StrSetting`; numeric
settings can append a `SettingNumericRange`. Use `kImmediate` only when a write changes
already-installed behavior.

| Section | For |
| --- | --- |
| `[Patches]` | Replacing an engine subsystem for performance or capability. |
| `[Fixes]` | Fixing a specific engine bug or crash. |
| `[Warnings]` | Reporting load order problems without changing gameplay. |
| `[Additional]` | Tunables for a feature in another section; reference it with `(needs bX)`. |

Prefix keys by type: `b` boolean, `n` signed, `u` unsigned, `f` float, and embed the section in the
variable name. Describe each key in one plain-language, user-facing line.

The C++ initializer is the only default. The game writes descriptions and user overrides to
`AddictolCustom.toml` if present, otherwise `Addictol.toml`. The `Addictol.toml` in `data/` is an
empty stub that packaging fills with `Tools/AdSettingsGenerator`; do not populate it by hand.

Default a new module to `true` only for an outright bug fix that is safe and validated in game.
Heuristics and behavior changes ship `false` until they have field data.

## Game addresses across OG, NG and AE

Addresses come from the Address Library through CommonLibF4's `REL` API:

```cpp
auto sub = REL::ID(2190427).address();                    // one id for all runtimes
REL::Relocation<uintptr_t>{ REL::ID{ 44611, 2277131 },    // OG, then NG/AE
	REL::Offset{ 0x174, 0x192 } }.address();
REL::Relocation<uintptr_t>(REL::ID{ 224250, 2277018, 4492363 },
	REL::Offset{ 0x114, 0x114, 0x10B }).address();          // all three differ
```

**The NG/AE rule.** `REL::ID{ OG, NG }` silently reuses the NG id for AE. NG and AE usually share
ids, which is exactly what makes the exceptions dangerous: a wrong AE id resolves and patches an
unrelated function. Verify every AE id independently against AE's Address Library before shipping,
especially ids ported from other mods.

If a runtime has no equivalent site, gate the code with `RELEX::IsRuntimeOG()`, `IsRuntimeNG()` or
`IsRuntimeAE()` rather than inventing an id. A module meaningful on only one runtime returns that
check from `DoQuery()`.

## Patching safely

**Disable yourself, do not crash.** If a patch cannot apply safely, return `false` from `DoQuery()`
and log why with `Skip`. Terminate the process only when quitting is the feature or the user chose it.

**Verify before you write.** Check expected bytes with `RELEX::Validate`, or hook through
`TryDetourJump` / `TryDetourCall`, which only patch on a match. Older modules that skip this are
debt, not precedent.

**Check for conflicts.** If a standalone mod already fixes the bug, detect it with `IsModDLLPresent`
(probe every filename it ships under) and stand down.

**Respect Wine and Proton.** Gate threading and performance paths with `Addictol::UserUseWine()`,
degrading the feature rather than disabling the module.

**Do not fight other modules.** Several modules touch the D3D device, swapchain, or loading screen.
Pick a hook site nobody else owns, or probe its current state first, as `ModuleLoadScreen` does.

**Know your thread.** Engine work belongs on the main thread. State touched from render, Papyrus or
worker threads needs a comment and `std::atomic` or a lock.

## Hooking techniques

- **Byte patches** with `REL::WriteSafe` for small surgical changes. Comment the original
  disassembly beside the bytes.
- **Function detours** through the `RELEX` wrappers in `Addictol/Include/Core/AdUtils.h`
  (`DetourJump`, `DetourCall`, `DetourVTable`, `DetourIAT`, `DetourClassVTable`, and the validating
  `Try` variants). Do not hand-roll hooks.
- **IAT and COM vtable detours** for anything Direct3D or DXGI. COM slots are stable across all
  three runtimes, so no Address Library id is needed. Prefer this to byte patching the renderer.
- **Xbyak code caves** for real replacement logic, entered with `RELEX::XbyakJump` / `XbyakCall`
  or a hand-written `E9` jump:

```cpp
const auto rel = static_cast<int32_t>(dst - (src + 5));
const auto* const r = reinterpret_cast<const uint8_t*>(&rel);
RELEX::WriteSafe(src, { 0xE9, r[0], r[1], r[2], r[3], 0x90 });
```

## Code style

Match the file you are editing: tabs, Allman braces, no line limit, C++ latest.

- Machine-code literals (signatures, opcodes, expected bytes) are `std::initializer_list<uint8_t>`,
  never `std::array<uint8_t, N>`; a miscounted `N` is silently zero-filled and validates wrong.
  `std::array` stays correct for storage that owns its bytes.
- Fixed-width integers are unqualified (`uint8_t`, `size_t`); library facilities keep `std::`.
- Files are prefixed `Ad`; modules are `AdModule<Name>` declaring `class Module<Name>` in
  `namespace Addictol`, with private helpers in `namespace <camelCaseModuleName>Detail`.
- PascalCase functions, `HK<OriginalName>` hooks, `a_` parameters (except signatures mirroring
  external code), `s_` statics, `m_` members.
- No exceptions: return `bool` and log with `REX::INFO` / `WARN` / `ERROR` and `{}` placeholders.
- String literals carry `sv` (`using namespace std::literals;` comes from `AdUtils.h`).
- `#pragma once`, angle-bracket includes, and a module `.cpp` includes its own header first.

## Tests

```powershell
xmake build -P . -y vmm-tests
.\.Build\Tests\vmm-tests.exe
```

The runner checks Addictol's out-of-game subsystems end to end: the VMM allocator and heaps,
decompression backends against zlib, the profiled-heap path, and settings persistence. Do not add
tests for a single module or tests that simulate the game; modules are validated in game.
`--bench=<voltek|mimalloc|rpmalloc>` and `--bench-profile` run opt-in benchmarks into `.Build\Tests\`;
run one backend per process.

## Pull requests

Target `master`. CI attaches a build artifact, but a green check only means it compiles.

Before opening, confirm the bug happens without your patch and stops with it, test the module on and
off, and check `Documents\My Games\Fallout4\F4SE\Addictol.log` for your module and any new warnings.

In the description, state the engine bug and how you know the cause, which runtimes you ran, where
new Address Library ids came from and how you verified AE, and what could interact with the patch.

Reviewers check, in order: ids on all three runtimes, failing closed, conflicts with other modules
or mods, the six-place wiring, then style.

## Changelog and releases

`CHANGELOG.md` is embedded in the DLL as the in-game **General > Changelog** page, so it accepts only
`# Changelog`, `## <version>` headings, single-line `- ` bullets, and blank lines. Add user-visible
changes under `## Unreleased` as they land; remove that heading while it has no bullets, because an
empty section fails the parser.

`Version/resource_version2.h` is the single product version, consumed by both builds. Every push to
`master` publishes a prerelease `vMAJOR.MINOR.PATCH-dev.RUN` while the DLL keeps reporting
`MAJOR.MINOR.PATCH.0`.

To cut a stable release:

1. Confirm `master` holds the intended code, the version header matches the release, and
   `## Unreleased` is renamed to that version.
2. Run **Actions > Release stable** with the **version**, **source_ref** (`master` or a SHA), and
   **next_version**.
3. The workflow builds, verifies, and publishes `vMAJOR.MINOR.PATCH` as **Latest**, then opens a
   pull request bumping the header to `next_version`. Merge it once its checks pass.

Issue labels live in `.github/labels.json`; apply edits with `.\.github\scripts\sync-labels.ps1`
(`-WhatIf` to preview).
