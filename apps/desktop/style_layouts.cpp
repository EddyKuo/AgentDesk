// The style layouts' shared plumbing: settings in ui.json, the hooks the window's shared files call,
// and the dispatch to each style's own drawing file.
#include "agentdesk/ui/tiling_layout.hpp"
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
const std::string& style_of(const App::State& s) {
    return s.layout().style;
}

bool is_string(const nlohmann::json& j, const char* k) {
    return j.contains(k) && j[k].is_string();
}
}  // namespace

// Wrong types and unknown values fall back to the defaults.
void read_style_settings(const nlohmann::json& j, StyleSettings& out) {
    const StyleSettings d;
    if (j.contains("tiling_gaps") && j["tiling_gaps"].is_number())
        out.tiling_gaps = ui::tiling::clamp_gaps(j["tiling_gaps"].get<float>());
    if (j.contains("tiling_gaps_on") && j["tiling_gaps_on"].is_boolean())
        out.tiling_gaps_on = j["tiling_gaps_on"].get<bool>();
    if (is_string(j, "tiling_titles")) {
        const std::string t = j["tiling_titles"].get<std::string>();
        out.tiling_titles = t == "always" || t == "never" ? t : d.tiling_titles;
    }
    if (j.contains("tiling_focus_follows_mouse") && j["tiling_focus_follows_mouse"].is_boolean())
        out.tiling_focus_follows_mouse = j["tiling_focus_follows_mouse"].get<bool>();
}

void write_style_settings(nlohmann::json& j, const StyleSettings& in) {
    j["tiling_gaps"] = in.tiling_gaps;
    j["tiling_gaps_on"] = in.tiling_gaps_on;
    j["tiling_titles"] = in.tiling_titles;
    j["tiling_focus_follows_mouse"] = in.tiling_focus_follows_mouse;
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
    return style == "tiling";
}

StyledPane draw_styled_pane(App::State& s, const std::string& id, const ui::SessionView& v, ImVec2 pos,
                            ImVec2 max, const ui::styles::PaneLook& look, bool focused, bool connected) {
    if (style_of(s) == "tiling")
        return draw_tiling_pane(s, id, v, pos, max, look, focused, connected);
    return {pos, max};
}

bool draw_styled_no_tab(App::State& s, ImDrawList* dl, ImVec2 pos, ImVec2 size, bool connected) {
    (void)connected;
    if (style_of(s) == "tiling")
        return draw_tiling_no_tab(s, dl, pos, size);
    return false;
}

void style_terminal_theme(App::State& s, ui::TerminalTheme& theme) {
    (void)s;
    (void)theme;
}

bool style_holds_keyboard(const App::State& s) {
    return s.styles.tiling.resizing;
}

void style_terminal_overlay(App::State& s, ImVec2 body_min, ImVec2 body_size) {
    (void)s;
    (void)body_min;
    (void)body_size;
}

bool draw_style_block(App::State& s, const lay::Node& n, ImVec2 pos, ImVec2 size) {
    const std::string& m = n.content.value;
    if (m == "tiling-bar")
        draw_tiling_bar(s, pos, size.x, size.y);
    else
        return false;
    return true;
}

std::optional<lay::Node> style_layout_tree(const App::State& s, const lay::Node& tree) {
    if (style_of(s) == "tiling")
        return tiling_layout_tree(s, tree);
    return std::nullopt;
}

void draw_style_backdrop(App::State& s, const lay::Node& tree) {
    (void)s;
    (void)tree;
}

void draw_style_overlay(App::State& s) {
    draw_tiling_overlay(s);  // also ends its resize mode once another layout shows
}

// A style designed with its own themes gets them the first time it shows, when the person has
// not picked themes for that layout yet (and both themes are installed).
void style_frame_upkeep(App::State& s) {
    const LayoutSpec& l = s.layout();
    if (s.styles.themed == l.name)
        return;
    s.styles.themed = l.name;
    if (!l.builtin || s.settings.layout_themes.contains(l.name))
        return;
    const auto pair = ui::styles::suggested_themes(l.style);
    if (!pair || palette_by_id(pair->first)->id != pair->first ||
        palette_by_id(pair->second)->id != pair->second)
        return;
    s.settings.layout_themes[l.name] = *pair;
    save_settings(s.settings);
}

void style_tick(App::State& s) {
    (void)s;
}

ui::styles::ToastArea style_toast_area(const App::State& s) {
    return ui::styles::toast_area(style_of(s));
}

bool style_draws_window_buttons(const App::State& s) {
    (void)s;
    return false;
}

bool style_layout_preview(ImDrawList* d, const std::string& name, ImVec2 a, ImVec2 size) {
    if (name == "tiling")
        tiling_layout_preview(d, a, size);
    else
        return false;
    return true;
}

bool look_style_options(App::State& s) {
    if (style_of(s) != "tiling")
        return false;
    settings_gap(px(14));
    settings_wrapped(s, ui::styles::display_name(s.layout().name).c_str(), g_pal->dim, 12.5f, 12.5f * 1.3f,
                     s.fonts.ui_medium);
    return look_tiling_options(s);
}

std::optional<std::string> exec_style_command(App::State& s, const std::string& name,
                                              const std::string& arg) {
    if (name == "pane.split" && arg == "auto") {
        // Toward the focused pane's longer side (any layout).
        const std::string id = s.ws.focused_session();
        ui::Rect r = s.pane_area;
        if (const auto it = s.pane_rects.find(id); it != s.pane_rects.end())
            r = it->second;
        run_command(s, std::string("pane.split:") + ui::tiling::split_direction(r.w, r.h));
        return std::string();
    }
    if (name == "pane.resize") {
        // "<dir>" moves 40 px, "<dir>:<px>" that far. In the Tiling resize mode a pane that cannot
        // move any further makes the hint flash once.
        const auto parsed = ui::tiling::parse_resize(arg);
        if (!parsed)
            return std::string("pane.resize needs <left|right|up|down>[:<px>]");
        if (!s.ws.resize(parse_dir(parsed->first), px(parsed->second), s.pane_area) &&
            s.styles.tiling.resizing)
            s.styles.tiling.flash_until = Clock::now() + std::chrono::milliseconds(150);
        return std::string();
    }
    return exec_tiling_command(s, name);
}

}  // namespace detail
}  // namespace agentdesk::desktop
