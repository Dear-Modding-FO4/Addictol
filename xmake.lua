-- include subprojects
includes("Depends/commonlibf4")

-- set project constants
local plugin_name = "Addictol"
local xmake_library_dir = ".Lib/xmake"
local xmake_compile_pdb = path.join(os.projectdir(), ".LinkConf", "xmake", "Addictol", "Addictol.pdb")
local xmake_ltcg_output = path.join(os.projectdir(), ".LinkConf", "xmake", "Addictol", "Addictol.iobj")
local xmake_plugin_pdb = path.join(os.projectdir(), ".Build", "F4SE", "Plugins", "Addictol.pdb")
local dependency_interface = {
    inherit = false
}

-- set project settings
set_project(plugin_name)
set_license("GPL-3.0")
set_allowedplats("windows")
set_allowedarchs("x64")
set_allowedmodes("release")
set_defaultmode("release")
set_arch("x64")
set_languages("c++23")
set_toolchains("msvc")
set_warnings("all")
set_runtimes("MT")

-- keep generated output out of the repo root, alongside the other intermediates
set_config("builddir", ".LinkConf/xmake")

-- set policies
set_policy("build.fence", true)

-- add options
option("msvc_package_toolchain", function()
    set_showmenu(false)

    on_check(function(option)
        os.setenv("CC", "cl")
        os.setenv("CXX", "cl")
        option:enable(true)
    end)
end)

set_config("commonlib_ini", true)
set_config("commonlib_toml", true)
set_config("commonlib_xbyak", true)

-- add requires
add_requires("libdeflate v1.25", { configs = { shared = false } })
add_requires("unordered_dense v4.8.1")

-- define targets
target("spdlog-vendored", function()
    set_kind("static")
    set_basename("spdlog")
    set_arch("x64")
    set_languages("c++latest")
    set_optimize("fastest")
    set_runtimes("MT")
    set_symbols("debug")
    set_exceptions("cxx")
    set_targetdir(xmake_library_dir)
    set_objectdir(".LinkConf/xmake/spdlog")
    set_dependir(".LinkConf/xmake/spdlog/deps")

    -- add source files
    add_files(
        "Depends/spdlog/src/async.cpp",
        "Depends/spdlog/src/bundled_fmtlib_format.cpp",
        "Depends/spdlog/src/cfg.cpp",
        "Depends/spdlog/src/color_sinks.cpp",
        "Depends/spdlog/src/file_sinks.cpp",
        "Depends/spdlog/src/spdlog.cpp",
        "Depends/spdlog/src/stdout_sinks.cpp"
    )

    -- add include directories
    add_includedirs("Depends/spdlog/include", { public = true })

    -- add defines
    add_defines(
        "SPDLOG_COMPILED_LIB",
        "SPDLOG_USE_STD_FORMAT",
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "WIN32",
        "NDEBUG",
        "_LIB",
        "FMT_UNICODE=0",
        "_MBCS"
    )

    -- add compiler flags
    add_cxxflags(
        "/Oi",
        "/Ot",
        "/GS",
        "/Gy",
        "/fp:precise",
        "/Zc:wchar_t",
        "/Zc:forScope",
        "/Zc:inline",
        "/Gd",
        "/MP",
        { force = true }
    )
end)

target("commonlib-shared", function()
    set_languages("c++latest")
    set_optimize("fastest")
    set_runtimes("MT")
    set_symbols("debug")
    set_targetdir(xmake_library_dir)
    set_objectdir(".LinkConf/xmake/commonlib-shared")
    set_dependir(".LinkConf/xmake/commonlib-shared/deps")

    -- preserve the MSBuild spdlog build
    add_deps("spdlog-vendored", dependency_interface)
    add_includedirs("Depends/spdlog/include", { public = true })

    -- preserve the MSBuild header ABI and Xbyak codegen
    add_includedirs("Depends/toml11/single_include", "Depends/INI", "Depends")
    add_defines(
        "NDEBUG",
        "_LIB",
        "FMT_UNICODE=0",
        "NORASTEROPS",
        "NOGDI",
        "NOMINMAX",
        "XBYAK_NO_OP_NAMES",
        "COMMONLIB_RUNTIMECOUNT=3"
    )
end)

