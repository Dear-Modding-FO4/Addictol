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

target("zlib-prefixed", function()
    static_library("zlib-prefixed")
    add_includedirs(".", {public = true})
    add_defines("Z_PREFIX", "_CRT_SECURE_NO_DEPRECATE", "_CRT_NONSTDC_NO_DEPRECATE")
    add_files(
        "zlib/adler32.c",
        "zlib/compress.c",
        "zlib/crc32.c",
        "zlib/deflate.c",
        "zlib/gzclose.c",
        "zlib/gzlib.c",
        "zlib/gzread.c",
        "zlib/gzwrite.c",
        "zlib/infback.c",
        "zlib/inffast.c",
        "zlib/inflate.c",
        "zlib/inftrees.c",
        "zlib/trees.c",
        "zlib/uncompr.c",
        "zlib/zutil.c"
    )
end)

rule("isal.nasm", function()
    set_extensions(".asm")

    on_load(function(target)
        import("lib.detect.find_tool")
        local nasm_path = os.getenv("NASM_PATH")
        local nasm = find_tool("nasm", {paths = nasm_path and {nasm_path} or nil})
        assert(nasm, "NASM is required for ISA-L. Add nasm.exe to PATH or set NASM_PATH to its directory.")
        target:data_set("isal.nasm", nasm.program)
    end)

    on_buildcmd_file(function(target, batchcmds, sourcefile, opt)
        local objectfile = target:objectfile(sourcefile)
        table.insert(target:objectfiles(), objectfile)
        batchcmds:show_progress(opt.progress, "${color.build.object}assembling %s", sourcefile)
        batchcmds:mkdir(path.directory(objectfile))
        batchcmds:vrunv(target:data("isal.nasm"), {
            "-f", "win64",
            "-I" .. path.join(os.projectdir(), "Depends/isa-l") .. "/",
            "-I" .. path.join(os.projectdir(), "Depends/isa-l/include") .. "/",
            "-I" .. path.directory(path.absolute(sourcefile)) .. "/",
            sourcefile, "-o", objectfile
        })
        batchcmds:add_depfiles(sourcefile)
        for _, dir in ipairs({"include", "igzip", "crc"}) do
            batchcmds:add_depfiles(table.unpack(os.files(path.join(os.projectdir(), "Depends/isa-l", dir, "*.asm"))))
        end
        batchcmds:add_depfiles(table.unpack(os.files(path.join(os.projectdir(), "Depends/isa-l/include/*.inc"))))
        batchcmds:set_depmtime(os.mtime(objectfile))
        batchcmds:set_depcache(target:dependfile(objectfile))
    end)
end)

