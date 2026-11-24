# The Windows implementation of the platform interfaces in libs/platform/include (ADR-30,
# docs/design/platform-abstraction.md 5).
agentdesk_library(agentdesk_platform
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
    src/dynamic_library_win.cpp
    src/detached.cpp
    src/process_tree.cpp)
add_library(agentdesk::platform ALIAS agentdesk_platform)
target_include_directories(agentdesk_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Headers include <windows.h>; consumers need the same lean/unicode definitions.
target_compile_definitions(agentdesk_platform PUBLIC
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_link_libraries(agentdesk_platform PUBLIC advapi32 shell32 bcrypt rpcrt4)
