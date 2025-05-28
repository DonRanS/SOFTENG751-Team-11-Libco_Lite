{
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
            "-Iinclude",
            "-Ibuild/.gens/minimumTest/macosx/arm64/release/platform/windows/idl",
            "-D_XOPEN_SOURCE",
            "-Wno-deprecated-declarations",
            "-DNDEBUG"
        }
    },
    files = {
        "src/libco-lite.cpp"
    },
    depfiles_format = "gcc",
    depfiles = "build/.objs/minimumTest/macosx/arm64/release/src/__cpp_libco-lite.cpp.cpp:   src/libco-lite.cpp src/../include/libco-lite.h\
"
}