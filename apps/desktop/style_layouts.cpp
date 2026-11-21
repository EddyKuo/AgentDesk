// The style layouts' shared plumbing: settings in ui.json, the hooks the window's shared files call,
// and the dispatch to each style's own drawing file.
#include "agentdesk/ui/terminals_options.hpp"
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// The shown layout's style; none while Focus mode puts its own tree in place (its panes, toasts
// and keys are the plain ones then).
const std::string& style_of(const App::State& s) {
    static const std::string kNone;
    return s.settings.focus_on ? kNone : s.layout().style;
}

bool is_string(const nlohmann::json& j, const char* k) {
    return j.contains(k) && j[k].is_string();
}

bool is_bool(const nlohmann::json& j, const char* k) {
    return j.contains(k) && j[k].is_boolean();
}
}  // namespace

// Wrong types and unknown values fall back to the defaults. The Tiling layout's tiling_gaps and
// tiling_gaps_on carry over to Terminals until the new keys are written.
void read_style_settings(const nlohmann::json& j, StyleSettings& out) {
    auto number = [&](const char* k, const char* old) -> const nlohmann::json* {
        if (j.contains(k) && j[k].is_number())
            return &j[k];
        if (j.contains(old) && j[old].is_number())
            return &j[old];
        return nullptr;
    };
    auto boolean = [&](const char* k, const char* old) -> const nlohmann::json* {
        if (is_bool(j, k))
            return &j[k];
        if (is_bool(j, old))
            return &j[old];
        return nullptr;
    };
    if (const auto* g = number("terminals_gaps", "tiling_gaps"))
        out.terminals_gaps = ui::terminals::clamp_gaps(g->get<float>());
    if (const auto* g = boolean("terminals_gaps_on", "tiling_gaps_on"))
        out.terminals_gaps_on = g->get<bool>();
    if (is_bool(j, "terminals_square"))
        out.terminals_square = j["terminals_square"].get<bool>();
    if (is_bool(j, "fun_layouts"))
        out.fun_layouts = j["fun_layouts"].get<bool>();
    const StyleSettings d;
    if (is_string(j, "classic_effects")) {
        const std::string e = j["classic_effects"].get<std::string>();
        out.classic_effects = e == "off" || e == "full" ? e : d.classic_effects;
    }
    if (is_bool(j, "classic_mono_terminal"))
        out.classic_mono_terminal = j["classic_mono_terminal"].get<bool>();
}

void write_style_settings(nlohmann::json& j, const StyleSettings& in) {
    j["terminals_square"] = in.terminals_square;
    j["terminals_gaps"] = in.terminals_gaps;
    j["terminals_gaps_on"] = in.terminals_gaps_on;
    j["fun_layouts"] = in.fun_layouts;
    j["classic_effects"] = in.classic_effects;
    j["classic_mono_terminal"] = in.classic_mono_terminal;
    // Keys of removed layouts are dropped when the file is next written.
    for (const char* k : {"tiling_gaps", "tiling_gaps_on", "tiling_titles", "tiling_focus_follows_mouse",
                          "cockpit_effects", "cockpit_done_today"})
        j.erase(k);
}

std::string style_chord_for(const App::State& s, const std::string& command) {
    for (const auto& [chord, cmd] : s.keymap.layout_map())
        if (cmd == command)
            return ui::Keymap::format(chord);
    return s.keymap.chord_for(command);
}

float line_px(float w) {
    if (w <= 1.0f)
        return std::max(0.0f, w);
    return std::max(1.0f, std::round(px(w)));
}

bool styled_panes(const std::string& style) {
    return style == "classic" || style == "pair";
}

StyledPane draw_styled_pane(App::State& s, const std::string& id, const ui::SessionView& v, ImVec2 pos,
                            ImVec2 max, const ui::styles::PaneLook& look, bool focused, bool connected) {
    if (style_of(s) == "classic")
        return draw_classic_pane(s, id, v, pos, max, look, focused, connected);
    if (style_of(s) == "pair")
        return draw_pair_pane(s, id, v, pos, max, focused);
    return {pos, max};
}

bool draw_styled_no_tab(App::State& s, ImDrawList* dl, ImVec2 pos, ImVec2 size, bool connected) {
    (void)connected;
    if (style_of(s) == "classic")
        return draw_classic_no_tab(s, dl, pos, size);
    return false;
}

