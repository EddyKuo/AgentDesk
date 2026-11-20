#pragma once
// Layout styles: which of AgentDesk's renderers a layout file asks for (the mods its blocks and
// pane headers name), and what the styles share: how panes are framed, where notes go, when
// screen effects may be drawn, the themes a style suggests. No drawing here, so it can be tested.
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "agentdesk/ui/layout.hpp"
#include "agentdesk/ui/mac_layout.hpp"

namespace agentdesk::ui::styles {

// The style a mod name selects ("mac", "terminals", "classic", "tiling", "cockpit"), or "" for a
// mod that selects none (workspace parts, the person's own mods).
std::string_view style_of_mod(std::string_view mod);

// The style of a whole tree, from every block's mod and every tab block's pane header mod, in
// tree order. The Mac style wins over the others (it rearranges the whole window); otherwise
// the first style found wins, so a hand-made layout mixing two styles stays predictable.
std::string style_of_tree(const layout::Node& tree);

// How Settings, the palette and toasts name a layout: the built-ins by their product names,
// any other layout by its file name.
std::string display_name(std::string_view layout);

// Styles whose chrome shows a clock (the frame loop wakes when the minute turns over).
bool has_clock(std::string_view style);

// How a style frames the panes of the tab it shows. Each field comes from the layout's pane
// frame when the file sets it, else from the style's own default (what that style always drew).
struct PaneLook {
    float radius = 10.0f;
    layout::Line border{1.0f, "border"};
    layout::Line focus{1.0f, "accent"};
    float header = 30.0f;  // header height, design px
    float gap = 8.0f;      // between panes, design px
};
PaneLook pane_look(std::string_view style, const layout::PaneFrame* frame);

// Where toasts stack. `top`: under a bar at the top, `offset` design px down from the window's
// top; else at the bottom, `offset` design px up from the window's bottom (on top of the usual
// 16 px margin).
struct ToastArea {
    bool top = false;
    float offset = 0.0f;
};
// `bottom_block`: design px a style keeps at the bottom that toasts must not cover (the Cockpit's
// log, when shown).
ToastArea toast_area(std::string_view style, float bottom_block = 0.0f);

// Whether decorative screen effects may be drawn: not when the setting is "off", not on a light
// theme (they look like smudges there), not while Windows' high contrast is on.
bool effects_allowed(std::string_view setting, bool light_theme, bool high_contrast);

// The dark and light theme a style is designed with (written for it the first time it shows,
// when the person has not picked themes for that layout yet).
std::optional<std::pair<std::string, std::string>> suggested_themes(std::string_view style);

}  // namespace agentdesk::ui::styles

// Neutral names for what the Mac layout first needed and the other styles reuse: agent counts,
// the clock, easing and the menu bar's menus. The code stays where it was written.
namespace agentdesk::ui::chrome_common {
using mac::AgentCounts;
using mac::approach;
using mac::clock_text;
using mac::count_agents;
using mac::ms_to_next_minute;
}  // namespace agentdesk::ui::chrome_common

namespace agentdesk::ui::menus {
using mac::kMenuCount;
using mac::Menu;
using mac::menu_items;
using mac::menu_title;
using mac::MenuContext;
using mac::MenuItem;
using mac::MenuTab;
}  // namespace agentdesk::ui::menus
