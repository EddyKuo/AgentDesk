// The AgentDesk Classic layout: the whole window drawn as character cells in one colour (a menu
// bar, a tab line, character-framed panes, a status line and an optional text sidebar), with
// optional scan lines, a vignette and a soft glow. AgentDesk's own drawing: the bundled monospace
// font, box characters painted as shapes (sprites), colours from the active theme.
#include "agentdesk/ui/classic_layout.hpp"

#include "agentdesk/ui/sprites.hpp"
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
namespace cl = ui::classic;
namespace menus = ui::menus;

constexpr const char* kMenuPopup = "##classic-menu";

// ------------------------------------------------------------------ the cell grid
// This frame's chrome text: the monospace font at 14 design px (with the interface text size),
// its cells, and whether text glows.
struct Chrome {
    ImFont* font = nullptr;
    ImFont* bold = nullptr;
    float fs = 14.0f;
    cl::Grid g;
    bool glow = false;
};

bool effects_on(const App::State& s) {
    return ui::styles::effects_allowed(s.settings.styles.classic_effects, g_pal->light,
                                       system_high_contrast());
}

bool effects_full(const App::State& s) {
    return effects_on(s) && s.settings.styles.classic_effects == "full";
}

Chrome chrome(const App::State& s) {
    Chrome c;
    c.font = s.fonts.mono;
    c.bold = s.fonts.mono_bold != nullptr ? s.fonts.mono_bold : s.fonts.mono;
    c.fs = fpx(14.0f * 13.0f / 15.0f);  // 14 px at the 15 px interface text, scaling with it
    c.g = cl::grid_for(c.font->GetFontBaked(c.fs)->GetCharAdvance(static_cast<ImWchar>('M')), g_scale);
    c.glow = effects_full(s);
    return c;
}

char32_t next_cp(std::string_view s, std::size_t& i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    const std::size_t len = b0 < 0x80 ? 1 : (b0 & 0xE0) == 0xC0 ? 2 : (b0 & 0xF0) == 0xE0 ? 3 : 4;
    char32_t cp = len == 1 ? b0 : len == 2 ? (b0 & 0x1Fu) : len == 3 ? (b0 & 0x0Fu) : (b0 & 0x07u);
    for (std::size_t m = 1; m < len && i + m < s.size(); ++m)
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + m]) & 0x3Fu);
    i = std::min(s.size(), i + len);
    return cp;
}

// A box or block character as shapes filling its cell, so lines join from cell to cell.
void paint_sprite(ImDrawList* dl, char32_t c, float x, float y, float w, float h, ImU32 col) {
    const float scale = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.x);
    const ui::sprites::Shapes shapes = ui::sprites::paint(c, x, y, w, h, scale);
    for (const auto& r : shapes.rects) {
        const int a = static_cast<int>(static_cast<float>((col >> IM_COL32_A_SHIFT) & 0xFF) * r.alpha);
        dl->AddRectFilled(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1),
                          (col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT), r.rounding);
    }
    for (const auto& p : shapes.paths) {
        dl->PathClear();
        for (const auto& st : p.steps) {
            if (st.kind == ui::sprites::PathStep::Kind::kCubic)
                dl->PathBezierCubicCurveTo(ImVec2(st.c1.x, st.c1.y), ImVec2(st.c2.x, st.c2.y),
                                           ImVec2(st.to.x, st.to.y));
            else
                dl->PathLineTo(ImVec2(st.to.x, st.to.y));
        }
        if (p.fill)
            dl->PathFillConvex(col);
        else
            dl->PathStroke(col, p.closed ? ImDrawFlags_Closed : ImDrawFlags_None, p.thickness);
    }
}

// Draws `text` a cell at a time from cell `col` of the row at (x0, y); wide characters take two
// cells, box characters are painted. Returns the cell after the text.
int put(ImDrawList* dl, const Chrome& c, float x0, float y, int col, std::string_view text, ImU32 color,
        bool bold = false) {
    ImFont* f = bold ? c.bold : c.font;
    const float ty = std::round(y + c.g.cell_h * 0.5f - c.fs * 0.6f);
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t start = i;
        const char32_t cp = next_cp(text, i);
        const int w = ui::is_wide_codepoint(cp) ? 2 : 1;
        const float x = x0 + static_cast<float>(col) * c.g.cell_w;
        if (ui::sprites::is_sprite(cp)) {
            paint_sprite(dl, cp, x, y, c.g.cell_w, c.g.cell_h, color);
        } else if (cp != U' ') {
            const char* a = text.data() + start;
            const char* b = text.data() + i;
            const float adv = f->CalcTextSizeA(c.fs, FLT_MAX, 0, a, b).x;
            const float gx = std::round(x + (static_cast<float>(w) * c.g.cell_w - adv) * 0.5f);
            if (c.glow) {  // a faint halo: four offset copies under the text
                const ImU32 halo = alpha(color, 51);
                for (const ImVec2 d : {ImVec2(-1, 0), ImVec2(1, 0), ImVec2(0, -1), ImVec2(0, 1)})
                    dl->AddText(f, c.fs, ImVec2(gx + d.x, ty + d.y), halo, a, b);
            }
            dl->AddText(f, c.fs, ImVec2(gx, ty), color, a, b);
        }
        col += w;
    }
    return col;
}

// Fills cells [col, col + n) of a row with `fill` (reverse video and highlights).
void fill_cells(ImDrawList* dl, const Chrome& c, float x0, float y, int col, int n, ImU32 fill) {
    dl->AddRectFilled(ImVec2(x0 + static_cast<float>(col) * c.g.cell_w, y),
                      ImVec2(x0 + static_cast<float>(col + n) * c.g.cell_w, y + c.g.cell_h), fill);
}

// A clickable run of cells; returns true when clicked, and whether it is hovered / held.
bool cell_button(const char* id, const Chrome& c, float x0, float y, int col, int n, bool* hov,
                 bool* held = nullptr) {
    ImGui::SetCursorScreenPos(ImVec2(x0 + static_cast<float>(col) * c.g.cell_w, y));
    const bool clicked =
        ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, static_cast<float>(n) * c.g.cell_w), c.g.cell_h));
    *hov = ImGui::IsItemHovered();
    if (held != nullptr)
        *held = ImGui::IsItemActive();
    return clicked;
}

