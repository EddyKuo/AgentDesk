// Settings dialog building blocks: rows, steppers, fonts, segmented controls, and the Updates
// and Privacy pages.
#include "win98_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

// ------------------------------------------------------------------ settings (herdr settings.js)
// Dialog 880 wide (window.rs 3594), 620 high, left navigation 190 px (settings/main.js 897-918);
// every change applies and saves at once (no Save). The body has no item spacing: each gap is
// herdr's own.
float line_h(float size) {
    return std::round(fpx(size) * 1.3f);
}

// Moves the cursor down by `h` (the body has no item spacing).
void settings_gap(float h) {
    ImGui::Dummy(ImVec2(0, std::max(0.0f, h)));
}

// Dim text wrapped to the body's width (herdr: 12.5 px with 18 px lines, or `size` and `lh`).
void settings_wrapped(App::State& s, const char* text, ImU32 color, float size, float lh,
                      ImFont* font) {
    ImFont* f = font != nullptr ? font : s.fonts.ui;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* t = text;
    const char* end = text + std::strlen(text);
    float y = p.y;
    while (t < end) {
        const char* brk = f->CalcWordWrapPosition(fpx(size), t, end, w);
        if (brk == t)
            brk = t + 1;
        const float th = f->CalcTextSizeA(fpx(size), FLT_MAX, 0, t, brk).y;
        dl->AddText(f, fpx(size), ImVec2(p.x, y + (px(lh) - th) * 0.5f), color, t, brk);
        y += px(lh);
        t = brk;
        while (t < end && (*t == ' ' || *t == '\n'))
            ++t;
    }
    ImGui::Dummy(ImVec2(w, y - p.y));
}

// settings/main.js 157: pt 6, pb 8, 12 px semibold, dim, upper case.
void settings_section(App::State& s, const char* title) {
    std::string up;
    for (const char* c = title; *c != 0; ++c)
        up += static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(s.fonts.ui_bold, fpx(12), ImVec2(p.x, p.y + px(6)), g_pal->faint, up.c_str());
    settings_gap(px(6) + line_h(12) + px(8));
}

