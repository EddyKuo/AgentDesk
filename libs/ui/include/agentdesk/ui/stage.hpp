#pragma once
// The Stage layout's rules, without drawing: which agents get a thumbnail in the filmstrip and in
// what order (the ones that need you first, the rest in a stable order), the strip's filters and
// thumbnail sizes, stepping through the agents, the history of what was on stage, when an agent
// may be put on stage by itself, the strip's horizontal scrolling and the needs-you glow.
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "agentdesk/ui/workspace.hpp"

namespace agentdesk::ui::stage {

// ---------------------------------------------------------------- filter
enum class Filter { kAll, kNeedsYou, kWorking };
// "all", "needs_you", "working". Mission Control's saved names read as the nearest one:
// "blocked" -> needs_you, "working" -> working, anything else -> all.
Filter parse_filter(std::string_view name);
std::string_view filter_name(Filter f);
bool filter_matches(Filter f, Attention a);

// ---------------------------------------------------------------- thumbnail sizes
enum class Size { kSmall, kMedium, kLarge };
// One size of thumbnail, in design px: the card, its header row (icon, name, state), the footer
// line (message or where it runs; 0: none) and the scaled-down monospace of its screen lines.
struct SizePreset {
    float thumb_w = 0;
    float thumb_h = 0;
    float header_h = 0;
    float footer_h = 0;
    float mono_size = 0;  // font size of the screen lines
    float line_h = 0;     // their line height
    int max_lines = 0;    // at most this many screen lines
};
const SizePreset& preset(Size s);
// "s", "m", "l" (also "small", "medium", "large"); anything else is medium.
Size parse_size(std::string_view name);
std::string_view size_name(Size s);
// S -> M -> L -> S (the size button in the strip).
Size next_size(Size s);

// The strip: a header row with the count, filters and size buttons, then one row of thumbnails.
inline constexpr float kStripHeaderH = 32.0f;
inline constexpr float kStripBottomPad = 12.0f;
inline constexpr float kThumbGap = 12.0f;
// The strip block's height for thumbnails of `s` (design px).
float strip_height(Size s);
// The size actually used in a window `window_h` design px tall: `wanted`, or a smaller one when
// the strip would take more than kMaxStripShare of the height (never smaller than S), so the stage
// stays the big thing on a small window.
inline constexpr float kMaxStripShare = 0.4f;
Size fit_size(Size wanted, float window_h);

// ---------------------------------------------------------------- order
struct StripAgent {
    std::string id;
    Attention att = Attention::kNone;
    long long blocked_since = 0;  // when it started needing you (any clock; only compared)
    long long first_seen = 0;     // when the strip first saw it: the stable order
    bool unreachable = false;     // its machine does not answer
};

// Every agent in its stable order: first seen first, agents on unreachable machines after the
// rest. Next / previous step through this ring, so it does not change while states change.
std::vector<std::string> lineup(const std::vector<StripAgent>& agents);

// The thumbnails, left to right: the agent on stage is left out (it is the big one), agents that
// need you come first (the one waiting longest first), then the others in their stable order;
// unreachable ones last. `filter` drops what does not match (needs-you agents always pass
// kNeedsYou; kWorking keeps only working ones).
std::vector<std::string> strip_order(const std::vector<StripAgent>& agents, std::string_view stage_id,
                                     Filter filter);

// The agent `dir` steps (+1 next, -1 previous) from `current` in `ring`, wrapping. The first (or
// last, going back) when `current` is not in it; "" for an empty ring.
std::string step(const std::vector<std::string>& ring, std::string_view current, int dir);

// ---------------------------------------------------------------- history
// What was on stage, most recent first, each agent once. `previous()` is the one before the
// current one: "back" swaps it in again, so two agents can be flipped between.
class History {
public:
    static constexpr std::size_t kMax = 16;
    // `id` is on stage now (moves to the front; ignored when empty or already in front).
    void push(std::string_view id);
    // The agent went away: it is dropped from the history.
    void forget(std::string_view id);
    [[nodiscard]] std::string current() const;
    [[nodiscard]] std::string previous() const;
    [[nodiscard]] const std::vector<std::string>& items() const { return items_; }

private:
    std::vector<std::string> items_;
};

// ---------------------------------------------------------------- putting an agent on stage by itself
// Settings > Look > Stage "Switch to an agent that needs you" (off by default). Even when on, the
// stage never changes while the person is typing or reading a menu, nor away from an agent that
// itself needs them.
inline constexpr double kTypingQuietMs = 4000.0;  // no key for this long counts as not typing
struct AutoSwitch {
    bool enabled = false;
    bool stage_needs_you = false;  // the agent on stage needs the person too
    double ms_since_input = 0;     // since the last key or click
    bool popup_open = false;       // a menu, dialog or the palette is open
};
// The agent to put on stage: the first of `newly_blocked` (agents that started needing the person
// since the last frame, in strip order) when allowed, else "".
std::string auto_switch_target(const AutoSwitch& in, const std::vector<std::string>& newly_blocked);

// ---------------------------------------------------------------- horizontal scrolling
// Width of `count` thumbnails `thumb_w` wide, `gap` apart.
float content_width(int count, float thumb_w, float gap);
// `scroll` kept between 0 and what is left over past `view_w`.
float clamp_scroll(float scroll, float content_w, float view_w);
// The scroll that brings thumbnail `index` fully into view, moving as little as possible.
float scroll_to_show(float scroll, int index, float thumb_w, float gap, float view_w);

// ---------------------------------------------------------------- the needs-you glow
// How strongly a thumbnail that needs you glows (0..1), `ms` after it started: a soft pulse for
// kPulseCount breaths of kPulseMs, then a steady glow, so the window does not redraw forever.
// With reduced motion it is steady from the start.
inline constexpr double kPulseMs = 1600.0;
inline constexpr int kPulseCount = 3;
inline constexpr float kGlowSteady = 0.7f;
float glow_level(double ms, bool reduced);
// The pulse is still moving at `ms` (the caller keeps frames coming).
bool glow_moving(double ms, bool reduced);

}  // namespace agentdesk::ui::stage
