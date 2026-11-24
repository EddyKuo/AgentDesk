// Overlays: the shared overlay frame and title row, the command palette, the "Go to" picker, the
// keybinds list, the launcher and the rename prompt.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "overlay_kit.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// The popover background: the card colour pushed 3 % toward black (light themes) or 6 % toward
// white (dark themes), so overlays stand off the window.
ImU32 popover_color() {
    return g_pal->light ? mix(g_pal->card, IM_COL32(0, 0, 0, 255), 0.03f)
                        : mix(g_pal->card, IM_COL32(255, 255, 255, 255), 0.06f);
}

}  // namespace

// Overlay frame: the popover background, a 1 px border, 8 px corners and 18 px padding.
void push_overlay_style() {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, v4(popover_color()));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, v4(popover_color()));
    ImGui::PushStyleColor(ImGuiCol_Border, v4(g_pal->border));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(18), px(18)));
}
void pop_overlay_style() {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
}

// Title row (`size` px semibold) with "Esc" at the right, then 10 px; returns true when Esc is
// clicked.
bool overlay_title(App::State& s, const char* title, EscChip esc, float size) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(size), FLT_MAX, 0, title);
    dl->AddText(s.fonts.ui_bold, fpx(size), p, g_pal->text, title);
    const ImVec2 e = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, "Esc");
    bool clicked = false;
    if (esc == EscChip::kPlain) {
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(p.x + w - e.x, p.y + (t.y - e.y) * 0.5f), g_pal->faint,
                    "Esc");
    } else {
        const ImVec2 cs(e.x + px(12), e.y + px(4));
        const ImVec2 ea(p.x + w - cs.x, p.y + (t.y - cs.y) * 0.5f);
        ImGui::SetCursorScreenPos(ea);
        clicked = ImGui::InvisibleButton("##esc", cs);
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(ea, ImVec2(ea.x + cs.x, ea.y + cs.y), g_pal->hover, px(5));
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(ea.x + px(6), ea.y + px(2)), hov ? g_pal->text : g_pal->faint,
                    "Esc");
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + t.y + px(10)));
    return clicked;
}

// The worktree dialogs' title: 13 px semibold, no Esc; `right` (Open's "N checkouts") at the
// line's end, 11 px dim. Then a 10 px gap.
void worktree_title(App::State& s, const std::string& title, const std::string& right) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = std::round(fpx(13) * 1.3f);
    float rw = 0;
    if (!right.empty()) {
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, right.c_str());
        rw = t.x;
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(p.x + w - t.x, p.y + (h - t.y) * 0.5f), g_pal->faint,
                    right.c_str());
    }
    const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(13), FLT_MAX, 0, title.c_str());
    text_clipped(dl, s.fonts.ui_bold, fpx(13), ImVec2(p.x, p.y + (h - t.y) * 0.5f), g_pal->text, title,
                 w - rw - (rw > 0 ? px(8) : 0.0f));
    ImGui::Dummy(ImVec2(w, h));
    ImGui::Dummy(ImVec2(0, std::max(0.0f, px(10) - 2 * ImGui::GetStyle().ItemSpacing.y)));
}

// The worktree dialogs' input: the full width, 32 px high, 10 px in, rounded 6, the background
// fill and a 1 px border (accent while focused).
bool worktree_input(App::State& s, const char* id, const char* hint, char* buf, std::size_t size,
                    ImGuiInputTextFlags flags) {
    ImGui::SetNextItemWidth(-1);
    ImGui::PushFont(s.fonts.ui, 13);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), (px(32) - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(6));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(g_pal->bg));
    const bool r = ImGui::InputTextWithHint(id, hint, buf, size, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                        ImGui::IsItemActive() ? g_pal->focus : g_pal->border, px(6));
    return r;
}

// ------------------------------------------------------------------ command palette
namespace {
// Key chips: each part of "Ctrl+Shift+P", lower case, in its own bordered chip, laid out leftward
// from `right`. Returns the width used.
float key_chips(App::State& s, ImDrawList* dl, float right, float cy, const std::string& chord) {
    if (chord.empty())
        return 0;
    std::vector<std::string> parts;
    std::string cur;
    for (const char c : chord) {
        if (c == '+' && !cur.empty()) {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty())
        parts.push_back(cur);
    float x = right;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        const std::string k = ascii_lower(*it);
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(10.5f), FLT_MAX, 0, k.c_str());
        const float w = t.x + px(10);
        x -= w;
        dl->AddRectFilled(ImVec2(x, cy - px(9)), ImVec2(x + w, cy + px(9)), raised_color(), px(4));
        dl->AddRect(ImVec2(x, cy - px(9)), ImVec2(x + w, cy + px(9)), g_pal->border, px(4));
        dl->AddText(s.fonts.ui, fpx(10.5f), ImVec2(x + px(5), cy - t.y * 0.5f), g_pal->dim, k.c_str());
        x -= px(4);
    }
    return right - x;
}

// Fuzzy score of `query` in `title`: the query's letters must appear in order (case aside). +1
// per letter, +6 at a word start, +3 x the run length for consecutive letters, -0.3 per skipped
// character between hits (at most 6 counted), and -0.02 per title character so shorter titles
// win ties. `hits` gets the matched byte offsets. No match: nullopt.
std::optional<float> palette_score(const std::string& title, const std::string& query,
                                   std::vector<int>* hits) {
    if (query.empty())
        return 0.0f;
    float score = 0;
    std::size_t qi = 0;
    int run = 0;
    int last = -1;
    for (std::size_t i = 0; i < title.size() && qi < query.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(title[i])) !=
            std::tolower(static_cast<unsigned char>(query[qi])))
            continue;
        score += 1;
        const char prev = i == 0 ? ' ' : title[i - 1];
        if (i == 0 || std::strchr(" :.-_/", prev) != nullptr)
            score += 6;
        if (last >= 0 && static_cast<int>(i) == last + 1) {
            ++run;
            score += 3.0f * static_cast<float>(run);
        } else {
            if (last >= 0)
                score -= 0.3f *
                         static_cast<float>(std::min<std::size_t>(6, i - static_cast<std::size_t>(last) - 1));
            run = 0;
        }
        last = static_cast<int>(i);
        if (hits != nullptr)
            hits->push_back(static_cast<int>(i));
        ++qi;
    }
    if (qi < query.size())
        return std::nullopt;
    return score - 0.02f * static_cast<float>(title.size());
}

