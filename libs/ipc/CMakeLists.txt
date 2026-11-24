# Local IPC (design doc 3.7) over the platform's byte streams: a Named Pipe on Windows, a Unix socket
# on macOS (ADR-30).
agentdesk_library(agentdesk_ipc src/pipe.cpp src/host_launcher.cpp)
add_library(agentdesk::ipc ALIAS agentdesk_ipc)
target_include_directories(agentdesk_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(agentdesk_ipc PUBLIC agentdesk::protocol agentdesk::platform)
