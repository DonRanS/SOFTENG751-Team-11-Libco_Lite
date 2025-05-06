-- Project name and version
set_project("libco-experimental-socket")
set_version("1.0.0")


-- Add platform-specific flags
-- force to use depreceated declarations on macOS
if is_plat("macosx") then
    add_defines("_XOPEN_SOURCE")
    add_cxflags("-Wno-deprecated-declarations")
end


-- Set language standards
set_languages("c++11")

-- Set build modes
add_rules("mode.debug", "mode.release")

-- Define platform-specific configurations
if is_plat("linux") then
    add_defines("__linux__")
elseif is_plat("macosx") then
    add_defines("_XOPEN_SOURCE")
    add_cxflags("-Wno-deprecated-declarations")
end

-- Main library target (coroutine implementation)
target("co-lite")
    set_kind("static")
    add_files("src/libco-lite.cpp")
    add_includedirs("include")
    
    if is_mode("release") then
        set_optimize("fastest")
    end
    
    if is_mode("debug") then
        set_symbols("debug")
        add_defines("DEBUG")
    end

-- Socket library target (socket implementation)
target("co-socket")
    set_kind("static")
    add_files("src/co_socket.cpp")
    add_includedirs("include")
    add_deps("co-lite")

-- Epoll scheduler library target
target("co-epoll")
    set_kind("static")
    add_files("src/scheduler_epoll.cpp")
    add_includedirs("include")
    add_deps("co-lite", "co-socket")
    
    -- Only build on Linux as epoll is Linux-specific
    if is_plat("linux") then
        add_defines("USE_EPOLL")
    else
        on_load(function (target)
            target:set("enabled", false)
        end)
    end

-- Echo server application (original version)
target("echo-server")
    set_kind("binary")
    add_files("test/libco-socketTest.cpp")
    add_includedirs("include")
    
    -- Link with both libraries
    add_deps("co-lite", "co-socket")
    
    -- Explicitly link system dependencies
    if is_plat("linux") then
        add_syslinks("pthread")
    end

-- Echo server with epoll (new test application)
target("echo-server-epoll")
    set_kind("binary")
    add_files("test/libco-epollTest.cpp")
    add_includedirs("include")
    
    -- Link with all libraries
    add_deps("co-lite", "co-socket", "co-epoll")
    
    -- Only build on Linux
    if is_plat("linux") then
        add_syslinks("pthread")
    else
        on_load(function (target)
            target:set("enabled", false)
        end)
    end