find_package(imgui CONFIG REQUIRED)
find_package(lunasvg CONFIG REQUIRED)

agentdesk_library(agentdesk_ui
    src/input_mapping.cpp
    src/imgui_adapter.cpp
    src/fonts.cpp
    src/terminal_view.cpp
    src/workspace.cpp
    src/chrome_logic.cpp
    src/keymap.cpp
    src/git_info.cpp
    src/mods.cpp
    src/remote_screen.cpp
    src/host_client.cpp
    src/links.cpp
    src/clipboard.cpp
    src/win98.cpp
    src/image_cache.cpp)
add_library(agentdesk::ui ALIAS agentdesk_ui)
target_include_directories(agentdesk_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# host/input.hpp is part of the public interface (TerminalSource::send).
target_link_libraries(agentdesk_ui PRIVATE agentdesk::modapi shell32 ole32 windowscodecs lunasvg::lunasvg)
target_link_libraries(agentdesk_ui PUBLIC imgui::imgui agentdesk::terminal agentdesk::host agentdesk::ipc agentdesk::protocol)