// A frame of box characters on a..b: single, double or dashed lines. Corners take one cell, the
// edges are tiled cell by cell (the last one cut at the corner).
void box(ImDrawList* dl, const Chrome& c, ImVec2 a, ImVec2 b, bool dbl, bool dashed, ImU32 col) {
    const float cw = c.g.cell_w, ch = c.g.cell_h;
    if (b.x - a.x < cw * 2 || b.y - a.y < ch * 2)
        return;
    const char32_t h = dbl ? U'═' : dashed ? U'┄' : U'─';
    const char32_t v = dbl ? U'║' : dashed ? U'┆' : U'│';
    dl->PushClipRect(ImVec2(a.x + cw, a.y), ImVec2(b.x - cw, b.y), true);
    for (float x = a.x + cw; x < b.x - cw; x += cw) {
        paint_sprite(dl, h, x, a.y, cw, ch, col);
        paint_sprite(dl, h, x, b.y - ch, cw, ch, col);
    }
    dl->PopClipRect();
    dl->PushClipRect(ImVec2(a.x, a.y + ch), ImVec2(b.x, b.y - ch), true);
    for (float y = a.y + ch; y < b.y - ch; y += ch) {
        paint_sprite(dl, v, a.x, y, cw, ch, col);
        paint_sprite(dl, v, b.x - cw, y, cw, ch, col);
    }
    dl->PopClipRect();
    paint_sprite(dl, dbl ? U'╔' : U'┌', a.x, a.y, cw, ch, col);
    paint_sprite(dl, dbl ? U'╗' : U'┐', b.x - cw, a.y, cw, ch, col);
    paint_sprite(dl, dbl ? U'╚' : U'└', a.x, b.y - ch, cw, ch, col);
    paint_sprite(dl, dbl ? U'╝' : U'┘', b.x - cw, b.y - ch, cw, ch, col);
}

long long now_ms_steady() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

// A state mark with the working spinner after it: "[~]|".
std::string mark_text(Attention a) {
    std::string m = cl::status_glyph(a);
    if (a == Attention::kWorking)
        m += cl::spinner_frame(now_ms_steady());
    return m;
}

// Draws a state mark at `col`; needs-you is reversed (filled with its colour). Returns the next cell.
int put_mark(ImDrawList* dl, const Chrome& c, float x0, float y, int col, Attention a, ImU32 base) {
    const std::string m = mark_text(a);
    if (a == Attention::kBlocked) {
        fill_cells(dl, c, x0, y, col, 3, g_pal->needs_you);
        return put(dl, c, x0, y, col, m, g_pal->term_bg, true);
    }
    const ImU32 ink = a == Attention::kWorking ? g_pal->working : a == Attention::kDone ? g_pal->done : base;
    return put(dl, c, x0, y, col, m, ink);
}

menus::MenuContext classic_menu_context(App::State& s) {
    menus::MenuContext ctx;
    for (const auto& l : s.layouts)
        ctx.layouts.emplace_back(l.name, ui::styles::display_name(l.name));
    ctx.layout = s.layout().name;
    if (ui::Space* sp = s.ws.active(); sp != nullptr) {
        ctx.space = s.ws.active_space;
        for (int i = 0; i < static_cast<int>(sp->tabs.size()); ++i) {
            const ui::Tab& t = sp->tabs[static_cast<std::size_t>(i)];
            ctx.tabs.push_back({tab_label(s, t, i, s.ws.sessions_in(t)), i == sp->active_tab, t.settled});
        }
    }
    return ctx;
}

// The menus the bar shows: all six, or AGENTDESK and View in a narrow window.
std::vector<menus::Menu> shown_menus(bool all) {
    if (all)
        return {menus::Menu::kApp,  menus::Menu::kFile,   menus::Menu::kEdit,
                menus::Menu::kView, menus::Menu::kWindow, menus::Menu::kHelp};
    return {menus::Menu::kApp, menus::Menu::kView};
}

std::string menu_label(menus::Menu m) {
    std::string t = menus::menu_title(m);
    if (m == menus::Menu::kApp)
        for (char& ch : t)
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return t;
}

// A menu's rows; in a narrow window AGENTDESK also carries the menus that have no title.
std::vector<menus::MenuItem> rows_of(App::State& s, menus::Menu m, bool all) {
    const auto ctx = classic_menu_context(s);
    auto out = menus::menu_items(m, ctx);
    if (!all && m == menus::Menu::kApp)
        for (const menus::Menu extra :
             {menus::Menu::kFile, menus::Menu::kEdit, menus::Menu::kWindow, menus::Menu::kHelp}) {
            out.push_back({menus::MenuItem::Kind::kSeparator, {}, {}, false, true});
            out.push_back({menus::MenuItem::Kind::kHeader, menus::menu_title(extra), {}, false, true});
            const auto more = menus::menu_items(extra, ctx);
            out.insert(out.end(), more.begin(), more.end());
        }
    // The Mac layout's "Fill the desktop" means nothing here.
    std::erase_if(out, [](const menus::MenuItem& it) { return it.command == "window.fill"; });
    return out;
}

void open_menu(App::State& s, int i) {
    s.styles.classic.menu = i;
    ImGui::OpenPopup(kMenuPopup);
}

