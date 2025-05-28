{
    files = {
        "build/.objs/minimumTest/macosx/arm64/release/test/minimumTest.cpp.o",
        "build/.objs/minimumTest/macosx/arm64/release/src/libco-lite.cpp.o"
    },
    values = {
        "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++",
        {
            "-target",
            "arm64-apple-macos15.4",
            "-isysroot",
            "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX15.4.sdk",
            "-lz",
            "-Wl,-x",
            "-Wl,-dead_strip"
        }
    }
}