// settings/main.js 161-176 row(): gap 16, py 12, items centred, a 1 px bottom border; the
// title 14 px foreground, then (gap 3) the note 12.5 px dim with 18 px lines; the control on
// the right. Call the control after this returns: the cursor is placed for it.
void settings_row(App::State& s, const char* label, const char* note, float control_w, float control_h) {
    if (control_h < 0)
        control_h = std::round(fpx(13) * 1.3f) + px(10) + 2.0f;  // a settings button
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float wrap = std::max(px(80), w - control_w - px(16));
    std::vector<std::pair<const char*, const char*>> lines;
    if (note != nullptr && *note != 0) {
        const char* t = note;
        const char* end = note + std::strlen(note);
        while (t < end) {
            const char* brk = s.fonts.ui->CalcWordWrapPosition(fpx(12.5f), t, end, wrap);
            if (brk == t)
                brk = t + 1;
            lines.emplace_back(t, brk);
            t = brk;
            while (t < end && (*t == ' ' || *t == '\n'))
                ++t;
        }
    }
    const float text_h = line_h(14) + (lines.empty() ? 0.0f : px(3) + px(18) * static_cast<float>(lines.size()));
    const float inner = std::max(text_h, control_h);
    float y = p.y + px(12) + (inner - text_h) * 0.5f;
    dl->AddText(s.fonts.ui, fpx(14), ImVec2(p.x, y), g_pal->text, label);
    y += line_h(14) + px(3);
    for (const auto& [a, b] : lines) {
        const float th = s.fonts.ui->CalcTextSizeA(fpx(12.5f), FLT_MAX, 0, a, b).y;
        dl->AddText(s.fonts.ui, fpx(12.5f), ImVec2(p.x, y + (px(18) - th) * 0.5f), g_pal->faint, a, b);
        y += px(18);
    }
    const float h = px(12) + inner + px(12);
    dl->AddLine(ImVec2(p.x, p.y + h - 0.5f), ImVec2(p.x + w, p.y + h - 0.5f), g_pal->border);
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - control_w, p.y + px(12) + (inner - control_h) * 0.5f));
    ImGui::SetNextItemWidth(control_w);
    s.settings_row_end = p.y + h;
    s.settings_row_x = p.x;
}
void settings_row_done(App::State& s) {
    ImGui::SetCursorScreenPos(ImVec2(s.settings_row_x, s.settings_row_end));
    ImGui::Dummy(ImVec2(0, 0));
}
bool stepper(App::State& s, const char* id, float& v, float step, float lo, float hi, const char* fmt, float scale) {
    bool changed = false;
    ImGui::PushID(id);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + px(kStepperW), a.y + px(kStepperH));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, raised_color(), px(7));
    auto cell = [&](const char* key, const char* label, float x, float dir) {
        const ImVec2 c(x, a.y + px(1));
        ImGui::SetCursorScreenPos(c);
        if (ImGui::InvisibleButton(key, ImVec2(px(28), px(28)))) {
            v = std::clamp(std::round((v + dir * step) / step) * step, lo, hi);
            changed = true;
        }
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(c, ImVec2(c.x + px(28), c.y + px(28)), g_pal->hover,
                              px(6), dir < 0 ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersRight);
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(15), FLT_MAX, 0, label);
        dl->AddText(s.fonts.ui, fpx(15), ImVec2(c.x + (px(28) - t.x) * 0.5f, c.y + (px(28) - t.y) * 0.5f),
                    hov ? g_pal->text : g_pal->dim, label);
    };
    cell("##minus", "\xE2\x88\x92", a.x + px(1), -1.0f);
    char buf[32];
    std::snprintf(buf, sizeof buf, fmt, v * scale);
    const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, buf);
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + px(29) + (px(54) - t.x) * 0.5f, a.y + (px(kStepperH) - t.y) * 0.5f),
                g_pal->text, buf);
    cell("##plus", "+", a.x + px(29) + px(54), 1.0f);
    dl->AddRect(a, b, g_pal->border, px(7));
    ImGui::SetCursorScreenPos(a);
    ImGui::Dummy(ImVec2(px(kStepperW), px(kStepperH)));
    ImGui::PopID();
    return changed;
}

namespace {
// Terminal fonts offered in Settings: the bundled default, then monospace faces Windows ships.
// ---- fonts (herdr: every installed family, filterable; weight; interface font)
const std::vector<ui::FontFamily>& font_families() {
    static const std::vector<ui::FontFamily> families = ui::installed_font_families();
    return families;
}

}  // namespace

const ui::FontFamily* find_family(const std::string& name) {
    for (const auto& f : font_families())
        if (_stricmp(f.name.c_str(), name.c_str()) == 0)
            return &f;
    return nullptr;
}

// Terminal font: family (empty: bundled JetBrains Mono) at the chosen weight, bold from the
// family's 700 face. A layout with fonts of its own (Herdr 98: win98.json "font") uses those.
void apply_terminal_font(App::State& s) {
    if (s.layout().style == "win98") {
        w98::ensure_fonts(s);
        s.fonts.mono = s.fonts.mono_bold = w98::layout_font(s, 0);
        return;
    }
    const auto* f = find_family(s.settings.terminal_font.empty() ? std::string("JetBrains Mono") : s.settings.terminal_font);
    ImFont* mono = f != nullptr ? ui::load_font_file(f->file_for(s.settings.terminal_weight), s.settings.font_size, true) : nullptr;
    s.fonts.mono = mono != nullptr ? mono : ui::load_terminal_font(L"", s.settings.font_size);
    ImFont* bold = f != nullptr ? ui::load_font_file(f->file_for(700), s.settings.font_size, true) : nullptr;
    s.fonts.mono_bold = bold != nullptr ? bold : s.fonts.mono;
}

