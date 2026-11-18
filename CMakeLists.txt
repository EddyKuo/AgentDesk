cmake_minimum_required(VERSION 3.25)

project(AgentDesk
    VERSION 0.1.0
    DESCRIPTION "Windows-native workbench for AI coding agent CLIs"
    LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "AgentDesk targets Windows 11 x64 only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(AgentDeskDefaults)
include(AgentDeskConPty)
include(AgentDeskClFilter)

option(AGENTDESK_BUILD_TESTS "Build AgentDesk tests" ON)
option(AGENTDESK_BUILD_APPS "Build AgentDesk executables" ON)

find_package(nlohmann_json CONFIG REQUIRED)

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