const char* command_group(const std::string& id) {
    auto starts = [&](const char* p) { return id.rfind(p, 0) == 0; };
    if (starts("tab.") || id == "app.picker")
        return "Tab";
    if (starts("pane."))
        return "Pane";
    if (starts("space.") || starts("worktree."))
        return "Space";
    if (starts("layout") || starts("sidebar.") || starts("details.") || starts("panel"))
        return "Layout";
    if (starts("terminal.") || starts("session."))
        return "Terminal";
    if (starts("agent."))
        return "Agent";
    if (starts("window."))
        return "Window";
    return "App";
}

struct PaletteEntry {
    std::string title, cmd, group, chord, words;
    float score = 0;
    std::vector<int> hits;  // byte offsets in `title` the query matched
};

// A list row: an entry, or a group title when `e` is null.
struct PaletteRow {
    const PaletteEntry* e = nullptr;
    std::string header;
};

// Every command the palette offers, in order: the named ones (with the themes after "Quit"),
// one per layout, the remaining built-in commands in their groups, then the mods' commands.
std::vector<PaletteEntry> palette_entries(App::State& s) {
    std::vector<PaletteEntry> all;
    auto add = [&](std::string title, std::string cmd, std::string group, std::string words) {
        all.push_back({std::move(title), cmd, std::move(group), s.keymap.chord_for(cmd), std::move(words)});
    };
    // Command, title, group and extra search words.
    struct Named {
        const char* cmd;
        const char* title;
        const char* group;
        const char* words;
    };
    static constexpr Named kNamed[] = {
        {"tab.new", "New tab", "Tab", "tab agent new task"},
        {"app.picker", "Go to space or tab\xE2\x80\xA6", "Tab", "switch jump find"},
        {"tab.rename", "Rename tab", "Tab", "tab"},
        {"tab.close", "Close tab", "Tab", "tab kill"},
        {"tab.next", "Next tab", "Tab", "tab"},
        {"tab.prev", "Previous tab", "Tab", "tab"},
        {"pane.split:right", "Split right", "Pane", "vertical"},
        {"pane.split:down", "Split down", "Pane", "horizontal"},
        {"pane.close", "Close pane", "Pane", "kill"},
        {"pane.zoom", "Zoom pane", "Pane", "maximize fullscreen"},
        {"pane.go:left", "Focus pane left", "Pane", "move go"},
        {"pane.go:right", "Focus pane right", "Pane", "move go"},
        {"pane.go:up", "Focus pane up", "Pane", "move go"},
        {"pane.go:down", "Focus pane down", "Pane", "move go"},
        {"pane.swap:left", "Swap pane left", "Pane", "move"},
        {"pane.swap:right", "Swap pane right", "Pane", "move"},
        {"pane.swap:up", "Swap pane up", "Pane", "move"},
        {"pane.swap:down", "Swap pane down", "Pane", "move"},
        {"space.new", "New space", "Space", "workspace"},
        {"space.rename", "Rename space", "Space", "workspace"},
        {"space.close", "Close space", "Space", "workspace kill"},
        {"space.next", "Next space", "Space", "workspace"},
        {"space.prev", "Previous space", "Space", "workspace"},
        {"layout.switch", "Next layout", "Layout", "look switch"},
        {"layout.reset", "Reset layout", "Layout", "new version default restore original"},
        {"layout.clone", "Duplicate layout", "Layout", "clone copy customize"},
        {"mod.clone", "Duplicate mod", "Layout", "clone copy customize"},
        {"terminal.open", "New terminal", "Terminal", "shell"},
        {"terminal.copy", "Copy", "Terminal", "clipboard"},
        {"terminal.paste", "Paste", "Terminal", "clipboard"},
        {"agent.next", "Next agent", "Agent", ""},
        {"agent.prev", "Previous agent", "Agent", ""},
        {"window.minimize", "Minimize window", "Window", ""},
        {"window.maximize", "Maximize window", "Window", "restore"},
        {"window.close", "Close window", "Window", "quit"},
        {"app.settings", "Settings", "App", "preferences options config look font theme sound"},
        {"app.help", "Keys", "App", "keybindings shortcuts help"},
        {"app.welcome", "Welcome", "App", "intro start onboarding"},
        {"app.whats_new", "What's new", "App", "release changes news update"},
        {"app.reset", "Reset to the default screen", "App", "escape hatch broken restore"},
        {"app.zoom_in", "Zoom in", "App", "bigger font size"},
        {"app.zoom_out", "Zoom out", "App", "smaller font size"},
        {"app.zoom_reset", "Reset zoom", "App", "font size"},
        {"app.quit", "Quit AgentDesk", "App", "exit"},
        {"app.palette", "", "", ""},  // not listed (the palette itself)
    };
    std::set<std::string> named;
    for (const auto& n : kNamed) {
        named.insert(n.cmd);
        if (*n.title != '\0')
            add(n.title, n.cmd, n.group, n.words);
        if (std::string_view(n.cmd) == "app.quit")
            for (const Palette* p : all_palettes())
                add(std::string("Theme: ") + p->label, std::string("theme:") + p->id, "App", "colors look");
    }
    for (const auto& l : s.layouts) {  // "Layout: <Name>", capitalised
        std::string t = layout_display_name(l.name);
        if (!t.empty())
            t[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])));
        add("Layout: " + t, "layout:" + l.name, "Layout", "look " + l.name);
    }
    for (const auto& c : kCommands)
        if (!named.contains(c.id))
            add(c.label, c.id, command_group(c.id), "");
    for (const auto& c : s.mods->commands())
        add(c.mod + ": " + c.label, "mod:" + c.mod + ":" + c.id, "Mods", "");
    return all;
}

// The entries that match `q`: fuzzy on the title (matched letters marked), else on
// "<group> <title> <command> <words>" at 4 points less and with no letters marked.
std::vector<PaletteEntry> palette_matches(std::vector<PaletteEntry>& all, const std::string& q) {
    std::vector<PaletteEntry> shown;
    for (auto& e : all) {
        if (auto sc = palette_score(e.title, q, &e.hits)) {
            e.score = *sc;
            shown.push_back(e);
        } else if (auto sc2 =
                       palette_score(e.group + " " + e.title + " " + e.cmd + " " + e.words, q, nullptr)) {
            e.score = *sc2 - 4;
            e.hits.clear();
            shown.push_back(e);
        }
    }
    return shown;
}