// Interface font: family (empty: bundled Geist, kept from startup) at 400 / 500 / 600.
void apply_ui_font(App::State& s) {
    if (s.base_ui == nullptr) {  // the bundled set, loaded once at startup
        s.base_ui = s.fonts.ui;
        s.base_ui_medium = s.fonts.ui_medium;
        s.base_ui_bold = s.fonts.ui_bold;
    }
    if (s.layout().style == "win98") {  // win98.json "ui_font": "Pixelated MS Sans Serif"
        w98::ensure_fonts(s);
        s.fonts.ui = s.fonts.ui_medium = w98::layout_font(s, 1);
        s.fonts.ui_bold = w98::layout_font(s, 2);
        ImGui::GetIO().FontDefault = s.fonts.ui;
        return;
    }
    const auto* f = s.settings.ui_font.empty() ? nullptr : find_family(s.settings.ui_font);
    ImFont* r = f != nullptr ? ui::load_font_file(f->file_for(400), 13.0f, false) : nullptr;
    if (r == nullptr) {
        s.fonts.ui = s.base_ui;
        s.fonts.ui_medium = s.base_ui_medium;
        s.fonts.ui_bold = s.base_ui_bold;
    } else {
        ImFont* m = ui::load_font_file(f->file_for(500), 13.0f, false);
        ImFont* b = ui::load_font_file(f->file_for(600), 13.0f, false);
        s.fonts.ui = r;
        s.fonts.ui_medium = m != nullptr ? m : r;
        s.fonts.ui_bold = b != nullptr ? b : s.fonts.ui_medium;
    }
    ImGui::GetIO().FontDefault = s.fonts.ui;
}

// herdr font list: a filter box and up to 200 matching families.
bool font_picker(App::State& /*s*/, const char* id, std::string& family, const char* default_label, float width) {
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    const std::string current = family.empty() ? std::string(default_label) : family;
    if (ImGui::BeginCombo(id, current.c_str(), ImGuiComboFlags_HeightLarge)) {
        static char filter[64] = {};
        if (ImGui::IsWindowAppearing()) {
            filter[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "Search fonts", filter, sizeof filter);
        if (ImGui::Selectable(default_label, family.empty())) {
            family.clear();
            changed = true;
        }
        int shown = 0;
        for (const auto& f : font_families()) {
            if (filter[0] != '\0') {
                std::string a = f.name, q = filter;
                for (auto& c : a)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                for (auto& c : q)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (a.find(q) == std::string::npos)
                    continue;
            }
            if (++shown > 200) {
                ImGui::TextDisabled("Showing 200: type to narrow the list");
                break;
            }
            if (ImGui::Selectable(f.name.c_str(), _stricmp(f.name.c_str(), family.c_str()) == 0)) {
                family = f.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// A row of choices, one selected (herdr's segment control).
bool segmented(App::State& s, const char* id, std::string& value,
               std::initializer_list<std::pair<const char*, const char*>> options) {
    bool changed = false;
    ImGui::PushID(id);
    const float bw = px(78), bh = px(28);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + bw * static_cast<float>(options.size()) + px(4), a.y + bh + px(4)), raised_color(), px(7));
    float x = a.x + px(2);
    for (const auto& [key, label] : options) {
        ImGui::SetCursorScreenPos(ImVec2(x, a.y + px(2)));
        if (ImGui::InvisibleButton(key, ImVec2(bw, bh)) && value != key) {
            value = key;
            changed = true;
        }
        const bool on = value == key;
        const bool hov = ImGui::IsItemHovered();
        if (on || hov)
            dl->AddRectFilled(ImVec2(x, a.y + px(2)), ImVec2(x + bw, a.y + px(2) + bh), on ? g_pal->selected : g_pal->hover, px(6));
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, label);
        dl->AddText(s.fonts.ui, fpx(12), ImVec2(x + (bw - t.x) * 0.5f, a.y + px(2) + (bh - t.y) * 0.5f),
                    on ? g_pal->text : g_pal->dim, label);
        x += bw;
    }
    ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + bh + px(6)));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopID();
    return changed;
}

