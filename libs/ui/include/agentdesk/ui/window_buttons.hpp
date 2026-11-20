#pragma once
// The colours of the round close / minimize / zoom buttons, worked out from the active theme so
// they follow it instead of a fixed red, amber and green. Pure colour math (0xRRGGBB), no ImGui.
#include <array>

#include "agentdesk/ui/color_math.hpp"

namespace agentdesk::ui::window_buttons {

using color::Rgb;

// What the derivation looks at: the theme's status colours, its accent, and the bar the buttons
// sit on (plus its border and text, for the inactive look).
struct Input {
    Rgb blocked = 0;  // "needs you": the close button starts from it
    Rgb working = 0;  // minimize starts from it when it is a warm colour
    Rgb done = 0;     // zoom starts from it
    Rgb accent = 0;   // every button turns a little toward it
    Rgb bar = 0;      // what the buttons are drawn on
    Rgb border = 0;
    Rgb text = 0;
};

struct Colors {
    std::array<Rgb, 3> fill{};   // close, minimize, zoom
    std::array<Rgb, 3> glyph{};  // the cross / bar / plus drawn on each fill
    Rgb muted = 0;               // all three in an inactive window
};

// Each fill reaches this contrast against the bar (WCAG, a graphic's AA level)...
inline constexpr double kMinContrast = 3.0;
// ...each pair of fills is at least this far apart (CIEDE2000)...
inline constexpr double kMinApart = 15.0;
// ...and each glyph reaches this contrast on its fill.
inline constexpr double kGlyphContrast = 3.0;
// The OKLCh hue (degrees) each button is kept near: red, amber, green. A theme's own hue is kept
// within kHueWindow of it, so the three never swap roles or merge.
inline constexpr std::array<double, 3> kAnchorHue = {25.0, 80.0, 145.0};
inline constexpr double kHueWindow = 25.0;

// The three colours for `in`. Deterministic; the same theme always gives the same buttons.
[[nodiscard]] Colors derive(const Input& in);

}  // namespace agentdesk::ui::window_buttons
