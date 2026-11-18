# libvterm 0.3.3 (MIT), vendored with one AgentDesk addition (state handover for agent-host's
# self-update: include/vterm_agentdesk.h, src/agentdesk_handover.*, the end of src/screen.c).
# See README.agentdesk.md for provenance.
file(GLOB VTERM_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_library(vterm STATIC ${VTERM_SRC})
add_library(agentdesk::vterm ALIAS vterm)
target_include_directories(vterm
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(vterm PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(MSVC)
    # Third-party code: keep upstream sources untouched, silence known benign warnings.
    target_compile_options(vterm PRIVATE /W3 /wd4018 /wd4244 /wd4267 /wd4996 /utf-8)
    target_compile_definitions(vterm PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
