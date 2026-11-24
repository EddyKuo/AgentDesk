agentdesk_library(agentdesk_platform_win
    src/text.cpp
    src/unique_handle.cpp
    src/command_line.cpp
    src/conpty_api.cpp
    src/job.cpp
    src/pty_process.cpp
    src/pty_win.cpp
    src/process_win.cpp
    src/ipc_win.cpp
    src/system_win.cpp
    src/detached.cpp
    src/process_tree.cpp)
add_library(agentdesk::platform_win ALIAS agentdesk_platform_win)
add_library(agentdesk::platform ALIAS agentdesk_platform_win)
target_include_directories(agentdesk_platform_win PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Headers include <windows.h>; consumers need the same lean/unicode definitions.
target_compile_definitions(agentdesk_platform_win PUBLIC
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_link_libraries(agentdesk_platform_win PUBLIC advapi32 shell32)