// The open menu: a single-line box under its title, 20 px rows one cell in from each side, the
// bound keys right-aligned in the muted colour. The row under the pointer is lit, the row chosen
// with the keys is reversed. Left and Right move to the neighbouring menu.
void draw_dropdown(App::State& s, const Chrome& c, const std::vector<menus::Menu>& shown, bool all) {
    auto& cs = s.styles.classic;
    if (cs.menu < 0 || cs.menu >= static_cast<int>(shown.size()) || cs.menu_at.size() != shown.size())
        return;
    const auto rows = rows_of(s, shown[static_cast<std::size_t>(cs.menu)], all);
    int label_cols = 0, chord_cols = 0;
    for (const auto& it : rows) {
        label_cols = std::max(label_cols, cl::text_cols(it.label) + 2);
        if (it.kind == menus::MenuItem::Kind::kItem)
            chord_cols = std::max(chord_cols, cl::text_cols(style_chord_for(s, it.command)));
    }
    const int cols = 2 + label_cols + (chord_cols > 0 ? 3 + chord_cols : 1) + 1;
    const float w = static_cast<float>(cols) * c.g.cell_w;
    const float h = static_cast<float>(rows.size() + 2) * c.g.cell_h;
    ImGui::SetNextWindowPos(cs.menu_at[static_cast<std::size_t>(cs.menu)]);
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, v4(g_pal->sidebar));
    int next = -1;
    if (ImGui::BeginPopup(kMenuPopup, ImGuiWindowFlags_NoMove)) {
        menu_keys();
        const int count = static_cast<int>(shown.size());
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            cs.keyed = true;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            next = (cs.menu + count - 1) % count;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            next = (cs.menu + 1) % count;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a = ImGui::GetWindowPos();
        box(dl, c, a, ImVec2(a.x + w, a.y + h), false, false, g_pal->text);
        const ImGuiIO& io = ImGui::GetIO();
        const bool moved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& it = rows[i];
            const float y = a.y + static_cast<float>(i + 1) * c.g.cell_h;
            if (it.kind == menus::MenuItem::Kind::kSeparator) {
                std::string line = "\xE2\x94\x9C";  // ├
                for (int k = 0; k < cols - 2; ++k)
                    line += "\xE2\x94\x80";  // ─
                line += "\xE2\x94\xA4";      // ┤
                put(dl, c, a.x, y, 0, line, g_pal->text);
                continue;
            }
            if (it.kind == menus::MenuItem::Kind::kHeader) {
                put(dl, c, a.x, y, 2, it.label, g_pal->faint);
                continue;
            }
            ImGui::SetCursorScreenPos(ImVec2(a.x + c.g.cell_w, y));
            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginDisabled(!it.enabled);
            bool clicked = ImGui::InvisibleButton("##row", ImVec2(w - 2 * c.g.cell_w, c.g.cell_h));
            ImGui::EndDisabled();
            ImGui::PopID();
            const bool hov = it.enabled && ImGui::IsItemHovered();
            bool run = false;
            const bool on = it.enabled && g_menu_nav.row(hov && moved, &run);
            clicked = it.enabled && (clicked || run);
            const bool keyed = on && cs.keyed;
            if (keyed)
                fill_cells(dl, c, a.x, y, 1, cols - 2, g_pal->text);
            else if (hov)
                fill_cells(dl, c, a.x, y, 1, cols - 2, alpha(g_pal->text, 89));
            const ImU32 ink = !it.enabled ? g_pal->faint : keyed ? g_pal->sidebar : g_pal->text;
            put(dl, c, a.x, y, 1, it.checked ? "*" : " ", ink);
            put(dl, c, a.x, y, 2, it.label, ink);
            const std::string chord = style_chord_for(s, it.command);
            if (!chord.empty())
                put(dl, c, a.x, y, cols - 2 - cl::text_cols(chord), chord,
                    keyed ? g_pal->sidebar : g_pal->dim);
            if (clicked) {
                ImGui::CloseCurrentPopup();
                cs.menu = -1;
                run_command(s, it.command);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    if (next >= 0)
        open_menu(s, next);
}

// The text window buttons: "(x)(-)(+)" on the left for round ones, "[_][#][X]" on the right for
// Windows' (Settings > Look > Window buttons). Returns the cell after them.
int draw_text_buttons(App::State& s, ImDrawList* dl, const Chrome& c, float x0, float y, int col,
                      bool windows) {
    static constexpr const char* kMac[3] = {"(x)", "(-)", "(+)"};
    static constexpr const char* kWin[3] = {"[_]", "[#]", "[X]"};
    static constexpr const char* kMacCmd[3] = {"window.close", "window.minimize", "window.maximize"};
    static constexpr const char* kWinCmd[3] = {"window.minimize", "window.maximize", "window.close"};
    for (int i = 0; i < 3; ++i) {
        bool hov = false, held = false;
        ImGui::PushID(i);
        const bool clicked = cell_button("##classic-win", c, x0, y, col, 3, &hov, &held);
        ImGui::PopID();
        // Under the pointer each takes the theme's colour for its kind (close, minimize, zoom).
        const int kind = windows ? (i + 1) % 3 : i;
        if (held)
            fill_cells(dl, c, x0, y, col, 3, g_pal->text);
        put(dl, c, x0, y, col, windows ? kWin[i] : kMac[i],
            held  ? g_pal->sidebar
            : hov ? window_button_color(kind, g_pal->sidebar)
                  : g_pal->dim);
        if (clicked)
            run_command(s, windows ? kWinCmd[i] : kMacCmd[i]);
        col += 3;
    }
    return col;
}

struct AgentTally {
    int needs = 0;
    int working = 0;
    int agents = 0;
    std::string first_blocked;
};

AgentTally tally(App::State& s) {
    AgentTally t;
    std::vector<Attention> states;
    for (const auto& [id, v] : s.views_by_id) {
        if (!live_agent(s, id, v))
            continue;
        states.push_back(s.attention_of(id));
        if (t.first_blocked.empty() && states.back() == Attention::kBlocked)
            t.first_blocked = id;
    }
    const auto n = ui::chrome_common::count_agents(states);
    t.needs = n.needs;
    t.working = n.working;
    t.agents = static_cast<int>(states.size());
    return t;
}
}  // namespace

