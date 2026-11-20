# The agentdesk_ui library: everything agent-ui needs that is not the window itself.
find_package(imgui CONFIG REQUIRED)
find_package(lunasvg CONFIG REQUIRED)

agentdesk_library(agentdesk_ui
    src/input_mapping.cpp
    src/imgui_adapter.cpp
    src/fonts.cpp
    src/terminal_view.cpp
    src/sprites.cpp
    src/workspace.cpp
    src/chrome_logic.cpp
    src/keymap.cpp
    src/git_info.cpp
    src/mods.cpp
    src/remote_screen.cpp
    src/host_client.cpp
    src/links.cpp
    src/clipboard.cpp
    src/system_process.cpp
    src/telemetry.cpp
    src/updates.cpp
    src/image_cache.cpp
    src/programs.cpp
    src/layout.cpp
    src/window_place.cpp
    src/command_names.cpp
    src/mac_layout.cpp
    src/focus_mode.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/layout_defaults.cpp)
add_library(agentdesk::ui ALIAS agentdesk_ui)
target_include_directories(agentdesk_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# host/input.hpp is part of the public interface (TerminalSource::send).
target_link_libraries(agentdesk_ui PRIVATE agentdesk::modapi shell32 ole32 bcrypt shcore lunasvg::lunasvg)
target_link_libraries(agentdesk_ui PUBLIC imgui::imgui agentdesk::terminal agentdesk::host agentdesk::ipc agentdesk::protocol)

# The shipped layout files (kept verbatim), compiled in as text for layout::builtin_text as
# kPresetMain, kPresetTerminals (herdr-app's, third_party/herdr/layouts) and kPresetMac
# (AgentDesk's own, layouts/ here).
set(_layout_defaults "")
foreach(_file
        "${PROJECT_SOURCE_DIR}/third_party/herdr/layouts/main.json"
        "${PROJECT_SOURCE_DIR}/third_party/herdr/layouts/terminals.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/layouts/mac.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/layouts/focus.json")
    get_filename_component(_name "${_file}" NAME_WE)
    file(READ "${_file}" _text)
    string(SUBSTRING "${_name}" 0 1 _first)
    string(TOUPPER "${_first}" _first)
    string(SUBSTRING "${_name}" 1 -1 _rest)
    string(APPEND _layout_defaults "extern const char* const kPreset${_first}${_rest};\nconst char* const kPreset${_first}${_rest} = R\"layoutjson(${_text})layoutjson\";\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_file}")
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/layout_defaults.cpp" CONTENT
"// Generated from the shipped layout files by libs/ui/CMakeLists.txt. Do not edit.
namespace agentdesk::ui::layout::detail {
@_layout_defaults@}  // namespace agentdesk::ui::layout::detail
" @ONLY)