target("commonlibf4", function()
    set_languages("c++latest")
    set_optimize("fastest")
    set_runtimes("MT")
    set_symbols("debug")
    set_targetdir(xmake_library_dir)
    set_objectdir(".LinkConf/xmake/commonlibf4")
    set_dependir(".LinkConf/xmake/commonlibf4/deps")
    add_defines("NDEBUG")
end)

target("imgui", function()
    set_languages("c++latest")
    set_optimize("fastest")
    set_runtimes("MT")
    set_symbols("debug")
    set_targetdir(xmake_library_dir)
    set_objectdir(".LinkConf/xmake/imgui")
    set_dependir(".LinkConf/xmake/imgui/deps")
    add_defines("NDEBUG")
end)

target("detours", function()
    set_kind("static")
    set_arch("x64")
    set_languages("c++latest")
    set_optimize("fastest")
    set_runtimes("MT")
    set_symbols("debug")
    set_exceptions("cxx")
    set_targetdir(xmake_library_dir)
    set_objectdir(".LinkConf/xmake/detours")
    set_dependir(".LinkConf/xmake/detours/deps")

    -- add source files
    add_files(
        "Depends/detours/detours/Detours.cpp",
        "Depends/detours/detours/Detours32.cpp",
        "Depends/detours/detours/Detours64.cpp",
        "Depends/detours/detours/stdafx.cpp",
        "Depends/detours/detours/HideStaticLibSymbols.c"
    )

    -- add include directories
    add_includedirs("Depends/detours", { public = true })
    add_includedirs(
        "Depends/detours/detours",
        "Depends/detours/detours/zydis/msvc",
        "Depends/detours/detours/zydis/src",
        "Depends/detours/detours/zydis/dependencies/zycore/include",
        "Depends/detours/detours/zydis/include"
    )

    -- add defines
    add_defines("ZYDIS_STATIC_DEFINE", "WIN32", "NDEBUG", "_LIB")

    -- set precompiled header
    set_pcxxheader("Depends/detours/detours/stdafx.h")
end)

target("vmm", function()
    set_kind("static")
    set_basename("VoltekLib.MemoryManager")
    set_arch("x64")
    set_languages("c++latest")
    set_optimize("fastest")
    set_runtimes("MT")
    set_symbols("debug")
    set_exceptions("cxx")
    set_targetdir(xmake_library_dir)
    set_objectdir(".LinkConf/xmake/vmm")
    set_dependir(".LinkConf/xmake/vmm/deps")

    -- add source files
    add_files(
        "Depends/vmm/dllmain.cpp",
        "Depends/vmm/iw/iw.cpp",
        "Depends/vmm/source/valloc.cpp",
        "Depends/vmm/source/vassert.cpp",
        "Depends/vmm/source/vbase.cpp",
        "Depends/vmm/source/vbits.cpp",
        "Depends/vmm/source/vio.cpp",
        "Depends/vmm/source/vmm.cpp",
        "Depends/vmm/source/vmmmain.cpp",
        "Depends/vmm/source/vmapper.cpp",
        "Depends/vmm/source/vsimplelock.cpp"
    )

    -- add include directories
    add_includedirs("Depends/vmm/include", { public = true })
    add_includedirs("Depends/vmm/iw", "Depends/vmm/source")

    -- add defines
    add_defines("VOLTEK_LIB_BUILD", { public = true })
    add_defines("NDEBUG", "NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS", "_WINDOWS")

    -- add compiler flags
    add_cxxflags(
        "/Ob2",
        "/Oi",
        "/Ot",
        "/GL",
        "/Gy",
        "/fp:fast",
        "/permissive-",
        "/sdl",
        { force = true }
    )
end)

target("vmm-tests", function()
    set_kind("binary")
    set_arch("x64")
    set_languages("c++23")
    set_optimize("fastest")
    set_runtimes("MT")
    set_targetdir(".Build/Tests")
    set_objectdir(".LinkConf/xmake/vmm-tests")
    set_dependir(".LinkConf/xmake/vmm-tests/deps")

    -- add dependencies
    add_deps("vmm")

    -- add packages
    add_packages("libdeflate", { links = {}, sysincludedirs = {}, defines = {} })

    -- add source files
    add_files("Tests/**.cpp")

    -- add include directories
    add_includedirs("Tests", "Depends", "Depends/vmm/source")

    -- add defines
    add_defines("NDEBUG", "NOMINMAX", "WIN32_LEAN_AND_MEAN")

    -- add libraries
    add_linkdirs(xmake_library_dir)
    add_links("deflatestatic", "Psapi")
end)