// ------------------------------------------------------------------ the menu bar
// The text window buttons, AGENTDESK and the menu titles; on the right the counts of agents that
// need you (!) and are working (~), and the clock. A click opens a menu; while one is open,
// pointing at another title opens that one. F10 opens the first. Empty cells move the window.
void draw_classic_menubar(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto& cs = s.styles.classic;
    const Chrome c = chrome(s);
    const ImVec2 b(pos.x + w, pos.y + h);
    dl->AddRectFilled(pos, b, g_pal->sidebar);
    const float y = std::round(pos.y + (h - c.g.cell_h) * 0.5f);
    const int cols = c.g.cols(w);
    const bool windows = s.config.hwnd != nullptr && s.settings.window_buttons == "windows";
    const bool buttons = s.config.hwnd != nullptr;

    // What fits: all menus over 80 cells; the clock, then the counts go when it is tight.
    const AgentTally t = tally(s);
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    const std::string clock = ui::chrome_common::clock_text(local.tm_wday, local.tm_hour, local.tm_min);
    const std::string counts = "!" + std::to_string(t.needs) + "  ~" + std::to_string(t.working);
    int full = 1 + (buttons && !windows ? 10 : 0), brief = full;
    for (const menus::Menu m : shown_menus(true))
        full += cl::text_cols(menu_label(m)) + 2;
    for (const menus::Menu m : shown_menus(false))
        brief += cl::text_cols(menu_label(m)) + 2;
    const int right_buttons = windows ? 10 : 0;
    const cl::BarFit fit =
        cl::fit_bar(cols - right_buttons, full, brief, cl::text_cols(counts) + 2, cl::text_cols(clock) + 2);
    const auto shown = shown_menus(fit.all_menus);

    // A click elsewhere or Esc closed the dropdown.
    const int was_open = cs.menu;
    if (cs.menu >= 0 && !ImGui::IsPopupOpen(kMenuPopup))
        cs.menu = -1;
    if (cs.open_request >= 0) {  // classic.menu (F10)
        open_menu(s, std::min(cs.open_request, static_cast<int>(shown.size()) - 1));
        cs.keyed = true;
        cs.open_request = -1;
    }
    int col = 1;
    if (buttons && !windows)
        col = draw_text_buttons(s, dl, c, pos.x, y, col, false) + 1;
    cs.menu_at.assign(shown.size(), ImVec2(pos.x, b.y));
    for (int i = 0; i < static_cast<int>(shown.size()); ++i) {
        const std::string label = menu_label(shown[static_cast<std::size_t>(i)]);
        const int n = cl::text_cols(label) + 2;
        bool hov = false;
        ImGui::PushID(i);
        cell_button("##classic-title", c, pos.x, y, col, n, &hov);
        const bool pressed = ImGui::IsItemActivated();
        ImGui::PopID();
        if (pressed) {
            if (was_open == i)
                cs.menu = -1;
            else {
                open_menu(s, i);
                cs.keyed = false;
            }
        } else if (cs.menu >= 0 && cs.menu != i && hov) {
            open_menu(s, i);
        }
        const bool open = cs.menu == i;
        if (open)
            fill_cells(dl, c, pos.x, y, col, n, g_pal->text);
        else if (hov)
            fill_cells(dl, c, pos.x, y, col, n, alpha(g_pal->focus, 64));
        put(dl, c, pos.x, y, col + 1, label, open ? g_pal->sidebar : g_pal->text, i == 0);
        cs.menu_at[static_cast<std::size_t>(i)] = ImVec2(pos.x + static_cast<float>(col) * c.g.cell_w, b.y);
        col += n;
    }
    draw_dropdown(s, c, shown, fit.all_menus);
    if (was_open >= 0 && cs.menu < 0)
        s.focus_view(s.ws.focused_session());  // the keyboard goes back to the terminal

    // Right, right to left: Windows' buttons, the clock, the counts.
    int rcol = cols - 1;
    if (windows) {
        rcol -= 9;
        draw_text_buttons(s, dl, c, pos.x, y, rcol, true);
        rcol -= 1;
    }
    if (fit.clock) {
        rcol -= cl::text_cols(clock);
        put(dl, c, pos.x, y, rcol, clock, g_pal->text);
        rcol -= 2;
    }
    if (fit.counts) {
        const std::string need = "!" + std::to_string(t.needs);
        const std::string work = "~" + std::to_string(t.working);
        const int n = cl::text_cols(need) + 2 + cl::text_cols(work);
        rcol -= n;
        bool hov = false;
        const bool clicked = cell_button("##classic-counts", c, pos.x, y, rcol, n, &hov);
        if (hov)
            fill_cells(dl, c, pos.x, y, rcol, n, alpha(g_pal->focus, 51));
        if (t.needs > 0) {
            fill_cells(dl, c, pos.x, y, rcol, cl::text_cols(need), g_pal->needs_you);
            put(dl, c, pos.x, y, rcol, need, g_pal->sidebar, true);
        } else {
            put(dl, c, pos.x, y, rcol, need, g_pal->dim);
        }
        put(dl, c, pos.x, y, rcol + cl::text_cols(need) + 2, work,
            t.working > 0 ? g_pal->working : g_pal->dim);
        if (clicked)
            run_command(
                s, t.first_blocked.empty() ? std::string("app.picker") : "agent.focus:" + t.first_blocked);
    }
    window_drag_area(s, pos, b);
}

