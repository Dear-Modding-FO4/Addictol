#include <Core/AdUtils.h>
#include <Core/AdPlugin.h>
#include <Windows.h>
#include <string_view>

// F4SE NP requirement
F4SE_PLUGIN_VERSION = []() noexcept
{
    F4SE::PluginVersionData data{};
    data.PluginVersion({ VERSION_MAJOR, VERSION_MINOR, VERSION_BUILD, 0 });
    data.PluginName(_PluginName);
    data.AuthorName(_PluginAuthor);
    data.UsesAddressLibrary(true);
#if SUPPORT_NG
    data.UsesAddressLibraryNG(true);
#endif // SUPPORT_NG
    data.UsesSigScanning(false);
    data.IsLayoutDependent(true);
#if SUPPORT_NG
    data.IsLayoutDependentNG(true);
#endif // SUPPORT_NG
    data.HasNoStructUse(false);

    return data;
}();

#if SUPPORT_OG
// For F4SE OG
F4SE_PLUGIN_QUERY(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
    if (!a_f4se)
        return false;

    if (!a_info)
        return false;

    if (a_f4se->RuntimeVersion() < REL::Version(F4SE::RUNTIME_1_10_163))
        return false;

    if (const auto data = F4SE::PluginVersionData::GetSingleton())
    {
        a_info->infoVersion = F4SE::PluginInfo::kVersion;
        a_info->version = data->GetPluginVersion().pack();
        a_info->name = data->GetPluginName().data();
    }

    if (!std::filesystem::exists(std::format("{}Data\\F4SE\\Plugins\\version-1-10-163-0.bin", AdGetRuntimeDirectory())))
    {
        MessageBoxA(nullptr, "" _PluginName ": disabled, address library needs to be updated", "Warnings", 
            MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);

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

static bool AdPreloadInitUnsafe(const F4SE::PreLoadInterface* a_preloadf4se)
{
    // run patches after LoadLibrary
    return Addictol::Plugin::GetSingleton()->PreloadInit(a_preloadf4se);
}

static bool AdPreloadInitSafe(const F4SE::PreLoadInterface* a_preloadf4se)
{
    __try
    {
        return AdPreloadInitUnsafe(a_preloadf4se);
    }
    __except (1)
    {
        return false;
    }
}

#define AdPreloadInit AdPreloadInitSafe

// No supported OG
F4SE_PLUGIN_PRELOAD(const F4SE::PreLoadInterface* a_preloadf4se)
{
    return AdPreloadInit(a_preloadf4se);
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
    return AdInit(a_f4se);
}