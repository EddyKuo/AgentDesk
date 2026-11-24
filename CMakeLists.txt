# AgentDesk top-level build: libraries, tools, apps, example mods and tests. Use the presets in
# CMakePresets.json through scripts\build.cmd (Windows, sets up the VS 2022 x64 environment) or
# scripts/build.sh (macOS).
cmake_minimum_required(VERSION 3.25)

# Must be set before project() to take effect. Minimum macOS version (ADR-30).
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")

project(AgentDesk
    VERSION 0.2.0
    DESCRIPTION "Native workbench for AI coding agent CLIs"
    LANGUAGES C CXX)

# Windows 11 x64, and macOS 13+ on Apple Silicon (design doc ADR-30). The macOS port is built up
# in steps (docs/design/platform-abstraction.md 7); until it is complete only the parts already
# ported are built there.
if(NOT WIN32 AND NOT APPLE)
    message(FATAL_ERROR "AgentDesk targets Windows 11 x64 and macOS (Apple Silicon)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(AgentDeskDefaults)

option(AGENTDESK_BUILD_TESTS "Build AgentDesk tests" ON)
option(AGENTDESK_BUILD_APPS "Build AgentDesk executables" ON)

find_package(nlohmann_json CONFIG REQUIRED)

if(APPLE)
    enable_language(OBJCXX)
    add_subdirectory(third_party/libvterm)
    add_subdirectory(libs)
    add_subdirectory(tools)
    if(AGENTDESK_BUILD_APPS)
        add_subdirectory(apps)
        add_subdirectory(mods)
    endif()
    if(AGENTDESK_BUILD_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()
    return()
endif()

include(AgentDeskConPty)
include(AgentDeskClFilter)

add_subdirectory(third_party/libvterm)
add_subdirectory(libs)
add_subdirectory(tools)

# The example mods are built next to agent-ui.exe, so they come with the apps.
if(AGENTDESK_BUILD_APPS)
    add_subdirectory(apps)
    add_subdirectory(mods)
endif()

if(AGENTDESK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
