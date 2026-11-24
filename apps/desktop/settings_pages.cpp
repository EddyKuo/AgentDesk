// Settings dialog building blocks (rows, buttons, steppers, toggles, font pickers, segmented
// controls, layout previews) and the Updates and Privacy pages.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "overlay_kit.hpp"

namespace agentdesk::desktop {
namespace detail {

// The settings body has no ImGui item spacing: every gap below is explicit. Sizes are pixels
// at scale 1 (px / fpx scale them).

// Line height of `size` px text: 1.3 times the size, rounded.
float line_h(float size) {
    return std::round(fpx(size) * 1.3f);
}

void settings_gap(float h) {
    ImGui::Dummy(ImVec2(0, std::max(0.0f, h)));
}

// Text wrapped to the body's width, each line centred in a line `lh` high (12.5 px text on
// 18 px lines unless given).
void settings_wrapped(App::State& s, const char* text, ImU32 color, float size, float lh, ImFont* font) {
    ImFont* f = font != nullptr ? font : s.fonts.ui;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = p.y;
    for_each_wrapped_line(f, fpx(size), text, w, [&](const char* a, const char* b) {
        const float th = f->CalcTextSizeA(fpx(size), FLT_MAX, 0, a, b).y;
        dl->AddText(f, fpx(size), ImVec2(p.x, y + (px(lh) - th) * 0.5f), color, a, b);
        y += px(lh);
    });
    ImGui::Dummy(ImVec2(w, y - p.y));
}

// A section heading: 12 px semibold, dim, upper case; 6 px above, 8 px below.
void settings_section(App::State& s, const char* title) {
    std::string up;
    for (const char* c = title; *c != 0; ++c)
        up += static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(s.fonts.ui_bold, fpx(12), ImVec2(p.x, p.y + px(6)), g_pal->faint,
                                        up.c_str());
    settings_gap(px(6) + line_h(12) + px(8));
}

// A settings row: 12 px above and below, a 1 px bottom border; the label (14 px) over an
// optional note (12.5 px dim, 18 px lines) on the left, the control on the right, both centred
// vertically. A negative `control_h` means a settings button. Call the control right after:
// the cursor is placed for it. settings_row_done() then moves below the row.
void settings_row(App::State& s, const char* label, const char* note, float control_w, float control_h) {
    if (control_h < 0)
        control_h = std::round(fpx(13) * 1.3f) + px(10) + 2.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float wrap = std::max(px(80), w - control_w - px(16));
    std::vector<std::pair<const char*, const char*>> lines;
    if (note != nullptr && *note != 0)
        for_each_wrapped_line(s.fonts.ui, fpx(12.5f), note, wrap,
                              [&](const char* a, const char* b) { lines.emplace_back(a, b); });
    const float text_h =
        line_h(14) + (lines.empty() ? 0.0f : px(3) + px(18) * static_cast<float>(lines.size()));
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

bool stepper(App::State& s, const char* id, float& v, float step, float lo, float hi, const char* fmt,
             float scale) {
    bool changed = false;
    ImGui::PushID(id);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + px(kStepperW), a.y + px(kStepperH));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, raised_color(), px(7));
    // One 28 x 28 cell: a click moves the value one step, snapped to the step and clamped.
    auto cell = [&](const char* key, const char* label, float x, float dir) {
        const ImVec2 c(x, a.y + px(1));
        ImGui::SetCursorScreenPos(c);
        if (ImGui::InvisibleButton(key, ImVec2(px(28), px(28)))) {
            v = std::clamp(std::round((v + dir * step) / step) * step, lo, hi);
            changed = true;
        }
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(c, ImVec2(c.x + px(28), c.y + px(28)), g_pal->hover, px(6),
                              dir < 0 ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersRight);
        text_centered(dl, s.fonts.ui, fpx(15), c, ImVec2(px(28), px(28)), hov ? g_pal->text : g_pal->dim,
                      label);
    };
    cell("##minus", "\xE2\x88\x92", a.x + px(1), -1.0f);
    char buf[32];
    std::snprintf(buf, sizeof buf, fmt, v * scale);
    text_centered(dl, s.fonts.ui, fpx(13), ImVec2(a.x + px(29), a.y), ImVec2(px(54), px(kStepperH)),
                  g_pal->text, buf);
    cell("##plus", "+", a.x + px(29) + px(54), 1.0f);
    dl->AddRect(a, b, g_pal->border, px(7));
    ImGui::SetCursorScreenPos(a);
    ImGui::Dummy(ImVec2(px(kStepperW), px(kStepperH)));
    ImGui::PopID();
    return changed;
}

namespace {
// Every font family installed on this computer, read once.
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

// Terminal font: the chosen family (empty: the bundled JetBrains Mono) at the chosen weight, and
// its 700 face for bold.
void apply_terminal_font(App::State& s) {
    const auto* f = find_family(s.settings.terminal_font.empty() ? std::string("JetBrains Mono")
                                                                 : s.settings.terminal_font);
    ImFont* mono =
        f != nullptr ? ui::load_font_file(f->file_for(s.settings.terminal_weight), s.settings.font_size, true)
                     : nullptr;
    s.fonts.mono = mono != nullptr ? mono : ui::load_terminal_font(L"", s.settings.font_size);
    ImFont* bold = f != nullptr ? ui::load_font_file(f->file_for(700), s.settings.font_size, true) : nullptr;
    s.fonts.mono_bold = bold != nullptr ? bold : s.fonts.mono;
}

// Interface font: the chosen family (empty: the bundled Geist, kept from startup) at 400, 500
// and 600.
void apply_ui_font(App::State& s) {
    if (s.base_ui == nullptr) {  // the bundled set, loaded once at startup
        s.base_ui = s.fonts.ui;
        s.base_ui_medium = s.fonts.ui_medium;
        s.base_ui_bold = s.fonts.ui_bold;
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

// A font family combo: a filter box, the default entry, then up to 200 matching families (a
// computer can have thousands).
bool font_picker(App::State& /*s*/, const char* id, std::string& family, const char* default_label,
                 float width) {
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
        const std::string query = ascii_lower(filter);
        int shown = 0;
        for (const auto& f : font_families()) {
            if (!query.empty() && ascii_lower(f.name).find(query) == std::string::npos)
                continue;
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

// A row of 78 x 28 choices on a raised strip; the chosen one is highlighted.
bool segmented(App::State& s, const char* id, std::string& value,
               std::initializer_list<std::pair<const char*, const char*>> options) {
    bool changed = false;
    ImGui::PushID(id);
    const float bw = px(78), bh = px(28);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + bw * static_cast<float>(options.size()) + px(4), a.y + bh + px(4)),
                      raised_color(), px(7));
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
            dl->AddRectFilled(ImVec2(x, a.y + px(2)), ImVec2(x + bw, a.y + px(2) + bh),
                              on ? g_pal->selected : g_pal->hover, px(6));
        text_centered(dl, s.fonts.ui, fpx(12), ImVec2(x, a.y + px(2)), ImVec2(bw, bh),
                      on ? g_pal->text : g_pal->dim, label);
        x += bw;
    }
    ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + bh + px(6)));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopID();
    return changed;
}

// A small drawing of a layout in the current theme's colors for its Settings card.
// workspace: a 42 px sidebar of lines, a tab strip (one accent tab, two raised), two terminals
// side by side. terminals: its own fixed dark look, a bar with one tab over two cards. Anything
// else: a plain frame with one terminal.
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

// A settings button: 13 px text with 12 px of padding each side and 5 px above and below, plus
// a 1 px border; rounded 7, raised, the hover colour under the mouse.
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
    text_centered(dl, s.fonts.ui, fpx(13), a, size, g_pal->text, label);
    return clicked;
}