// The list's rows, and the selectable entries in order. Empty query: "Recent" (the 5 newest of
// the remembered 20), then each group in a fixed order without those. Typing: best score first,
// the more recently run first on a tie.
void palette_rows(App::State& s, const std::vector<PaletteEntry>& shown, const std::string& q,
                  std::vector<PaletteRow>& rows, std::vector<const PaletteEntry*>& order) {
    const auto& recent_cmds = s.settings.palette_recent;
    if (q.empty()) {
        std::vector<const PaletteEntry*> recent;
        for (const auto& cmd : recent_cmds) {
            if (recent.size() >= 5)
                break;
            for (const auto& e : shown)
                if (e.cmd == cmd) {
                    recent.push_back(&e);
                    break;
                }
        }
        auto is_recent = [&](const PaletteEntry* e) {
            return std::find(recent.begin(), recent.end(), e) != recent.end();
        };
        for (const auto* e : recent) {
            if (order.empty())
                rows.push_back({nullptr, "Recent"});
            rows.push_back({e, {}});
            order.push_back(e);
        }
        for (const char* g :
             {"Tab", "Pane", "Space", "Layout", "Terminal", "Agent", "Window", "App", "Mods"}) {
            bool head = false;
            for (const auto& e : shown) {
                if (e.group != g || is_recent(&e))
                    continue;
                if (!head) {
                    rows.push_back({nullptr, g});
                    head = true;
                }
                rows.push_back({&e, {}});
                order.push_back(&e);
            }
        }
        return;
    }
    auto recency = [&](const std::string& cmd) {
        const auto it = std::find(recent_cmds.begin(), recent_cmds.end(), cmd);
        return it == recent_cmds.end() ? 1000 : static_cast<int>(it - recent_cmds.begin());
    };
    std::vector<const PaletteEntry*> sorted;
    for (const auto& e : shown)
        sorted.push_back(&e);
    std::stable_sort(sorted.begin(), sorted.end(), [&](const PaletteEntry* a, const PaletteEntry* b) {
        if (a->score != b->score)
            return a->score > b->score;
        return recency(a->cmd) < recency(b->cmd);
    });
    for (const auto* e : sorted) {
        rows.push_back({e, {}});
        order.push_back(e);
    }
}

// The search box: 36 px high, rounded 9, the background fill with an accent border; a 16 px
// search icon, the 12.8 px input and a dim "Esc" at the right. Then 10 px.
void palette_search_box(App::State& s) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 sp = ImGui::GetCursorScreenPos();
    const float fw = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(sp, ImVec2(sp.x + fw, sp.y + px(36)), g_pal->bg, px(9));
    dl->AddRect(sp, ImVec2(sp.x + fw, sp.y + px(36)), g_pal->focus, px(9));
    draw_icon(s, dl, ui::icon::kSearch, fpx(16), ImVec2(sp.x + px(10) + px(8), sp.y + px(18)), g_pal->dim);
    const float esc_w = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, "Esc").x;
    dl->AddText(s.fonts.ui, fpx(11), ImVec2(sp.x + fw - px(10) - esc_w, sp.y + px(18) - fpx(11) * 0.6f),
                g_pal->dim, "Esc");
    const float input_x = px(10) + px(16) + px(10);
    ImGui::SetCursorScreenPos(ImVec2(sp.x + input_x, sp.y));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, (px(36) - fpx(12.8f)) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(alpha(g_pal->text, 0)));
    ImGui::PushFont(s.fonts.ui, 12.8f);
    ImGui::SetNextItemWidth(fw - input_x - px(10) - esc_w - px(10));
    if (ImGui::InputTextWithHint("##q", "Type a command\xE2\x80\xA6", s.palette_buf, sizeof s.palette_buf))
        s.palette_sel = 0;
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(ImVec2(sp.x, sp.y + px(36) + px(10)));
}

bool palette_nav_key_pressed() {
    return ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
           ImGui::IsKeyPressed(ImGuiKey_PageDown) || ImGui::IsKeyPressed(ImGuiKey_PageUp);
}

// One entry row, 32 px, 6 px in from the sides, rounded 7 when selected or hovered. The title's
// matched letters are accent semibold, the rest foreground when selected, else muted; the key
// chips at the right, and the group left of them while typing. Returns true when clicked.
bool palette_entry_row(App::State& s, ImDrawList* d, const PaletteEntry& e, int idx, float lw, bool typing) {
    const ImU32 muted = mix(g_pal->dim, g_pal->text, 0.4f);
    const ImVec2 a0 = ImGui::GetCursorScreenPos();
    const ImVec2 a(a0.x + px(6), a0.y);
    const float rw = lw - px(12);
    ImGui::PushID(idx);
    ImGui::SetCursorScreenPos(a);
    const bool clicked = ImGui::InvisibleButton("##e", ImVec2(rw, px(32)));
    const bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    const bool sel = idx == s.palette_sel;
    if (hov || sel)
        d->AddRectFilled(a, ImVec2(a.x + rw, a.y + px(32)),
                         hov ? tok::hover(popover_color()) : tok::selected(popover_color()), px(7));
    if (sel && palette_nav_key_pressed())
        ImGui::SetScrollHereY();
    const float cy = a.y + px(16);
    // The title one UTF-8 character at a time, each in its own font and colour.
    float x = a.x + px(10);
    for (std::size_t i = 0; i < e.title.size();) {
        std::size_t len = 1;
        const unsigned char c = static_cast<unsigned char>(e.title[i]);
        if (c >= 0xF0)
            len = 4;
        else if (c >= 0xE0)
            len = 3;
        else if (c >= 0xC0)
            len = 2;
        const bool hit = std::find(e.hits.begin(), e.hits.end(), static_cast<int>(i)) != e.hits.end();
        ImFont* f = hit ? s.fonts.ui_bold : s.fonts.ui;
        const char* b = e.title.c_str() + i;
        d->AddText(f, fpx(12.8f), ImVec2(x, cy - fpx(12.8f) * 0.6f),
                   hit   ? g_pal->focus
                   : sel ? g_pal->text
                         : muted,
                   b, b + len);
        x += f->CalcTextSizeA(fpx(12.8f), FLT_MAX, 0, b, b + len).x;
        i += len;
    }
    float right = a.x + rw - px(10);
    right -= key_chips(s, d, right, cy, e.chord);
    if (typing) {
        const float gw = s.fonts.ui->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, e.group.c_str()).x;
        d->AddText(s.fonts.ui, fpx(11.5f),
                   ImVec2(right - gw - (e.chord.empty() ? 0.0f : px(10)), cy - fpx(11.5f) * 0.6f), g_pal->dim,
                   e.group.c_str());
    }
    return clicked;
}

