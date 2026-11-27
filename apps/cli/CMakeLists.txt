# agentctl: the shared commands in main.cpp; the entry point and the console, environment and
# bridge code per system (cli_win.cpp, cli_posix.cpp; ADR-30).
agentdesk_executable(agentctl main.cpp $<IF:$<BOOL:${WIN32}>,cli_win.cpp,cli_posix.cpp>)
# agentdesk::ui for agent-host's update (updates.hpp: release check, download, checks).
target_link_libraries(agentctl PRIVATE agentdesk::ipc agentdesk::protocol agentdesk::ui)
if(WIN32)
    # The icon Explorer and the taskbar show; the same meerkat as agent-ui on another colour.
    target_sources(agentctl PRIVATE agentctl.rc)
    set_source_files_properties(agentctl.rc PROPERTIES OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/assets/icon/agentctl.ico")
endif()