namespace {
// The primary settings button: the settings button's size in semibold, an accent fill and no
// border; 90 % opacity under the mouse.
bool settings_primary(App::State& s, const char* id, const char* label) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 size = settings_button_size(s, label, s.fonts.ui_bold);
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + size.x, a.y + size.y), hov ? alpha(g_pal->focus, 230) : g_pal->focus,
                      px(7));
    text_centered(dl, s.fonts.ui_bold, fpx(13), a, size,
                  hov ? alpha(accent_foreground(), 230) : accent_foreground(), label);
    return clicked;
}

// Text in a settings button's place (the same size, no frame), for a state that is not an action.
void settings_chip(App::State& s, const char* label, ImU32 color) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 size = settings_button_size(s, label);
    ImGui::Dummy(size);
    const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, label);
    ImGui::GetWindowDrawList()->AddText(
        s.fonts.ui, fpx(13), ImVec2(a.x + px(12) + 1.0f, a.y + (size.y - t.y) * 0.5f), color, label);
}

}  // namespace

// An on/off switch, 36 x 20, rounded 10: accent with the 16 px knob at the end when on, the hover
// colour with a dim knob at the start when off. Returns true when clicked.
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

// A dim note under a page's rows: 12 px above, 12.5 px text on 18 px lines.
void settings_footnote(App::State& s, const char* text) {
    ImGui::Dummy(ImVec2(0, px(12)));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = p.y;
    for_each_wrapped_line(s.fonts.ui, fpx(12.5f), text, w, [&](const char* a, const char* b) {
        dl->AddText(s.fonts.ui, fpx(12.5f), ImVec2(p.x, y), g_pal->faint, a, b);
        y += px(18);
    });
    ImGui::Dummy(ImVec2(w, y - p.y));
}

