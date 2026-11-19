// The welcome dialog shown on first run: four pages with dots, Back and Next.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "overlay_kit.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
constexpr int kPages = 4;
constexpr float kHeaderH = 52;  // the drag bar at the top
constexpr float kFooterH = 56;  // dots and buttons at the bottom

// A page title (18 px semibold) over its paragraph (dim, wrapped), then 14 px.
void page_title(App::State& s, const char* t, const char* sub) {
    ImGui::PushFont(s.fonts.ui_bold, 18);
    ImGui::TextWrapped("%s", t);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
    ImGui::TextWrapped("%s", sub);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, px(14)));
}

// A feature: a 32 px tile (rounded 9, raised, 1 px border) with a 16 px accent icon, 12 px on,
// the title (13.5 px medium) over its text (dim, wrapped to the dialog width `w` less 120 px).
void feature(App::State& s, float w, const char* glyph, const char* head, const char* text) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImDrawList* fd = ImGui::GetWindowDrawList();
    fd->AddRectFilled(a, ImVec2(a.x + px(32), a.y + px(32)), raised_color(), px(9));
    fd->AddRect(a, ImVec2(a.x + px(32), a.y + px(32)), g_pal->border, px(9));
    draw_icon(s, fd, glyph, fpx(16), ImVec2(a.x + px(16), a.y + px(16)), g_pal->focus);
    ImGui::SetCursorScreenPos(ImVec2(a.x + px(32) + px(12), a.y));
    ImGui::BeginGroup();
    ImGui::PushFont(s.fonts.ui_medium, 13.5f);
    ImGui::TextUnformatted(head);
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - px(120));
    ImGui::TextWrapped("%s", text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, px(10)));
}

// A paragraph: 13 px dim text on 20 px lines, wrapped to `width` and drawn at `p` (only
// measured when `draw` is false). Returns its height.
float para_at(App::State& s, ImVec2 p, float width, const char* text, bool draw = true) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    float y = p.y;
    for_each_wrapped_line(s.fonts.ui, fpx(13), text, width, [&](const char* a, const char* b) {
        if (draw) {
            const float th = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, a, b).y;
            d->AddText(s.fonts.ui, fpx(13), ImVec2(p.x, y + (px(20) - th) * 0.5f), g_pal->dim, a, b);
        }
        y += px(20);
    });
    return y - p.y;
}

// A paragraph at the cursor, the full width.
void para(App::State& s, const char* text) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = para_at(s, p, ImGui::GetContentRegionAvail().x, text);
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, h));
}

// A gap of `g` between blocks, the body's own item spacing taken off.
void gap(float g) {
    ImGui::Dummy(ImVec2(0, std::max(0.0f, g - 2 * ImGui::GetStyle().ItemSpacing.y)));
}

