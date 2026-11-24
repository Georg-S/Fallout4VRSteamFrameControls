cmake_minimum_required(VERSION 3.20)
project(SteamFrameControls VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(SteamFrameControls SHARED
    src/main.cpp
    src/exports.def
)

target_compile_definitions(SteamFrameControls PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    # Static CRT (/MT) so the DLL has zero external VCRUNTIME dependencies
    set_property(TARGET SteamFrameControls PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(SteamFrameControls PRIVATE /W4 /EHsc /O2)
endif()

set_target_properties(SteamFrameControls PROPERTIES
    OUTPUT_NAME "SteamFrameControls"
    PREFIX ""
    SUFFIX ".dll"
)
