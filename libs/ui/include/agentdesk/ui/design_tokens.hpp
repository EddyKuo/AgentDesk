#pragma once
// AgentDesk's shared design tokens: the spacing grid, corner radii, the three type levels, motion
// timing and easing, and the hover / selected / focus / elevation colours derived from a theme.
// Pure values and arithmetic (no ImGui), so every layout draws from one set and it can be tested;
// apps/desktop/ui_tokens.hpp turns them into screen pixels, fonts and draw calls.
#include <cstdint>

namespace agentdesk::ui::tokens {

// ---------------------------------------------------------------- spacing (design px)
// Everything sits on a 4 px grid; 8 px is the usual step between related things.
inline constexpr float kGrid = 4.0f;
[[nodiscard]] constexpr float space(int steps) {
    return kGrid * static_cast<float>(steps);
}
inline constexpr float kSpaceXs = 4.0f;    // inside a chip, icon to its label
inline constexpr float kSpaceS = 8.0f;     // between related items, row padding
inline constexpr float kSpaceM = 12.0f;    // card padding (compact), list item sides
inline constexpr float kSpaceL = 16.0f;    // card padding, between groups
inline constexpr float kSpaceXl = 24.0f;   // between sections
inline constexpr float kSpaceXxl = 32.0f;  // around an empty state

// ---------------------------------------------------------------- radii (design px)
inline constexpr float kRadiusSmall = 6.0f;   // chips, buttons, list rows, inputs
inline constexpr float kRadiusCard = 10.0f;   // cards, menus, toasts
inline constexpr float kRadiusPanel = 12.0f;  // dialogs, overlays, large panels

// A hairline: one device pixel at any zoom.
inline constexpr float kHairline = 1.0f;

// ---------------------------------------------------------------- type (bundled Geist)
enum class Weight : std::uint8_t { kRegular, kMedium, kSemibold };
struct TypeLevel {
    float size;  // design px
    Weight weight;
    float line;  // line height, design px (on the 4 px grid where it matters)
};
inline constexpr TypeLevel kTitle{15.0f, Weight::kSemibold, 20.0f};  // card and dialog titles
inline constexpr TypeLevel kBody{13.0f, Weight::kRegular, 18.0f};    // rows, descriptions
inline constexpr TypeLevel kCaption{11.5f, Weight::kMedium, 16.0f};  // labels, hints, counts

// ---------------------------------------------------------------- motion
// Durations in ms. Fast for hover and small colour changes, normal for fades and slides, slow for
// larger panels. Nothing loops except progress indicators.
inline constexpr double kFastMs = 120.0;
inline constexpr double kNormalMs = 180.0;
inline constexpr double kSlowMs = 260.0;
// How far a sliding element travels while it fades in (design px).
inline constexpr float kSlideDistance = 8.0f;

// Easing curves on t in 0..1 (clamped).
[[nodiscard]] float ease_out(float t);     // cubic: quick start, soft landing (entering, state changes)
[[nodiscard]] float ease_in_out(float t);  // cubic: for things that move across the screen

// A value that eases from where it is toward a target. A new target starts a new ease from the
// current value; with `reduced` (Windows' animation effects off) it jumps.
class Tween {
public:
    // Asks for `target` at `now_ms`; returns the value to draw.
    float update(float target, double now_ms, double duration_ms = kNormalMs, bool reduced = false);
    [[nodiscard]] float value(double now_ms) const;
    // Still moving at `now_ms` (the caller keeps frames coming).
    [[nodiscard]] bool moving(double now_ms) const;
    [[nodiscard]] float target() const { return to_; }

private:
    float from_ = 0.0f, to_ = 0.0f;
    double start_ = 0.0, duration_ = 0.0;
    bool set_ = false;
};

// ---------------------------------------------------------------- colour treatments (0xRRGGBB)
// The surface a treatment sits on, with the theme's text and accent colours.
struct Surface {
    std::uint32_t bg = 0;
    std::uint32_t fg = 0;
    std::uint32_t accent = 0;
    bool light = false;
};
// Hover: the text colour faintly over the surface.
[[nodiscard]] std::uint32_t hover_fill(const Surface& s);
// Selected: a calm accent wash, clearly stronger than hover.
[[nodiscard]] std::uint32_t selected_fill(const Surface& s);
// The edge or bar that marks the selected item (an accent that still reads on the wash).
[[nodiscard]] std::uint32_t selected_edge(const Surface& s);
// A keyboard-focus ring: the accent, lifted until it is at least 3:1 against the surface.
[[nodiscard]] std::uint32_t focus_ring(const Surface& s);
// A 1 px divider that is visible without shouting.
[[nodiscard]] std::uint32_t divider(const Surface& s);

// Elevation: a soft shadow under a raised card and a subtle highlight along its top edge.
struct Elevation {
    std::uint32_t shadow = 0;  // colour of the shadow
    int shadow_alpha = 0;      // its strongest alpha (0..255), fading outward
    float shadow_spread = 0;   // design px
    float shadow_offset = 0;   // design px down
    std::uint32_t highlight = 0;
    int highlight_alpha = 0;  // 0: none
};
// `level` 1: cards, toasts, menus; 2: dialogs and overlays.
[[nodiscard]] Elevation elevation(const Surface& s, int level);

}  // namespace agentdesk::ui::tokens
