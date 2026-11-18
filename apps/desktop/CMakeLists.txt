agentdesk_executable(agent-ui WIN32 main.cpp app.cpp agent-ui.manifest)
target_link_libraries(agent-ui PRIVATE agentdesk::ui agentdesk::ipc d3d11 dxgi dwmapi dbghelp winmm imm32 shell32 nlohmann_json::nlohmann_json)
# The UI starts agent-host.exe from its own directory (design doc 2.2).
add_dependencies(agent-ui agent-host agentctl)
add_custom_command(TARGET agent-ui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:agent-host>" "$<TARGET_FILE:agentctl>" "$<TARGET_FILE_DIR:agent-ui>")
agentdesk_bundle_conpty(agent-ui)
# Bundled fonts (OFL / MIT, third_party/fonts/README.agentdesk.md): fonts\ next to agent-ui.exe.
add_custom_command(TARGET agent-ui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/fonts" "$<TARGET_FILE_DIR:agent-ui>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/sounds" "$<TARGET_FILE_DIR:agent-ui>/sounds")
target_compile_definitions(agent-ui PRIVATE AGENTDESK_VERSION_STRING="${PROJECT_VERSION}")
