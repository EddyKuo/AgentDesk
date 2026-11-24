# The host library: shared sources, and each system's own (*_win.cpp, *_posix.cpp; ADR-30).
set(_host_sources
    src/states.cpp
    src/state_timeline.cpp
    src/session.cpp
    src/store.cpp
    src/session_manager.cpp
    src/handover.cpp
    src/git_tools.cpp)
if(WIN32)
    list(APPEND _host_sources src/host_os_win.cpp src/handover_win.cpp)
else()
    list(APPEND _host_sources src/host_os_posix.cpp src/handover_posix.cpp)
endif()
agentdesk_library(agentdesk_host ${_host_sources})
add_library(agentdesk::host ALIAS agentdesk_host)
target_include_directories(agentdesk_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(agentdesk_host PUBLIC
    agentdesk::platform agentdesk::terminal agentdesk::protocol agentdesk::ipc)
