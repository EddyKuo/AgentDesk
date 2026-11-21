// The first-use tip cards; see layout_tips.hpp. The wording follows the user guide: keep the two
// in step when a layout's behaviour changes.
#include "agentdesk/ui/layout_tips.hpp"

#include <algorithm>

#include "agentdesk/ui/layout_styles.hpp"

namespace agentdesk::ui::tips {

namespace {
Tip key(std::string text, std::string command, std::string command2 = {}) {
    return {std::move(text), std::move(command), std::move(command2), false};
}
// Shown whether or not `command` has a key.
Tip mouse(std::string text, std::string command = {}) {
    return {std::move(text), std::move(command), {}, true};
}

Card make(std::string_view k) {
    Card c;
    c.key = std::string(k);
    c.title = k == kFocusKey ? std::string("Focus mode") : styles::display_name(k);
    if (k == "workspace") {
        c.purpose = "Spaces and agents on the left, tabs and split panes in the middle.";
        c.tips = {key("Hide or show the sidebar of spaces and agents", "sidebar.toggle"),
                  key("Go to any space, tab or agent", "app.picker"),
                  key("Split the pane right / down", "pane.split:right", "pane.split:down"),
                  mouse("Files and Changes: the buttons at the right end of the tab bar"),
                  key("Every command, searchable", "app.palette")};
    } else if (k == "terminals") {
        c.purpose =
            "Just your terminals: the tabs in one bar at the top, panes you can tile from the keyboard.";
        c.tips = {
            mouse("The sidebar button at the far left brings back the layout with the sidebar",
                  "sidebar.toggle"),
            key("Go to any space, tab or agent (or click the space chip)", "app.picker"),
            key("Resize mode: H J K L or the arrows, Shift for small steps, Esc to finish",
                "terminals.resize_mode"),
            key("Split toward the pane's longer side / close the pane", "pane.split:auto", "pane.close"),
            mouse("Square corners and gaps: Settings > Look; gaps off and on here too", "terminals.gaps")};
    } else if (k == "mac") {
        c.purpose = "A desktop look: a menu bar on top, your tab in a floating window, a Dock below.";
        c.tips = {mouse("The menus at the top run every command; the AgentDesk mark opens the app menu"),
                  mouse("On the window: red and yellow put the tab away in the Dock (programs keep running), "
                        "green fills the desktop"),
                  mouse("The Dock holds your spaces, then this space's tabs; right-click a tab to rename or "
                        "close it"),
                  key("Go to any space, tab or agent", "app.picker"),
                  key("Back to the layout with the sidebar", "sidebar.toggle")};
    } else if (k == "stage") {
        c.purpose = "One agent big on the stage, every other agent live in the filmstrip below.";
        c.tips = {
            mouse("Click a thumbnail to swap it onto the stage; the one there goes back to the strip"),
            key("Next / previous agent on the stage", "stage.next", "stage.prev"),
            mouse("An agent that needs you glows and moves to the front; above the strip, All / Needs you "
                  "/ Working filter it and S M L sets the thumbnail size"),
            mouse("To let an agent that needs you take the stage by itself, turn it on in Settings > Look "
                  "(never while you type)",
                  "stage.auto_switch"),
            key("Back to your layout", "stage.toggle")};
    } else if (k == "review") {
        c.purpose = "The agent's terminal on the left, its uncommitted changes and diff on the right.";
        c.tips = {key("Next / previous changed file", "review.next_file", "review.prev_file"),
                  key("Focus another agent and the list follows it", "agent.next"),
                  mouse("Type a message in the box at the bottom to commit everything listed"),
                  mouse("The list reloads when the agent finishes a turn; refresh is at the top right",
                        "review.refresh"),
                  key("Back to where you were", "review.toggle")};
    } else if (k == "classic") {
        c.purpose = "The whole window drawn in text, like a monochrome terminal of old.";
        c.tips = {
            key("Open the menu bar: arrows move, Enter runs, Esc closes", "classic.menu"),
            mouse("States are always written out: [!] needs you, [~] working, [+] done"),
            mouse("Screen effects and the monochrome terminal (off for coloured diffs): Settings > Look",
                  "classic.effects"),
            key("Show or hide the text sidebar", "sidebar.toggle"),
            mouse("A fun layout: turn on Settings > Look > Fun layouts to keep it in the layout list",
                  "app.settings")};
    } else if (k == kFocusKey) {
        c.purpose = "Only the terminal you are working in, with the other agents as dots in a thin strip.";
        c.tips = {mouse("The dots are your other agents: red needs you, green done, a yellow ring working. "
                        "Click one to switch to it"),
                  mouse("When an agent needs you, the window edge flashes and a sound plays, with no "
                        "notification (Settings > Alerts)"),
                  mouse("Rest the pointer at the top of the terminal for its title bar: split, zoom, close"),
                  key("Next agent", "agent.next"),
                  key("Leave Focus mode (Exit focus in the strip does the same)", "focus.toggle")};
    } else {
        c.key.clear();
    }
    return c;
}
}  // namespace

const std::vector<std::string>& card_keys() {
    static const std::vector<std::string> k = {"workspace", "terminals",           "mac", "stage", "review",
                                               "classic",   std::string(kFocusKey)};
    return k;
}

std::optional<Card> card(std::string_view key) {
    Card c = make(key);
    if (c.key.empty())
        return std::nullopt;
    return c;
}

std::string card_key(std::string_view layout, std::string_view style, bool focus) {
    if (focus)
        return std::string(kFocusKey);
    const auto& keys = card_keys();
    auto known = [&](std::string_view k) {
        return k != kFocusKey && std::find(keys.begin(), keys.end(), k) != keys.end();
    };
    if (known(layout))
        return std::string(layout);
    if (!style.empty() && known(style))
        return std::string(style);
    return {};
}

std::vector<ShownTip> resolve(const Card& c, const std::function<std::string(std::string_view)>& chord_for) {
    std::vector<ShownTip> out;
    for (const Tip& t : c.tips) {
        std::string chord = t.command.empty() ? std::string() : chord_for(t.command);
        if (!chord.empty() && !t.command2.empty())
            if (const std::string second = chord_for(t.command2); !second.empty())
                chord += " / " + second;
        if (chord.empty() && !t.always)
            continue;
        out.push_back({t.text, std::move(chord)});
    }
    return out;
}

bool should_show(std::string_view key, bool enabled, const std::vector<std::string>& seen) {
    if (!enabled || key.empty() || !card(key))
        return false;
    return std::find(seen.begin(), seen.end(), key) == seen.end();
}

bool mark_seen(std::vector<std::string>& seen, std::string_view key) {
    if (key.empty() || std::find(seen.begin(), seen.end(), key) != seen.end())
        return false;
    seen.emplace_back(key);
    return true;
}

}  // namespace agentdesk::ui::tips