// The list under the search box, or a line saying nothing matches. Returns the clicked command.
std::string palette_list(App::State& s, const std::vector<PaletteRow>& rows, bool any, bool typing) {
    std::string run;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float fw = ImGui::GetContentRegionAvail().x;
    float list_h = 0;
    for (const auto& row : rows)
        list_h += row.e == nullptr ? px(26) : px(32);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    if (any) {
        // At most 320 px (10 rows), and the whole overlay within 85 % of the window's height.
        const float room = vp->WorkPos.y + vp->WorkSize.y * 0.85f - ImGui::GetCursorScreenPos().y - px(24);
        ImGui::BeginChild("##list", ImVec2(fw, std::max(px(64), std::min({px(320), list_h, room}))),
                          ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        ImDrawList* d = ImGui::GetWindowDrawList();
        const float lw = ImGui::GetContentRegionAvail().x;
        int idx = -1;
        for (const auto& row : rows) {
            if (row.e == nullptr) {  // group title: 26 px, 11 px semibold dim, 14 px in, 4 px off the bottom
                const ImVec2 a = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(lw, px(26)));
                d->AddText(s.fonts.ui_bold, fpx(11), ImVec2(a.x + px(14), a.y + px(26) - px(4) - fpx(11)),
                           g_pal->dim, row.header.c_str());
                continue;
            }
            ++idx;
            if (palette_entry_row(s, d, *row.e, idx, lw, typing))
                run = row.e->cmd;
        }
        ImGui::EndChild();
        ImGui::Dummy(ImVec2(0, px(6)));
    } else {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const std::string msg =
            std::string("Nothing matches \"") + s.palette_buf + "\". Places are in the picker.";
        ImGui::Dummy(ImVec2(fw, px(14) * 2 + fpx(13)));
        ImGui::GetWindowDrawList()->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + px(16), a.y + px(14)),
                                            g_pal->dim, msg.c_str());
    }
    ImGui::PopStyleVar();
    return run;
}

// Puts `cmd` first in the remembered recent commands (20 kept) and saves.
void remember_recent(App::State& s, const std::string& cmd) {
    auto& r = s.settings.palette_recent;
    r.erase(std::remove(r.begin(), r.end(), cmd), r.end());
    r.insert(r.begin(), cmd);
    if (r.size() > 20)
        r.resize(20);
    save_settings(s.settings);
}

}  // namespace

// Command palette: 620 wide, 60 px from the top, 10 px between its parts.
void draw_palette(App::State& s) {
    if (!s.show_palette)
        return;
    std::vector<PaletteEntry> all = palette_entries(s);
    std::string q;  // spaces are ignored
    for (const char* c = s.palette_buf; *c != 0; ++c)
        if (*c != ' ')
            q += *c;
    const std::vector<PaletteEntry> shown = palette_matches(all, q);
    std::vector<PaletteRow> rows;
    std::vector<const PaletteEntry*> order;
    palette_rows(s, shown, q, rows, order);
    const int n = static_cast<int>(order.size());
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = std::min(px(620), vp->WorkSize.x - px(16));
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + px(60)));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    push_overlay_style();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(10), px(10)));
    ImGui::Begin("##palette", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    palette_search_box(s);
    // Up/Down stop at the ends; PageUp/PageDown move 10.
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        ++s.palette_sel;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        --s.palette_sel;
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
        s.palette_sel += 10;
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
        s.palette_sel -= 10;
    s.palette_sel = std::clamp(s.palette_sel, 0, std::max(0, n - 1));
    std::string run = palette_list(s, rows, n > 0, !q.empty());
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && n > 0)
        run = order[static_cast<std::size_t>(s.palette_sel)]->cmd;
    const bool close =
        ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
         (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)));
    ImGui::End();
    pop_overlay_style();
    if (!run.empty() || close) {
        s.show_palette = false;
        s.focus_view(s.ws.focused_session());
    }
    if (!run.empty()) {
        remember_recent(s, run);
        run_command(s, run);
    }
}

// ------------------------------------------------------------------ "Go to" picker
namespace {
struct PickerRow {
    enum Kind { kSpace, kTab, kEarlier, kNew } kind;
    int space = -1;
    int tab = -1;
    std::string label, detail, cmd;
    Attention att = Attention::kNone;
};

// A space's tab rows that pass the filter.
struct PickerGroup {
    int index;
    std::vector<PickerRow> tabs;
};

// What a tab row says at its right: its agents by program name ("agent" without one) joined by
// " · ", then "  +N" for the live terminals they started (and those started in turn). `names`
// gets the agents' program names for the filter.
std::string tab_agents(App::State& s, const std::vector<std::string>& ids, std::string& names) {
    std::string programs;
    std::vector<std::string> mine;
    for (const auto& id : ids) {
        if (const auto it = s.views_by_id.find(id); it != s.views_by_id.end() && is_agent(it->second)) {
            std::string pn = program_of(it->second).name;
            if (pn.empty())
                pn = "agent";
            programs += (programs.empty() ? "" : " \xC2\xB7 ") + pn;
            names += " " + program_of(it->second).name;
            mine.push_back(id);
        }
    }
    int team = 0;
    std::set<std::string> seen(mine.begin(), mine.end());
    std::vector<std::string> todo = mine;
    while (!todo.empty()) {
        const std::string p = todo.back();
        todo.pop_back();
        for (const auto& [cid, cv] : s.views_by_id)
            if (cv.parent == p && cv.process_state == "Running" && seen.insert(cid).second) {
                ++team;
                todo.push_back(cid);
            }
    }
    if (team > 0)
        programs += "  +" + std::to_string(team);
    return programs;
}

// Each space's tabs matching `q` (a lower-case substring of "<space> <tab> <programs>"), and
// per space what plan_picker needs. A space counts as busy when any of its tabs has a status,
// idle included.
std::vector<PickerGroup> picker_groups(App::State& s, const std::string& q,
                                       const std::map<std::string, std::string>& cwds,
                                       std::vector<ui::PickerSpace>& facts) {
    std::vector<PickerGroup> groups;
    for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i) {
        const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
        const std::string sname = s.ws.space_title(i, cwds);
        PickerGroup sr{i, {}};
        std::vector<Attention> space_atts;
        for (const auto& tab : sp.tabs)
            for (const auto& id : s.ws.sessions_in(tab))
                space_atts.push_back(s.attention_of(id));
        const Attention space_att = ui::most_urgent(space_atts);
        for (int t = 0; t < static_cast<int>(sp.tabs.size()); ++t) {
            const auto& tab = sp.tabs[static_cast<std::size_t>(t)];
            const auto ids = s.ws.sessions_in(tab);
            std::vector<Attention> atts;
            for (const auto& id : ids)
                atts.push_back(s.attention_of(id));
            std::string names;
            const std::string programs = tab_agents(s, ids, names);
            const std::string tname = ui::Workspace::tab_title(tab, t);  // an unnamed tab shows its number
            if (!q.empty() && ascii_lower(sname + " " + tname + names).find(q) == std::string::npos)
                continue;
            sr.tabs.push_back(PickerRow{PickerRow::kTab, i, t, tname, programs, "", ui::most_urgent(atts)});
        }
        facts.push_back({space_att != Attention::kNone, space_att == Attention::kBlocked,
                         static_cast<int>(sr.tabs.size())});
        groups.push_back(std::move(sr));
    }
    return groups;
}