target("isa-l", function()
    static_library("isal")
    set_basename("isal")
    add_rules("isal.nasm")
    add_includedirs("isa-l/include", {public = true})
    add_includedirs("isa-l")
    add_defines("x86_64", "ISAL_DEPRECATED_INTERNAL", "_CRT_SECURE_NO_WARNINGS")

    -- Keep upstream igzip/CRC dispatch groups intact; exclude unrelated ISA-L modules.
    add_files(
        "isa-l/igzip/igzip.c",
        "isa-l/igzip/hufftables_c.c",
        "isa-l/igzip/igzip_base.c",
        "isa-l/igzip/igzip_icf_base.c",
        "isa-l/igzip/adler32_base.c",
        "isa-l/igzip/flatten_ll.c",
        "isa-l/igzip/encode_df.c",
        "isa-l/igzip/igzip_icf_body.c",
        "isa-l/igzip/huff_codes.c",
        "isa-l/igzip/igzip_inflate.c",
        "isa-l/crc/crc_base.c",
        "isa-l/crc/crc64_base.c",
        "isa-l/igzip/igzip_body.asm",
        "isa-l/igzip/igzip_finish.asm",
        "isa-l/igzip/igzip_icf_body_h1_gr_bt.asm",
        "isa-l/igzip/igzip_icf_finish.asm",
        "isa-l/igzip/rfc1951_lookup.asm",
        "isa-l/igzip/adler32_sse.asm",
        "isa-l/igzip/adler32_avx2_4.asm",
        "isa-l/igzip/igzip_multibinary.asm",
        "isa-l/igzip/igzip_update_histogram_01.asm",
        "isa-l/igzip/igzip_update_histogram_04.asm",
        "isa-l/igzip/igzip_decode_block_stateless_01.asm",
        "isa-l/igzip/igzip_decode_block_stateless_04.asm",
        "isa-l/igzip/igzip_inflate_multibinary.asm",
        "isa-l/igzip/encode_df_04.asm",
        "isa-l/igzip/encode_df_06.asm",
        "isa-l/igzip/proc_heap.asm",
        "isa-l/igzip/igzip_deflate_hash.asm",
        "isa-l/igzip/igzip_gen_icf_map_lh1_06.asm",
        "isa-l/igzip/igzip_gen_icf_map_lh1_04.asm",
        "isa-l/igzip/igzip_set_long_icf_fg_04.asm",
        "isa-l/igzip/igzip_set_long_icf_fg_06.asm",
        "isa-l/crc/crc_const.asm",
        "isa-l/crc/crc16_t10dif_01.asm",
        "isa-l/crc/crc16_t10dif_avx2.asm",
        "isa-l/crc/crc16_t10dif_by16_10.asm",
        "isa-l/crc/crc16_t10dif_copy_by4.asm",
        "isa-l/crc/crc16_t10dif_copy_by4_02.asm",
        "isa-l/crc/crc32_ieee_01.asm",
        "isa-l/crc/crc32_ieee_avx2.asm",
        "isa-l/crc/crc32_ieee_by16_10.asm",
        "isa-l/crc/crc32_iscsi_01.asm",
        "isa-l/crc/crc32_iscsi_by8_02.asm",
        "isa-l/crc/crc32_iscsi_by16_10.asm",
        "isa-l/crc/crc32_iscsi_avx2.asm",
        "isa-l/crc/crc_multibinary.asm",
        "isa-l/crc/crc64_ecma_refl_by8.asm",
        "isa-l/crc/crc64_ecma_refl_avx2.asm",
        "isa-l/crc/crc64_ecma_refl_by16_10.asm",
        "isa-l/crc/crc64_ecma_norm_by8.asm",
        "isa-l/crc/crc64_ecma_norm_avx2.asm",
        "isa-l/crc/crc64_ecma_norm_by16_10.asm",
        "isa-l/crc/crc64_iso_refl_by8.asm",
        "isa-l/crc/crc64_iso_refl_avx2.asm",
        "isa-l/crc/crc64_iso_refl_by16_10.asm",
        "isa-l/crc/crc64_iso_norm_by8.asm",
        "isa-l/crc/crc64_iso_norm_avx2.asm",
        "isa-l/crc/crc64_iso_norm_by16_10.asm",
        "isa-l/crc/crc64_jones_refl_by8.asm",
        "isa-l/crc/crc64_jones_refl_avx2.asm",
        "isa-l/crc/crc64_jones_refl_by16_10.asm",
        "isa-l/crc/crc64_jones_norm_by8.asm",
        "isa-l/crc/crc64_jones_norm_avx2.asm",
        "isa-l/crc/crc64_jones_norm_by16_10.asm",
        "isa-l/crc/crc64_rocksoft_refl_by8.asm",
        "isa-l/crc/crc64_rocksoft_refl_avx2.asm",
        "isa-l/crc/crc64_rocksoft_refl_by16_10.asm",
        "isa-l/crc/crc64_rocksoft_norm_by8.asm",
        "isa-l/crc/crc64_rocksoft_norm_avx2.asm",
        "isa-l/crc/crc64_rocksoft_norm_by16_10.asm",
        "isa-l/crc/crc32_gzip_refl_by8.asm",
        "isa-l/crc/crc32_gzip_refl_avx2.asm",
        "isa-l/crc/crc32_gzip_refl_by16_10.asm"
    )
end)