// ------------------------------------------------------------------ Updates page
namespace {
constexpr const char* kChecking = "Checking\xE2\x80\xA6";
constexpr const char* kUpToDate = "\xE2\x9C\x93 Up to date";

// Local time of day as HH:MM.
std::string clock_at(std::uint64_t secs) {
    const std::time_t t = static_cast<std::time_t>(secs);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return buf;
}

// Unix seconds as the date and time in the person's own format.
std::string local_date_time(std::uint64_t secs) {
    return os::local_date_time(secs);
}

enum class UpdateControl { kNone, kCheck, kRestart, kRetry, kUpdateCore };

// One row of the Updates page: what it says and the control at its right.
struct UpdateRow {
    std::string text;
    UpdateControl control = UpdateControl::kNone;
};

// The outcome of the last check, in words.
std::string check_result(const ui::updates::Status& u) {
    const std::string checked = u.checked ? " Checked at " + clock_at(*u.checked) + "." : std::string();
    return u.checking  ? std::string(kChecking)
           : u.checked ? "Up to date." + checked
                       : std::string("No successful check. Check your connection and try again.");
}

UpdateRow app_update_row(const ui::updates::Status& u, const std::string& result) {
    if (!u.app_updates)
        return {"A build from source: it updates with git, not here.", UpdateControl::kNone};
    if (u.app_ready)
        return {*u.app_ready + " is ready. Restart to switch now; it also switches when you quit.",
                UpdateControl::kRestart};
    return {result, u.checks ? UpdateControl::kCheck : UpdateControl::kNone};
}

UpdateRow core_update_row(const ui::updates::Status& u, const std::string& result) {
    const auto& c = u.core;
    if (c.managed)
        return {"Managed by " + *c.managed + ": update it there." +
                    (c.ready ? " " + *c.ready + " is available." : ""),
                u.checks ? UpdateControl::kCheck : UpdateControl::kNone};
    if (c.updating)
        return {"Updating\xE2\x80\xA6 your terminals keep running.", UpdateControl::kNone};
    if (c.error)
        return {"The update failed: " + *c.error, UpdateControl::kRetry};
    if (c.ready)
        return {*c.ready + " is ready. Your terminals keep running through it.", UpdateControl::kUpdateCore};
    if (u.checks)
        return {result, UpdateControl::kCheck};
    return {"Not checked: a build from source looks for no updates.", UpdateControl::kNone};
}

// Measures and draws the controls at the right of the Updates rows.
struct UpdateControls {
    App::State& s;
    const ui::updates::Status& u;
    bool just_asked;  // "Check now" was pressed within the last 3 s (App::tick ends it)

