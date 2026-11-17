agentdesk_executable(agentctl main.cpp)
target_link_libraries(agentctl PRIVATE agentdesk::ipc agentdesk::protocol)
