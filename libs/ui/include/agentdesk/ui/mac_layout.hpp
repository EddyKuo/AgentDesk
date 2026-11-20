#pragma once
// The rules behind the AgentDesk Mac layout (menu bar, Dock, window title, clock), kept apart from
// its drawing (apps/desktop/mac_layout.cpp) so they can be unit-tested.
#include <string>
#include <utility>
#include <vector>

#include "agentdesk/ui/workspace.hpp"

namespace agentdesk::ui::mac {

// ---------------------------------------------------------------- menu bar
// The bar's menus, left to right. kApp is opened from the mark and the "AgentDesk" name.
enum class Menu { kApp, kFile, kEdit, kView, kWindow, kHelp };
inline constexpr int kMenuCount = 6;
const char* menu_title(Menu m);

struct MenuItem {
    enum class Kind { kItem, kSeparator, kHeader };
    Kind kind = Kind::kItem;
    std::string label;
    std::string command;  // what the row runs, with its argument ("layout.switch:mac")
    bool checked = false;
    bool enabled = true;
};

struct MenuTab {
    std::string title;
    bool active = false;
    bool settled = false;
};

// What the menus need to know about the app right now.
struct MenuContext {
    std::vector<std::pair<std::string, std::string>> layouts;  // (name, shown name), in order
    std::string layout;                                        // the one shown
    int space = -1;                                            // the active space's index, -1: none
    std::vector<MenuTab> tabs;                                 // the active space's tabs
    bool fill = false;                                         // the window fills the desktop
};

// The rows of a menu. Every row runs an existing command; rows that cannot act right now (no
// terminal to copy from) are disabled rather than left out, so the menus keep their shape.
std::vector<MenuItem> menu_items(Menu m, const MenuContext& c);

// ---------------------------------------------------------------- Dock
struct DockItem {
    enum class Kind { kSpace, kTab, kSeparator, kNew, kSettings };
    Kind kind = Kind::kSpace;
    int index = -1;   // the space's or the tab's index
    std::string key;  // stable across frames: "s:<space id>", "t:<tab id>", "sep:<n>", "new", "settings"
    [[nodiscard]] bool tile() const { return kind != Kind::kSeparator; }
};

// One tile per space, a separator, one per tab of the active space, a separator, then "+" and the
// gear. Without spaces only "+" and the gear remain.
std::vector<DockItem> dock_items(const std::vector<std::string>& space_ids,
                                 const std::vector<std::string>& tab_ids);

// Where each item sits from the pill's left edge, and the pill's width: `pad` inside each end,
// `gap` between items; a tile is `tile` wide, a separator 1.
struct DockGeometry {
    std::vector<float> x;
    std::vector<float> w;
    float width = 0;
};
DockGeometry dock_geometry(const std::vector<DockItem>& items, float tile, float gap, float pad);

// The magnification a tile aims for while `hovered` (an item index, -1: none) is under the
// pointer: 1.25 for it, 1.1 for the tiles right beside it (separators do not count), else 1.
float dock_target_scale(const std::vector<DockItem>& items, int item, int hovered);
// One animation step from `current` toward `target` over `dt` seconds (exponential, about 0.15 s
// to settle); lands exactly on the target once close.
float approach(float current, float target, float dt);

// ---------------------------------------------------------------- the rest
// The menu bar clock, local time on a 24-hour clock: "Thu 16:25". weekday: 0 = Sunday.
std::string clock_text(int weekday, int hour, int minute);

// Milliseconds until the clock's minute changes, from the milliseconds into the current minute.
long long ms_to_next_minute(long long ms_into_minute);

// How many agents need the person and how many are working.
struct AgentCounts {
    int needs = 0;
    int working = 0;
};
AgentCounts count_agents(const std::vector<Attention>& states);

// The window's title: "<space> — <tab>", or just one of them when the other is empty.
std::string window_title(const std::string& space, const std::string& tab);

}  // namespace agentdesk::ui::mac
