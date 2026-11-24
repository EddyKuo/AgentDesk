#pragma once
// The Terminals layout's options (square corners, gaps) and its keyboard tools (the resize mode,
// splitting toward the longer side), kept apart from the drawing (apps/desktop/terminals_options.cpp)
// so they can be unit-tested. These used to be the separate Tiling layout.
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "agentdesk/ui/layout.hpp"

namespace agentdesk::ui::terminals {

// pane.split:auto: a pane wider than this times its height splits to the right, else down.
inline constexpr float kWideRatio = 1.6f;
// "right" or "down" for a pane of w x h.
const char* split_direction(float w, float h);

// The resize mode's keys: H/J/K/L and the arrows resize (40 px, 10 px with Shift), Esc and Enter
// leave the mode.
enum class Key { kH, kJ, kK, kL, kLeft, kDown, kUp, kRight, kEscape, kEnter };
struct ResizeAction {
    bool leave = false;
    std::string command;  // "pane.resize:<dir>" or "pane.resize:<dir>:10"; empty when leaving
};
ResizeAction resize_key(Key key, bool shift);

// "left", "left:10": the direction and the distance in design px (40 when not given). nullopt
// when the distance is not a positive number.
std::optional<std::pair<std::string, float>> parse_resize(std::string_view arg);

// The options as ui.json keeps them.
struct Options {
    bool square = false;  // terminals_square: panes without rounded corners
    float gaps = 8.0f;    // terminals_gaps: design px between panes, 0..24
    bool gaps_on = true;  // terminals_gaps_on: terminals.gaps turns the gaps off and on
};
inline constexpr float kDefaultGaps = 8.0f;
// The gaps setting kept within 0..24 design px.
float clamp_gaps(float gaps);
// True when the options leave the layout as its file draws it.
bool is_default(const Options& o);

// Puts the options on a Terminals tree: the first tab block's pane gap and corner radius, and the
// root's padding and gap around the bar (three quarters of the gap, so the default 8 keeps the
// file's 6). Gaps off: panes touch each other and the window's edges. Returns false when the tree
// has no tab block.
bool apply_options(layout::Node& tree, const Options& o);

}  // namespace agentdesk::ui::terminals
