#pragma once

// Where fonts come from on this system: fonts_win.cpp (the registry, %WINDIR%\Fonts) or
// macos/fonts_macos.cpp (CoreText, ADR-30). CMake builds one of them; installed_font_families
// (fonts.hpp) is there too.

#include <filesystem>
#include <initializer_list>
#include <string>

#include <imgui.h>

namespace agentdesk::ui::fonts_os {

inline std::string utf8(const std::filesystem::path& p) {
    const auto u8 = p.u8string();  // ImGui opens files from UTF-8 paths
    return {u8.begin(), u8.end()};
}

// First existing font: `bundled` names are looked up in the fonts shipped with the app, `system`
// names (Windows file names) among the system's fonts or, elsewhere, their stand-ins there.
std::string find_font(std::initializer_list<const wchar_t*> bundled,
                      std::initializer_list<const wchar_t*> system);

// What this system needs on a face's config before it is added from `path` (macOS: the face
// inside a collection, glyphs without hinting).
void adjust_config(const std::string& path, ImFontConfig& c);

}  // namespace agentdesk::ui::fonts_os
