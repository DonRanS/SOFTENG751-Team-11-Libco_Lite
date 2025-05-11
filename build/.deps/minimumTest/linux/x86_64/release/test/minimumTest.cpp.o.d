{
    depfiles_format = "gcc",
    depfiles = "minimumTest.o: test/minimumTest.cpp test/../include/libco-lite.h\
",
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
    files = {
        "test/minimumTest.cpp"
    }
}