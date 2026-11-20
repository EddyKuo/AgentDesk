// The style layouts' shared plumbing: settings in ui.json, the hooks the window's shared files call,
// and the dispatch to each style's own drawing file.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

void read_style_settings(const nlohmann::json& j, StyleSettings& out) {
    (void)j;
    (void)out;
}

void write_style_settings(nlohmann::json& j, const StyleSettings& in) {
    (void)j;
    (void)in;
}

float line_px(float w) {
    if (w <= 1.0f)
        return std::max(0.0f, w);
    return std::max(1.0f, std::round(px(w)));
}

bool styled_panes(const std::string& style) {
    (void)style;
    return false;
}

StyledPane draw_styled_pane(App::State& s, const std::string& id, const ui::SessionView& v, ImVec2 pos,
                            ImVec2 max, const ui::styles::PaneLook& look, bool focused, bool connected) {
    (void)s;
    (void)id;
    (void)v;
    (void)look;
    (void)focused;
    (void)connected;
    return {pos, max};
}

bool draw_styled_no_tab(App::State& s, ImDrawList* dl, ImVec2 pos, ImVec2 size, bool connected) {
    (void)s;
    (void)dl;
    (void)pos;
    (void)size;
    (void)connected;
    return false;
}

void style_terminal_theme(App::State& s, ui::TerminalTheme& theme) {
    (void)s;
    (void)theme;
}

bool style_holds_keyboard(const App::State& s) {
    (void)s;
    return false;
}

void style_terminal_overlay(App::State& s, ImVec2 body_min, ImVec2 body_size) {
    (void)s;
    (void)body_min;
    (void)body_size;
}

bool draw_style_block(App::State& s, const lay::Node& n, ImVec2 pos, ImVec2 size) {
    (void)s;
    (void)n;
    (void)pos;
    (void)size;
    return false;
}

std::optional<lay::Node> style_layout_tree(const App::State& s, const lay::Node& tree) {
    (void)s;
    (void)tree;
    return std::nullopt;
}

void draw_style_backdrop(App::State& s, const lay::Node& tree) {
    (void)s;
    (void)tree;
}

void draw_style_overlay(App::State& s) {
    (void)s;
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
    return ui::styles::toast_area(s.layout().style);
}

bool style_draws_window_buttons(const App::State& s) {
    (void)s;
    return false;
}

bool style_layout_preview(ImDrawList* d, const std::string& name, ImVec2 a, ImVec2 size) {
    (void)d;
    (void)name;
    (void)a;
    (void)size;
    return false;
}

bool look_style_options(App::State& s) {
    (void)s;
    return false;
}

std::optional<std::string> exec_style_command(App::State& s, const std::string& name,
                                              const std::string& arg) {
    (void)s;
    (void)name;
    (void)arg;
    return std::nullopt;
}

}  // namespace detail
}  // namespace agentdesk::desktop
