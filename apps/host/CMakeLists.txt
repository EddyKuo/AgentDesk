# agent-host: main.cpp is the Windows entry point (no console window), main_posix.cpp the macOS
# one (ADR-30).
if(WIN32)
    agentdesk_executable(agent-host WIN32 main.cpp)
    target_link_libraries(agent-host PRIVATE agentdesk::host agentdesk::ipc shell32)
    agentdesk_bundle_conpty(agent-host)
else()
    agentdesk_executable(agent-host main_posix.cpp)
    target_link_libraries(agent-host PRIVATE agentdesk::host agentdesk::ipc)
endif()
target_compile_definitions(agent-host PRIVATE AGENTDESK_VERSION_STRING="${PROJECT_VERSION}")