// The 52 px header: a small logo, "Welcome to AgentDesk", "Drag to move" and a close button.
// Only the header drags the dialog (and shows the hand cursor). Then the scrolling body begins
// between the header and the footer, 22 px in. Returns true when the close button was clicked.
bool welcome_header(App::State& s, float w, float h) {
    bool skip = false;
    ImDrawList* hd = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float hh = px(kHeaderH);
    const float cy = wp.y + hh * 0.5f;
    float x = wp.x + px(20);
    const float m = px(24);
    hd->AddRectFilled(ImVec2(x, cy - m * 0.5f), ImVec2(x + m, cy + m * 0.5f), g_pal->focus, px(6));
    hd->AddRectFilled(ImVec2(x + px(6), cy - px(5)), ImVec2(x + px(17), cy - px(2)), g_pal->card, px(1));
    hd->AddRectFilled(ImVec2(x + px(6), cy + px(2)), ImVec2(x + px(13), cy + px(5)), g_pal->card, px(1));
    x += m + px(10);
    const char* kTitle = "Welcome to AgentDesk";
    const ImVec2 tt = s.fonts.ui_bold->CalcTextSizeA(fpx(14), FLT_MAX, 0, kTitle);
    hd->AddText(s.fonts.ui_bold, fpx(14), ImVec2(x, cy - tt.y * 0.5f), g_pal->text, kTitle);
    const float close_x = wp.x + w - px(12) - px(26);
    const ImVec2 dt = s.fonts.ui->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, "Drag to move");
    hd->AddText(s.fonts.ui, fpx(11.5f), ImVec2(close_x - px(10) - dt.x, cy - dt.y * 0.5f), g_pal->faint,
                "Drag to move");
    ImGui::SetCursorScreenPos(ImVec2(close_x, cy - px(13)));
    if (ImGui::InvisibleButton("##w-close", ImVec2(px(26), px(26))))
        skip = true;
    const bool close_hov = ImGui::IsItemHovered();
    if (close_hov)
        hd->AddRectFilled(ImVec2(close_x, cy - px(13)), ImVec2(close_x + px(26), cy + px(13)), g_pal->hover,
                          px(7));
    draw_icon(s, hd, ui::icon::kClose, fpx(14), ImVec2(close_x + px(13), cy),
              close_hov ? g_pal->text : g_pal->dim);
    ImGui::SetCursorScreenPos(wp);
    ImGui::InvisibleButton("##w-drag", ImVec2(std::max(1.0f, close_x - px(4) - wp.x), hh));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        s.welcome_pos.x += ImGui::GetIO().MouseDelta.x;
        s.welcome_pos.y += ImGui::GetIO().MouseDelta.y;
    }
    hd->AddLine(ImVec2(wp.x, wp.y + hh), ImVec2(wp.x + w, wp.y + hh), g_pal->border);
    hd->AddLine(ImVec2(wp.x, wp.y + h - px(kFooterH)), ImVec2(wp.x + w, wp.y + h - px(kFooterH)),
                g_pal->border);
    ImGui::SetCursorScreenPos(ImVec2(wp.x, wp.y + hh + 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(22), px(22)));
    ImGui::BeginChild("##w-body", ImVec2(w, h - hh - 1 - px(kFooterH + 1)),
                      ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    return skip;
}

// Page 1: what AgentDesk is, three features, then Appearance and Size.
bool page_intro(App::State& s, float w) {
    bool changed = false;
    page_title(
        s, "AgentDesk runs your terminals in the background",
        "The terminals and agents you open here don't live in this window. They run in agent-host, a small "
        "background program on your computer. Close AgentDesk, update it, or let it crash, and your agents "
        "keep working. Open it again and everything is where you left it, as long as the computer is on.");
    feature(
        s, w, ui::icon::kBot, "Agents tell you when they need you",
        "A ring while one works, \xE2\x80\x9CNeeds you\xE2\x80\x9D when it asks something, a sound when it "
        "finishes.");
    feature(s, w, ui::icon::kMessages, "Agents can start helpers",
            "They show up under the agent that started them. Click one to watch it.");
    feature(s, w, ui::icon::kLayers, "Your other machines, in the same window",
            "A server or a Mac mini runs its own agent-host, so its agents keep going even while this laptop "
            "sleeps.");
    {  // a 1 px line before the settings
        const ImVec2 lp = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(lp, ImVec2(lp.x + ImGui::GetContentRegionAvail().x, lp.y),
                                            g_pal->border);
        ImGui::Dummy(ImVec2(0, px(10)));
    }
    ImGui::TextDisabled("Appearance");
    ImGui::SameLine(px(120));
    if (segmented(s, "##wappearance", s.settings.appearance,
                  {{"system", "System"}, {"light", "Light"}, {"dark", "Dark"}}))
        changed = true;
    ImGui::TextDisabled("Size");
    ImGui::SameLine(px(120));
    if (stepper(s, "wzoom", s.settings.zoom, 0.1f, 0.5f, 3.0f, "%.0f%%", 100.0f)) {
        apply_zoom(s);
        changed = true;
    }
    return changed;
}

