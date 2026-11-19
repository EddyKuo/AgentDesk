agentdesk_executable(agent-ui WIN32
    main.cpp
    app.cpp
    chrome.cpp
    code_preview.cpp
    commands.cpp
    files_changes.cpp
    frame.cpp
    git_cache.cpp
    layouts.cpp
    machine_add.cpp
    notes.cpp
    notices.cpp
    overlays.cpp
    panes.cpp
    programs.cpp
    sessions.cpp
    settings_dialog.cpp
    settings_pages.cpp
    settings_store.cpp
    side_panels.cpp
    sidebar.cpp
    tab_bar.cpp
    terminals_layout.cpp
    themes.cpp
    welcome.cpp
    win98_chrome.cpp
    win98_desktop.cpp
    win98_dialogs.cpp
    win98_games.cpp
    win98_kit.cpp
    worktrees.cpp
    agent-ui.manifest)
target_link_libraries(agent-ui PRIVATE agentdesk::ui agentdesk::ipc d3d11 dxgi dwmapi dbghelp winmm imm32 shell32 nlohmann_json::nlohmann_json)
# The UI starts agent-host.exe from its own directory (design doc 2.2).
add_dependencies(agent-ui agent-host agentctl)
# Bundled files next to agent-ui.exe: fonts\ (OFL / MIT), the alert sounds\, the AgentDesk 98
# layout's fonts, icons, pictures and sounds in win98\, the program table and icons in programs\
# and the theme files in themes\. Each third_party folder's README.agentdesk.md says where its
# files come from and under which license.
add_custom_command(TARGET agent-ui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/fonts" "$<TARGET_FILE_DIR:agent-ui>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/sounds" "$<TARGET_FILE_DIR:agent-ui>/sounds"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/win98" "$<TARGET_FILE_DIR:agent-ui>/win98"
    # AgentDesk's own Start icon (its mark in pixels) over the shipped one.
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/win98" "$<TARGET_FILE_DIR:agent-ui>/win98"
    # The source notes stay in the repository; the app ships the files and their licenses.
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:agent-ui>/win98/README.herdr.md" "$<TARGET_FILE_DIR:agent-ui>/win98/README.agentdesk.md" "$<TARGET_FILE_DIR:agent-ui>/fonts/README.agentdesk.md"
    # AgentDesk 98 draws its name itself (win98_kit.cpp brand_*), so the pictures with another
    # name in them are not shipped.
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:agent-ui>/win98/img/splash.png" "$<TARGET_FILE_DIR:agent-ui>/win98/img/about.png" "$<TARGET_FILE_DIR:agent-ui>/win98/img/banner.png" "$<TARGET_FILE_DIR:agent-ui>/win98/img/banner@2x.png"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/programs" "$<TARGET_FILE_DIR:agent-ui>/programs"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/themes" "$<TARGET_FILE_DIR:agent-ui>/themes")
# Last: a running agent-host holds its exe and conpty.dll open, and a failed copy stops the steps
# after it.
add_custom_command(TARGET agent-ui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:agent-host>" "$<TARGET_FILE:agentctl>" "$<TARGET_FILE_DIR:agent-ui>")
agentdesk_bundle_conpty(agent-ui)
target_compile_definitions(agent-ui PRIVATE AGENTDESK_VERSION_STRING="${PROJECT_VERSION}")