void style_terminal_theme(App::State& s, ui::TerminalTheme& theme) {
    if (style_of(s) == "classic")
        classic_terminal_theme(s, theme);
}

bool style_holds_keyboard(const App::State& s) {
    return s.styles.resize.on || (style_of(s) == "classic" && s.styles.classic.menu >= 0);
}

void style_terminal_overlay(App::State& s, ImVec2 body_min, ImVec2 body_size) {
    if (style_of(s) == "classic")
        classic_terminal_overlay(s, body_min, body_size);
}

bool draw_style_block(App::State& s, const lay::Node& n, ImVec2 pos, ImVec2 size) {
    const std::string& m = n.content.value;
    if (m == "classic-menubar")
        draw_classic_menubar(s, pos, size.x, size.y);
    else if (m == "classic-tabs")
        draw_classic_tabs(s, pos, size.x, size.y);
    else if (m == "classic-status")
        draw_classic_status(s, pos, size.x, size.y);
    else if (m == "classic-sidebar")
        draw_classic_sidebar(s, pos, size.x, size.y);
    else
        return false;
    return true;
}

std::optional<lay::Node> style_layout_tree(const App::State& s, const lay::Node& tree) {
    if (style_of(s) == "terminals")
        return terminals_layout_tree(s, tree);
    return std::nullopt;
}

void draw_style_backdrop(App::State& s, const lay::Node& tree) {
    (void)s;
    (void)tree;
}

void draw_style_overlay(App::State& s) {
    draw_resize_mode(s);  // any layout; ends itself in Focus or when the window loses focus
    if (style_of(s) == "classic")
        draw_classic_overlay(s);
}

void style_tick(App::State& s) {
    track_state_changes(s);  // whatever layout shows
    pair_tick(s);
}

ui::styles::ToastArea style_toast_area(const App::State& s) {
    return ui::styles::toast_area(style_of(s));
}

bool style_draws_window_buttons(const App::State& s) {
    return style_of(s) == "classic";  // text buttons in its menu bar
}

bool style_layout_preview(ImDrawList* d, const std::string& name, ImVec2 a, ImVec2 size) {
    if (name == "classic")
        classic_layout_preview(d, a, size);
    else if (name == "pair")
        pair_layout_preview(d, a, size);
    else
        return false;
    return true;
}

bool look_style_options(App::State& s) {
    const std::string& st = s.layout().style;  // the layout picked, also under Focus mode
    if (st != "terminals" && st != "classic")
        return false;
    settings_gap(px(14));
    settings_wrapped(s, ui::styles::display_name(s.layout().name).c_str(), g_pal->dim, 12.5f, 12.5f * 1.3f,
                     s.fonts.ui_medium);
    if (st == "terminals")
        return look_terminals_options(s);
    return look_classic_options(s);
}

void track_state_changes(App::State& s) {
    auto& ch = s.styles.changes;
    const long long now =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    for (const auto& [id, v] : s.views_by_id) {
        if (v.lost || v.process_state != "Running")
            continue;
        const Attention a = s.attention_of(id);
        const auto it = ch.last.find(id);
        if (it == ch.last.end() || it->second != a) {
            ch.last[id] = a;
            ch.changed_at[id] = now;
        }
    }
}

std::optional<std::string> exec_style_command(App::State& s, const std::string& name,
                                              const std::string& arg) {
    if (name == "pane.split" && arg == "auto") {
        // Toward the focused pane's longer side (any layout).
        const std::string id = s.ws.focused_session();
        ui::Rect r = s.pane_area;
        if (const auto it = s.pane_rects.find(id); it != s.pane_rects.end())
            r = it->second;
        run_command(s, std::string("pane.split:") + ui::terminals::split_direction(r.w, r.h));
        return std::string();
    }
    if (name == "pane.resize") {
        // "<dir>" moves 40 px, "<dir>:<px>" that far. In the resize mode a pane that cannot move
        // any further makes the hint flash once.
        const auto parsed = ui::terminals::parse_resize(arg);
        if (!parsed)
            return std::string("pane.resize needs <left|right|up|down>[:<px>]");
        if (!s.ws.resize(parse_dir(parsed->first), px(parsed->second), s.pane_area) && s.styles.resize.on)
            s.styles.resize.flash_until = Clock::now() + std::chrono::milliseconds(150);
        return std::string();
    }
    if (auto r = exec_terminals_command(s, name))
        return r;
    return exec_classic_command(s, name);
}

}  // namespace detail
}  // namespace agentdesk::desktop
