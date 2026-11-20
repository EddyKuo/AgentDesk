#pragma once
// The style layouts (Tiling, Cockpit, Classic) as agent-ui's shared files see them: their ui.json
// settings, their per-window state, and the hooks frame.cpp, panes.cpp, notices.cpp, commands.cpp
// and the settings pages call. Each style draws in its own file (tiling_layout.cpp,
// cockpit_layout.cpp, classic_layout.cpp); style_layouts.cpp dispatches to them.
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "agentdesk/ui/host_client.hpp"
#include "agentdesk/ui/layout.hpp"
#include "agentdesk/ui/layout_styles.hpp"
#include "agentdesk/ui/terminal_view.hpp"
#include "agentdesk/ui/workspace.hpp"
#include "app.hpp"

namespace agentdesk::desktop {

// The styles' keys in ui.json (all optional; defaults as below).
struct StyleSettings {};

// What the styles remember between frames (one window).
struct StyleUi {
    std::string themed;  // layouts whose suggested themes were checked this run
};

namespace detail {
// ui.json: read and written with the rest of the settings (settings_store.cpp).
void read_style_settings(const nlohmann::json& j, StyleSettings& out);
void write_style_settings(nlohmann::json& j, const StyleSettings& in);

// A layout colour: one of the theme's names (canvas, chrome, surface, ...) or #rrggbb /
// #rrggbbaa (frame.cpp).
std::optional<ImU32> layout_color(const std::string& c);
// A line `w` design px wide in screen px: 1 stays a hairline (one device pixel), wider lines
// scale with the zoom and land on whole pixels.
float line_px(float w);

// Styles whose panes are drawn by their own files.
bool styled_panes(const std::string& style);
// Where a styled pane's body goes, after its frame and header are drawn.
struct StyledPane {
    ImVec2 body_min;
    ImVec2 body_max;
};
StyledPane draw_styled_pane(App::State& s, const std::string& id, const ui::SessionView& v, ImVec2 pos,
                            ImVec2 max, const ui::styles::PaneLook& look, bool focused, bool connected);
// No tab: a style's own hint. False when the style has none (the default card shows).
bool draw_styled_no_tab(App::State& s, ImDrawList* dl, ImVec2 pos, ImVec2 size, bool connected);
// A style's change to the terminals' colours (Classic's monochrome).
void style_terminal_theme(App::State& s, ui::TerminalTheme& theme);
// A style has taken the keyboard (an open menu, a mode): terminals do not ask for focus.
bool style_holds_keyboard(const App::State& s);
// Drawn over a terminal, inside its child window, after the terminal.
void style_terminal_overlay(App::State& s, ImVec2 body_min, ImVec2 body_size);

// The layout: a style's blocks (true when `n` names one of its mods), its adjusted tree, what
// goes under the backgrounds and what goes over everything.
bool draw_style_block(App::State& s, const ui::layout::Node& n, ImVec2 pos, ImVec2 size);
std::optional<ui::layout::Node> style_layout_tree(const App::State& s, const ui::layout::Node& tree);
void draw_style_backdrop(App::State& s, const ui::layout::Node& tree);
void draw_style_overlay(App::State& s);
// Every frame before the palette is chosen; every tick (drawn or not).
void style_frame_upkeep(App::State& s);
void style_tick(App::State& s);
// Where toasts go in the shown style.
ui::styles::ToastArea style_toast_area(const App::State& s);
// The round window buttons are not drawn (a style draws its own).
bool style_draws_window_buttons(const App::State& s);

// Settings > Look: a style's layout card picture (false: not a style layout) and its extra rows
// under the layout cards (true when a setting changed).
bool style_layout_preview(ImDrawList* d, const std::string& name, ImVec2 a, ImVec2 size);
bool look_style_options(App::State& s);

// The styles' commands, and the arguments they add to existing ones (pane.split:auto,
// pane.resize:<dir>:<px>). nullopt: not one of them; "" ran; else why it failed.
std::optional<std::string> exec_style_command(App::State& s, const std::string& name, const std::string& arg);
}  // namespace detail

}  // namespace agentdesk::desktop