// The rows in order: busy spaces (blocked first) with their tabs, then the "earlier: N quiet
// spaces" toggle and, when expanded or while filtering, the quiet spaces; then the "+ new" rows.
std::vector<PickerRow> picker_rows(App::State& s, const std::string& q) {
    const auto cwds = s.session_cwds();
    std::vector<ui::PickerSpace> facts;
    const std::vector<PickerGroup> groups = picker_groups(s, q, cwds, facts);
    const auto plan = ui::plan_picker(facts, !q.empty(), s.picker_earlier);
    std::vector<PickerRow> rows;
    auto put = [&](int g) {
        const auto& sr = groups[static_cast<std::size_t>(g)];
        rows.push_back(
            {PickerRow::kSpace, sr.index, -1, s.ws.space_title(sr.index, cwds), "", "", Attention::kNone});
        for (const auto& t : sr.tabs)
            rows.push_back(t);
    };
    for (const int g : plan.busy)
        put(g);
    if (plan.earlier_row) {
        char b[64];
        std::snprintf(b, sizeof b, "earlier: %zu quiet space%s %s", plan.quiet.size(),
                      plan.quiet.size() > 1 ? "s" : "", s.picker_earlier ? "\xE2\x96\xB4" : "\xE2\x96\xBE");
        rows.push_back({PickerRow::kEarlier, -1, -1, b, "", "", Attention::kNone});
    }
    if (plan.quiet_listed)
        for (const int g : plan.quiet)
            put(g);
    rows.push_back({PickerRow::kNew, -1, -1, "+ new tab", "", "tab.new", Attention::kNone});
    rows.push_back({PickerRow::kNew, -1, -1, "+ new space", "", "space.new", Attention::kNone});
    for (const auto& r : s.remotes)
        if (r.host->connected())
            rows.push_back({PickerRow::kNew, -1, -1, "+ new space on " + r.cfg.name, "",
                            "space.new_on:" + r.cfg.name, Attention::kNone});
    return rows;
}

float picker_row_height(const PickerRow& r) {
    return r.kind == PickerRow::kSpace     ? px(31)
           : r.kind == PickerRow::kEarlier ? px(35)
           : r.kind == PickerRow::kNew     ? px(36)
                                           : px(32);
}

// A space heading: 10 px in, 10 above, 3 below; an 18 px avatar (rounded 5) with the first
// letter in white 10.5 px bold, then 8 px on, the name 11 px semibold dim.
void picker_space_heading(App::State& s, ImDrawList* d, const PickerRow& r, float fw) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(fw, px(31)));
    const auto& spc = s.ws.spaces[static_cast<std::size_t>(r.space)];
    const float side = px(18), cy = a.y + px(10) + side * 0.5f;
    d->AddRectFilled(ImVec2(a.x + px(10), cy - side * 0.5f), ImVec2(a.x + px(10) + side, cy + side * 0.5f),
                     r.label == "~" ? g_pal->faint : avatar_color(spc.color), px(5));
    const std::string letter =
        r.label.empty()
            ? std::string("?")
            : std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(r.label[0]))));
    const ImVec2 lt = s.fonts.ui_bold->CalcTextSizeA(fpx(10.5f), FLT_MAX, 0, letter.c_str());
    d->AddText(s.fonts.ui_bold, fpx(10.5f), ImVec2(a.x + px(10) + (side - lt.x) * 0.5f, cy - lt.y * 0.5f),
               IM_COL32(255, 255, 255, 255), letter.c_str());
    const ImVec2 nt = s.fonts.ui_bold->CalcTextSizeA(fpx(11), FLT_MAX, 0, r.label.c_str());
    text_clipped(d, s.fonts.ui_bold, fpx(11), ImVec2(a.x + px(10) + side + px(8), cy - nt.y * 0.5f),
                 g_pal->faint, r.label, fw - px(20) - side - px(8));
}

