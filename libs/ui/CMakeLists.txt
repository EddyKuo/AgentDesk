# The agentdesk_ui library: everything agent-ui needs that is not the window itself.
find_package(imgui CONFIG REQUIRED)
find_package(lunasvg CONFIG REQUIRED)

set(_ui_sources
    src/input_mapping.cpp
    src/imgui_adapter.cpp
    src/fonts.cpp
    src/terminal_view.cpp
    src/sprites.cpp
    src/workspace.cpp
    src/chrome_logic.cpp
    src/keymap.cpp
    src/keyboard.cpp
    src/git_info.cpp
    src/path_text.cpp
    src/mods.cpp
    src/remote_screen.cpp
    src/host_client.cpp
    src/links.cpp
    src/telemetry.cpp
    src/updates.cpp
    src/image_cache.cpp
    src/programs.cpp
    src/layout.cpp
    src/window_place.cpp
    src/command_names.cpp
    src/mac_layout.cpp
    src/focus_mode.cpp
    src/mission.cpp
    src/stage.cpp
    src/inbox.cpp
    src/review.cpp
    src/pair.cpp
    src/timeline.cpp
    src/color_math.cpp
    src/schedule.cpp
    src/theme_file.cpp
    src/ambient.cpp
    src/layout_styles.cpp
    src/terminals_options.cpp
    src/classic_layout.cpp
    src/window_buttons.cpp
    src/layout_tips.cpp
    src/design_tokens.cpp
    src/garden.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/layout_defaults.cpp)
# What differs per system lives in its own files (*_win.cpp; *_posix.cpp and macos/*.mm, ADR-30)
# behind the internal headers in src/ (*_os.hpp, system_process.hpp).
if(WIN32)
    list(APPEND _ui_sources
        src/clipboard.cpp
        src/fonts_win.cpp
        src/keyboard_win.cpp
        src/keymap_win.cpp
        src/links_win.cpp
        src/mods_win.cpp
        src/system_process.cpp
        src/updates_win.cpp
        src/window_place_win.cpp)
else()
    list(APPEND _ui_sources
        src/macos/clipboard_macos.mm
        src/macos/fonts_macos.cpp
        src/macos/keyboard_macos.mm
        src/macos/keymap_macos.cpp
        src/macos/window_place_macos.mm
        src/links_posix.cpp
        src/mods_posix.cpp
        src/system_process_posix.cpp
        src/updates_posix.cpp)
endif()
agentdesk_library(agentdesk_ui ${_ui_sources})
add_library(agentdesk::ui ALIAS agentdesk_ui)
target_include_directories(agentdesk_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# host/input.hpp is part of the public interface (TerminalSource::send).
if(APPLE)
    # fonts.cpp asks FreeType which face of a collection (PingFang.ttc) a font is.
    find_package(Freetype REQUIRED)
    target_link_libraries(agentdesk_ui PRIVATE Freetype::Freetype)
    target_link_libraries(agentdesk_ui PRIVATE agentdesk::modapi lunasvg::lunasvg
        "-framework AppKit" "-framework CoreText" "-framework CoreFoundation" "-framework Carbon")
else()
    target_link_libraries(agentdesk_ui PRIVATE agentdesk::modapi shell32 ole32 bcrypt shcore lunasvg::lunasvg)
endif()
target_link_libraries(agentdesk_ui PUBLIC imgui::imgui agentdesk::terminal agentdesk::host agentdesk::ipc agentdesk::protocol)

# The shipped layout files (kept verbatim), compiled in as text for layout::builtin_text as
# kPresetMain, kPresetTerminals (herdr-app's, third_party/herdr/layouts) and kPresetMac, kPresetFocus,
# kPresetStage, kPresetInbox, kPresetReview, kPresetPair, kPresetClassic, kPresetGarden (AgentDesk's own,
# layouts/ here).
set(_layout_defaults "")
foreach(_file
        "${PROJECT_SOURCE_DIR}/third_party/herdr/layouts/main.json"
        "${PROJECT_SOURCE_DIR}/third_party/herdr/layouts/terminals.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/layouts/mac.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/layouts/focus.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/layouts/stage.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/layouts/review.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/layouts/inbox.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/layouts/pair.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/layouts/classic.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/layouts/garden.json")
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