    // A check the person asked for shows "Checking…" while it runs, then "✓ Up to date" for a
    // moment; otherwise the "Check now" button.
    const char* check_chip() const {
        if (u.checking && s.updates_asked)
            return kChecking;
        if (just_asked && u.checked && !u.checking)
            return kUpToDate;
        return nullptr;
    }

    float width(UpdateControl k) const {
        switch (k) {
            case UpdateControl::kCheck: {
                const char* chip = check_chip();
                return settings_button_size(s, chip != nullptr ? chip : "Check now").x;
            }
            case UpdateControl::kRestart:
                return settings_button_size(s, "Restart to update", s.fonts.ui_bold).x;
            case UpdateControl::kRetry:
                return settings_button_size(s, "Try again", s.fonts.ui_bold).x;
            case UpdateControl::kUpdateCore:
                return settings_button_size(s, "Update agent-host", s.fonts.ui_bold).x;
            case UpdateControl::kNone:
                break;
        }
        return 0.0f;
    }

    void draw(UpdateControl k, const char* check_id) const {
        switch (k) {
            case UpdateControl::kCheck:
                if (const char* chip = check_chip()) {
                    settings_chip(s, chip, chip == kUpToDate ? g_pal->done : g_pal->faint);
                } else if (settings_button(s, check_id, "Check now")) {
                    s.updates_asked = Clock::now();
                    run_command(s, "app.check_updates");
                }
                break;
            case UpdateControl::kRestart:
                if (settings_primary(s, "##st-app-update", "Restart to update"))
                    run_command(s, "app.update");
                break;
            case UpdateControl::kRetry:
                if (settings_primary(s, "##st-core-retry", "Try again"))
                    run_command(s, "app.update_core");
                break;
            case UpdateControl::kUpdateCore:
                if (settings_primary(s, "##st-core-update", "Update agent-host"))
                    run_command(s, "app.update_core");
                break;
            case UpdateControl::kNone:
                break;
        }
    }
};

}  // namespace

// Two sections, AgentDesk itself and agent-host, each one row with its version, its state and a
// control. Returns true when the settings dialog should close (What's new opens over it).
bool settings_updates(App::State& s) {
    bool close = false;
    const auto u = ui::updates::status();
    const UpdateControls controls{
        s, u, s.updates_asked && Clock::now() - *s.updates_asked < std::chrono::milliseconds(3000)};
    const std::string result = check_result(u);
    const UpdateRow app = app_update_row(u, result);
    const UpdateRow core = core_update_row(u, result);
    settings_section(s, "AgentDesk");
    {
        const float whats_new_w = settings_button_size(s, "What's new").x;
        const float extra = controls.width(app.control);
        const std::string title = "AgentDesk " + u.app_version;
        settings_row(s, title.c_str(), app.text.c_str(), whats_new_w + (extra > 0 ? px(8) + extra : 0.0f));
        if (settings_button(s, "##st-whats-new", "What's new")) {
            close = true;
            run_command(s, "app.whats_new");
        }
        if (extra > 0) {
            ImGui::SameLine(0, px(8));
            controls.draw(app.control, "##st-check-app");
        }
        settings_row_done(s);
    }
    ImGui::Dummy(ImVec2(0, px(18)));
    settings_section(s, "agent-host");
    {
        const std::string title = "agent-host " + u.core.version.value_or("");
        settings_row(s, title.c_str(), core.text.c_str(), controls.width(core.control));
        controls.draw(core.control, "##st-check-core");
        settings_row_done(s);
    }
    settings_footnote(s,
                      "AgentDesk looks for both every hour and tells you in the corner. agent-host runs your "
                      "terminals, so they keep running through its updates.");
    return close;
}

