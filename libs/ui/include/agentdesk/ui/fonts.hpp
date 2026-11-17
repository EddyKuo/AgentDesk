#pragma once

#include <string>

struct ImFont;

namespace agentdesk::ui {

struct FontSet {
    ImFont* mono = nullptr;     // terminal: monospace Latin (integer advances) merged with a CJK face
    ImFont* ui = nullptr;       // interface text: Segoe UI merged with the CJK face
    ImFont* ui_bold = nullptr;  // headings, names
    float mono_size = 15.0f;
    float ui_size = 14.0f;
    std::string mono_source;  // files used, for diagnostics
    std::string ui_source;
    std::string cjk_source;
};

// Loads fonts from %WINDIR%\Fonts at run time (system fonts are never redistributed, skill
// rd/frontend-imgui §4): Cascadia Mono or Consolas for the terminal, Segoe UI for the interface,
// both merged with Microsoft JhengHei for CJK. Falls back to the ImGui default font.
FontSet load_fonts(float mono_size_px, float ui_size_px);

}  // namespace agentdesk::ui