// Page 2: one rounded box (12 px corners, 1 px border, the background) listing "This computer",
// each machine and an "Add a machine" row, then a note. Rows are 42 px with a bottom border: a
// 15 px dim icon, the name (13 px), the state (12 px dim) and an 8 px dot, green when connected.
void page_machines(App::State& s) {
    page_title(
        s, "Add your other machines",
        "A desktop at home, a server: AgentDesk reaches them over ssh, and agents there keep running while "
        "this PC sleeps.");
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 b0 = ImGui::GetCursorScreenPos();
    const float bw = ImGui::GetContentRegionAvail().x;
    const float rh = px(42);
    const int n_rows = 1 + static_cast<int>(s.settings.machines.size());
    const float bh = rh * static_cast<float>(n_rows + 1);
    d->AddRectFilled(b0, ImVec2(b0.x + bw, b0.y + bh), g_pal->bg, px(12));
    auto mrow = [&](int i, const char* glyph, const std::string& name, const std::string& note, bool ok) {
        const float y = b0.y + rh * static_cast<float>(i);
        const float cy = y + rh * 0.5f;
        draw_icon(s, d, glyph, fpx(15), ImVec2(b0.x + px(12) + px(7.5f), cy), g_pal->dim);
        const float nx = b0.x + px(12) + px(15) + px(10);
        const ImVec2 nt = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, note.c_str());
        const float dot_x = b0.x + bw - px(12) - px(8);
        const float note_x = dot_x - px(10) - nt.x;
        text_clipped(d, s.fonts.ui, fpx(13), ImVec2(nx, cy - line_h(13) * 0.5f), g_pal->text, name,
                     std::max(0.0f, note_x - px(10) - nx));
        d->AddText(s.fonts.ui, fpx(12), ImVec2(note_x, cy - nt.y * 0.5f), g_pal->faint, note.c_str());
        d->AddCircleFilled(ImVec2(dot_x + px(4), cy), px(4), ok ? g_pal->done : g_pal->faint);
        d->AddLine(ImVec2(b0.x, y + rh - 0.5f), ImVec2(b0.x + bw, y + rh - 0.5f), g_pal->border);
    };
    mrow(0, ui::icon::kMonitor, "This computer", "here", true);
    for (std::size_t i = 0; i < s.settings.machines.size(); ++i) {
        const auto& m = s.settings.machines[i];
        const std::string st = s.machine_state(m.name);
        mrow(static_cast<int>(i) + 1, ui::icon::kServer, m.name, st, st == "connected");
    }
    // The add row: accent 13 px text with a 15 px plus; the hover colour under the mouse.
    const ImVec2 ar(b0.x, b0.y + rh * static_cast<float>(n_rows));
    ImGui::SetCursorScreenPos(ar);
    if (ImGui::InvisibleButton("##w-add-machine", ImVec2(bw, rh)))
        s.show_machine_add = true;
    if (ImGui::IsItemHovered())
        d->AddRectFilled(ar, ImVec2(ar.x + bw, ar.y + rh), g_pal->hover, px(12),
                         ImDrawFlags_RoundCornersBottom);
    draw_icon(s, d, ui::icon::kPlus, fpx(15), ImVec2(ar.x + px(12) + px(7.5f), ar.y + rh * 0.5f),
              g_pal->focus);
    d->AddText(s.fonts.ui, fpx(13), ImVec2(ar.x + px(12) + px(15) + px(10), ar.y + (rh - line_h(13)) * 0.5f),
               g_pal->focus, "Add a machine");
    d->AddRect(b0, ImVec2(b0.x + bw, b0.y + bh), g_pal->border, px(12));
    ImGui::SetCursorScreenPos(b0);
    ImGui::Dummy(ImVec2(bw, bh));
    gap(px(18));
    para(s, "Not now? Add machine is at the bottom of the sidebar too.");
}

