-- Project name and version
set_project("libco-lite")
set_version("1.0.0")

-- Set language standards
set_languages("c++11")

-- Set build modes
add_rules("mode.debug", "mode.release")

-- Define platform-specific configurations
if is_plat("linux") then
    add_defines("__linux__")
elseif is_plat("macosx") then
    add_defines("_XOPEN_SOURCE")
end

-- Main library target
target("libco-lite")
    set_kind("static")
    
    -- Source files - use explicit file names
    add_files("./src/libco-lite.cpp")
    
    -- Header files
    add_headerfiles("./include/libco-lite.h")
    
    -- Include directories
    add_includedirs(".")
    
    -- Set optimization level for release mode
    if is_mode("release") then
        set_optimize("fastest")
    end
    
    -- Set debugging symbols for debug mode
    if is_mode("debug") then
        set_symbols("debug")
        add_defines("DEBUG")
    end

-- Example application target
-- adjust test targets accordingly
target("minimumTest")
    set_kind("binary")
    add_deps("libco-lite")
    add_files("./test/libco-minimumTest.cpp")
    add_includedirs(".")
    
    -- Link against the coroutine library
    add_links("libco-lite")
    
    -- Link against system libraries
    if is_plat("linux") then
        add_syslinks("pthread")
    end

-- Installation configuration
target("install")
    set_kind("phony")
    on_install(function (target)
        os.cp("*.h", "$(installdir)/include")
        os.cp("$(buildir)/libco-lite.a", "$(installdir)/lib")
    end)