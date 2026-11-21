#pragma once
// The design tokens (libs/ui design_tokens.hpp) in screen terms: spacing in pixels at the current
// zoom, the type levels' fonts, hover / selected / focus colours from the active palette, the one
// elevation helper, crisp hairlines, eased transitions that follow Windows' "Animation effects",
// and the empty-state illustration. Every layout draws with these so they look like one app.
// Included by app_internal.hpp; use the `tok` names below.
#include <cstdint>
#include <string>

#include <imgui.h>

#include "agentdesk/ui/design_tokens.hpp"
#include "app.hpp"

namespace agentdesk::desktop::detail {
namespace tok {
using ui::tokens::TypeLevel;
namespace t = ui::tokens;

// ---------------------------------------------------------------- spacing, radii (screen px)
float sp(int steps);  // steps of the 4 px grid at the current zoom
inline float gap_xs() {
    return sp(1);
}  // 4
inline float gap_s() {
    return sp(2);
}  // 8
inline float gap_m() {
    return sp(3);
}  // 12
inline float gap_l() {
    return sp(4);
}  // 16
inline float gap_xl() {
    return sp(6);
}  // 24
float radius_small();
float radius_card();
float radius_panel();

// ---------------------------------------------------------------- type
ImFont* font(const App::State& s, const TypeLevel& level);
float size(const TypeLevel& level);  // in screen px
// Draws `text` at the level's size and weight; returns its width.
float text(const App::State& s, ImDrawList* dl, const TypeLevel& level, ImVec2 pos, ImU32 col,
           const char* text, const char* end = nullptr);
ImVec2 measure(const App::State& s, const TypeLevel& level, const char* text, const char* end = nullptr);

// ---------------------------------------------------------------- colour treatments
// On `bg` (0: the palette's card colour), with the palette's text and accent.
ImU32 hover(ImU32 bg = 0);
ImU32 selected(ImU32 bg = 0);
ImU32 selected_edge(ImU32 bg = 0);
ImU32 focus_ring(ImU32 bg = 0);
ImU32 divider(ImU32 bg = 0);

// ---------------------------------------------------------------- lines and elevation
// A 1 px line on whole device pixels (no blur), horizontal or vertical.
void hline(ImDrawList* dl, float x0, float x1, float y, ImU32 col);
void vline(ImDrawList* dl, float x, float y0, float y1, ImU32 col);
// A raised card's soft shadow (outside [a, b]) and the faint highlight along its top edge.
// Draw before the card's fill (shadow) — the highlight is drawn by `card()` after it.
void shadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, int level = 1, ImU32 bg = 0);
// A whole raised card: shadow, fill, 1 px border and top highlight. `fill` 0: the palette's card.
void card(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, int level = 1, ImU32 fill = 0, ImU32 border = 0);

// ---------------------------------------------------------------- motion
// Windows' "Animation effects" is off (Settings > Accessibility > Visual effects); read at most
// every 2 s. Every transition below jumps instead of easing then.
bool reduce_motion();
// Windows' high contrast is on (read at most every 2 s).
bool high_contrast();
double now_ms();
// An eased 0..1 for `id` that moves toward 1 while `on`, else toward 0. Starts at the first value
// asked for (nothing animates on the first frame).
float fade(ImGuiID id, bool on, double duration_ms = t::kNormalMs);
// `target` reached by easing from the colour shown for `id` before (state dots, badges).
ImU32 cross_fade(ImGuiID id, ImU32 target, double duration_ms = t::kNormalMs);
// A transition drew a frame that is not its last: the frame loop wakes again soon.
void keep_animating();
// Milliseconds until the next frame a transition needs (a large number when none).
long long wakeup_ms();
// Called once per frame: drops transitions that were not asked for in a while.
void end_frame();

// ---------------------------------------------------------------- empty states
// Small drawings of our own (simple shapes in the theme's colours) for empty views.
enum class Art : std::uint8_t { kNone, kTerminal, kChanges, kSearch, kAgents, kAllDone, kFiles };
// Draws `art` centred on `c`, `side` screen px across.
void draw_art(ImDrawList* dl, Art art, ImVec2 c, float side);
// The height an illustration takes above an empty state's line.
float art_height();

}  // namespace tok
}  // namespace agentdesk::desktop::detail