// herdr frame/settings/main.js 242-270: a small drawing of each layout in the theme's colors.
// main: a 42 px sidebar of lines, a tab strip (accent + two raised), two terminals side by side.
// terminals: its own dark look, a bar with one tab and two cards. Anything else: a plain frame.
void layout_preview(ImDrawList* d, const std::string& name, ImVec2 a, ImVec2 size) {
    const ImVec2 b(a.x + size.x, a.y + size.y);
    auto line = [&](float x, float y, float w, ImU32 c) {
        d->AddRectFilled(ImVec2(x, y), ImVec2(x + px(w), y + px(3)), c, px(2));
    };
    auto lines = [&](float x, float y, int n, ImU32 c) {
        for (int i = 0; i < n; ++i)
            line(x + px(5), y + px(5) + static_cast<float>(i) * px(7), i % 2 ? 22.0f : 32.0f, c);
    };
    auto term = [&](ImVec2 p, ImVec2 q) {
        d->AddRectFilled(p, q, g_pal->bg, px(2));
        d->AddRect(p, q, g_pal->border, px(2));
    };
    if (name == "terminals") {
        const ImU32 bg = IM_COL32(0x13, 0x16, 0x19, 255);
        d->AddRectFilled(a, b, bg, px(6));
        d->AddRect(a, b, g_pal->border, px(6));
        const ImVec2 in(a.x + px(4), a.y + px(4));
        const float iw = size.x - px(8);
        d->AddRectFilled(in, ImVec2(in.x + iw, in.y + px(10)), IM_COL32(0x1d, 0x1f, 0x22, 255), px(4));
        d->AddRectFilled(ImVec2(in.x + px(1), in.y + px(1)), ImVec2(in.x + px(31), in.y + px(9)),
                         IM_COL32(0x34, 0x37, 0x3c, 255), px(3));
        d->AddRect(ImVec2(in.x + px(1), in.y + px(1)), ImVec2(in.x + px(31), in.y + px(9)),
                   IM_COL32(0x4a, 0x4d, 0x53, 255), px(3));
        const float top = in.y + px(14);
        const float cw = (iw - px(4)) * 0.5f;
        for (int i = 0; i < 2; ++i) {
            const ImVec2 p(in.x + static_cast<float>(i) * (cw + px(4)), top);
            const ImVec2 q(p.x + cw, b.y - px(4));
            d->AddRectFilled(p, q, IM_COL32(0x17, 0x19, 0x1c, 255), px(4));
            d->AddRect(p, q, IM_COL32(0x2c, 0x2f, 0x34, 255), px(4));
            d->AddLine(ImVec2(p.x, p.y + px(7)), ImVec2(q.x, p.y + px(7)), IM_COL32(0x25, 0x28, 0x2c, 255));
            line(p.x + px(3), p.y + px(2), 14, IM_COL32(0x5d, 0x61, 0x68, 255));
        }
        return;
    }
    d->AddRectFilled(a, b, g_pal->sidebar, px(6));
    if (name == "workspace") {
        const float sx = a.x + px(42);
        d->AddLine(ImVec2(sx, a.y), ImVec2(sx, b.y), g_pal->border);
        lines(a.x, a.y, 4, g_pal->dim);
        const float ty = a.y + px(11);
        d->AddLine(ImVec2(sx, ty), ImVec2(b.x, ty), g_pal->border);
        float x = sx + px(2);
        d->AddRectFilled(ImVec2(x, a.y + px(2)), ImVec2(x + px(20), ty - px(2)), g_pal->focus, px(2));
        x += px(22);
        for (int i = 0; i < 2; ++i, x += px(18))
            d->AddRectFilled(ImVec2(x, a.y + px(2)), ImVec2(x + px(16), ty - px(2)), raised_color(), px(2));
        const float tw = (b.x - sx - px(8) - px(3)) * 0.5f;
        term(ImVec2(sx + px(4), ty + px(4)), ImVec2(sx + px(4) + tw, b.y - px(4)));
        term(ImVec2(sx + px(4) + tw + px(3), ty + px(4)), ImVec2(b.x - px(4), b.y - px(4)));
    } else {
        const float ty = a.y + px(11);
        d->AddLine(ImVec2(a.x, ty), ImVec2(b.x, ty), g_pal->border);
        term(ImVec2(a.x + px(4), ty + px(4)), ImVec2(b.x - px(4), b.y - px(4)));
    }
    d->AddRect(a, b, g_pal->border, px(6));
}

