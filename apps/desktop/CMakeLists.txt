agentdesk_executable(agent-ui WIN32 main.cpp app.cpp agent-ui.manifest)
target_link_libraries(agent-ui PRIVATE agentdesk::ui agentdesk::ipc d3d11 dxgi dwmapi dbghelp winmm imm32 shell32 nlohmann_json::nlohmann_json)
# app.cpp (with the Herdr 98 .inl files) passes the COFF section limit in Debug.
target_compile_options(agent-ui PRIVATE /bigobj)
# The UI starts agent-host.exe from its own directory (design doc 2.2).
add_dependencies(agent-ui agent-host agentctl)
# Bundled fonts (OFL / MIT, third_party/fonts/README.agentdesk.md): fonts\ next to agent-ui.exe;
# Herdr 98's fonts, icons, pictures and sounds (third_party/herdr/win98/README.agentdesk.md): win98\.
add_custom_command(TARGET agent-ui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/fonts" "$<TARGET_FILE_DIR:agent-ui>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/sounds" "$<TARGET_FILE_DIR:agent-ui>/sounds"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/win98" "$<TARGET_FILE_DIR:agent-ui>/win98"
    # AgentDesk's own Start icon (its mark in pixels) over the shipped one.
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/win98" "$<TARGET_FILE_DIR:agent-ui>/win98"
    # The source notes stay in the repository; the app ships the files and their licenses.
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:agent-ui>/win98/README.herdr.md" "$<TARGET_FILE_DIR:agent-ui>/win98/README.agentdesk.md" "$<TARGET_FILE_DIR:agent-ui>/fonts/README.agentdesk.md"
    # AgentDesk 98 draws its name itself (win98_kit.inl brand_*): not the pictures that say Herdr.
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:agent-ui>/win98/img/splash.png" "$<TARGET_FILE_DIR:agent-ui>/win98/img/about.png" "$<TARGET_FILE_DIR:agent-ui>/win98/img/banner.png" "$<TARGET_FILE_DIR:agent-ui>/win98/img/banner@2x.png"
    # herdr's program table and icons (defaults/programs.json, defaults/icons): programs\.
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/programs" "$<TARGET_FILE_DIR:agent-ui>/programs"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/themes" "$<TARGET_FILE_DIR:agent-ui>/themes")
# Last: a running agent-host holds its exe and conpty.dll open, and a failed copy stops the steps
# after it.
add_custom_command(TARGET agent-ui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:agent-host>" "$<TARGET_FILE:agentctl>" "$<TARGET_FILE_DIR:agent-ui>")
agentdesk_bundle_conpty(agent-ui)
target_compile_definitions(agent-ui PRIVATE AGENTDESK_VERSION_STRING="${PROJECT_VERSION}")
