agentdesk_executable(agent-ui WIN32 main.cpp agent-ui.manifest)
target_link_libraries(agent-ui PRIVATE agentdesk::ui agentdesk::host d3d11 dxgi imm32 shell32 nlohmann_json::nlohmann_json)
agentdesk_bundle_conpty(agent-ui)
