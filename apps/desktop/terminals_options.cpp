// The Terminals layout's options (square corners, gaps) and keyboard tools: the resize mode with
// its floating hint, the options put on the layout tree, the Settings rows and the commands
// (terminals.resize_mode, terminals.gaps, terminals.corners and their old tiling.* names).
#include "agentdesk/ui/terminals_options.hpp"

#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
namespace to = ui::terminals;

constexpr const char* kResizePopup = "##terminals-resize";

void leave_resize(App::State& s) {
    s.styles.resize.on = false;
    s.focus_view(s.ws.focused_session());
}

to::Options options_of(const Settings& st) {
    return {st.styles.terminals_square, st.styles.terminals_gaps, st.styles.terminals_gaps_on};
}

// The hint while resizing: a small card centred near the bottom of the panes, sliding up as it
// appears. Flashes in the "needs you" colour when a pane could not move any further.
void draw_resize_hint(App::State& s) {
    auto& rs = s.styles.resize;
    const float k = tok::fade(ImGui::GetID("##resize-hint"), rs.on, ui::tokens::kNormalMs);
    if (k <= 0.0f)
        return;
    const char* keys = "H J K L";
    const char* rest = "resize  \xC2\xB7  Shift for small steps  \xC2\xB7  Esc to finish";
    const ImVec2 kw = tok::measure(s, ui::tokens::kCaption, keys);
    const ImVec2 rw = tok::measure(s, ui::tokens::kBody, rest);
    const float h = px(32);
    const float w = tok::gap_m() + kw.x + tok::gap_s() * 2 + tok::gap_s() + rw.x + tok::gap_m();
    const ui::Rect area = s.pane_area;
    const float slide = (1.0f - k) * px(ui::tokens::kSlideDistance);
    const ImVec2 a(std::round(area.x + (area.w - w) * 0.5f),
                   std::round(area.y + area.h - h - tok::gap_l() + slide));
    const ImVec2 b(a.x + w, a.y + h);
    rs.hint_min = a;
    rs.hint_size = ImVec2(w, h);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const bool flash = Clock::now() < rs.flash_until;
    with_alpha(dl, k, [&] {
        tok::card(dl, a, b, tok::radius_card(), 2, 0, flash ? g_pal->needs_you : 0);
        // The keys in a chip, then what they do.
        const ImVec2 ca(a.x + tok::gap_m() - tok::gap_xs(), a.y + (h - px(20)) * 0.5f);
        const ImVec2 cb(ca.x + kw.x + tok::gap_s() * 2, ca.y + px(20));
        dl->AddRectFilled(ca, cb, flash ? alpha(g_pal->needs_you, 60) : tok::selected(), tok::radius_small());
        tok::text(s, dl, ui::tokens::kCaption, ImVec2(ca.x + tok::gap_s(), a.y + (h - kw.y) * 0.5f),
                  flash ? g_pal->needs_you : tok::selected_edge(), keys);
        tok::text(s, dl, ui::tokens::kBody, ImVec2(cb.x + tok::gap_s(), a.y + (h - rw.y) * 0.5f), g_pal->dim,
                  rest);
    });
}
}  // namespace