// ------------------------------------------------------------------ the tab line
// "[repo-a v]" (the space; a click opens Go to), then the tabs "1 claude [!]" split by "|", the
// current one reversed, then "+". Tabs that do not fit shrink to their number, oldest first.
void draw_classic_tabs(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Chrome c = chrome(s);
    const ImVec2 b(pos.x + w, pos.y + h);
    dl->AddRectFilled(pos, b, g_pal->sidebar);
    const float y = std::round(pos.y + (h - c.g.cell_h) * 0.5f);
    const int cols = c.g.cols(w);
    int col = 1;
    ui::Space* sp = s.ws.active();
    {
        std::string name = sp != nullptr ? s.ws.space_title(s.ws.active_space, s.session_cwds()) : "no space";
        name = "[" + cl::cut_cols(name, 18) + " v]";
        const int n = cl::text_cols(name);
        bool hov = false;
        if (cell_button("##classic-space", c, pos.x, y, col, n, &hov))
            run_command(s, "app.picker");
        if (hov)
            fill_cells(dl, c, pos.x, y, col, n, alpha(g_pal->focus, 51));
        col = put(dl, c, pos.x, y, col, name, g_pal->text) + 1;
    }
    std::string close_tab;
    if (sp == nullptr || sp->tabs.empty()) {
        col = put(dl, c, pos.x, y, col, "No tabs", g_pal->faint) + 1;
    } else {
        std::vector<std::string> labels;
        std::vector<Attention> atts;
        std::vector<bool> lost;
        for (int i = 0; i < static_cast<int>(sp->tabs.size()); ++i) {
            const ui::Tab& t = sp->tabs[static_cast<std::size_t>(i)];
            const auto ids = s.ws.sessions_in(t);
            bool all_lost = !ids.empty();
            for (const auto& id : ids)
                if (const auto v = s.views_by_id.find(id); v == s.views_by_id.end() || !v->second.lost)
                    all_lost = false;
            lost.push_back(all_lost);
            atts.push_back(tab_attention(s, ids));
            labels.push_back(std::to_string(i + 1) + " " + tab_label(s, t, i, ids));
        }
        // Each tab's mark takes 4 or 5 cells after its name; the "+" 3 at the end.
        std::vector<std::string> with_marks = labels;
        for (std::size_t i = 0; i < labels.size(); ++i)
            if (lost[i])
                with_marks[i] += " [lost]";
            else if (atts[i] != Attention::kNone && atts[i] != Attention::kIdle)
                with_marks[i] += " " + mark_text(atts[i]);
        const auto fitted = cl::fit_tabs(with_marks, sp->active_tab, cols - col - 4);
        for (int i = 0; i < static_cast<int>(fitted.size()); ++i) {
            if (i > 0)
                col = put(dl, c, pos.x, y, col, " | ", g_pal->border);
            const bool cut = fitted[static_cast<std::size_t>(i)] != with_marks[static_cast<std::size_t>(i)];
            const std::string& name =
                cut ? fitted[static_cast<std::size_t>(i)] : labels[static_cast<std::size_t>(i)];
            const int n = cl::text_cols(fitted[static_cast<std::size_t>(i)]);
            if (col + n > cols - 4)
                break;
            const ui::Tab& t = sp->tabs[static_cast<std::size_t>(i)];
            bool hov = false;
            ImGui::PushID(t.id.c_str());
            const bool clicked = cell_button("##classic-tab", c, pos.x, y, col, n, &hov);
            if (clicked)
                run_command(s, "tab.goto:" + std::to_string(s.ws.active_space) + ":" + std::to_string(i));
            if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                run_command(s, "tab.rename:" + t.id);
            tab_menu(s, t, close_tab);
            ImGui::PopID();
            const bool on = i == sp->active_tab;
            if (on)
                fill_cells(dl, c, pos.x, y, col, cl::text_cols(name), g_pal->text);
            else if (hov)
                fill_cells(dl, c, pos.x, y, col, n, alpha(g_pal->focus, 51));
            const ImU32 ink = on ? g_pal->sidebar : t.settled ? g_pal->faint : g_pal->text;
            col = put(dl, c, pos.x, y, col, name, ink);
            if (!cut && lost[static_cast<std::size_t>(i)])
                col = put(dl, c, pos.x, y, col, " [lost]", g_pal->faint);
            else if (!cut && atts[static_cast<std::size_t>(i)] != Attention::kNone &&
                     atts[static_cast<std::size_t>(i)] != Attention::kIdle)
                col = put_mark(dl, c, pos.x, y, put(dl, c, pos.x, y, col, " ", ink),
                               atts[static_cast<std::size_t>(i)], g_pal->text);
        }
        col = put(dl, c, pos.x, y, col, " | ", g_pal->border);
    }
    if (!close_tab.empty())
        run_command(s, "tab.close:" + close_tab);
    {
        bool hov = false;
        if (cell_button("##classic-plus", c, pos.x, y, col, 1, &hov))
            ImGui::OpenPopup("##newmenu");
        if (hov)
            fill_cells(dl, c, pos.x, y, col, 1, alpha(g_pal->focus, 51));
        put(dl, c, pos.x, y, col, "+", g_pal->text, true);
        ImGui::SetNextWindowPos(ImVec2(pos.x + static_cast<float>(col) * c.g.cell_w, b.y), ImGuiCond_Always);
        draw_new_menu(s);
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.MouseWheel != 0.0f && ImGui::IsMouseHoveringRect(pos, b) && ImGui::IsWindowHovered())
        run_command(s, io.MouseWheel > 0 ? "tab.prev" : "tab.next");
    window_drag_area(s, pos, b);
}