// A panel card: 12 px padding, rounded 12, a 1 px border (accent when on, dim on hover), an
// accent tint when on; a 32 px tile with the 16 px icon, the title (13.5 px semibold) with a
// "mods/<id>" chip (11 px dim on the hover colour), the description, and a 34 x 20 switch at the
// right. A click turns the panel on or off. Returns true when it did.
bool panel_card(App::State& s, const char* id, const char* glyph, const char* label, const char* text) {
    bool on = std::find(s.settings.panels.begin(), s.settings.panels.end(), id) != s.settings.panels.end();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float cw = ImGui::GetContentRegionAvail().x;
    const float tx = a.x + px(12) + px(32) + px(12);
    const float text_w = cw - px(12) - px(32) - px(12) - px(12) - px(34) - px(12);
    const float body_h = para_at(s, ImVec2(tx, 0), text_w, text, false);
    const float inner = std::max(px(32), line_h(13.5f) + px(4) + body_h);
    const float ch = px(12) + inner + px(12);
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##panel", ImVec2(cw, ch));
    const bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    const ImVec2 b(a.x + cw, a.y + ch);
    if (on)
        d->AddRectFilled(a, b, alpha(g_pal->focus, 0x10), px(12));
    d->AddRect(a, b, on ? g_pal->focus : hov ? g_pal->dim : g_pal->border, px(12));
    const ImVec2 t0(a.x + px(12), a.y + px(12));
    d->AddRectFilled(t0, ImVec2(t0.x + px(32), t0.y + px(32)), raised_color(), px(9));
    d->AddRect(t0, ImVec2(t0.x + px(32), t0.y + px(32)), g_pal->border, px(9));
    draw_icon(s, d, glyph, fpx(16), ImVec2(t0.x + px(16), t0.y + px(16)), g_pal->text);
    const ImVec2 lt = s.fonts.ui_bold->CalcTextSizeA(fpx(13.5f), FLT_MAX, 0, label);
    const float ly = a.y + px(12);
    d->AddText(s.fonts.ui_bold, fpx(13.5f), ImVec2(tx, ly + (line_h(13.5f) - lt.y) * 0.5f), g_pal->text,
               label);
    const std::string chip = std::string("mods/") + id;
    const ImVec2 ct = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, chip.c_str());
    const ImVec2 c0(tx + lt.x + px(8), ly + (line_h(13.5f) - ct.y - px(1)) * 0.5f);
    d->AddRectFilled(c0, ImVec2(c0.x + ct.x + px(12), c0.y + ct.y + px(1)), g_pal->hover, px(5));
    d->AddText(s.fonts.ui, fpx(11), ImVec2(c0.x + px(6), c0.y + px(0.5f)), g_pal->faint, chip.c_str());
    para_at(s, ImVec2(tx, ly + line_h(13.5f) + px(4)), text_w, text);
    const ImVec2 sw(b.x - px(12) - px(34), a.y + px(12) + px(4));
    d->AddRectFilled(sw, ImVec2(sw.x + px(34), sw.y + px(20)), on ? g_pal->focus : g_pal->border, px(10));
    d->AddCircleFilled(ImVec2(on ? sw.x + px(34) - px(10) : sw.x + px(10), sw.y + px(10)), px(8),
                       on ? accent_foreground() : g_pal->dim);
    gap(px(14));
    if (!clicked)
        return false;
    on = !on;
    if (on)
        s.settings.panels.push_back(id);
    else
        s.settings.panels.erase(std::remove(s.settings.panels.begin(), s.settings.panels.end(), id),
                                s.settings.panels.end());
    // Turning one on shows it at once; turning off the one shown (or the last one) hides it.
    if (auto* sp = s.ws.active()) {
        if (on)
            sp->panel = id;
        else if (sp->panel == id || s.settings.panels.empty())
            sp->panel.clear();
        s.doc_dirty = true;
        s.doc_dirty_at = Clock::now();
    }
    return true;
}

// Page 3: the Files and Changes panels as switches, then a box suggesting what else to ask for.
bool page_panels(App::State& s) {
    bool changed = false;
    page_title(
        s, "Choose your panels",
        "AgentDesk comes with a couple of panels. Turn one on and it appears beside your terminals. Drag "
        "this window aside to see it. Later, the buttons in the tab bar open and close them.");
    // The first visit to this page starts with both panels off; skipping the welcome leaves
    // both on.
    if (!s.welcome_panels_seen) {
        s.welcome_panels_seen = true;
        s.settings.panels.clear();
        changed = true;
    }
    changed |= panel_card(s, "files", ui::icon::kFile, "Files",
                          "The folder you're in as a tree. Open a file to read it, highlighted.");
    changed |= panel_card(s, "changes", ui::icon::kChanges, "Changes",
                          "What your agents changed: every file with its diff, and a commit box.");
    // 12 px padding, rounded 10, raised.
    const char* kBoard =
        "Ask your agent for a board of your agents, your pull requests, a cost meter, or the "
        "sidebar on the right. Not sure what's possible? Ask it that too.";
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float cw = ImGui::GetContentRegionAvail().x;
    const float th = para_at(s, ImVec2(a.x + px(12), 0), cw - px(24), kBoard, false);
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(a, ImVec2(a.x + cw, a.y + th + px(24)), raised_color(), px(10));
    para_at(s, ImVec2(a.x + px(12), a.y + px(12)), cw - px(24), kBoard);
    ImGui::Dummy(ImVec2(cw, th + px(24)));
    return changed;
}

