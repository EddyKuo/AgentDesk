#pragma once
// Herdr 98's state in agent-ui (herdr-app defaults/mods/win98, commit 321c9a8): the open menus,
// dialogs and game windows that herdr opens as overlays, the shared values the mod keeps
// (herdr's shared(), persisted: <data>\win98.json), and the games. Drawn by win98.inl.
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "agentdesk/ui/image_cache.hpp"
#include "agentdesk/ui/win98.hpp"

namespace agentdesk::desktop::w98state {

using Clock = std::chrono::steady_clock;

// kit.js 350-355: a menu item. `act` is what perform() does (kit.js 388-400).
struct MenuItem {
    std::string label;
    std::string icon;  // an icon of the mod (icons/<name>.svg)
    std::string img;   // a picture file under win98\ (programs/claude.svg)
    std::string shortcut;
    std::function<void()> act;
    std::vector<MenuItem> sub;
    bool disabled = false;
    bool checked = false;
    bool radio = false;
    bool bold = false;
    bool sep = false;
};

// menu.js: one popup menu. Its parents are the menus it hangs off.
struct Menu {
    int id = 0;
    std::vector<MenuItem> items;
    ImVec2 pos{};
    ImVec2 size{};
    bool start = false;
    std::string owner;  // a menu bar's label that stays pressed while it is open
    std::vector<int> parents;
    int at = -1;  // the highlighted row
    int sub_index = -1;
    int sub_id = 0;
    int opened_frame = -1;  // the press that opened it is not one beside it
};

// dialog.js: one dialog window, by kind, with its props and state.
struct Button {
    std::string label;
    bool def = false;
    std::function<void()> act;
};
struct Dialog {
    std::string kind;  // prompt, confirm, message, run, shutdown, logoff, about, taskman, display,
                       // datetime, mycomputer, network, bin, welcome, screen
    std::string title;
    std::string text;
    std::string icon;
    std::optional<std::string> value;
    bool machines = false;
    // prompt: what OK runs with the name and the computer ("" this one).
    std::function<void(const std::string& name, const std::string& machine)> command;
    std::vector<Button> buttons;
    std::string mode;   // screen: boot, shutdown, restart
    float width = 340;  // Win98 px
    // state (dialog.js 47-75)
    std::string input;
    bool fresh = true;
    int choice = 0;
    int sel = 0;
    int page = 0;
    std::string machine;
    int stage = 0;
    Clock::time_point opened{};
    Clock::time_point stage_at{};
};

// An overlay window: a dialog, a game or the Solitaire options (herdr openOverlay kind "window",
// "dialog"). Game windows have a key and come back where they were dropped.
struct Window {
    int id = 0;
    std::string role;  // dialog, mines, sol, sol-dialog
    std::string key;
    std::unique_ptr<Dialog> dialog;
    std::string sol_dialog;  // options, deck
    int draw = 3;            // the Solitaire dialog's choices until OK
    bool scoring = true;
    int deck = 0;
    bool modal = false;  // kind "dialog": dims the window, a click beside it does nothing
    ImVec2 pos{};
    ImVec2 size{};
    bool placed = false;
    bool want_focus = true;
    bool closing = false;
};

struct Clippy {
    std::vector<std::string> dismissed;  // "<session>:<status>"
    std::optional<int> tip;
    float y_anim = 0;
};

struct W98 {
    nlohmann::json shared = nlohmann::json::object();  // "win98.scale", "win98.mute", ...
    bool shared_loaded = false;
    std::vector<Menu> menus;
    std::vector<std::unique_ptr<Window>> windows;  // drawing order: last on top
    std::map<std::string, ImVec2> dropped;         // a keyed window's last place
    int next_id = 1;
    int focus = 0;    // the window that has the keyboard (0: none)
    int drag_id = 0;  // the window being dragged
    ImVec2 drag_grab{};
    bool was_shown = false;  // the layout showed last frame
    int menu_focus = 0;      // the menu given the keyboard
    std::string sol_menu;    // sol.js 380: the Solitaire menu opened last
    long long wakeup_ms = 1000;
    // taskbar.js
    bool booted = false;                 // the taskbar started for this showing of the layout
    std::vector<std::string> installed;  // agent profiles the Start menu offers
    ImVec4 start_bounds{};               // the Start button (x, y, w, h)
    std::string desktop_sel;
    float desktop_scroll = 0;
    // Games (mines.js, sol.js); made when first opened.
    std::unique_ptr<ui::win98::Mines> mines;
    Clock::time_point mines_tick{};
    std::unique_ptr<ui::win98::Solitaire> sol;
    Clock::time_point sol_tick{};
    Clock::time_point sol_anim{};
    struct SolPress {
        ui::win98::Solitaire::Loc loc;
        ImVec2 at;
        ImVec2 grab;  // where in the card it was taken (Win98 px)
    };
    std::optional<SolPress> sol_press;
    std::optional<ImVec2> sol_drag;
    Clippy clippy;
    ui::ImageCache images;
    ImFont* font = nullptr;  // Pixelated MS Sans Serif
    ImFont* bold = nullptr;
    ImFont* dos = nullptr;    // PxPlus IBM VGA 8x16
    ImFont* arial = nullptr;  // Mines numbers (mines.js 157: Arial bold)
    bool fonts_tried = false;
    std::string applied_layout;  // the layout whose fonts are in use
};

}  // namespace agentdesk::desktop::w98state
