# Contributing to Addictol

Addictol patches Fallout 4's engine in memory at runtime. A single DLL supports three different
game builds, and a mistake does not produce a failed test; it produces a crash in somebody's
200 hour save. Most rules here exist because of that.

Three things shape everything else:

**One DLL, three runtimes.** OG 1.10.163, NG 1.10.984 and AE 1.11.240 are all supported from the
same binary. Every game address you add must resolve correctly on all three, or be explicitly gated
to the runtimes where it is valid. An address that is right on NG and wrong on AE will silently
patch unrelated code.

**Fail closed.** A module that cannot apply itself safely must disable itself and log why. Trading
a rare vanilla bug for a new crash is a regression, however correct the patch is in isolation.

**Automated coverage starts at the allocator boundary.** CI builds and runs the deterministic
`vmm-tests` checks through xmake. Plugin and runtime correctness still comes from reasoning about
the engine and from running the game.

## Setting up

You need Visual Studio 2022, or the standalone VS 2022 Build Tools, with the C++ workload. The
project pins `PlatformToolset v143`. Newer Visual Studio releases ship v145 and fail with **MSB8020**
("The build tools for v143 cannot be found"); install the v143 build tools or override the toolset
with `-p:PlatformToolset=...` on the command line. Do not edit the pin in the tracked project file.

```powershell
git clone --recurse-submodules https://github.com/Dear-Modding-FO4/Addictol.git
cd Addictol
MSBuild VC/Addictol.sln -p:Configuration=Release -p:Platform=x64
```

If you already cloned without submodules, run `git submodule update --init --recursive`. The
`--recursive` matters: `commonlibf4` has its own nested submodule.