// "One key opens everything", shown while a key opens the palette: 12 px padding, rounded 10,
// an accent border and tint; the key in a chip (8 x 3 px padding, rounded 6, bordered, raised,
// 13 px semibold), the title (13.5 px semibold) and a line. Then 16 px.
void palette_key_box(App::State& s, const std::string& pal) {
    const char* kPal =
        "It opens the command palette, with every action by name and its key. It works the same "
        "in every layout.";
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float cw = ImGui::GetContentRegionAvail().x;
    const ImVec2 kt = s.fonts.ui_bold->CalcTextSizeA(fpx(13), FLT_MAX, 0, pal.c_str());
    const ImVec2 ks(kt.x + px(16) + 2.0f, line_h(13) + px(6) + 2.0f);
    const float tx = a.x + px(12) + ks.x + px(12);
    const float tw = a.x + cw - px(12) - tx;
    const float body = para_at(s, ImVec2(tx, 0), tw, kPal, false);
    const float inner = std::max(ks.y, line_h(13.5f) + px(2) + body);
    const ImVec2 b(a.x + cw, a.y + px(12) + inner + px(12));
    d->AddRectFilled(a, b, alpha(g_pal->focus, 0x10), px(10));
    d->AddRect(a, b, g_pal->focus, px(10));
    const ImVec2 k0(a.x + px(12), a.y + px(12) + (inner - ks.y) * 0.5f);
    d->AddRectFilled(k0, ImVec2(k0.x + ks.x, k0.y + ks.y), raised_color(), px(6));
    d->AddRect(k0, ImVec2(k0.x + ks.x, k0.y + ks.y), g_pal->border, px(6));
    d->AddText(s.fonts.ui_bold, fpx(13), ImVec2(k0.x + (ks.x - kt.x) * 0.5f, k0.y + (ks.y - kt.y) * 0.5f),
               g_pal->text, pal.c_str());
    const float ty = a.y + px(12) + (inner - (line_h(13.5f) + px(2) + body)) * 0.5f;
    d->AddText(s.fonts.ui_bold, fpx(13.5f), ImVec2(tx, ty), g_pal->text, "One key opens everything");
    para_at(s, ImVec2(tx, ty + line_h(13.5f) + px(2)), tw, kPal);
    ImGui::Dummy(ImVec2(cw, b.y - a.y));
    gap(px(16));
}

// Page 4: the palette key, how to make AgentDesk yours, a line to give your agent so it learns
// to drive the app, the other layouts, and where to send feedback.
void page_keys(App::State& s) {
    if (const std::string pal = s.keymap.chord_for("app.palette"); !pal.empty())
        palette_key_box(s, pal);
    page_title(s, "Make it yours", "Everything AgentDesk does is a command.");
    ImGui::Text("Press %s for the command palette, %s to jump to a space or agent.",
                s.keymap.chord_for("app.palette").c_str(), s.keymap.chord_for("app.picker").c_str());
    ImGui::Dummy(ImVec2(0, px(6)));
    ImGui::TextWrapped(
        "Change any shortcut in keys.json, the look in Settings (%s), and add C++ mods in the mods "
        "folder. agentctl drives AgentDesk from a terminal or from your agents.",
        s.keymap.chord_for("app.settings").c_str());
    ImGui::Dummy(ImVec2(0, px(8)));
    ImGui::TextDisabled("Tell your agent:");
    static constexpr const char* kAsk =
        "Run `agentctl --skill` and read it, then use agentctl to start, watch and report to other agents "
        "here.";
    ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(raised_color()));
    ImGui::BeginChild("##ask", ImVec2(-1, px(52)), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPos(ImVec2(px(10), px(8)));
    ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - px(80));
    ImGui::TextUnformatted(kAsk);
    ImGui::PopTextWrapPos();
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - px(72), px(12)));
    if (ImGui::Button(s.welcome_copied ? "Copied" : "Copy", ImVec2(px(62), px(26)))) {
        ImGui::SetClipboardText(kAsk);
        s.welcome_copied = true;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    // "More examples in Settings": 12 px padding, rounded 10, raised.
    ImGui::Dummy(ImVec2(0, px(8)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(raised_color()));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(10));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(12)));
    ImGui::BeginChild("##examples", ImVec2(-1, 0),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PushFont(s.fonts.ui_bold, 13.5f);
    ImGui::TextUnformatted("More examples in Settings");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(2)));
    ImGui::TextWrapped(
        "Settings \xE2\x80\xBA Look has more layouts to try or start from, AgentDesk 98 among them. Your "
        "agent can copy any of them and change anything.");
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    gap(px(16));
    para(s, "Something off or missing? Settings \xE2\x80\xBA Send feedback. We read every one.");
}

