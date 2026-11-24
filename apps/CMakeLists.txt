# Executables: agent-ui (desktop), agent-host (host), agentctl (cli).
add_subdirectory(host)
add_subdirectory(cli)
if(APPLE AND NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/desktop/macos)
    return()  # macOS: the window shell is still being ported (ADR-30)
endif()
add_subdirectory(desktop)