// ---- herdr settings main.js helpers used by Updates and Privacy
// main.js 180-192: px 12, py 5, rounded 7, a 1 px border, raised, 13 px text, hover background.
ImVec2 settings_button_size(App::State& s, const char* label, ImFont* font) {
    const ImVec2 t = (font != nullptr ? font : s.fonts.ui)->CalcTextSizeA(fpx(13), FLT_MAX, 0, label);
    return ImVec2(t.x + px(24) + 2.0f, std::round(fpx(13) * 1.3f) + px(10) + 2.0f);
}

bool settings_button(App::State& s, const char* id, const char* label) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 size = settings_button_size(s, label);
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(a.x + size.x, a.y + size.y);
    dl->AddRectFilled(a, b, hov ? g_pal->hover : raised_color(), px(7));
    dl->AddRect(a, b, g_pal->border, px(7));
    const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, label);
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + (size.x - t.x) * 0.5f, a.y + (size.y - t.y) * 0.5f), g_pal->text, label);
    return clicked;
}

namespace {
// main.js 787-788 primary(): px 12, py 5, rounded 7, accent fill, accent_foreground semibold 13,
// hover opacity 0.9.
bool settings_primary(App::State& s, const char* id, const char* label) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 size = settings_button_size(s, label, s.fonts.ui_bold);
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + size.x, a.y + size.y), hov ? alpha(g_pal->focus, 230) : g_pal->focus, px(7));
    const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(13), FLT_MAX, 0, label);
    dl->AddText(s.fonts.ui_bold, fpx(13), ImVec2(a.x + (size.x - t.x) * 0.5f, a.y + (size.y - t.y) * 0.5f),
                hov ? alpha(accent_foreground(), 230) : accent_foreground(), label);
    return clicked;
}

// main.js 779, 781: a button's place taken by text (px 12, py 5, 13 px).
void settings_chip(App::State& s, const char* label, ImU32 color) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 size = settings_button_size(s, label);
    ImGui::Dummy(size);
    const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, label);
    ImGui::GetWindowDrawList()->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + px(12) + 1.0f, a.y + (size.y - t.y) * 0.5f),
                                        color, label);
}

}  // namespace

// main.js 225-238: 36 x 20, rounded 10, padding 2; accent when on (knob at the end), hover
// colour when off; a 16 px knob, accent_foreground on, muted off.
bool settings_toggle(const char* id, bool on) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 size(px(36), px(20));
    const bool clicked = ImGui::InvisibleButton(id, size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + size.x, a.y + size.y), on ? g_pal->focus : g_pal->hover, px(10));
    const float r = px(8);
    const float cx = on ? a.x + size.x - px(2) - r : a.x + px(2) + r;
    dl->AddCircleFilled(ImVec2(cx, a.y + size.y * 0.5f), r, on ? accent_foreground() : g_pal->dim);
    return clicked;
}

// main.js 757, 815: a dim note under the rows (pt 12, 12.5 px, 18 px lines).
void settings_footnote(App::State& s, const char* text) {
    ImGui::Dummy(ImVec2(0, px(12)));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* t = text;
    const char* end = text + std::strlen(text);
    float y = p.y;
    while (t < end) {
        const char* brk = s.fonts.ui->CalcWordWrapPosition(fpx(12.5f), t, end, w);
        if (brk == t)
            brk = t + 1;
        dl->AddText(s.fonts.ui, fpx(12.5f), ImVec2(p.x, y), g_pal->faint, t, brk);
        y += px(18);
        t = brk;
        while (t < end && (*t == ' ' || *t == '\n'))
            ++t;
    }
    ImGui::Dummy(ImVec2(w, y - p.y));
}

