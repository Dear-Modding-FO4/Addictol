#include <AdUtils.h>
#include <AdPlugin.h>
#include <Windows.h>

#define MAKE_EXE_VERSION_EX(major, minor, build, sub)	((((major) & 0xFF) << 24) | (((minor) & 0xFF) << 16) | (((build) & 0xFFF) << 4) | ((sub) & 0xF))
#define MAKE_EXE_VERSION(major, minor, build)			MAKE_EXE_VERSION_EX(major, minor, build, 0)

enum class F4SEAddressIndependence : std::uint32_t
{
    // set this if you exclusively use signature matching to find your addresses and have NO HARDCODED ADDRESSES
    // the F4SE code does not use signature matching, so calling functions in the F4SE headers is not safe with this flag
    kSignatures = 1 << 0,

    // set this if you are using a 1.10.980+ version of the Address Library
    kAddressLibrary_1_10_980 = 1 << 1,

    // set this if you are using a 1.11.137+ version of the Address Library
    kAddressLibrary_1_11_137 = 1 << 2,
};

enum class F4SEStructureIndependence : std::uint32_t
{
    // set this if your plugin doesn't use any game structures
    kNoStructs = 1 << 0,

    // works with the structure layout in 1.10.980+
    k1_10_980Layout = 1 << 1,

    // works with the structure layout in 1.11.137+
    k1_11_137Layout = 1 << 2,
};

// F4SE NP requirement
F4SE_EXPORT auto F4SEPlugin_Version = []() noexcept
{
    F4SE::PluginVersionData data
    {
        F4SE::PluginVersionData::kVersion,
        MAKE_EXE_VERSION(VERSION_MAJOR, VERSION_MINOR, VERSION_BUILD),
        _PluginName,
        _PluginAuthor,
#if SUPPORT_NG
    static_cast<std::uint32_t>(F4SEAddressIndependence::kAddressLibrary_1_10_980) |
#endif // SUPPORT_NG
    static_cast<std::uint32_t>(F4SEAddressIndependence::kAddressLibrary_1_11_137),
#if SUPPORT_NG
    static_cast<std::uint32_t>(F4SEStructureIndependence::k1_10_980Layout) |
#endif // SUPPORT_NG
    static_cast<std::uint32_t>(F4SEStructureIndependence::k1_11_137Layout),
        // compatibleVersions - no need
        {},
        // xseMinimum - no need
        0,
        // reserved
        0,
        0,
        {}
    };

    return data;
}();

#if SUPPORT_OG
// For F4SE OG
F4SE_EXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
    if (!a_f4se)
        return false;
    if (!a_info)
        return false;

    if (a_f4se->RuntimeVersion() != REL::Version{ 1, 10, 163, 0 } /*RUNTIME_VERSION_1_10_163*/)
        return false;

    a_info->infoVersion = F4SE::PluginInfo::kVersion;
    a_info->version = MAKE_EXE_VERSION(VERSION_MAJOR, VERSION_MINOR, VERSION_BUILD);
    a_info->name = _PluginName;

    if (!std::filesystem::exists(AdGetRuntimeDirectory() + "Data\\F4SE\\Plugins\\version-1-10-163-0.bin"))
    {
        MessageBoxA(0, "" _PluginName ": disabled, address library needs to be updated", "Warnings", MB_OK | MB_ICONWARNING);

        return false;
    }
    
    return true;
}
#endif // SUPPORT_OG

static bool AdInitUnsafe(const F4SE::LoadInterface* a_f4se)
{
    return Addictol::Plugin::GetSingleton()->Init(a_f4se);
}

static bool AdInitSafe(const F4SE::LoadInterface* a_f4se)
{
    __try
    {
        return AdInitUnsafe(a_f4se);
    }
    __except (1)
    {
        return false;
    }
}

#define AdInit AdInitSafe

#if SUPPORT_PRELOAD
// Hint: Preload no support OG
F4SE_PLUGIN_PRELOAD(const F4SE::LoadInterface* a_f4se)
{
    return AdInit(a_f4se);
}
#else
F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
    return AdInit(a_f4se);
}
#endif