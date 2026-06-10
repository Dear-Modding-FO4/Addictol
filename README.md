# Addictol

An F4SE plugin that combines engine fixes, crash fixes, and performance patches for Fallout 4. Supports OG, NG, and AE runtimes.

Consolidates patches from Buffout 4, X-Cell, Mentats, Escape Freeze Fix, Maka MaxPapyrusOps, Interior NavCut Fix, and Faster Workshop into a single plugin.

## Features

- **Memory Manager** - Replaces the game's allocator with Voltek for better performance
- **Faster Workshop** - O(1) keyword lookups instead of scanning all constructible objects
- **LibDeflate** - Faster BA2 decompression via libdeflate
- **Facegen** - Validates NPC face textures before using preprocessed data
- **Input Switch** - Proper keyboard/gamepad device switching
- **Scaleform Allocator** - Replaces Scaleform's memory mapper with configurable page/heap sizes
- **Archive Limits** - Increases max BA2 archives the game can load
- **Profiler** - Profiler for definitions performance your collection mods
- **Papyrus GC Bug** - Fixes a critical bug in garbage collection that causes premature loop termination
- ~60 additional crash fixes and stability patches

Each module can be individually toggled via `Addictol.toml`.

## Building

Requires Visual Studio 2022 Build Tools (or VS 2022).

```powershell
git clone --recurse-submodules https://github.com/Dear-Modding-FO4/Addictol.git
MSBuild VC/Addictol.sln -p:Configuration=Release -p:Platform=x64
```

Output: `.Build/F4SE/Plugins/Addictol.dll`
