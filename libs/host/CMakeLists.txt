# The host library. Shared sources first; each system adds its own files (*_win.cpp, handover.cpp
# on Windows; *_posix.cpp on macOS, ADR-30) and its own include folder for the headers whose
# declarations differ (agentdesk/host/handover.hpp).
set(_host_sources
    src/states.cpp
    src/state_timeline.cpp
    src/session.cpp
    src/store.cpp
    src/session_manager.cpp
    src/git_tools.cpp)
if(WIN32)
    list(APPEND _host_sources
        src/host_os_win.cpp src/handover.cpp)
    set(_host_platform_include include_win)
    set(_host_platform_libs agentdesk::platform)
else()
    list(APPEND _host_sources
        src/host_os_posix.cpp
        src/handover_posix.cpp)
    set(_host_platform_include include_posix)
    set(_host_platform_libs agentdesk::platform)
endif()
agentdesk_library(agentdesk_host ${_host_sources})
add_library(agentdesk::host ALIAS agentdesk_host)
target_include_directories(agentdesk_host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/${_host_platform_include})
target_link_libraries(agentdesk_host PUBLIC
    ${_host_platform_libs} agentdesk::terminal agentdesk::protocol agentdesk::ipc)
