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

#include "agentdesk/ui/cockpit.hpp"
#include "agentdesk/ui/host_client.hpp"
#include "agentdesk/ui/layout.hpp"
#include "agentdesk/ui/layout_styles.hpp"
#include "agentdesk/ui/terminal_view.hpp"
#include "agentdesk/ui/workspace.hpp"
#include "app.hpp"

namespace agentdesk::desktop {

// The styles' keys in ui.json (all optional; defaults as below).
struct StyleSettings {
    // Tiling
    float tiling_gaps = 8.0f;            // window edge to panes and between panes, design px 0..24
    bool tiling_gaps_on = true;          // tiling.gaps turns them off and on
    std::string tiling_titles = "auto";  // pane title rows: auto (2+ panes), always, never
    bool tiling_focus_follows_mouse = false;
    // Cockpit
    std::string cockpit_effects = "full";  // "full" (grid and glow) or "off"
    ui::cockpit::DoneToday cockpit_done;   // cockpit_done_today: {"date", "count"}
    // Classic
    std::string classic_effects = "light";  // "off", "light" (scan lines, vignette), "full" (+ glow)
    bool classic_mono_terminal = true;      // terminal colours put on the theme's one-colour ramp
};

// What the styles remember between frames (one window).
struct StyleUi {
    struct Tiling {
        bool resizing = false;    // the keyboard resize mode is on
        bool popup_open = false;  // its keyboard-holding popup is open
        ImVec2 hint_min{};        // where the bar drew the mode's hint (the popup sits on it)
        ImVec2 hint_size{};
        std::chrono::steady_clock::time_point flash_until{};  // a resize that could not move
        std::string hover_pane;  // focus follows mouse: the pane under the pointer, since when
        std::chrono::steady_clock::time_point hover_since{};
    } tiling;
    struct Cockpit {
        // Each agent's last state and when it changed (steady ms), and its first sighting.
        std::map<std::string, ui::Attention> last;
        std::map<std::string, long long> changed_at;
        ui::cockpit::EventLog log;
        float shown[3] = {0, 0, 0};  // the gauges' eased values: working, needs you, done today
        bool shown_set = false;
    } cockpit;
    struct Classic {
        int menu = -1;                // the open menu (index among the shown titles), -1: none
        int open_request = -1;        // classic.menu asked for this one, opened on the next draw
        bool keyed = false;           // the open menu is being moved through with the keys
        std::vector<ImVec2> menu_at;  // where each menu's dropdown opens, this frame
    } classic;
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

// Windows' high contrast is on (ui_tokens.cpp, read at most every 2 s).
bool system_high_contrast();

// The keys bound to `command`, as shown in menus: the shown layout's keys.json section first,
// then the global and terminal ones.
std::string style_chord_for(const App::State& s, const std::string& command);

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
// Every tick (drawn or not).
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

// ------------------------------------------------------------ tiling_layout.cpp
void draw_tiling_bar(App::State& s, ImVec2 pos, float w, float h);
StyledPane draw_tiling_pane(App::State& s, const std::string& id, const ui::SessionView& v, ImVec2 pos,
                            ImVec2 max, const ui::styles::PaneLook& look, bool focused, bool connected);
bool draw_tiling_no_tab(App::State& s, ImDrawList* dl, ImVec2 pos, ImVec2 size);
void draw_tiling_overlay(App::State& s);
std::optional<ui::layout::Node> tiling_layout_tree(const App::State& s, const ui::layout::Node& tree);
void tiling_layout_preview(ImDrawList* d, ImVec2 a, ImVec2 size);
bool look_tiling_options(App::State& s);
std::optional<std::string> exec_tiling_command(App::State& s, const std::string& name);

// ------------------------------------------------------------ cockpit_layout.cpp
void draw_cockpit_header(App::State& s, ImVec2 pos, float w, float h);
void draw_cockpit_gauges(App::State& s, ImVec2 pos, float w, float h);
void draw_cockpit_roster(App::State& s, ImVec2 pos, float w, float h);
void draw_cockpit_log(App::State& s, ImVec2 pos, float w, float h);
void draw_cockpit_backdrop(App::State& s);
void draw_cockpit_overlay(App::State& s);
std::optional<ui::layout::Node> cockpit_layout_tree(const App::State& s, const ui::layout::Node& tree);
void cockpit_tick(App::State& s);
float cockpit_toast_offset(const App::State& s);
void cockpit_layout_preview(ImDrawList* d, ImVec2 a, ImVec2 size);
bool look_cockpit_options(App::State& s);
std::optional<std::string> exec_cockpit_command(App::State& s, const std::string& name);

// ------------------------------------------------------------ classic_layout.cpp
void draw_classic_menubar(App::State& s, ImVec2 pos, float w, float h);
void draw_classic_tabs(App::State& s, ImVec2 pos, float w, float h);
void draw_classic_status(App::State& s, ImVec2 pos, float w, float h);
void draw_classic_sidebar(App::State& s, ImVec2 pos, float w, float h);
StyledPane draw_classic_pane(App::State& s, const std::string& id, const ui::SessionView& v, ImVec2 pos,
                             ImVec2 max, const ui::styles::PaneLook& look, bool focused, bool connected);
bool draw_classic_no_tab(App::State& s, ImDrawList* dl, ImVec2 pos, ImVec2 size);
void draw_classic_overlay(App::State& s);
void classic_terminal_overlay(App::State& s, ImVec2 body_min, ImVec2 body_size);
void classic_terminal_theme(App::State& s, ui::TerminalTheme& theme);
void classic_layout_preview(ImDrawList* d, ImVec2 a, ImVec2 size);
bool look_classic_options(App::State& s);
std::optional<std::string> exec_classic_command(App::State& s, const std::string& name);
}  // namespace detail

}  // namespace agentdesk::desktop