// A tab row's content: the status glyph, the name (semibold for the tab on screen) taking the
// room, then 10 px apart at the row's end "needs you" / "finished" and the agents, both 11 px.
void picker_tab_content(App::State& s, ImDrawList* d, const PickerRow& r, ImVec2 a, float fw, float cy) {
    const float gx = a.x + px(33);
    attention_glyph(s, d, gx - px(2), gx + px(4), cy, r.att, g_pal->faint);
    const bool here =
        s.ws.active_space == r.space && s.ws.active() != nullptr && s.ws.active()->active_tab == r.tab;
    const float x = gx + px(16);
    float right = a.x + fw - px(4) - px(10);
    const ImVec2 wt = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, r.detail.c_str());
    if (!r.detail.empty()) {
        right -= wt.x;
        d->AddText(s.fonts.ui, fpx(11), ImVec2(right, cy - wt.y * 0.5f), g_pal->faint, r.detail.c_str());
        right -= px(10);
    }
    if (r.att == Attention::kBlocked || r.att == Attention::kDone) {
        const char* word = r.att == Attention::kBlocked ? "needs you" : "finished";
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, word);
        right -= t.x;
        d->AddText(s.fonts.ui, fpx(11), ImVec2(right, cy - t.y * 0.5f), attention_color(r.att), word);
        right -= px(10);
    }
    ImFont* nf = here ? s.fonts.ui_bold : s.fonts.ui;
    const float nh = nf->CalcTextSizeA(fpx(13), FLT_MAX, 0, "Ag").y;
    text_clipped(d, nf, fpx(13), ImVec2(x, cy - nh * 0.5f), g_pal->text, r.label, std::max(0.0f, right - x));
}

// What a picker list pass asked for.
struct PickerAction {
    std::string run;
    bool toggle_earlier = false;
};

// The rows, in a list at most 320 px high; selectable rows are numbered by `k` for picker_sel.
PickerAction picker_list(App::State& s, const std::vector<PickerRow>& rows, float fw) {
    PickerAction act;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    float list_h = 0;
    for (const auto& r : rows)
        list_h += picker_row_height(r);
    ImGui::BeginChild("##plist", ImVec2(fw, std::min(px(320), list_h + px(2))), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    int k = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& r = rows[static_cast<std::size_t>(i)];
        if (r.kind == PickerRow::kSpace) {
            picker_space_heading(s, d, r, fw);
            continue;
        }
        ++k;
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float rh = picker_row_height(r);
        ImGui::PushID(i);
        const bool clicked = ImGui::InvisibleButton("##r", ImVec2(fw - px(4), rh));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hov && (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0))
            s.picker_sel = k;  // the mouse selects only when it moves, so keys keep working
        const bool sel = k == s.picker_sel;
        if (sel) {
            d->AddRectFilled(a, ImVec2(a.x + fw - px(4), a.y + rh), tok::selected(popover_color()), px(7));
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                ImGui::SetScrollHereY();
        }
        const float cy = a.y + rh * 0.5f;
        if (r.kind == PickerRow::kTab)
            picker_tab_content(s, d, r, a, fw, cy);
        else
            d->AddText(s.fonts.ui, fpx(r.kind == PickerRow::kEarlier ? 12.0f : 13.0f),
                       ImVec2(a.x + px(12), cy - fpx(13) * 0.6f),
                       r.kind == PickerRow::kEarlier ? g_pal->faint : g_pal->dim, r.label.c_str());
        if (clicked || (sel && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
            if (r.kind == PickerRow::kEarlier)
                act.toggle_earlier = true;
            else if (r.kind == PickerRow::kNew)
                act.run = r.cmd;
            else
                act.run = "tab.goto:" + std::to_string(r.space) + ":" + std::to_string(r.tab);
        }
        if (sel && r.kind == PickerRow::kTab && ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_D, false))
            act.run = "tab.close_at:" + std::to_string(r.space) + ":" + std::to_string(r.tab);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    return act;
}

// Opening (or typing) selects the current tab, or the first matching tab while filtering;
// Up/Down wrap.
void picker_select(App::State& s, const std::vector<PickerRow>& rows, const std::string& q) {
    std::vector<int> selectable;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (rows[static_cast<std::size_t>(i)].kind != PickerRow::kSpace)
            selectable.push_back(i);
    if (s.picker_sel < 0 || s.picker_reset) {
        s.picker_sel = 0;
        for (int k = 0; k < static_cast<int>(selectable.size()); ++k) {
            const auto& r = rows[static_cast<std::size_t>(selectable[static_cast<std::size_t>(k)])];
            if (r.kind == PickerRow::kTab && (!q.empty() || (r.space == s.ws.active_space && s.ws.active() &&
                                                             r.tab == s.ws.active()->active_tab))) {
                s.picker_sel = k;
                break;
            }
        }
        s.picker_reset = false;
    }
    const int n = static_cast<int>(selectable.size());
    if (n > 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            s.picker_sel = (s.picker_sel + 1) % n;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            s.picker_sel = (s.picker_sel + n - 1) % n;
        s.picker_sel = std::clamp(s.picker_sel, 0, n - 1);
    }
}

}  // namespace

// "Go to…" picker: spaces with their tabs, busy ones first, quiet ones folded under "earlier";
// a substring filter; Enter goes, Ctrl+D closes the selected tab.
void draw_picker(App::State& s) {
    if (!s.show_picker)
        return;
    const std::string q = ascii_lower(s.picker_buf);
    const std::vector<PickerRow> rows = picker_rows(s, q);
    picker_select(s, rows, q);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = std::min(px(640), vp->WorkSize.x - px(16));
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + px(40)));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    push_overlay_style();
    ImGui::Begin("##picker", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_AlwaysAutoResize);
    bool close = overlay_title(s, "Go to\xE2\x80\xA6");
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    // The filter: 36 px, rounded 7, an accent border, with room on the left for the search icon.
    const ImVec2 sp = ImGui::GetCursorScreenPos();
    const float fw = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(34), (px(36) - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(7));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, v4(g_pal->focus));
    ImGui::SetNextItemWidth(fw);
    if (ImGui::InputTextWithHint("##pq", "Go to a tab, space or agent\xE2\x80\xA6", s.picker_buf,
                                 sizeof s.picker_buf))
        s.picker_reset = true;
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    draw_icon(s, ImGui::GetWindowDrawList(), ui::icon::kSearch, fpx(14), ImVec2(sp.x + px(17), sp.y + px(18)),
              g_pal->faint);
    ImGui::Dummy(ImVec2(0, px(6)));
    const PickerAction act = picker_list(s, rows, fw);
    close = close || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                     ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
             (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)));
    ImGui::End();
    pop_overlay_style();
    if (act.toggle_earlier)
        s.picker_earlier = !s.picker_earlier;  // stays open, selection stays on the toggle
    if (!act.run.empty() || close) {
        s.show_picker = false;
        s.focus_view(s.ws.focused_session());
    }
    if (!act.run.empty())
        run_command(s, act.run);
}

// ------------------------------------------------------------------ simple dialogs
void center_next(float w, float y_frac) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * y_frac),
        ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
}

