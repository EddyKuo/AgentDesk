# Local IPC (design doc 3.7): a Named Pipe on Windows, a Unix socket on macOS (ADR-30). The two
# pipe.hpp headers declare the same interface; the include folder picks the system's one.
if(WIN32)
    agentdesk_library(agentdesk_ipc src/pipe.cpp src/host_launcher.cpp)
    target_include_directories(agentdesk_ipc PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/include_win)
    target_link_libraries(agentdesk_ipc PUBLIC agentdesk::protocol agentdesk::platform_win advapi32)
else()
    agentdesk_library(agentdesk_ipc src/pipe_posix.cpp src/host_launcher_posix.cpp)
    target_include_directories(agentdesk_ipc PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/include_posix)
    target_link_libraries(agentdesk_ipc PUBLIC agentdesk::protocol agentdesk::platform bsm)
endif()
add_library(agentdesk::ipc ALIAS agentdesk_ipc)
