# agent-host: main_win.cpp is the Windows entry point (no console window), main_posix.cpp the macOS
# one (ADR-30).
if(WIN32)
    agentdesk_executable(agent-host WIN32 main_win.cpp agent-host.rc)
    set_source_files_properties(agent-host.rc PROPERTIES OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/assets/icon/agent-host.ico")
    target_link_libraries(agent-host PRIVATE agentdesk::host agentdesk::ipc shell32)
    agentdesk_bundle_conpty(agent-host)
else()
    agentdesk_executable(agent-host main_posix.cpp)
    target_link_libraries(agent-host PRIVATE agentdesk::host agentdesk::ipc)
endif()
target_compile_definitions(agent-host PRIVATE AGENTDESK_VERSION_STRING="${PROJECT_VERSION}")
