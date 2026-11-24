# agentctl: the shared commands in main.cpp; the entry point and the console, environment and
# bridge code per system (cli_win.cpp, cli_posix.cpp; ADR-30).
agentdesk_executable(agentctl main.cpp $<IF:$<BOOL:${WIN32}>,cli_win.cpp,cli_posix.cpp>)
# agentdesk::ui for agent-host's update (updates.hpp: release check, download, checks).
target_link_libraries(agentctl PRIVATE agentdesk::ipc agentdesk::protocol agentdesk::ui)
