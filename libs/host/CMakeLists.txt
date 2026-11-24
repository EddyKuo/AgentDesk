if(APPLE)
    # macOS (ADR-30): handover and tool processes have their own POSIX files.
    agentdesk_library(agentdesk_host
        src/states.cpp
        src/session.cpp
        src/store.cpp
        src/session_manager.cpp
        src/handover_posix.cpp
        src/git_tools.cpp)
    add_library(agentdesk::host ALIAS agentdesk_host)
    target_include_directories(agentdesk_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(agentdesk_host PUBLIC agentdesk::platform agentdesk::terminal agentdesk::protocol agentdesk::ipc)
    return()
endif()
agentdesk_library(agentdesk_host
    src/states.cpp
    src/session.cpp
    src/store.cpp
    src/session_manager.cpp
    src/handover.cpp
    src/git_tools.cpp)
add_library(agentdesk::host ALIAS agentdesk_host)
target_include_directories(agentdesk_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(agentdesk_host PUBLIC agentdesk::platform_win agentdesk::terminal agentdesk::protocol agentdesk::ipc rpcrt4)
