#pragma once
// The Pair layout's settings in ui.json and its per-window state, as agent-ui's shared files see
// them. The drawing and the pair.* commands are in pair_layout.cpp; the rules in libs/ui pair.hpp.
#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentdesk/ui/pair.hpp"

namespace agentdesk::desktop {

// ui.json "pair": the setup card's last choices, and the pair shown (so it is still there after
// the window is closed and opened again; it is dropped when its terminals are gone).
struct PairSettings {
    std::string left_profile;   // "": the default (Claude Code)
    std::string right_profile;  // "": the default (Codex)
    bool worktrees = true;      // each side in its own git worktree
    std::string left;           // the pair's sessions ("" none)
    std::string right;
};

// What Pair remembers between frames (one window).
struct PairUi {
    ui::pair::Model model;
    ui::pair::Race race;
    bool restored = false;  // ui.json's pair was looked for once the sessions were listed
    bool shown = false;     // Pair showed last frame (the frame it appears focuses the left side)
    int drawn_frame = -10;  // the ImGui frame a Pair block was last drawn in
    // The shared prompt bar.
    char prompt[2048] = {};
    bool prompt_focus = false;   // pair.prompt asked for the cursor
    bool prompt_active = false;  // the bar has the keyboard
    // The race strip: closed by the person for this round; the side marked to keep.
    bool dismissed = false;
    std::optional<ui::pair::Side> kept;
    // When a side crossed the finish line first (steady ms; the accent fades in from here).
    std::optional<ui::pair::Side> finish_side;
    // The re-pair menu is open for this side.
    std::optional<ui::pair::Side> repair_menu;
    // The setup card's open program list (0 left, 1 right, -1 none).
    int picker = -1;
    // Starting: what the card says, and answers from agent-host threads (under placements_mutex).
    std::string starting_note;
    std::vector<std::string> errors;
    std::chrono::steady_clock::time_point starting_since{};
    // The folders' change counts are read again while a side works (every 3 s) and when a turn ends.
    std::array<std::string, 2> last_state;
    std::chrono::steady_clock::time_point counts_at{};
};

namespace detail {
void read_pair_settings(const nlohmann::json& j, PairSettings& out);
void write_pair_settings(nlohmann::json& j, const PairSettings& in);
}  // namespace detail

}  // namespace agentdesk::desktop
