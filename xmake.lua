-- xmake.lua

set_project("libco-lite-libdll")
set_version("1.0.0")

-- global compiler settings
--set_toolset("clang", "gcc")                 -- compiler toolset
--set_arch("x86_64")                          -- architecture
--set_mode("release")                        -- build mode
--set_rpath("$(projectdir)/lib")              -- include path
--set_targetdir("$(projectdir)/bin")          -- target directory
set_languages("c++20")

-- options
add_rules("mode.debug", "mode.release")

if is_plat("linux") then
    add_defines("__linux__")
elseif is_plat("macosx") then
    add_defines("_XOPEN_SOURCE")
    add_cxflags("-Wno-deprecated-declarations")
end


-- target: libco-lite
target("minimumTest")
    set_kind("binary")                        
    add_includedirs("include")                       
    add_files("./test/minimumTest.cpp")             
    add_files("./src/libco-lite.cpp")    

   
    if is_mode("debug") then
        add_defines("DEBUG")
        set_symbols("debug")
    else
        set_optimize("fastest")
        set_strip("all")
    end

-- target("broadcastTest")
--     set_kind("binary")
--     add_includedirs("include")
--     add_files("src/libco-lite.cpp", "test/broadcastTest.cpp")

-- target("bufferedChannelTest")
--     set_kind("binary")
--     add_files("libco-lite.cpp", "bufferedChannelTest.cpp")
--     add_includedirs("include")
--     add_links("pthread")