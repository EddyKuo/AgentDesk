if(APPLE)
    # macOS (ADR-30): the same interface over Unix sockets.
    agentdesk_library(agentdesk_ipc
        src/pipe_posix.cpp
        src/host_launcher_posix.cpp)
    add_library(agentdesk::ipc ALIAS agentdesk_ipc)
    target_include_directories(agentdesk_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(agentdesk_ipc PUBLIC agentdesk::protocol agentdesk::platform bsm)
    return()
endif()
agentdesk_library(agentdesk_ipc
    src/pipe.cpp
    src/host_launcher.cpp)
add_library(agentdesk::ipc ALIAS agentdesk_ipc)
target_include_directories(agentdesk_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(agentdesk_ipc PUBLIC agentdesk::protocol agentdesk::platform_win advapi32)