// ------------------------------------------------------------------ the status line
// "─[ NEED YOU 2 ]─[ WORKING 3 ]─[ repo-a · main +1 ]────[ Ctrl+Shift+P commands ]─": the parts
// a click acts on light up under the pointer.
void draw_classic_status(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Chrome c = chrome(s);
    const ImVec2 b(pos.x + w, pos.y + h);
    dl->AddRectFilled(pos, b, g_pal->sidebar);
    const float y = std::round(pos.y + (h - c.g.cell_h) * 0.5f);
    const int cols = c.g.cols(w);
    std::string line;
    for (int i = 0; i < cols; ++i)
        line += "\xE2\x94\x80";  // ─
    put(dl, c, pos.x, y, 0, line, g_pal->border);

    struct Seg {
        std::string text;
        ImU32 ink;
        bool reverse = false;
        std::string command;
    };
    std::vector<Seg> left;
    const bool connected = s.host->connected();
    const AgentTally t = tally(s);
    if (!connected && s.machine_loading("")) {
        static constexpr const char* kDots[] = {"\xE2\xA0\x8B", "\xE2\xA0\x99", "\xE2\xA0\xB9",
                                                "\xE2\xA0\xB8", "\xE2\xA0\xBC", "\xE2\xA0\xB4",
                                                "\xE2\xA0\xA6", "\xE2\xA0\xA7"};
        left.push_back({std::string("connecting ") + kDots[(now_ms_steady() / 100) % 8], g_pal->dim});
        if (s.config.wake)
            s.config.wake();
    } else if (!connected) {
        left.push_back({"DISCONNECTED", g_pal->needs_you, true});
    } else if (t.agents == 0) {
        left.push_back({"no agents", g_pal->dim});
    } else {
        left.push_back(
            {"NEED YOU " + std::to_string(t.needs), t.needs > 0 ? g_pal->needs_you : g_pal->dim, t.needs > 0,
             t.first_blocked.empty() ? std::string("app.picker") : "agent.focus:" + t.first_blocked});
        left.push_back({"WORKING " + std::to_string(t.working), t.working > 0 ? g_pal->working : g_pal->dim});
    }
    if (ui::Space* sp = s.ws.active(); sp != nullptr) {
        const std::string name = s.ws.space_title(s.ws.active_space, s.session_cwds());
        const GitTail g = git_tail(s.git.get(s.focused_cwd(), sp->machine), name, false);
        std::string text = name + (g.text.empty() ? std::string() : " \xC2\xB7 " + g.text);
        if (g.ahead > 0)
            text += " +" + std::to_string(g.ahead);
        if (g.behind > 0)
            text += " -" + std::to_string(g.behind);
        left.push_back({cl::cut_cols(text, std::max(8, cols / 3)), g_pal->text});
    }
    const std::string chord = s.keymap.chord_for("app.palette");
    const Seg right{(chord.empty() ? std::string() : chord + " ") + "commands", g_pal->text, false,
                    "app.palette"};

    auto draw_seg = [&](int col, const Seg& sg, int idx) {
        const int n = cl::text_cols(sg.text) + 4;  // "[ " text " ]"
        bool hov = false;
        if (!sg.command.empty()) {
            ImGui::PushID(idx);
            if (cell_button("##classic-seg", c, pos.x, y, col, n, &hov))
                run_command(s, sg.command);
            ImGui::PopID();
        }
        fill_cells(dl, c, pos.x, y, col, n, g_pal->sidebar);  // break the line under the part
        if (sg.reverse)
            fill_cells(dl, c, pos.x, y, col + 1, n - 2, sg.ink);
        else if (hov)
            fill_cells(dl, c, pos.x, y, col + 1, n - 2, alpha(g_pal->focus, 51));
        put(dl, c, pos.x, y, col, "[", g_pal->border);
        put(dl, c, pos.x, y, col + 2, sg.text, sg.reverse ? g_pal->sidebar : sg.ink, sg.reverse);
        put(dl, c, pos.x, y, col + n - 1, "]", g_pal->border);
        return col + n;
    };
    const int right_n = cl::text_cols(right.text) + 4;
    int col = 1;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const int n = cl::text_cols(left[i].text) + 4;
        if (col + n + 1 > cols - right_n - 2)
            break;
        col = draw_seg(col, left[i], static_cast<int>(i)) + 1;
    }
    if (cols - right_n - 1 > col)
        draw_seg(cols - right_n - 1, right, 99);
}

// ------------------------------------------------------------------ the sidebar
// Text only: SPACES (the active one marked ">", its tabs drawn as a tree) and AGENTS (state mark,
// space/program, how long since it changed). A click goes there.
void draw_classic_sidebar(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Chrome c = chrome(s);
    const ImVec2 b(pos.x + w, pos.y + h);
    dl->AddRectFilled(pos, b, g_pal->sidebar);
    // A single vertical rule on the right edge.
    for (float y = pos.y; y < b.y; y += c.g.cell_h)
        paint_sprite(dl, U'│', b.x - c.g.cell_w, y, c.g.cell_w, c.g.cell_h, g_pal->border);
    const int cols = c.g.cols(w) - 1;
    float y = pos.y + px(4);
    const auto cwds = s.session_cwds();
    auto row_button = [&](const std::string& id, const std::string& cmd) {
        bool hov = false;
        ImGui::PushID(id.c_str());
        if (cell_button("##classic-side", c, pos.x, y, 0, cols, &hov))
            run_command(s, cmd);
        ImGui::PopID();
        if (hov)
            fill_cells(dl, c, pos.x, y, 0, cols, alpha(g_pal->focus, 51));
    };
    put(dl, c, pos.x, y, 1, "SPACES", g_pal->dim, true);
    y += c.g.cell_h;
    for (int i = 0; i < static_cast<int>(s.ws.spaces.size()) && y < b.y - c.g.cell_h; ++i) {
        const ui::Space& sp = s.ws.spaces[static_cast<std::size_t>(i)];
        const bool on = i == s.ws.active_space;
        row_button("s" + sp.id, "space.select:" + std::to_string(i + 1));
        const std::string title = s.ws.space_title(i, cwds);
        std::string branch;
        if (!sp.tabs.empty())
            if (const auto ids = s.ws.sessions_in(sp.tabs.front()); !ids.empty())
                if (const auto v = s.views_by_id.find(ids.front()); v != s.views_by_id.end()) {
                    const GitInfo g = s.git.get(v->second.cwd, sp.machine);
                    if (g.repo)
                        branch = g.branch;
                }
        const int bw = cl::text_cols(branch);
        put(dl, c, pos.x, y, 1, on ? ">" : " ", g_pal->focus, true);
        put(dl, c, pos.x, y, 3, cl::cut_cols(title, std::max(4, cols - 5 - bw)),
            on ? g_pal->text : g_pal->dim, on);
        if (!branch.empty() && cols - 1 - bw > 3 + cl::text_cols(title))
            put(dl, c, pos.x, y, cols - 1 - bw, branch, g_pal->faint);
        y += c.g.cell_h;
        if (!on)
            continue;
        for (int k = 0; k < static_cast<int>(sp.tabs.size()) && y < b.y - c.g.cell_h; ++k) {
            const ui::Tab& t = sp.tabs[static_cast<std::size_t>(k)];
            row_button("t" + t.id, "tab.goto:" + std::to_string(i) + ":" + std::to_string(k));
            const bool last = k + 1 == static_cast<int>(sp.tabs.size());
            const auto ids = s.ws.sessions_in(t);
            put(dl, c, pos.x, y, 3, last ? "\xE2\x94\x94" : "\xE2\x94\x9C", g_pal->border);  // └ ├
            const std::string label = std::to_string(k + 1) + " " + tab_label(s, t, k, ids);
            put(dl, c, pos.x, y, 5, cl::cut_cols(label, std::max(4, cols - 11)),
                k == sp.active_tab ? g_pal->text : g_pal->dim);
            const Attention a = tab_attention(s, ids);
            if (a != Attention::kNone && a != Attention::kIdle)
                put_mark(dl, c, pos.x, y, cols - 5, a, g_pal->text);
            y += c.g.cell_h;
        }
    }
    y += c.g.cell_h * 0.5f;
    if (y >= b.y - c.g.cell_h)
        return;
    put(dl, c, pos.x, y, 1, "AGENTS", g_pal->dim, true);
    y += c.g.cell_h;
    std::vector<std::string> ids;
    for (const auto& [id, v] : s.views_by_id)
        if (live_agent(s, id, v))
            ids.push_back(id);
    if (ids.empty()) {
        put(dl, c, pos.x, y, 3, "no agents", g_pal->faint);
        return;
    }
    std::stable_sort(ids.begin(), ids.end(), [&](const std::string& a, const std::string& z) {
        return ui::attention_rank(s.attention_of(a)) > ui::attention_rank(s.attention_of(z));
    });
    for (const auto& id : ids) {
        if (y >= b.y - c.g.cell_h)
            break;
        row_button("a" + id, "agent.focus:" + id);
        const Attention a = s.attention_of(id);
        int col = put_mark(dl, c, pos.x, y, 2, a, g_pal->text) + 1;
        const auto loc = s.ws.find(id);
        std::string who = loc.space >= 0 ? s.ws.space_title(loc.space, cwds) : std::string();
        who += (who.empty() ? "" : "/") + program_of(s.views_by_id[id]).name;
        std::string when;  // since its state last changed (tracked every tick, cockpit_tick)
        if (const auto ch = s.styles.cockpit.changed_at.find(id); ch != s.styles.cockpit.changed_at.end()) {
            const long long m = std::max(0LL, now_ms_steady() - ch->second) / 60000;
            when = m < 60 ? std::to_string(m) + "m" : std::to_string(m / 60) + "h";
        }
        put(dl, c, pos.x, y, col, cl::cut_cols(who, std::max(4, cols - col - 1 - cl::text_cols(when) - 1)),
            g_pal->text);
        if (!when.empty())
            put(dl, c, pos.x, y, cols - 1 - cl::text_cols(when), when, g_pal->faint);
        y += c.g.cell_h;
    }
}