void heading(App::State& s, const char* text) {
    ImGui::PushFont(s.fonts.ui_bold, 15);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

// The solid look of a dialog's main button: a foreground-coloured fill with card-coloured text.
void primary_button_style(bool push) {
    if (push) {
        ImGui::PushStyleColor(ImGuiCol_Button, v4(g_pal->text));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(alpha(g_pal->text, 220)));
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->card));
    } else {
        ImGui::PopStyleColor(3);
    }
}

// Keybinds, 640 x 520: every keys.json entry as written (a non-global section's name after its
// keys), grouped spaces / tabs / panes / agents / terminal / window, filtered by typing (a
// substring of the keys or of what they do).
void draw_help(App::State& s) {
    if (s.show_help && !ImGui::IsPopupOpen("##help"))
        ImGui::OpenPopup("##help");
    center_next_window(dialog_size(px(640), px(520)));
    push_overlay_style();
    const bool open = ImGui::BeginPopupModal(
        "##help", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    pop_overlay_style();
    if (!open)
        return;
    if (!s.show_help) {  // app.help pressed again
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    bool close = overlay_title(s, "Keybinds");
    if (ImGui::IsWindowAppearing()) {
        s.help_buf[0] = '\0';
        ImGui::SetKeyboardFocusHere();
    }
    // The filter: 36 px, 10 px in, rounded 9, an accent border on the background, 12.8 px text.
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), (px(36) - fpx(12.8f)) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(9));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, v4(g_pal->focus));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(g_pal->bg));
    ImGui::PushFont(s.fonts.ui, 12.8f);
    ImGui::InputTextWithHint("##hf", "Type to filter\xE2\x80\xA6", s.help_buf, sizeof s.help_buf);
    ImGui::PopFont();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    // keys.json as it is now (the defaults when it does not parse), read again when it changes.
    static std::vector<ui::Keymap::Binding> listed;
    static std::filesystem::file_time_type listed_at{};
    if (ImGui::IsWindowAppearing() || listed_at != s.keys_mtime) {
        listed_at = s.keys_mtime;
        listed = ui::Keymap::listing(Json::parse(read_text_file(data_dir() / L"keys.json"), nullptr, false));
    }
    const std::string f = ascii_lower(s.help_buf);
    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::BeginChild("##keys", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, px(2)));  // rows 2 px apart
    ImDrawList* d = ImGui::GetWindowDrawList();
    bool first = true;
    for (const char* g : {"spaces", "tabs", "panes", "agents", "terminal", "window"}) {
        bool head = false;
        for (const auto& b : listed) {
            if (std::string_view(ui::Keymap::help_group(b.action)) != g)
                continue;
            const std::string label = ui::Keymap::help_label(b.action);
            if (!f.empty() && b.keys.find(f) == std::string::npos && label.find(f) == std::string::npos)
                continue;
            if (!head) {  // group title: 11.5 px medium dim; groups 10 px apart
                if (!first)
                    ImGui::Dummy(ImVec2(0, px(10)));
                first = false;
                const ImVec2 a = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(0, fpx(11.5f) + px(4)));
                d->AddText(s.fonts.ui_medium, fpx(11.5f), a, g_pal->dim, g);
                head = true;
            }
            // 32 px, 8 px in: the keys (accent, 190 px column), then what they do.
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, px(32)));
            const std::string keys = b.keys + (b.section != "global" ? "  (" + b.section + ")" : "");
            text_clipped(d, s.fonts.ui, fpx(12), ImVec2(a.x + px(8), a.y + px(16) - fpx(12) * 0.6f),
                         g_pal->focus, keys, px(186));
            d->AddText(s.fonts.ui, fpx(12), ImVec2(a.x + px(8) + px(190), a.y + px(16) - fpx(12) * 0.6f),
                       g_pal->text, label.c_str());
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    close = close || ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click();
    if (close) {
        s.show_help = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_launcher(App::State& s) {
    if (s.show_launcher) {
        ImGui::OpenPopup("##launcher");
        s.show_launcher = false;
    }
    center_next(px(520), 0.14f);
    if (!ImGui::BeginPopupModal("##launcher", nullptr,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
        return;
    const char* what = s.launch_target == LaunchTarget::kSpace        ? "New space"
                       : s.launch_target == LaunchTarget::kSplitRight ? "Split right"
                       : s.launch_target == LaunchTarget::kSplitDown  ? "Split down"
                                                                      : "New tab";
    heading(s, what);
    ImGui::TextDisabled("Runs in agent-host; it keeps running when this window closes.");
    ImGui::Dummy(ImVec2(0, px(2)));
    ImGui::TextDisabled("Folder");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##cwd", s.launch_cwd, sizeof s.launch_cwd);
    ImGui::TextDisabled("Name (optional)");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##name", s.launch_name, sizeof s.launch_name);
    ImGui::Dummy(ImVec2(0, px(2)));
    if (!s.remotes.empty()) {
        ImGui::TextDisabled("Machine");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##machine", s.machine_label(s.launch_machine).c_str())) {
            if (ImGui::Selectable("This computer", s.launch_machine.empty()))
                s.launch_machine.clear();
            for (const auto& r : s.remotes) {
                const std::string label = r.cfg.name + (r.host->connected() ? "" : "  (not connected)");
                if (ImGui::Selectable(label.c_str(), s.launch_machine == r.cfg.name))
                    s.launch_machine = r.cfg.name;
            }
            ImGui::EndCombo();
        }
    }
    ui::HostClient* lhost = s.host_for(s.launch_machine);
    const auto profiles = lhost->profiles();
    s.launcher_sel = std::clamp(s.launcher_sel, 0, std::max(0, static_cast<int>(profiles.size()) - 1));
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        ++s.launcher_sel;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        --s.launcher_sel;
    s.launcher_sel = std::clamp(s.launcher_sel, 0, std::max(0, static_cast<int>(profiles.size()) - 1));
    int start = -1;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fs = ImGui::GetFontSize();
    // One 34 px row per profile: its program's 14 px icon, the name, and 1-9 as its key.
    for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
        ImGui::PushID(i);
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::Selectable("##p", i == s.launcher_sel, 0, ImVec2(0, px(34))))
            start = i;
        if (ImGui::IsItemHovered())
            s.launcher_sel = i;
        const auto& pid = profiles[static_cast<std::size_t>(i)].first;
        program_icon(dl, program_of_profile(lhost->profile_executable(pid), pid),
                     ImVec2(a.x + px(11), a.y + px(10)), px(14), g_pal->text);
        dl->AddText(s.fonts.ui_bold, fs, ImVec2(a.x + px(38), a.y + (px(34) - fs) * 0.5f), g_pal->text,
                    profiles[static_cast<std::size_t>(i)].second.c_str());
        if (i < 9) {
            char k[8];
            std::snprintf(k, sizeof k, "%d", i + 1);
            dl->AddText(s.fonts.ui, fs, ImVec2(a.x + w - px(18), a.y + (px(34) - fs) * 0.5f), g_pal->faint,
                        k);
            if (!ImGui::IsAnyItemActive() &&
                ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false))
                start = i;
        }
        ImGui::PopID();
    }
    if (profiles.empty())
        ImGui::TextDisabled("No profiles (agent-host not connected?)");
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !profiles.empty())
        start = s.launcher_sel;
    if (!s.launch_error.empty())
        ImGui::TextColored(v4(g_pal->needs_you), "%s", s.launch_error.c_str());
    if (s.launch_busy)
        ImGui::TextDisabled("Starting...");
    ImGui::Separator();
    if (ImGui::Button("New git worktree...")) {
        ImGui::CloseCurrentPopup();
        run_command(s, "worktree.new");
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click())
        ImGui::CloseCurrentPopup();
    if (start >= 0 && lhost->connected() && !s.launch_busy) {
        Placement p;
        p.machine = s.launch_machine;
        p.target = s.launch_target;
        p.split_of = s.launch_split_of;
        p.space = s.ws.active_space;
        p.space_id = s.ws.active() != nullptr ? s.ws.active()->id : std::string();
        s.launch_error.clear();
        s.settings.last_cwd = s.launch_cwd;
        save_settings(s.settings);
        create_session(s, profiles[static_cast<std::size_t>(start)].first, s.launch_cwd, s.launch_name, p);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

namespace {
// Applies the rename prompt's (trimmed) name to its space, tab or pane. An empty name goes
// back to the automatic one.
void apply_rename(App::State& s) {
    for (auto& sp : s.ws.spaces) {
        if (s.rename_kind == "space" && sp.id == s.rename_target)
            sp.name = s.rename_buf;
        for (auto& t : sp.tabs)
            if (s.rename_kind == "tab" && t.id == s.rename_target)
                t.name = s.rename_buf;
    }
    if (s.rename_kind == "pane") {
        const auto loc = s.ws.find(s.rename_target);
        if (loc.space >= 0) {
            auto& t =
                s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
            if (s.rename_buf[0] == '\0')
                t.pane_names.erase(s.rename_target);
            else
                t.pane_names[s.rename_target] = s.rename_buf;
        }
    }
}

}  // namespace

// The rename prompt (also used to name a duplicated mod): 380 wide, the title with a plain
// "Esc", a "Name" field prefilled and fully selected, a hint, Cancel and Save.
void draw_rename(App::State& s) {
    if (s.show_rename) {
        ImGui::OpenPopup("##rename");
        s.show_rename = false;
    }
    center_next(px(380), 0.22f);
    push_overlay_style();
    const bool open =
        ImGui::BeginPopupModal("##rename", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    pop_overlay_style();
    s.rename_open = open;
    if (!open) {
        s.rename_close = false;
        return;
    }
    const bool mod_clone = s.rename_kind == "mod.clone";
    // Pressing the rename key again closes the prompt. The field holds the keyboard, so the
    // command never runs: the prompt watches that command's keys itself.
    if (!mod_clone && !ImGui::IsWindowAppearing())
        for (const auto& [chord, cmd] : s.keymap.global_map())
            if (cmd.rfind(s.rename_kind + ".rename", 0) == 0 && ImGui::IsKeyChordPressed(chord))
                s.rename_close = true;
    const char* title = mod_clone                  ? "Duplicate mod"
                        : s.rename_kind == "space" ? "Rename space"
                        : s.rename_kind == "tab"   ? "Rename tab"
                                                   : "Rename pane";
    bool cancel = overlay_title(s, title, EscChip::kPlain);
    ImGui::PushFont(s.fonts.ui, 12);
    ImGui::TextDisabled(mod_clone ? "Which mod" : "Name");
    ImGui::PopFont();
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), (px(36) - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, v4(g_pal->focus));
    const bool enter =
        ImGui::InputText("##n", s.rename_buf, sizeof s.rename_buf,
                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PushFont(s.fonts.ui, 11.5f);
    ImGui::TextDisabled("%s", mod_clone                  ? "its folder name in the mods folder"
                              : s.rename_kind == "space" ? "empty: named after its folder"
                              : s.rename_kind == "tab"   ? "empty: its number"
                                                         : "empty: unnamed");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(6)));
    const float bw = px(80);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw * 2 - px(26));
    ImGui::PushStyleColor(ImGuiCol_Button, v4(raised_color()));
    cancel = ImGui::Button("Cancel", ImVec2(bw, px(30))) || cancel || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
             dialog_outside_click() || std::exchange(s.rename_close, false);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    primary_button_style(true);
    const bool pressed = ImGui::Button(mod_clone ? "Duplicate" : "Save", ImVec2(bw, px(30))) || enter;
    const bool ok = !cancel && pressed;
    primary_button_style(false);
    if (ok) {  // surrounding white space is dropped
        std::string name = s.rename_buf;
        const auto b = name.find_first_not_of(" \t\r\n");
        const auto e = name.find_last_not_of(" \t\r\n");
        name = b == std::string::npos ? std::string() : name.substr(b, e - b + 1);
        std::snprintf(s.rename_buf, sizeof s.rename_buf, "%s", name.c_str());
    }
    if (cancel)
        ImGui::CloseCurrentPopup();
    if (ok && mod_clone) {
        ImGui::CloseCurrentPopup();
        if (s.rename_buf[0] != '\0')
            run_command(s, std::string("mod.clone:") + s.rename_buf);
    } else if (ok) {
        apply_rename(s);
        ImGui::CloseCurrentPopup();
        s.focus_view(s.ws.focused_session());
    }
    ImGui::EndPopup();
}
}  // namespace detail

}  // namespace agentdesk::desktop