namespace {
// main.js 764-767: local HH:MM.
std::string clock_at(std::uint64_t secs) {
    const std::time_t t = static_cast<std::time_t>(secs);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return buf;
}

// main.js 847: new Date(secs * 1000).toLocaleString(): the date and time as Windows writes them.
std::string local_date_time(std::uint64_t secs) {
    ULARGE_INTEGER u{};
    u.QuadPart = (secs + 11644473600ull) * 10000000ull;
    FILETIME ft{u.LowPart, u.HighPart};
    FILETIME local{};
    SYSTEMTIME st{};
    ::FileTimeToLocalFileTime(&ft, &local);
    ::FileTimeToSystemTime(&local, &st);
    wchar_t d[64]{};
    wchar_t t[64]{};
    ::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, d, 64, nullptr);
    ::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, nullptr, t, 64);
    return win::to_utf8(std::wstring(d) + L", " + t);
}

}  // namespace

// ---- Updates (herdr settings main.js 760-816). True: the settings dialog should close.
bool settings_updates(App::State& s) {
    bool close = false;
    const auto u = ui::updates::status();
    const std::string checked = u.checked ? " Checked at " + clock_at(*u.checked) + "." : std::string();
    const std::string result = u.checking ? std::string("Checking\xE2\x80\xA6")
                               : u.checked ? "Up to date." + checked
                                           : std::string("No successful check. Check your connection and try again.");
    // Pressed: "Checking..." while it looks, then "Up to date" for a moment (a find changes the
    // row instead). One per row.
    const bool asked =
        s.updates_asked && Clock::now() - *s.updates_asked < std::chrono::milliseconds(3000);
    enum class Control { kNone, kCheck, kRestart, kRetry, kUpdateCore };
    // The check control's look (main.js 775-786).
    const auto check_w = [&]() -> float {
        if (u.checking && s.updates_asked)
            return settings_button_size(s, "Checking\xE2\x80\xA6").x;
        if (asked && u.checked && !u.checking)
            return settings_button_size(s, "\xE2\x9C\x93 Up to date").x;
        return settings_button_size(s, "Check now").x;
    };
    const auto draw_check = [&](const char* id) {
        if (u.checking && s.updates_asked) {
            settings_chip(s, "Checking\xE2\x80\xA6", g_pal->faint);
        } else if (asked && u.checked && !u.checking) {
            settings_chip(s, "\xE2\x9C\x93 Up to date", g_pal->done);  // t.ansi2
        } else if (settings_button(s, id, "Check now")) {
            s.updates_asked = Clock::now();
            run_command(s, "app.check_updates");
        }
    };
    // main.js 789-793
    std::string app_text;
    Control app_control = Control::kNone;
    if (!u.app_updates) {
        app_text = "A build from source: it updates with git, not here.";
    } else if (u.app_ready) {
        app_text = *u.app_ready + " is ready. Restart to switch now; it also switches when you quit.";
        app_control = Control::kRestart;
    } else {
        app_text = result;
        app_control = u.checks ? Control::kCheck : Control::kNone;
    }
    // main.js 794-805
    const auto& c = u.core;
    std::string core_text;
    Control core_control = Control::kNone;
    if (c.managed) {
        core_text = "Managed by " + *c.managed + ": update it there." + (c.ready ? " " + *c.ready + " is available." : "");
        core_control = u.checks ? Control::kCheck : Control::kNone;
    } else if (c.updating) {
        core_text = "Updating\xE2\x80\xA6 your terminals keep running.";
    } else if (c.error) {
        core_text = "The update failed: " + *c.error;
        core_control = Control::kRetry;
    } else if (c.ready) {
        core_text = *c.ready + " is ready. Your terminals keep running through it.";
        core_control = Control::kUpdateCore;
    } else if (u.checks) {
        core_text = result;
        core_control = Control::kCheck;
    } else {
        core_text = "Not checked: a build from source looks for no updates.";
    }
    const auto control_w = [&](Control k) -> float {
        switch (k) {
            case Control::kCheck:
                return check_w();
            case Control::kRestart:
                return settings_button_size(s, "Restart to update", s.fonts.ui_bold).x;
            case Control::kRetry:
                return settings_button_size(s, "Try again", s.fonts.ui_bold).x;
            case Control::kUpdateCore:
                return settings_button_size(s, "Update agent-host", s.fonts.ui_bold).x;
            case Control::kNone:
                break;
        }
        return 0.0f;
    };
    const auto draw_control = [&](Control k, const char* check_id) {
        switch (k) {
            case Control::kCheck:
                draw_check(check_id);
                break;
            case Control::kRestart:
                if (settings_primary(s, "##st-app-update", "Restart to update"))
                    run_command(s, "app.update");
                break;
            case Control::kRetry:
                if (settings_primary(s, "##st-core-retry", "Try again"))
                    run_command(s, "app.update_core");
                break;
            case Control::kUpdateCore:
                if (settings_primary(s, "##st-core-update", "Update agent-host"))
                    run_command(s, "app.update_core");
                break;
            case Control::kNone:
                break;
        }
    };
    // main.js 806-811: heading "Herdr" (the product), "Herdr <version>", What's new + the control.
    settings_section(s, "AgentDesk");
    {
        const float wn = settings_button_size(s, "What's new").x;
        const float extra = control_w(app_control);
        const std::string title = "AgentDesk " + u.app_version;
        settings_row(s, title.c_str(), app_text.c_str(), wn + (extra > 0 ? px(8) + extra : 0.0f));
        if (settings_button(s, "##st-whats-new", "What's new")) {
            close = true;  // main.js 810: closeOverlay, then app.whats_new
            run_command(s, "app.whats_new");
        }
        if (extra > 0) {
            ImGui::SameLine(0, px(8));
            draw_control(app_control, "##st-check-app");
        }
        settings_row_done(s);
    }
    // main.js 812-814: 18 px, heading "herdr-core", "herdr-core <version>".
    ImGui::Dummy(ImVec2(0, px(18)));
    settings_section(s, "agent-host");
    {
        const std::string title = "agent-host " + c.version.value_or("");
        settings_row(s, title.c_str(), core_text.c_str(), control_w(core_control));
        draw_control(core_control, "##st-check-core");
        settings_row_done(s);
    }
    // main.js 815
    settings_footnote(s, "AgentDesk looks for both every hour and tells you in the corner. agent-host runs your "
                         "terminals, so they keep running through its updates.");
    return close;
}

