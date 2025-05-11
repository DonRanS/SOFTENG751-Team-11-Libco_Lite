{
    files = {
        "src/libco-lite.cpp"
    },
    values = {
        "/usr/bin/gcc",
        {
            "-m64",
            "-fvisibility=hidden",
            "-fvisibility-inlines-hidden",
            "-O3",
            "-std=c++2a",
            "-I.",
            "-Ibuild/.gens/minimumTest/linux/x86_64/release/platform/windows/idl",
            "-DNDEBUG"
        }
    },
    depfiles = "libco-lite.o: src/libco-lite.cpp src/../include/libco-lite.h\
",
    depfiles_format = "gcc"
}