# The macOS implementation of the platform interfaces in libs/platform/include (ADR-30,
# docs/design/platform-abstraction.md 5).
agentdesk_library(agentdesk_platform
    src/posix_util.cpp
    src/text.cpp
    src/pty_posix.cpp
    src/proc_info_macos.cpp
    src/process_posix.cpp
    src/ipc_unix.cpp
    src/system_macos.cpp
    src/compat_win.cpp)
add_library(agentdesk::platform ALIAS agentdesk_platform)
target_include_directories(agentdesk_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    # The POSIX versions of headers Windows has under the same name (crt.hpp).
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    # Interim stand-ins for the Windows helper headers shared code still includes (compat/README.md).
    ${CMAKE_CURRENT_SOURCE_DIR}/compat)
