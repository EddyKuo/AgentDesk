if(APPLE)
    # macOS (ADR-30): the same window contents with the AppKit/Metal shell in macos/. Built as a
    # plain executable with its files beside it, like the Windows build folder; tools/package
    # makes the AgentDesk.app bundle for release.
    find_package(imgui CONFIG REQUIRED)
    agentdesk_executable(agent-ui
        macos/main_macos.mm macos/desktop_os_macos.mm
        ${PROJECT_SOURCE_DIR}/third_party/imgui_osx/imgui_impl_osx.mm
        app.cpp chrome.cpp code_preview.cpp commands.cpp files_changes.cpp frame.cpp git_cache.cpp
        layouts.cpp machine_add.cpp notes.cpp notices.cpp overlays.cpp panes.cpp programs.cpp
        sessions.cpp settings_dialog.cpp settings_pages.cpp settings_store.cpp side_panels.cpp
        sidebar.cpp tab_bar.cpp terminals_layout.cpp themes.cpp welcome.cpp worktrees.cpp)
    target_include_directories(agent-ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/third_party/imgui_osx)
    # Vendored backend: upstream code, its own warning level.
    set_source_files_properties(${PROJECT_SOURCE_DIR}/third_party/imgui_osx/imgui_impl_osx.mm PROPERTIES COMPILE_OPTIONS "-Wno-error;-w")
    target_compile_options(agent-ui PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(agent-ui PRIVATE agentdesk::ui agentdesk::ipc imgui::imgui nlohmann_json::nlohmann_json
        "-framework AppKit" "-framework Metal" "-framework MetalKit" "-framework QuartzCore"
        "-framework UniformTypeIdentifiers" "-framework GameController" "-framework Carbon")
    add_dependencies(agent-ui agent-host agentctl)
    add_custom_command(TARGET agent-ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/fonts" "$<TARGET_FILE_DIR:agent-ui>/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/sounds" "$<TARGET_FILE_DIR:agent-ui>/sounds"
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:agent-ui>/fonts/README.agentdesk.md"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/programs" "$<TARGET_FILE_DIR:agent-ui>/programs"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/themes" "$<TARGET_FILE_DIR:agent-ui>/themes"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:agent-host>" "$<TARGET_FILE:agentctl>" "$<TARGET_FILE_DIR:agent-ui>")
    target_compile_definitions(agent-ui PRIVATE AGENTDESK_VERSION_STRING="${PROJECT_VERSION}")
    return()
endif()
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
    worktrees.cpp
    agent-ui.manifest)
target_link_libraries(agent-ui PRIVATE agentdesk::ui agentdesk::ipc d3d11 dxgi dwmapi shcore dbghelp winmm imm32 shell32 nlohmann_json::nlohmann_json)
# The UI starts agent-host.exe from its own directory (design doc 2.2).
add_dependencies(agent-ui agent-host agentctl)
# Bundled files next to agent-ui.exe: fonts\ (OFL / MIT), the alert sounds\, the program table
# and icons in programs\ and the theme files in themes\. Each third_party folder's README.agentdesk.md says where its
# files come from and under which license.
add_custom_command(TARGET agent-ui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/fonts" "$<TARGET_FILE_DIR:agent-ui>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/sounds" "$<TARGET_FILE_DIR:agent-ui>/sounds"
    # The source notes stay in the repository; the app ships the files and their licenses.
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:agent-ui>/fonts/README.agentdesk.md"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/programs" "$<TARGET_FILE_DIR:agent-ui>/programs"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/themes" "$<TARGET_FILE_DIR:agent-ui>/themes")
# Last: a running agent-host holds its exe and conpty.dll open, and a failed copy stops the steps
# after it.
add_custom_command(TARGET agent-ui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:agent-host>" "$<TARGET_FILE:agentctl>" "$<TARGET_FILE_DIR:agent-ui>")
agentdesk_bundle_conpty(agent-ui)
target_compile_definitions(agent-ui PRIVATE AGENTDESK_VERSION_STRING="${PROJECT_VERSION}")
