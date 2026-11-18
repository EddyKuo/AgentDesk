agentdesk_executable(agentctl main.cpp)
# agentdesk::ui for agent-host's update (updates.hpp: release check, download, checks).
target_link_libraries(agentctl PRIVATE agentdesk::ipc agentdesk::protocol agentdesk::ui)