namespace {
// main.js 821-842: a report as it goes out, one line per line of JSON.
void settings_report(App::State& s, const std::string& title, const std::string& about,
                     const ui::telemetry::Json& report) {
    if (s.report_font == nullptr)
        s.report_font = ui::load_terminal_font(L"", 12.0f);  // the bundled JetBrains Mono
    ImFont* mono = s.report_font != nullptr ? s.report_font : s.fonts.mono;
    const std::string text = report.dump(2, ' ', false, ui::telemetry::Json::error_handler_t::replace);
    ImGui::Dummy(ImVec2(0, px(14) - ImGui::GetStyle().ItemSpacing.y));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddText(s.fonts.ui, fpx(13.5f), p, g_pal->text, title.c_str());
    p.y += std::round(fpx(13.5f) * 1.3f) + px(4);
    if (!about.empty()) {
        dl->AddText(s.fonts.ui, fpx(12.5f), p, g_pal->faint, about.c_str());
        p.y += std::round(fpx(12.5f) * 1.3f) + px(4);
    }
    p.y += px(4);  // mt 4
    int lines = 1;
    for (const char ch : text)
        lines += ch == '\n' ? 1 : 0;
    const float h = px(10) * 2 + px(17) * static_cast<float>(lines);
    const ImVec2 b(p.x + w, p.y + h);
    dl->AddRectFilled(p, b, g_pal->bg, px(8));
    dl->AddRect(p, b, g_pal->border, px(8));
    dl->PushClipRect(ImVec2(p.x + 1, p.y + 1), ImVec2(b.x - 1, b.y - 1), true);
    float y = p.y + px(10);
    for (std::size_t at = 0; at <= text.size();) {
        const auto nl = text.find('\n', at);
        const std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        dl->AddText(mono, fpx(12), ImVec2(p.x + px(10), y + (px(17) - fpx(12)) * 0.5f), g_pal->dim,
                    line.empty() ? " " : line.c_str());
        y += px(17);
        if (nl == std::string::npos)
            break;
        at = nl + 1;
    }
    dl->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(p.x, b.y));
    ImGui::Dummy(ImVec2(w, 0));
}

}  // namespace