target(plugin_name, function()
    set_kind("shared")
    set_arch("x64")
    set_languages("c++latest")
    set_runtimes("MT")
    set_warnings("all")
    set_targetdir(".Build/F4SE/Plugins")
    set_objectdir(".LinkConf/xmake/Addictol")
    set_dependir(".LinkConf/xmake/Addictol/deps")

    -- add dependencies
    add_deps("detours", dependency_interface)
    add_deps("commonlib-shared", dependency_interface)
    add_deps("commonlibf4", dependency_interface)
    add_deps("vmm", dependency_interface)

    -- add packages
    add_packages("libdeflate", { links = {}, sysincludedirs = {}, defines = {} })
    add_packages("unordered_dense", { sysincludedirs = {}, defines = {} })

    -- add libraries
    add_linkdirs(xmake_library_dir)
    add_links(
        "detours",
        "deflatestatic",
        "commonlibf4",
        "commonlib-shared",
        "spdlog",
        "Advapi32",
        "Bcrypt",
        "dxgi",
        "d3d11",
        "d3dcompiler",
        "ws2_32",
        "version",
        "Dbghelp",
        "shlwapi",
        "winhttp",
        "Shell32",
        "VoltekLib.MemoryManager",
        "imgui"
    )

    -- add source files
    add_rules("win.sdk.resource")
    add_files("Addictol/Source/**.cpp")
    add_files("Version/resource_version.rc")
    add_headerfiles("Addictol/Include/**.h")

    -- add include directories
    add_includedirs(
        "Depends/commonlibf4/include",
        "Depends",
        "Depends/detours",
        "Depends/commonlibf4/lib/commonlib-shared/include",
        "Version",
        "Addictol/Include",
        "Depends/vmm/include",
        "Depends/vmm/source",
        "Depends/imgui"
    )

    -- add defines
    add_defines(
        "NDEBUG",
        "ADDICTOL_EXPORTS",
        "_WINDOWS",
        "_USRDLL",
        "XBYAK_NO_OP_NAMES",
        "COMMONLIB_OPTION_XBYAK",
        "COMMONLIB_OPTION_INI",
        "COMMONLIB_OPTION_TOML",
        "SPDLOG_USE_STD_FORMAT",
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "FMT_UNICODE=0",
        "WIN32_LEAN_AND_MEAN",
        "_CRT_SECURE_NO_WARNINGS",
        "NOMINMAX",
        "SUPPORT_NG",
        "SUPPORT_OG",
        "COMMONLIB_RUNTIMECOUNT=3",
        "VOLTEK_LIB_BUILD",
        "AD_TRACER=0",
        "AD_TRAMPOLINE_SIZE=4096"
    )

    -- add compiler flags
    add_cxxflags(
        "/O2",
        "/Ob2",
        "/Oi",
        "/Ot",
        "/GL",
        "/Gy",
        "/Gr",
        "/GS",
        "/arch:AVX",
        "/fp:fast",
        "/EHsc",
        "/permissive-",
        "/sdl",
        "/Zc:forScope",
        "/Zc:inline",
        "/Zc:wchar_t",
        "/MP",
        "/Zi",
        "/FS",
        "/wd4200",
        "/wd4996",
        "/wd26444",
        "/wd26495",
        "/wd4806",
        "/Fd" .. xmake_compile_pdb,
        { force = true }
    )

    -- set precompiled header
    set_pcxxheader("Addictol/Include/AdPCH.h")

    -- add linker flags
    add_shflags(
        "/SUBSYSTEM:WINDOWS",
        "/DEBUG",
        "/DYNAMICBASE",
        "/NXCOMPAT",
        "/OPT:REF",
        "/OPT:ICF",
        "/LTCG:incremental",
        "/MANIFEST",
        "/manifest:embed",
        "/MANIFESTUAC:NO",
        "/LTCGOUT:" .. xmake_ltcg_output,
        "/PDB:" .. xmake_plugin_pdb,
        { force = true }
    )

    -- update version resources
    before_build(function()
        os.vrunv(
            "powershell",
            {
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                path.join(os.projectdir(), "Version", "scripts.ps1")
            },
            { curdir = path.join(os.projectdir(), "VC") }
        )
    end)
end)