// ------------------------------------------------------------------ panes
// A pane is a box of characters: double lines in the accent when focused, single lines in the
// border colour otherwise, dashed while agent-host is away. The top line carries the title
// ("claude · repo-a (main)") and the state mark; a press there focuses the pane.
StyledPane draw_classic_pane(App::State& s, const std::string& id, const ui::SessionView& v, ImVec2 pos,
                             ImVec2 max, const ui::styles::PaneLook& look, bool focused, bool connected) {
    (void)look;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Chrome c = chrome(s);
    dl->AddRectFilled(pos, max, g_pal->term_bg);
    const ImU32 line = focused ? g_pal->focus : g_pal->border;
    box(dl, c, pos, max, focused && connected, !connected, line);
    const ImVec2 hb(max.x, pos.y + c.g.cell_h);
    const bool hov =
        ImGui::IsMouseHoveringRect(pos, hb) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const int cols = c.g.cols(max.x - pos.x);
    const Attention att = s.attention_of(id);
    const bool marked = v.process_state == "Running" && att != Attention::kNone && att != Attention::kIdle;
    std::string title = s.pane_title(id);
    if (const std::string f = folder_name(v.cwd); !f.empty())
        title += " \xC2\xB7 " + f;
    const auto mi = s.machine_of.find(id);
    const GitInfo g = s.git.get(v.cwd, mi != s.machine_of.end() ? mi->second : std::string());
    if (g.repo && !g.branch.empty() && g.branch != "HEAD")
        title += " (" + g.branch + ")";
    const int room = cols - 6 - (marked ? 5 : 0);
    if (room > 3) {
        const std::string shown = " " + cl::cut_cols(title, room) + " ";
        const int n = cl::text_cols(shown);
        fill_cells(dl, c, pos.x, pos.y, 2, n, g_pal->term_bg);
        put(dl, c, pos.x, pos.y, 2, shown, focused || hov ? g_pal->text : g_pal->dim, focused);
    }
    if (marked && cols > 10) {
        const int mc = cols - 2 - 4;
        fill_cells(dl, c, pos.x, pos.y, mc - 1, 5, g_pal->term_bg);
        put_mark(dl, c, pos.x, pos.y, mc, att, g_pal->text);
    }
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
        s.ws.focus(id);
        s.views[id].focus_next = true;
    }
    return {ImVec2(pos.x + c.g.cell_w + px(2), pos.y + c.g.cell_h),
            ImVec2(max.x - c.g.cell_w - px(2), max.y - c.g.cell_h)};
}

// No tab: a single-line box in the middle saying how to open one.
bool draw_classic_no_tab(App::State& s, ImDrawList* dl, ImVec2 pos, ImVec2 size) {
    const Chrome c = chrome(s);
    const std::string chord = s.keymap.chord_for("tab.new");
    const std::string text = chord.empty() ? std::string("No tabs. Open one with + above.")
                                           : "No tabs. Press " + chord + " for a new tab.";
    const int n = cl::text_cols(text) + 6;
    const float w = static_cast<float>(n) * c.g.cell_w;
    const float h = c.g.cell_h * 3;
    const ImVec2 a(std::round(pos.x + (size.x - w) * 0.5f), std::round(pos.y + (size.y - h) * 0.5f));
    box(dl, c, a, ImVec2(a.x + w, a.y + h), false, false, g_pal->border);
    put(dl, c, a.x, a.y + c.g.cell_h, 3, text, g_pal->text);
    return true;
}