// ------------------------------------------------------------------ Privacy page
namespace {
// A usage report exactly as it goes out: a title, a dim line about it, then the JSON pretty
// printed in a bordered box, 12 px monospace on 17 px lines.
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
    p.y += px(4);
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

// Under "What we send" when shown: every report waiting to go, today's draft and the last one
// sent; or a line saying nothing is collected while sharing is off.
void privacy_reports(App::State& s, const ui::telemetry::Json& st) {
    const bool have = st.is_object();
    if (!have || !st.value("enabled", false)) {
        ImGui::Dummy(ImVec2(0, px(14) - ImGui::GetStyle().ItemSpacing.y));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText(s.fonts.ui, fpx(13), p, g_pal->faint,
                                            "Nothing is collected or sent while it's off.");
        ImGui::Dummy(ImVec2(0, std::round(fpx(13) * 1.3f)));
        return;
    }
    if (const auto q = st.find("queued"); q != st.end() && q->is_array())
        for (const auto& r : *q)
            if (r.is_object())
                settings_report(s, "Waiting to send: " + safe::string_or(r, "day"), "Sent as is, soon.", r);
    if (const auto d = st.find("draft"); d != st.end() && d->is_object())
        settings_report(s, "Today so far",
                        "A draft: it changes until the day ends (UTC), then goes out once.", *d);
    if (const auto l = st.find("last_sent"); l != st.end() && l->is_object() && l->contains("report")) {
        const auto& r = (*l)["report"];
        const std::uint64_t at =
            l->contains("at") && (*l)["at"].is_number_unsigned() ? (*l)["at"].get<std::uint64_t>() : 0;
        settings_report(s, "Last sent: " + safe::string_or(r, "day"), "Sent " + local_date_time(at) + ".", r);
    }
}

}  // namespace

// The usage-data switch and a way to read exactly what is sent. Returns true when ui.json
// changed.
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
                 !why.empty()
                     ? why.c_str()
                     : "Once a day: which agents and features you use, how many spaces and tabs, your system "
                       "and AgentDesk's version. Never what's in your terminals, never names of files, "
                       "folders or projects.",
                 px(36));
    if (settings_toggle("##st-telemetry", on)) {
        s.settings.telemetry = !on;
        changed = true;
        ui::telemetry::set_setting(s.settings.telemetry);  // the reporter reads it again now
    }
    settings_row_done(s);
    const bool sends = have && st.value("sends", false);
    const char* label = s.show_data ? "Hide" : "Show";
    settings_row(s, "What we send",
                 have && !sends
                     ? "This build never sends: it's built from source. You can still see what a release "
                       "would send."
                     : "Exactly what leaves your computer, word for word.",
                 settings_button_size(s, label).x);
    if (settings_button(s, "##st-telemetry-show", label))
        s.show_data = !s.show_data;
    settings_row_done(s);
    if (s.show_data)
        privacy_reports(s, st);
    return changed;
}
// ------------------------------------------------------------------ Keyboard page (macOS)
// The Option keys in terminals: Alt for terminal shortcuts (an ESC prefix), or characters
// (accents, symbols). Choices and wording as in herdr-app's macOS settings.
bool settings_keyboard(App::State& s) {
    static constexpr std::pair<const char*, const char*> kChoices[] = {
        {"auto", "Automatic (Alt on US keyboards)"},
        {"on", "Alt"},
        {"off", "Types characters"},
        {"left", "Left Option is Alt"},
        {"right", "Right Option is Alt"},
    };
    bool changed = false;
    settings_section(s, "Option key");
    ImGui::PushFont(s.fonts.ui, fpx(13));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(g_pal->dim));
    ImGui::TextWrapped("Use Option for terminal shortcuts or to type accents and special characters.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(12)));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = px(10) + line_h(14) + px(10);
    for (const auto& [value, label] : kChoices) {
        const bool on = s.settings.option_as_alt == value;
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::PushID(value);
        if (ImGui::InvisibleButton("##opt", ImVec2(w, h)) && !on) {
            s.settings.option_as_alt = value;
            ui::keyboard::set_option_as_alt(value);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), g_pal->hover, px(7));
        ImGui::PopID();
        const float ty = a.y + (h - line_h(14)) * 0.5f;
        dl->AddText(s.fonts.ui, fpx(14), ImVec2(a.x + px(12), ty), on ? g_pal->focus : g_pal->faint,
                    on ? "\xE2\x97\x89" : "\xE2\x97\x8B");  // ◉ / ○
        dl->AddText(s.fonts.ui, fpx(14), ImVec2(a.x + px(12) + px(24), ty), g_pal->text, label);
    }
    return changed;
}
}  // namespace detail

}  // namespace agentdesk::desktop
