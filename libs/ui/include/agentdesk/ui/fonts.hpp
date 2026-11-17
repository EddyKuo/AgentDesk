#pragma once

#include <string>

struct ImFont;

namespace agentdesk::ui {

struct FontSet {
    ImFont* mono = nullptr;   // terminal font: monospace Latin merged with a CJK face
    std::string mono_source;  // file used, for diagnostics
    std::string cjk_source;
};

// Loads fonts from %WINDIR%\Fonts at run time (system fonts are never redistributed, skill
// rd/frontend-imgui §4): Cascadia Mono or Consolas, merged with Microsoft JhengHei for CJK.
// Falls back to the ImGui default font when none is found.
FontSet load_fonts(float size_px);

}  // namespace agentdesk::ui
