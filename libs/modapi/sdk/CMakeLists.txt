# AgentDesk mod SDK: builds the example mods, and your own next to them.
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   (in a VS 2022 x64 prompt)
#   cmake --build build
# Each mod comes out as build\mods\<name>\mod.dll with its mod.json; copy that folder into
# %LOCALAPPDATA%\AgentDesk\mods and enable it in Settings > Mods.
cmake_minimum_required(VERSION 3.20)
project(agentdesk_mods CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/utf-8 /EHsc /W4)
endif()

# agentdesk_mod(<name> <sources>): one mod DLL, built as mods/<name>/mod.dll.
function(agentdesk_mod name)
    string(REPLACE "-" "_" target "mod_${name}")
    add_library(${target} SHARED ${ARGN})
    target_include_directories(${target} PRIVATE "${AGENTDESK_SDK_DIR}/include")
    set(out "${CMAKE_BINARY_DIR}/mods/${name}")
    set_target_properties(${target} PROPERTIES OUTPUT_NAME mod RUNTIME_OUTPUT_DIRECTORY "${out}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${out}" RUNTIME_OUTPUT_DIRECTORY_RELEASE "${out}")
    foreach(f mod.json settings.json)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${f}" "${out}/${f}")
        endif()
    endforeach()
endfunction()

set(AGENTDESK_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}")
file(GLOB _mods LIST_DIRECTORIES true "${CMAKE_CURRENT_LIST_DIR}/examples/*")
foreach(m ${_mods})
    if(IS_DIRECTORY "${m}" AND EXISTS "${m}/CMakeLists.txt")
        add_subdirectory("${m}")
    endif()
endforeach()
