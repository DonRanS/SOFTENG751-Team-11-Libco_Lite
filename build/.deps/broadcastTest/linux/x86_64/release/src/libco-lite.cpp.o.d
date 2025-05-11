{
    depfiles_format = "gcc",
    files = {
        "src/libco-lite.cpp"
    },
    depfiles = "libco-lite.o: src/libco-lite.cpp src/../include/libco-lite.h\
",
    values = {
        "/usr/bin/gcc",
        {
            "-m64",
            "-fvisibility=hidden",
            "-fvisibility-inlines-hidden",
            "-O3",
            "-std=c++2a",
            "-Iinclude",
            "-Ibuild/.gens/broadcastTest/linux/x86_64/release/platform/windows/idl",
            "-DNDEBUG"
        }
    }
}