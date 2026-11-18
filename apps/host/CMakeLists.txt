agentdesk_executable(agent-host WIN32 main.cpp)
target_link_libraries(agent-host PRIVATE agentdesk::host agentdesk::ipc shell32)
target_compile_definitions(agent-host PRIVATE AGENTDESK_VERSION_STRING="${PROJECT_VERSION}")
agentdesk_bundle_conpty(agent-host)