// ---- Privacy (herdr settings main.js 844-882). True: ui.json changed.
bool settings_privacy(App::State& s) {
    bool changed = false;
    const ui::telemetry::Json st = ui::telemetry::status();
    const bool have = st.is_object();
    const bool on = have ? st.value("setting", false) : s.settings.telemetry;
    std::string why;
    if (have && st.contains("off_because") && st["off_because"].is_string())
        why = "Off for now: " + st["off_because"].get<std::string>() + " is set where AgentDesk started.";
    settings_section(s, "Usage data");
    settings_row(s, "Share anonymous usage data",
                 !why.empty() ? why.c_str()
                              : "Once a day: which agents and features you use, how many spaces and tabs, your system "
                                "and AgentDesk's version. Never what's in your terminals, never names of files, "
                                "folders or projects.",
                 px(36));
    if (settings_toggle("##st-telemetry", on)) {
        // main.js 858-861: on -> "telemetry": false; off -> the key goes.
        s.settings.telemetry = !on;
        changed = true;
        ui::telemetry::set_setting(s.settings.telemetry);  // window.rs 955: read again on change
    }
    settings_row_done(s);
    const bool sends = have && st.value("sends", false);
    const char* label = s.show_data ? "Hide" : "Show";
    settings_row(s, "What we send",
                 have && !sends ? "This build never sends: it's built from source. You can still see what a release "
                                  "would send."
                                : "Exactly what leaves your computer, word for word.",
                 settings_button_size(s, label).x);
    if (settings_button(s, "##st-telemetry-show", label))
        s.show_data = !s.show_data;
    settings_row_done(s);
    if (!s.show_data)
        return changed;
    if (!have || !st.value("enabled", false)) {
        ImGui::Dummy(ImVec2(0, px(14) - ImGui::GetStyle().ItemSpacing.y));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText(s.fonts.ui, fpx(13), p, g_pal->faint,
                                            "Nothing is collected or sent while it's off.");
        ImGui::Dummy(ImVec2(0, std::round(fpx(13) * 1.3f)));
        return changed;
    }
    // main.js 879-881
    if (const auto q = st.find("queued"); q != st.end() && q->is_array())
        for (const auto& r : *q)
            if (r.is_object())
                settings_report(s, "Waiting to send: " + safe::string_or(r, "day"), "Sent as is, soon.", r);
    if (const auto d = st.find("draft"); d != st.end() && d->is_object())
        settings_report(s, "Today so far", "A draft: it changes until the day ends (UTC), then goes out once.", *d);
    if (const auto l = st.find("last_sent"); l != st.end() && l->is_object() && l->contains("report")) {
        const auto& r = (*l)["report"];
        const std::uint64_t at = l->contains("at") && (*l)["at"].is_number_unsigned() ? (*l)["at"].get<std::uint64_t>() : 0;
        settings_report(s, "Last sent: " + safe::string_or(r, "day"),
                        "Sent " + local_date_time(at) + ".", r);
    }
    return changed;
}
}  // namespace detail

}  // namespace agentdesk::desktop
