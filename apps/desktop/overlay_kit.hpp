// Small helpers shared by the dialogs and overlays (settings, welcome, palette, notes, notices):
// word-wrapped text, centred dialog placement and a few string utilities.
#pragma once

#include "app_internal.hpp"

namespace agentdesk::desktop::detail {

// ASCII lower case; other bytes (UTF-8 sequences included) stay as they are.
inline std::string ascii_lower(std::string text) {
    for (auto& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Breaks `text` into lines no wider than `width` (font at `size` pixels) and calls
// `line(begin, end)` for each. Spaces after a break are skipped, and so are newlines unless
// `skip_newlines` is false.
template <class LineFn>
void for_each_wrapped_line(ImFont* font, float size, const char* text, float width, LineFn&& line,
                           bool skip_newlines = true) {
    const char* t = text;
    const char* end = text + std::strlen(text);
    while (t < end) {
        const char* brk = font->CalcWordWrapPosition(size, t, end, width);
        if (brk == t)
            brk = t + 1;  // nothing fits: take one byte so the loop always advances
        line(t, brk);
        t = brk;
        while (t < end && (*t == ' ' || (skip_newlines && *t == '\n')))
            ++t;
    }
}

// Draws `text` centred in the box at `origin` of `box` size.
inline void text_centered(ImDrawList* dl, ImFont* font, float size, ImVec2 origin, ImVec2 box, ImU32 color,
                          const char* text) {
    const ImVec2 t = font->CalcTextSizeA(size, FLT_MAX, 0, text);
    dl->AddText(font, size, ImVec2(origin.x + (box.x - t.x) * 0.5f, origin.y + (box.y - t.y) * 0.5f), color,
                text);
}

// A fixed-size dialog: `want` at scale, shrunk to leave 8 px on each side of a small window.
inline ImVec2 dialog_size(float want_w, float want_h) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    return ImVec2(std::min(want_w, vp->WorkSize.x - px(16)), std::min(want_h, vp->WorkSize.y - px(16)));
}

// Places the next window at `size` in the middle of the work area.
inline void center_next_window(ImVec2 size) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - size.x) * 0.5f,
                                   vp->WorkPos.y + (vp->WorkSize.y - size.y) * 0.5f));
    ImGui::SetNextWindowSize(size);
}

// How a layout is named in Settings and the command palette.
inline std::string layout_display_name(const std::string& name) {
    if (name == "workspace")
        return "AgentDesk";
    if (name == "terminals")
        return "Terminals";
    if (name == "mac")
        return "AgentDesk Mac";
    if (name == "mission")
        return "Mission Control";
    return name;
}

// An agent's state as one small glyph: the spinner while working (drawn from `spin_x`), a ring
// when idle, a dot in the state's colour, or a 1.2 px dot in `none` without a state. The ring and
// dots centre on (`dot_x`, `cy`).
inline void attention_glyph(App::State& s, ImDrawList* dl, float spin_x, float dot_x, float cy, Attention att,
                            ImU32 none) {
    if (att == Attention::kWorking)
        status_mark(s, dl, spin_x, cy, att, true);
    else if (att == Attention::kIdle)
        dl->AddCircle(ImVec2(dot_x, cy), px(3.5f), g_pal->idle, 0, px(1.3f));
    else if (att != Attention::kNone)
        dl->AddCircleFilled(ImVec2(dot_x, cy), px(3.5f), attention_color(att));
    else
        dl->AddCircleFilled(ImVec2(dot_x, cy), px(1.2f), none);
}

// After the zoom or the interface text size changed: the scale and the style follow.
inline void apply_zoom(App::State& s) {
    g_scale = s.dpi * ui_scale(s.settings);
    apply_style(s.dpi, ui_scale(s.settings));
}

}  // namespace agentdesk::desktop::detail
