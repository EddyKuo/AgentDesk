# The mod interface (ADR-17): a C header (mod_api.h) and a header-only C++ helper (mod_sdk.hpp).
add_library(agentdesk_modapi INTERFACE)
add_library(agentdesk::modapi ALIAS agentdesk_modapi)
target_include_directories(agentdesk_modapi INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(agentdesk_modapi INTERFACE nlohmann_json::nlohmann_json)

# agentdesk_mod(<name> <sources>): a mod DLL written with mod_sdk.hpp, built as
# <agent-ui dir>/mods/<name>/mod.dll with the mod.json next to its sources.
function(agentdesk_mod name)
    set(target mod_${name})
    string(REPLACE "-" "_" target "${target}")
    add_library(${target} SHARED ${ARGN})
    agentdesk_target_defaults(${target})
    target_link_libraries(${target} PRIVATE agentdesk::modapi)
    set(out "${CMAKE_BINARY_DIR}/apps/desktop/mods/${name}")
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME mod
        RUNTIME_OUTPUT_DIRECTORY "${out}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${out}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${out}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${out}")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mod.json")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/mod.json" "${out}/mod.json")
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/settings.json")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/settings.json" "${out}/settings.json")
    endif()
endfunction()
