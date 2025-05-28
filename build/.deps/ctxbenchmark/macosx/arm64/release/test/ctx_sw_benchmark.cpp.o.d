{
    files = {
        "test/ctx_sw_benchmark.cpp"
    },
    depfiles_format = "gcc",
    values = {
        "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang",
        {
            "-Qunused-arguments",
            "-target",
            "arm64-apple-macos15.4",
            "-isysroot",
            "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX15.4.sdk",
            "-fvisibility=hidden",
            "-fvisibility-inlines-hidden",
            "-O3",
            "-std=c++20",
            "-I.",
            "-Ibuild/.gens/ctxbenchmark/macosx/arm64/release/platform/windows/idl",
            "-D_XOPEN_SOURCE",
            "-Wno-deprecated-declarations",
            "-DNDEBUG"
        }
    },
    depfiles = "build/.objs/ctxbenchmark/macosx/arm64/release/test/ctx_sw_benchmark.cpp.o:   test/ctx_sw_benchmark.cpp test/../include/libco-lite.h\
"
}