local function static_library(name)
    set_kind("static")
    set_languages("c11")
    set_optimize("fastest")
    set_runtimes("MT")
    set_symbols("debug")
    set_targetdir(path.join(os.projectdir(), ".Lib/xmake"))
    set_objectdir(".LinkConf/xmake/" .. name)
    set_dependir(".LinkConf/xmake/" .. name .. "/deps")
    add_defines("NDEBUG")
end

target("mimalloc", function()
    static_library("mimalloc")
    add_includedirs("mimalloc/include", {public = true})
    -- foreign pointers passed to mi_free are ignored instead of dereferenced
    add_defines("MI_BUILD_RELEASE", "MI_FREE_IS_CHECKED=1")
    add_files("mimalloc/src/static.c")
end)

target("rpmalloc", function()
    static_library("rpmalloc")
    add_includedirs("rpmalloc/rpmalloc", {public = true})
    -- overflowing size + alignment requests fail instead of wrapping
    add_defines("ENABLE_OVERRIDE=0", "ENABLE_VALIDATE_ARGS=1", "_CRT_SECURE_NO_WARNINGS")
    -- argument validation uses SizeTMult without including its header
    add_forceincludes("intsafe.h")
    add_cflags("/experimental:c11atomics", {force = true})
    add_files("rpmalloc/rpmalloc/rpmalloc.c")
end)