The build stages `.Build/F4SE/Plugins/Addictol.dll` with the authored configuration and Papyrus
payload from `data/`. Deploy that payload to your game or mod manager. Running the plugin also needs
[F4SE](https://f4se.silverlock.org/) and the Address Library for your runtime.

Set `FO4_DEV_MODS` to your mod manager's mods directory and `xmake install Addictol` deploys that
payload to an `Addictol - Dev` folder inside it. Without the variable the build still succeeds and
deploys nothing. Name the target explicitly; a bare `xmake install` also selects the test and library
targets.

Staging only ever adds files. If you built before `data/` existed, delete `.Build` once: earlier
revisions copied DearModdingUI into it, and `.Build` is now fully ignored, so leftovers no longer
show up in `git status`.

### Things that will confuse you the first time

**There is no Debug configuration.** The solution lists `Debug|x64`, but every solution
configuration maps to `Release|x64`, and the project defines only `Release|x64`. Picking "Debug" in
the IDE builds Release.

**New source files are not picked up automatically.** There is no globbing. Every new `.cpp` needs a
`<ClCompile>` entry in `VC/Addictol.vcxproj` or it is never compiled, and since
`AdRegisterModules.cpp` references your module's constructor you get an unresolved symbol at link
time. By convention also add the header as `<ClInclude>`, and add both to
`VC/Addictol.vcxproj.filters` so they land in the right IDE folder. The `.filters` file affects only
Visual Studio's presentation, not the build.

**You do not include the precompiled header.** `Addictol/Include/Core/AdPCH.h` is force included into
every translation unit via `/FI`. Never add anything to it whose content changes from build to build,
which would invalidate the PCH on every build.

`.Lib/` and `.LinkConf/` are gitignored build directories. Build the whole solution, not just the
Addictol project, or the link step will not find the dependency libraries.

## Repository layout

```
Addictol/Include/Core/       core infrastructure and utilities
Addictol/Include/Memory/     memory allocation and tracing
Addictol/Include/Zlib/       compression backend and helpers
Addictol/Include/Telemetry/  telemetry interfaces and hub
Addictol/Include/Menu/       menu interfaces and widgets
Addictol/Include/Modules/    one header per feature module
Addictol/Source/             mirrors the concern folders under Include
Addictol/Source/Modules/     one .cpp per feature module (90 total)
VC/                          MSBuild solution and project files
Depends/                     submodules and vendored libraries
Version/                     version resource and the tracked version header
data/                        authored mod payload
.Build/                      generated build output and staged mod payload
```

`Depends/` holds submodules (`commonlibf4`, which provides the `RE::`, `REL::`, `REX::`, `F4SE::`
and DearModdingUI client APIs, plus `detours`, `imgui`, `libdeflate`, `spdlog`, `toml11` and `INI`)
and vendored libraries (`vmm`, `xbyak`, `unordered_dense`).

Crash logging is not part of this plugin. It ships separately as
[AddictolCrashLogger](https://github.com/Dear-Modding-FO4/AddictolCrashLogger).

The neutral cross-DLL UI contract, client lifecycle, and shared visual helpers come from the
standalone DearModdingUI API repository through CommonLibF4's nested
`lib/dearmoddingui-api` public dependency.

Addictol packages only its own payload. DearModdingUI is a separate mod installed independently.

## Versioning

`Version/resource_version2.h` is tracked and hand edited; it is the single source for the DLL's
`FileVersion` and `ProductVersion`, the F4SE plugin version and the startup log line. Major and
minor live in the file. The third field is `VERSION_BUILD`, which CI sets to the GitHub Actions run
number via `-p:AddictolVersionBuild=<n>` and which defaults to 0 otherwise, so a local build always
reports `1.6.0.0` and never writes to a tracked file. F4SE packs that field into 12 bits, so values
outside `0..4095` fail the build.

## The module model

Every feature is a subclass of `Addictol::Module` (`Addictol/Include/Core/AdModule.h`) that owns exactly
one concern. Nearly all are toggled by exactly one TOML key; a few are mandatory and always run.

```cpp
Module(const char* a_name, const REX::TOML::Bool<>* a_option = nullptr,
	std::initializer_list<uint32_t> a_listeners = {}, bool a_papyrusListener = false);
```

`a_name` is the registry key and appears in every log line, and it must be unique; a collision is
only logged as an error and the module is silently dropped. `a_option` is the TOML toggle, and
passing `nullptr` makes the module mandatory so the user can never turn it off. `a_listeners` lists
F4SE message types to deliver to `DoListener` after startup, and `a_papyrusListener` opts into
`DoPapyrusListener`.

### Lifecycle

`DoInstall` is the only pure virtual. The other three have default implementations that return
`true`, so override them only when the module needs them.

| Method | When | Return value |
| --- | --- | --- |
| `DoQuery()` | first, before anything is patched | `false` means "I cannot run here". The registration is dropped and the reason is logged. |
| `DoInstall(msg)` | only if `DoQuery()` returned true | `false` means install failed. It is counted and logged, and nothing is rolled back. |
| `DoListener(msg)` | per subscribed message, after startup | conventionally `true` |
| `DoPapyrusListener(vm)` | when the Papyrus VM binds, if opted in | conventionally `true` |

Before `DoQuery()` runs, a module whose option is `false` is unregistered and logged as `disabled`,
and a module with no option is logged as `mandatory`.

Registrations are tracked per stage, so a failed query removes only that registration. A module
registered under several stages can still install at the others.

Every one of these calls is wrapped in Win32 structured exception handling, so an access violation
in your module is caught, logged and treated as a `false` return instead of taking down the game.
That is a safety net, not a licence to be careless: a caught fault still means your module did not
install.

### Registration timing

```cpp
modules.Register(sModuleUnalignedLoad);                    // kLoad: immediately, at plugin init
modules.Register(sModuleThreads,        kGameDataReady);   // deferred until game data is loaded
modules.Register(sModuleInputSwitch,    kGameLoaded);
```

`Register` with no stage means `kLoad`, queried and installed during plugin init before the game has
loaded anything. Use it for pure code patches that depend only on the executable.

Register with a stage when your patch needs something that does not exist yet at load time: form
data, the Papyrus VM, a loaded save, or another mod's DLL being present so you can check for it. The
stages are in `ModuleManager::Type`: `kPostLoad`, `kPostPostLoad`, `kPreLoadGame`, `kPostLoadGame`,
`kPreSaveGame`, `kPostSaveGame`, `kDeleteGame`, `kInputLoaded`, `kNewGame`, `kGameLoaded`,
`kGameDataReady`.

A module may be registered under several stages, and is then queried and installed once per stage.
Only do that deliberately, and only if installing twice is harmless.

## Adding a module

A configurable module touches seven places. The last two produce no compiler error, which is why they
are the ones people forget.

1. `Addictol/Include/Modules/AdModule<Name>.h`, the class declaration.
2. `Addictol/Source/Modules/AdModule<Name>.cpp`, the implementation.
3. The constructor, inside that `.cpp`, wiring name, option, listener stages and Papyrus flag.
4. `Addictol/Source/Core/AdRegisterModules.cpp`: the `#include`, the `static auto sModule<Name> =
   std::make_shared<...>()`, and the `modules.Register(...)` call.
5. `VC/Addictol.vcxproj`, plus the header and the `.filters` entries by convention.
6. `Addictol/Include/Core/Settings/AdSettings.h` and the matching section source under
   `Addictol/Source/Core/Settings`, declaring the setting and its metadata.
7. `data/F4SE/Plugins/Addictol.toml`, the key with the same user-facing description under the right
   section. Registry tests enforce that the shipped file and registered keys match in both directions.

### Worked example

The header is boilerplate: a constructor and the four `DoX` overrides, each `[[nodiscard]]`,
`virtual`, `noexcept` and `override`. The `.cpp` is where the shape matters.
`ModuleUnalignedLoad` is about as small as a real module gets and still shows the important parts:

```cpp
#include <Modules/AdModuleUnalignedLoad.h>
#include <Core/AdUtils.h>

namespace Addictol
{
	ModuleUnalignedLoad::ModuleUnalignedLoad() :
		Module("Unaligned Load", &bFixesUnalignedLoad)
	{}

	bool ModuleUnalignedLoad::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto target = REL::Relocation<uintptr_t>{ REL::ID{ 44611, 2277131 }, REL::Offset{ 0x174, 0x192 } }.address();

		if (RELEX::IsRuntimeOG())
		{
			// CreateCommandBuffer (not needed in NG/AE)
			// ... OG only byte patches ...
		}

		// ApplySkinningToGeometry
		const uint8_t value = 0x10;
		REL::WriteSafe(target, &value, sizeof(value));

		return true;
	}
}
```

Registration and config:

```cpp
#include <Modules/AdModuleUnalignedLoad.h>
static auto sModuleUnalignedLoad			= std::make_shared<Addictol::ModuleUnalignedLoad>();
	modules.Register(sModuleUnalignedLoad);
```

```cpp
BoolSetting bFixesUnalignedLoad{
	"Fixes"sv,
	"bUnalignedLoad"sv,
	true,
	"Fixes a crash related to SIMD intrinsics with an aligned move on unaligned memory."sv,
	SettingApplyTiming::kNextLaunch
};
```

```toml
# Fixes a crash related to SIMD intrinsics with an aligned move on unaligned memory.
bUnalignedLoad = true
```

Note how the module scopes its patch: one address resolved for all runtimes, plus an explicit
`IsRuntimeOG()` branch for the extra sites only OG needs.

## Configuration

Options are declared under `Addictol/Source/Core/Settings`, split by TOML section. Each declaration
provides its section, key, default, shipped description, apply timing, and any enforced numeric range:

```cpp
I32Setting nAdditionalSleepTimer{
	"Additional"sv,
	"nSleepTimer"sv,
	125,
	"Sampling interval in milliseconds for Escape Freeze (needs bEscapeFreeze)."sv,
	SettingApplyTiming::kNextLaunch,
	SettingNumericRange{ 1.0, 60000.0 }
};
```

Use `BoolSetting`, `F32Setting`, `I32Setting`, `U32Setting`, or `StrSetting`. They derive from the
matching REX TOML setting, so existing typed accessors and module-gate pointer conversions still work.
Use `kImmediate` only when writes affect already-installed runtime behavior; otherwise use
`kNextLaunch`.

| Section | For |
| --- | --- |
| `[Patches]` | Replacing an engine subsystem for performance or capability. |
| `[Fixes]` | Fixing a specific engine bug or crash. Most modules live here. |
| `[Warnings]` | Detectors for problems in the user's load order. They exist to report, not to change gameplay. |
| `[Additional]` | Tunables belonging to a feature in another section. Cross reference the owner with `(needs bX)`. |

Prefix keys by type: `b` boolean, `n` signed, `u` unsigned, `f` float. The C++ variable name
conventionally embeds the section too, as in `bFixesUnalignedLoad`.

Give every key a one line, user facing comment in `data/F4SE/Plugins/Addictol.toml` that explains what
it does in plain language rather than implementation terms:

```toml
# The page size (in KB), vanilla size is 64. More, better, but the higher the memory consumption. Limit 2Mb (2048), number must be a multiple of 8 (needs bScaleformAllocator).
uScaleformPageSize = 64
```

Declare the default in the C++ setting and ship the same value in the TOML, and keep the two in sync.
The shipped TOML value wins at load time, so a stale C++ default is invisible to users but misleads
the next person reading the source. Users override settings in their own `AddictolCustom.toml`;
never expect them to edit the shipped file.

Default a new fix to `true` only if you are confident it is safe and well tested. Heuristics,
anything that changes behaviour rather than fixing an outright bug, and anything you have not
validated in game should ship `false` and be promoted later once it has field data.

## Game addresses across OG, NG and AE

Addresses come from the Address Library and are expressed with CommonLibF4's `REL` API, resolved per
runtime at load time.

```cpp
// One id valid on all three runtimes
auto sub = REL::ID(2190427).address();

// OG id plus a shared NG/AE id, with per runtime offsets into the function
const auto target = REL::Relocation<uintptr_t>{ REL::ID{ 44611, 2277131 },
	REL::Offset{ 0x174, 0x192 } }.address();

// All three ids differ
const auto target = REL::Relocation<uintptr_t>(REL::ID{ 224250, 2277018, 4492363 },
	REL::Offset{ 0x114, 0x114, 0x10B }).address();
```

`.address()` already returns a `uintptr_t`, so do not cast it again.

### The NG/AE rule

`REL::ID` fills any runtime slot you leave out with the last value you supplied, so the two argument
form `REL::ID{ OG, NG }` silently reuses the NG id for AE.

That is usually right, because NG and AE share the same Address Library id roughly 90% of the time,
which is exactly what makes the other 10% dangerous. When they diverge the two argument form does not
fail; it resolves an unrelated function on AE and patches it. The DXGI renderer init function above
is a real example in this repository: NG `2277018` against AE `4492363`.

So do not assume the ids are the same, and do not assume they differ. Verify AE independently by
round tripping the id against AE's Address Library database before you ship. Be especially careful
porting an id from another mod, since mods that ship a single NG/AE DLL frequently copy one id
across without checking.

If a runtime has no equivalent site, gate the code rather than inventing an id. `RELEX::IsRuntimeOG()`,
`IsRuntimeNG()` and `IsRuntimeAE()` are declared in `Addictol/Include/Core/AdUtils.h`:

```cpp
if (RELEX::IsRuntimeOG())
{
	// OG only patch site
}
```

A module only meaningful on one runtime should say so from `DoQuery()`:

```cpp
bool ModuleToggleGrassCommand::DoQuery() const noexcept
{
	return RELEX::IsRuntimeAE();
}
```

## Patching safely

**Disable yourself, do not crash.** If a patch cannot apply safely, return `false` from `DoQuery()`
and log why with `Skip`. Never terminate the process or deliberately fault over a condition you
merely dislike. Termination is legitimate only when quitting is the feature, as in `SafeExit`, or
behind an explicit user choice in a message box.

Real reasons modules bow out, all from current code:

```cpp
// A standalone mod already fixes this
if (IsModDLLPresent("FollowerStrayBulletFix.dll"))
{
	Skip("..."sv);
	return false;
}

// Only conflicts on one runtime
if (RELEX::IsRuntimeOG() && IsModDLLPresent("Drop7FFFPatch.dll"))
	return false;
```

**Verify before you write.** `RELEX::Validate` compares the bytes at a resolved address against what
you expect, and `TryDetourJump` / `TryDetourCall` only hook if they match:

```cpp
if (RELEX::Validate(thumb.address(), { 0xFF, 0x15 }))
	// safe to patch
```

Only a minority of existing modules do this today. That is technical debt, not a precedent, so new
byte patches should verify. It costs a few bytes of code and converts "a future game update silently
corrupts memory" into "the module cleanly disables itself".

**Check for conflicts.** If a standalone mod already fixes the same bug, detect it with
`IsModDLLPresent` and stand down rather than double patching. Probe for several filenames
when a mod ships under more than one name.

**Respect Wine and Proton.** Use `Addictol::UserUseWine()` to gate threading and performance paths
that misbehave off Windows. Degrade the feature rather than disabling the whole module.

**Do not fight other modules.** Several modules touch the same subsystems, such as the D3D device and
swapchain or the loading screen. The rule is disjoint hook points: pick a site nobody else owns, or
probe the current state before patching. `ModuleLoadScreen` is the reference, checking that High FPS
Physics Fix is loaded and then reading that mod's already applied byte patch before deciding what to
do.

**Know your thread.** Most engine work belongs on the main thread. If you touch state from a render,
Papyrus or worker thread, say so in a comment and protect it: `std::atomic` for simple flags and
counters, a lock for containers. Byte writes should go through the `RELEX` helpers, which handle
`VirtualProtect` and instruction cache flushing.

## Hooking techniques

Roughly in order of how often they appear:

**Direct byte patching** with `REL::WriteSafe` and friends at an address resolved through `REL::ID`.
The default for small surgical changes such as flipping an instruction or NOPing a branch. Comment
the original disassembly next to the bytes; it is the only thing that makes such a patch reviewable.

**Function detours** through the `RELEX` wrappers over Nukem Detours in
`Addictol/Include/Core/AdUtils.h`: `DetourJump`, `DetourCall`, `DetourVTable`, `DetourIAT`,
`DetourIATDelayed`, `DetourClassVTable`, plus the validating `TryDetourJump` and `TryDetourCall`.
Prefer these over hand rolled hooks.

**IAT and COM vtable detours** for anything Direct3D or DXGI, for example
`RELEX::DetourIAT("d3d11.dll", "D3D11CreateDeviceAndSwapChain", ...)`. These are runtime agnostic,
since COM vtable slots are stable across OG, NG and AE, so you need no Address Library id at all and
the whole NG/AE divergence risk disappears. Always prefer this to byte patching the renderer.

**Xbyak code caves** when you need real replacement logic rather than a patched constant. Build a
`Xbyak::CodeGenerator` and branch into it with `RELEX::XbyakJump` / `XbyakCall`, or write the five
byte `E9` relative jump yourself:

```cpp
const auto rel = static_cast<std::int32_t>(dst - (src + 5));
const auto* const r = reinterpret_cast<const uint8_t*>(&rel);
RELEX::WriteSafe(src, { 0xE9, r[0], r[1], r[2], r[3], 0x90 });
```

## Code style

There is no `.clang-format`, so match the file you are editing.

Tabs for indentation, Allman braces, no enforced line length.

Fixed-width integer types are unqualified: `uint8_t`, `int32_t`, `size_t`, not `std::uint8_t`. Real
library facilities keep the namespace: `std::span`, `std::array`, `std::initializer_list`,
`std::atomic`.

Machine-code literals — engine signatures, opcode patches, expected pre-patch bytes — are
`std::initializer_list<uint8_t>`, never `std::array<uint8_t, N>`. A hand-counted `N` that is too
small is silently zero-filled, producing a signature that reads correctly and validates wrong, so
the length must always be deduced from the bytes themselves:

```cpp
inline constexpr std::initializer_list<uint8_t> MODE_LOAD{ 0x41, 0x8B, 0x45, 0x00 };

if (!RELEX::Validate(target, { 0x48, 0x83, 0xEC, 0x28, 0xC6, 0x44, 0x24, 0x38, 0x00 }))
	return;
```

`std::array` remains correct for fixed-capacity storage that owns its bytes, such as saved original
instructions or an API output buffer.

Files are prefixed `Ad`, and modules are `AdModule<Name>.h` / `.cpp` declaring `class Module<Name>`
in `namespace Addictol`. Vendored upstream headers retain their upstream names. Functions and
methods are PascalCase, and hook functions are conventionally
`HK<OriginalName>`. Parameters take an `a_` prefix, except in a hook or thunk mirroring an external
signature. File scope and static variables take `s_` and class members `m_`; TOML options use the
type prefix instead. Private module helpers go in a nested `namespace <camelCaseModuleName>Detail`.

Built as C++ latest. Use `[[nodiscard]]`, `noexcept` and `override` on the module API, and
`[[maybe_unused]]` on unused `a_msg` and `a_vm` parameters. Errors are reported by returning `bool`
and logging; the codebase does not use exceptions.

String literals carry the `sv` suffix, including logging format strings. That comes from
`using namespace std::literals;` in `AdUtils.h`, not from the PCH, so declare it yourself if you do
not include that header.

Headers use `#pragma once`. Includes use angle brackets even for first party headers, and a module
`.cpp` includes its own header first. Do not include the PCH.

Logging is `REX::INFO`, `REX::WARN` and `REX::ERROR` with `{}` placeholders:

```cpp
REX::WARN("Module \"{}\": failed verification, the game version may not be supported"sv, mod->GetName());
```

## Mutation controls

Run `python Tests/run_mutations.py` from the repository root to prove the named negative controls in
`Tests/mutations.json`. The runner requires a green baseline, rebuilds and runs `vmm-tests` for each
mutation, matches the exact named failure and message, restores the target byte-for-byte, then
requires a green restored suite. It is a deliberate local check, not a CI gate, because every entry
requires a rebuild.

To add an entry, provide its unique exact `find` string, `replace` string, production `target`, and
the exact `check` and `message` printed by the expected `[FAIL]` line. Run the complete manifest
before submitting; a missing or repeated find string is a stale-manifest failure, never a skip.

## Submitting a pull request

Target `master`. CI builds your branch and attaches an artifact you can download and test. A green
check means it compiles and nothing more, so the burden of proof is on you.

Before opening it, confirm the bug really happens without your patch and stops with it, test the
module both on and off, and read `Addictol.log` (in `Documents\My Games\Fallout4\F4SE\`) to check
your module appears and that nothing else started warning.

In the description, say what engine bug this fixes and how you know that is the cause, which
runtimes you actually ran, where any new Address Library ids came from and how you verified AE, and
anything that could interact with the patch.

Reviewers look for, roughly in order: whether the ids are correct on all three runtimes, whether the
module fails closed, whether it can fight another module or mod, whether the TOML key is wired
through all seven places, and only then style.
