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
    src/system_process.cpp
    src/telemetry.cpp
    src/updates.cpp
    src/win98.cpp
    src/image_cache.cpp
    src/programs.cpp
    src/layout.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/layout_defaults.cpp)
add_library(agentdesk::ui ALIAS agentdesk_ui)
target_include_directories(agentdesk_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# host/input.hpp is part of the public interface (TerminalSource::send).
target_link_libraries(agentdesk_ui PRIVATE agentdesk::modapi shell32 ole32 bcrypt windowscodecs lunasvg::lunasvg)
target_link_libraries(agentdesk_ui PUBLIC imgui::imgui agentdesk::terminal agentdesk::host agentdesk::ipc agentdesk::protocol)

# herdr's shipped layouts (third_party/herdr/layouts, verbatim), compiled in as text
# (layout::builtin_text; herdr window.rs 160-166 include_str!).
set(_herdr_layouts "${PROJECT_SOURCE_DIR}/third_party/herdr/layouts")
set(_layout_defaults "")
foreach(_name main terminals win98)
    file(READ "${_herdr_layouts}/${_name}.json" _text)
    string(SUBSTRING "${_name}" 0 1 _first)
    string(TOUPPER "${_first}" _first)
    string(SUBSTRING "${_name}" 1 -1 _rest)
    string(APPEND _layout_defaults "extern const char* const kHerdr${_first}${_rest};\nconst char* const kHerdr${_first}${_rest} = R\"herdrjson(${_text})herdrjson\";\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_herdr_layouts}/${_name}.json")
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/layout_defaults.cpp" CONTENT
"// Generated from third_party/herdr/layouts by libs/ui/CMakeLists.txt. Do not edit.
namespace agentdesk::ui::layout::detail {
@_layout_defaults@}  // namespace agentdesk::ui::layout::detail
" @ONLY)