// The footer: one dot per page, Back (hidden on the first page) and Next. Enter or Right goes
// on (on the last page only Enter, which finishes); Left goes back. Returns true when ui.json
// changed.
bool welcome_footer(App::State& s, float w, float h) {
    int& page = s.welcome_page;
    const float fy = ImGui::GetWindowPos().y + h - px(kFooterH) + px(12);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < kPages; ++i)
        dl->AddCircleFilled(
            ImVec2(ImGui::GetWindowPos().x + px(36) + static_cast<float>(i) * px(14), fy + px(15)), px(3.5f),
            i == page ? g_pal->focus : g_pal->border);
    const char* next = page < kPages - 1 ? (page == 1 && s.settings.machines.empty() ? "Not now" : "Next")
                                         : (s.ws.spaces.empty() ? "Open a space" : "Done");
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + w - px(32) - px(220), fy));
    bool back = false;
    if (page > 0) {
        ImGui::PushStyleColor(ImGuiCol_Button, v4(raised_color()));
        back = ImGui::Button("Back", ImVec2(px(100), px(30)));
        ImGui::PopStyleColor();
    } else {
        ImGui::Dummy(ImVec2(px(100), px(30)));
    }
    ImGui::SameLine(0, px(12));
    primary_button_style(true);
    bool fwd = ImGui::Button(next, ImVec2(px(108), px(30)));
    primary_button_style(false);
    const bool typing = ImGui::GetIO().WantTextInput;
    if (!typing && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                    (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && page < kPages - 1)))
        fwd = true;
    if (!typing && ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && page > 0)
        back = true;
    if (back)
        --page;
    if (!fwd)
        return false;
    if (page < kPages - 1) {
        ++page;
        return false;
    }
    s.settings.welcomed = "finished";
    ImGui::CloseCurrentPopup();
    if (s.ws.spaces.empty())
        run_command(s, "space.new");
    return true;
}

}  // namespace

// Esc or the close button skips the welcome.
void draw_welcome(App::State& s) {
    if (s.show_welcome) {
        ImGui::OpenPopup("##welcome");
        s.show_welcome = false;
        s.welcome_page = 0;
        s.welcome_pos = ImVec2(-1.0f, -1.0f);
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 size = dialog_size(px(660), px(600));
    const float w = size.x;
    const float h = size.y;
    if (s.welcome_pos.x < 0.0f)
        s.welcome_pos =
            ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + (vp->WorkSize.y - h) * 0.5f);
    // Keep the header reachable when the window shrinks.
    s.welcome_pos.x =
        std::clamp(s.welcome_pos.x, vp->WorkPos.x - w + px(80), vp->WorkPos.x + vp->WorkSize.x - px(80));
    s.welcome_pos.y =
        std::clamp(s.welcome_pos.y, vp->WorkPos.y, vp->WorkPos.y + vp->WorkSize.y - px(kHeaderH));
    ImGui::SetNextWindowPos(s.welcome_pos);
    ImGui::SetNextWindowSize(size);
    push_overlay_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(32), px(28)));
    const bool open = ImGui::BeginPopupModal("##welcome", nullptr,
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    pop_overlay_style();
    if (!open)
        return;
    const bool skip = welcome_header(s, w, h);
    bool changed = false;
    switch (s.welcome_page) {
        case 0:
            changed = page_intro(s, w);
            break;
        case 1:
            page_machines(s);
            break;
        case 2:
            changed = page_panels(s);
            break;
        default:
            page_keys(s);
            break;
    }
    ImGui::Dummy(ImVec2(0, px(20)));
    ImGui::EndChild();
    changed |= welcome_footer(s, w, h);
    if (skip || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        s.settings.welcomed = "skipped";
        changed = true;
        ImGui::CloseCurrentPopup();
    }
    if (changed)
        save_settings(s.settings);
    ImGui::EndPopup();
}
}  // namespace detail

}  // namespace agentdesk::desktop
