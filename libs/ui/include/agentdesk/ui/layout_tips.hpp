#pragma once
// First-use tips: a short card per built-in layout (and Focus mode) saying what the layout is for
// and how to use it, with the real keys looked up when the card is shown. Which card goes with
// the shown layout, and which cards a person has already seen. No drawing here.
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace agentdesk::ui::tips {

struct Tip {
    std::string text;
    std::string command;   // its key is shown beside the text ("" for none)
    std::string command2;  // a second key shown after the first ("F8 / Shift+F8")
    // A mouse action or a note: shown even when `command` has no key. Otherwise a tip whose
    // command has no key is left out (it would only say "there is a command").
    bool always = false;
};

struct Card {
    std::string key;      // a built-in layout's name, or "focus"
    std::string title;    // the layout's display name
    std::string purpose;  // one sentence: what the layout is for
    std::vector<Tip> tips;
};

// The key Focus mode's card is stored under (tips_seen in ui.json).
inline constexpr std::string_view kFocusKey = "focus";

// Every card key, in the order Settings lists the layouts, then "focus".
[[nodiscard]] const std::vector<std::string>& card_keys();
// The card for `key`, or nullopt when there is none.
[[nodiscard]] std::optional<Card> card(std::string_view key);

// The card for what is shown: Focus mode's while it is on; a built-in layout's own; a layout of the
// person's that uses a built-in style (a copy of mac, tiling, ...) gets that style's card; else "".
[[nodiscard]] std::string card_key(std::string_view layout, std::string_view style, bool focus);

// A tip ready to draw: its text and its key ("" when it has none).
struct ShownTip {
    std::string text;
    std::string chord;
};
// The card's tips with their keys from `chord_for` (a command -> "Ctrl+Shift+M", "" when unbound).
[[nodiscard]] std::vector<ShownTip> resolve(const Card& c,
                                            const std::function<std::string(std::string_view)>& chord_for);

// Whether the card `key` should appear by itself: tips are on, there is a card, not yet seen.
[[nodiscard]] bool should_show(std::string_view key, bool enabled, const std::vector<std::string>& seen);
// Adds `key` to `seen` once. Returns true when it was not there before.
bool mark_seen(std::vector<std::string>& seen, std::string_view key);

}  // namespace agentdesk::ui::tips