// The resize mode holds the keyboard in a small invisible popup over its hint, so no key reaches a
// terminal; a click anywhere, Esc, Enter, the mode's key again or the window losing focus ends it.
void draw_resize_mode(App::State& s) {
    auto& rs = s.styles.resize;
    if (rs.on && (s.settings.focus_on || !s.window_focused))
        leave_resize(s);
    draw_resize_hint(s);
    if (rs.on && !rs.popup_open) {
        ImGui::OpenPopup(kResizePopup);
        rs.popup_open = true;
    }
    if (rs.popup_open) {
        ImGui::SetNextWindowPos(rs.hint_min);
        ImGui::SetNextWindowSize(ImVec2(std::max(rs.hint_size.x, 1.0f), std::max(rs.hint_size.y, 1.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
        const bool open =
            ImGui::BeginPopup(kResizePopup, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                                                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav);
        ImGui::PopStyleVar(2);
        if (open) {
            if (!rs.on) {
                ImGui::CloseCurrentPopup();
                rs.popup_open = false;
            } else {
                const ImGuiIO& io = ImGui::GetIO();
                static constexpr std::pair<ImGuiKey, to::Key> kKeys[] = {
                    {ImGuiKey_H, to::Key::kH},
                    {ImGuiKey_J, to::Key::kJ},
                    {ImGuiKey_K, to::Key::kK},
                    {ImGuiKey_L, to::Key::kL},
                    {ImGuiKey_LeftArrow, to::Key::kLeft},
                    {ImGuiKey_DownArrow, to::Key::kDown},
                    {ImGuiKey_UpArrow, to::Key::kUp},
                    {ImGuiKey_RightArrow, to::Key::kRight},
                    {ImGuiKey_Escape, to::Key::kEscape},
                    {ImGuiKey_Enter, to::Key::kEnter},
                    {ImGuiKey_KeypadEnter, to::Key::kEnter}};
                bool leave = false;
                // The mode's own key ends it too (whatever keys.json binds it to).
                for (const auto* map : {&s.keymap.layout_map(), &s.keymap.global_map()})
                    for (const auto& [chord, cmd] : *map)
                        if ((cmd == "terminals.resize_mode" || cmd == "tiling.resize_mode") &&
                            ImGui::IsKeyChordPressed(chord))
                            leave = true;
                if (!leave && !io.KeyCtrl && !io.KeyAlt) {
                    for (const auto& [key, k] : kKeys) {
                        if (!ImGui::IsKeyPressed(key))
                            continue;
                        const to::ResizeAction a = to::resize_key(k, io.KeyShift);
                        if (a.leave)
                            leave = true;
                        else
                            run_command(s, a.command);
                    }
                }
                if (leave) {
                    ImGui::CloseCurrentPopup();
                    rs.popup_open = false;
                    leave_resize(s);
                }
            }
            ImGui::EndPopup();
        } else {  // closed from outside: a click elsewhere
            rs.popup_open = false;
            if (rs.on)
                leave_resize(s);
        }
    }
    if ((rs.on || Clock::now() < rs.flash_until) && s.config.wake)
        s.config.wake();
}

std::optional<lay::Node> terminals_layout_tree(const App::State& s, const lay::Node& tree) {
    const to::Options o = options_of(s.settings);
    if (to::is_default(o))
        return std::nullopt;
    lay::Node t = tree;
    if (!to::apply_options(t, o))
        return std::nullopt;
    return t;
}

bool terminals_gapless(const App::State& s) {
    return s.layout().style == "terminals" && !s.settings.focus_on &&
           (!s.settings.styles.terminals_gaps_on || s.settings.styles.terminals_gaps <= 0.0f);
}

// Settings > Look while Terminals is the layout: Square corners and Gaps.
bool look_terminals_options(App::State& s) {
    bool changed = false;
    auto& st = s.settings.styles;
    settings_row(s, "Square corners", "Panes with straight edges, like a tiling window manager.", px(36),
                 px(20));
    if (settings_toggle("##tm-square", st.terminals_square)) {
        st.terminals_square = !st.terminals_square;
        changed = true;
    }
    settings_row_done(s);
    const std::string chord = style_chord_for(s, "terminals.gaps");
    const std::string note = "Space around and between panes." +
                             (chord.empty() ? std::string() : " " + chord + " turns it off and on.");
    settings_row(s, "Gaps", note.c_str(), px(36) + tok::gap_m() + px(kStepperW), px(kStepperH));
    if (settings_toggle("##tm-gaps-on", st.terminals_gaps_on)) {
        st.terminals_gaps_on = !st.terminals_gaps_on;
        changed = true;
    }
    ImGui::SameLine(0, tok::gap_m());
    ImGui::BeginDisabled(!st.terminals_gaps_on);
    if (stepper(s, "tm-gaps", st.terminals_gaps, 2.0f, 0.0f, 24.0f, "%.0f px"))
        changed = true;
    ImGui::EndDisabled();
    settings_row_done(s);
    return changed;
}

std::optional<std::string> exec_terminals_command(App::State& s, const std::string& name) {
    if (name == "terminals.resize_mode" || name == "tiling.resize_mode") {
        if (s.settings.focus_on)
            return std::string("not in Focus mode");
        if (s.styles.resize.on)
            leave_resize(s);
        else
            s.styles.resize.on = true;
        return std::string();
    }
    if (name == "terminals.gaps" || name == "tiling.gaps") {
        s.settings.styles.terminals_gaps_on = !s.settings.styles.terminals_gaps_on;
        save_settings(s.settings);
        return std::string();
    }
    if (name == "terminals.corners") {
        s.settings.styles.terminals_square = !s.settings.styles.terminals_square;
        save_settings(s.settings);
        return std::string();
    }
    return std::nullopt;
}

}  // namespace detail
}  // namespace agentdesk::desktop
