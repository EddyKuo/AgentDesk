# agent-ui: the window contents are shared; each system has its own shell (main.cpp: Win32 and
# DX11; macos/main_macos.mm: AppKit and Metal, ADR-30) and its own desktop_os.hpp, window_ops.hpp
# and watcher files (win/, macos/). Bundled files are copied next to the program: fonts/ (OFL /
# MIT), the alert sounds/, the program table and icons in programs/ and the theme files in
# themes/. Each third_party folder's README.agentdesk.md says where its files come from and under
# which license.
find_package(imgui CONFIG REQUIRED)
set(_ui_app_sources
    app.cpp
    chrome.cpp
    classic_layout.cpp
    code_preview.cpp
    commands.cpp
    files_changes.cpp
    frame.cpp
    git_cache.cpp
    layouts.cpp
    mac_layout.cpp
    machine_add.cpp
    notes.cpp
    notices.cpp
    overlays.cpp
    panes.cpp
    programs.cpp
    review_layout.cpp
    pair_layout.cpp
    sessions.cpp
    settings_dialog.cpp
    settings_pages.cpp
    settings_store.cpp
    side_panels.cpp
    sidebar.cpp
    state_card.cpp
    style_layouts.cpp
    tab_bar.cpp
    terminals_layout.cpp
    terminals_options.cpp
    timeline.cpp
    themes.cpp
    appearance.cpp
    theme_commands.cpp
    theme_suggest.cpp
    ambient.cpp
    theme_editor.cpp
    welcome.cpp
    worktrees.cpp
    focus_mode.cpp
    stage_layout.cpp
    inbox_layout.cpp
    garden_layout.cpp
    layout_tips.cpp
    ui_tokens.cpp)
if(WIN32)
    agentdesk_executable(agent-ui WIN32
        main.cpp
        ${_ui_app_sources}
        win/desktop_os_win.cpp
        win/git_cache_win.cpp
        win/side_panels_win.cpp
        win/window_ops_win.cpp
        agent-ui.manifest)
    target_include_directories(agent-ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(agent-ui PRIVATE agentdesk::ui agentdesk::ipc imgui::imgui nlohmann_json::nlohmann_json
        d3d11 dxgi dwmapi shcore dbghelp winmm imm32 shell32)
else()
    agentdesk_executable(agent-ui
        macos/main_macos.mm
        ${_ui_app_sources}
        macos/desktop_os_macos.mm
        macos/git_cache_macos.cpp
        macos/side_panels_macos.cpp
        ${PROJECT_SOURCE_DIR}/third_party/imgui_osx/imgui_impl_osx.mm)
    target_include_directories(agent-ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/third_party/imgui_osx)
    # Vendored backend: upstream code, its own warning level.
    set_source_files_properties(${PROJECT_SOURCE_DIR}/third_party/imgui_osx/imgui_impl_osx.mm PROPERTIES COMPILE_OPTIONS "-Wno-error;-w")
    target_compile_options(agent-ui PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(agent-ui PRIVATE agentdesk::ui agentdesk::ipc imgui::imgui nlohmann_json::nlohmann_json
        "-framework AppKit" "-framework Metal" "-framework MetalKit" "-framework QuartzCore"
        "-framework UniformTypeIdentifiers" "-framework GameController" "-framework Carbon")
endif()
target_compile_definitions(agent-ui PRIVATE AGENTDESK_VERSION_STRING="${PROJECT_VERSION}")
# The UI starts agent-host from its own directory (design doc 2.2).
add_dependencies(agent-ui agent-host agentctl)
add_custom_command(TARGET agent-ui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/fonts" "$<TARGET_FILE_DIR:agent-ui>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/sounds" "$<TARGET_FILE_DIR:agent-ui>/sounds"
    # The source notes stay in the repository; the app ships the files and their licenses.
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:agent-ui>/fonts/README.agentdesk.md"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/programs" "$<TARGET_FILE_DIR:agent-ui>/programs"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/themes" "$<TARGET_FILE_DIR:agent-ui>/themes"
    # AgentDesk's own theme files (the layouts' suggested themes), MIT like the rest.
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/libs/ui/themes" "$<TARGET_FILE_DIR:agent-ui>/themes")
# agent-host and agentctl next to agent-ui, on every build (a POST_BUILD step would only run when
# agent-ui itself relinks and leave stale copies). On Windows a running agent-host holds its exe
# open; the copy then fails and says so.
add_custom_target(agent-ui-siblings ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:agent-host>" "$<TARGET_FILE:agentctl>" "$<TARGET_FILE_DIR:agent-ui>"
    DEPENDS agent-host agentctl agent-ui)
if(WIN32)
    agentdesk_bundle_conpty(agent-ui)
endif()