// ------------------------------------------------------------------ effects
// Scan lines over the chrome (and, at full, faintly over the terminals), and a dark vignette on
// the window's edges. All static: they add no frames.
void draw_classic_overlay(App::State& s) {
    if (!effects_on(s))
        return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 line = IM_COL32(0, 0, 0, 46);  // 18 %
    for (const lay::Node* n : s.layout().tree.walk()) {
        const std::string m = n->mod();
        if (m != "classic-menubar" && m != "classic-tabs" && m != "classic-status" && m != "classic-sidebar")
            continue;
        if (const lay::Placed* p = s.placed.at(n->id))
            for (const auto& [y, hh] : cl::scanlines(p->outer.y, p->outer.bottom(), g_scale))
                dl->AddRectFilled(ImVec2(p->outer.x, y), ImVec2(p->outer.right(), y + hh), line);
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 a = vp->WorkPos;
    const ImVec2 b(a.x + vp->WorkSize.x, a.y + vp->WorkSize.y);
    const float e = px(12);
    const ImU32 dark = IM_COL32(0, 0, 0, 77);  // 30 %
    const ImU32 clear = IM_COL32(0, 0, 0, 0);
    dl->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + e), dark, dark, clear, clear);
    dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - e), b, clear, clear, dark, dark);
    dl->AddRectFilledMultiColor(a, ImVec2(a.x + e, b.y), dark, clear, clear, dark);
    dl->AddRectFilledMultiColor(ImVec2(b.x - e, a.y), b, clear, dark, dark, clear);
}

void classic_terminal_overlay(App::State& s, ImVec2 body_min, ImVec2 body_size) {
    if (!effects_full(s))
        return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const auto& [y, hh] : cl::scanlines(body_min.y, body_min.y + body_size.y, g_scale))
        dl->AddRectFilled(ImVec2(body_min.x, y), ImVec2(body_min.x + body_size.x, y + hh),
                          IM_COL32(0, 0, 0, 15));
}

void classic_terminal_theme(App::State& s, ui::TerminalTheme& theme) {
    if (s.settings.styles.classic_mono_terminal)
        theme.mono = cl::mono_ramp(g_pal->term_bg, g_pal->term_fg);
}

// ------------------------------------------------------------------ Settings
// The card: one colour, two text lines at the top, two character frames (one doubled) and a
// status line at the bottom, in the theme's text and border colours.
void classic_layout_preview(ImDrawList* d, ImVec2 a, ImVec2 size) {
    const ImVec2 b(a.x + size.x, a.y + size.y);
    d->AddRectFilled(a, b, g_pal->term_bg, px(6));
    const ImU32 ink = alpha(g_pal->text, 200);
    auto dash = [&](float x, float y, float w) {
        d->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + px(1.5f)), ink);
    };
    for (int i = 0; i < 5; ++i)
        dash(a.x + px(5) + static_cast<float>(i) * px(10), a.y + px(4), px(i == 0 ? 8.0f : 6.0f));
    for (int i = 0; i < 3; ++i)
        dash(a.x + px(5) + static_cast<float>(i) * px(14), a.y + px(9), px(10));
    const float top = a.y + px(14);
    const float bottom = b.y - px(9);
    const float mid = a.x + size.x * 0.56f;
    const ImVec2 p1(a.x + px(5), top), q1(mid - px(2), bottom);
    d->AddRect(p1, q1, g_pal->focus, 0.0f, 0, 1.0f);
    d->AddRect(ImVec2(p1.x + px(1.5f), p1.y + px(1.5f)), ImVec2(q1.x - px(1.5f), q1.y - px(1.5f)),
               g_pal->focus, 0.0f, 0, 1.0f);
    d->AddRect(ImVec2(mid + px(2), top), ImVec2(b.x - px(5), bottom), g_pal->border, 0.0f, 0, 1.0f);
    dash(p1.x + px(5), p1.y + px(5), px(18));
    dash(mid + px(6), top + px(5), px(12));
    d->AddRectFilled(ImVec2(a.x + px(4), b.y - px(6)), ImVec2(b.x - px(4), b.y - px(5)), g_pal->border);
    d->AddRectFilled(ImVec2(a.x + px(9), b.y - px(7.5f)), ImVec2(a.x + px(24), b.y - px(3.5f)), ink);
    d->AddRect(a, b, g_pal->border, px(6));
}

bool look_classic_options(App::State& s) {
    bool changed = false;
    auto& st = s.settings.styles;
    settings_row(s, "Screen effects",
                 "Scan lines, a vignette and, at Full, a soft glow. Never on light themes.", px(240), px(32));
    if (segmented(s, "##classic-effects", st.classic_effects,
                  {{"off", "Off"}, {"light", "Light"}, {"full", "Full"}}))
        changed = true;
    settings_row_done(s);
    settings_row(s, "Monochrome terminal",
                 "Terminal colours in the theme's one colour. Turn off for coloured diffs.", px(36), px(20));
    if (settings_toggle("##classic-mono", st.classic_mono_terminal)) {
        st.classic_mono_terminal = !st.classic_mono_terminal;
        changed = true;
    }
    settings_row_done(s);
    return changed;
}

std::optional<std::string> exec_classic_command(App::State& s, const std::string& name) {
    if (name == "classic.menu") {
        if (s.layout().style != "classic" || s.settings.focus_on)
            return std::string("not in this layout");
        s.styles.classic.open_request = 0;
        return std::string();
    }
    if (name == "classic.effects") {
        auto& e = s.settings.styles.classic_effects;
        e = e == "off" ? "light" : e == "light" ? "full" : "off";
        save_settings(s.settings);
        s.toast("Screen effects: " + e, "", "", g_pal->info, 2);
        return std::string();
    }
    return std::nullopt;
}

}  // namespace detail
}  // namespace agentdesk::desktop
