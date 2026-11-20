#pragma once
// The rules behind the AgentDesk Tiling layout (a thin bar, square panes with gaps, a resize mode
// driven by the keyboard), kept apart from its drawing (apps/desktop/tiling_layout.cpp) so they
// can be unit-tested.
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "agentdesk/ui/layout.hpp"

namespace agentdesk::ui::tiling {

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

// The gaps setting kept within 0..24 design px.
float clamp_gaps(float gaps);
// Puts the gaps on the tree's first tab block: the block's padding (window edge to panes) and the
// pane gap. Off: both 0. Returns false when the tree has no tab block.
bool apply_gaps(layout::Node& tree, float gaps, bool on);

// Whether panes get their title row: "always", "never", or "auto" (two panes or more).
bool show_titles(std::string_view mode, int panes);

// The bar's segments, widest form first. The room runs out in this order: the focused pane's
// title (A4) goes first, then tabs other than the current one shrink to their numbers, then the
// clock goes, and last the tabs are cut at the right edge.
struct BarWidths {
    float lead = 0;        // window buttons and space chips, always shown
    float tabs_full = 0;   // "1:claude  2:pwsh*"
    float tabs_short = 0;  // "1  2:pwsh*"
    float title = 0;       // the focused pane's program, folder and branch
    float counts = 0;      // "!1 ~2", always shown
    float clock = 0;
};
struct BarFit {
    bool title = true;
    bool short_tabs = false;
    bool clock = true;
    float tabs_room = 0;  // what the tabs may use
};
BarFit fit_bar(const BarWidths& w, float available);

// The space chips: 1..9, and the active space when it is past 9; the rest counted as "+n".
struct SpaceChips {
    std::vector<int> shown;  // space indices, in order
    int more = 0;
};
SpaceChips space_chips(int count, int active);

}  // namespace agentdesk::ui::tiling
