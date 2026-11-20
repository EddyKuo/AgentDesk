#pragma once
// Colour arithmetic on 0xRRGGBB values, independent of ImGui: sRGB <-> linear, WCAG contrast,
// OKLab / OKLCh, CIELAB with CIEDE2000, colour-vision-deficiency simulation, and a palette check
// (contrast and "can these status colours be told apart") shared by the theme features.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace agentdesk::ui::color {

using Rgb = std::uint32_t;  // 0xRRGGBB

struct Linear {
    double r = 0, g = 0, b = 0;  // linear-light sRGB, 0..1 (may leave the range before clamping)
};
struct OkLab {
    double L = 0, a = 0, b = 0;
};
struct OkLch {
    double L = 0, C = 0, h = 0;  // h in degrees, 0..360
};
struct Lab {
    double L = 0, a = 0, b = 0;  // CIELAB, D65 white
};

[[nodiscard]] Rgb rgb(int r, int g, int b);
[[nodiscard]] int red(Rgb c);
[[nodiscard]] int green(Rgb c);
[[nodiscard]] int blue(Rgb c);

[[nodiscard]] double srgb_to_linear(double v);  // one channel, 0..1
[[nodiscard]] double linear_to_srgb(double v);
[[nodiscard]] Linear to_linear(Rgb c);
// Clamped to 0..1 per channel, then rounded to 8 bits.
[[nodiscard]] Rgb from_linear(const Linear& l);

// WCAG 2.1 relative luminance and contrast ratio (1..21; symmetric).
[[nodiscard]] double relative_luminance(Rgb c);
[[nodiscard]] double contrast(Rgb a, Rgb b);
// a mixed toward b by t (0..1) per sRGB channel.
[[nodiscard]] Rgb mix(Rgb a, Rgb b, double t);
// `fg` at `alpha` (0..255) over the opaque `bg`.
[[nodiscard]] Rgb over(Rgb fg, int alpha, Rgb bg);
// Black or white, whichever reads better on `bg`.
[[nodiscard]] Rgb best_on(Rgb bg);

[[nodiscard]] OkLab linear_to_oklab(const Linear& l);
[[nodiscard]] Linear oklab_to_linear(const OkLab& o);
[[nodiscard]] OkLab to_oklab(Rgb c);
[[nodiscard]] Rgb from_oklab(const OkLab& o);  // out-of-gamut values are clamped
[[nodiscard]] OkLch to_oklch(const OkLab& o);
[[nodiscard]] OkLab from_oklch(const OkLch& o);
[[nodiscard]] double hue_of(Rgb c);  // OKLCh hue in degrees

// The signed shortest turn from hue `from` to hue `to`, in (-180, 180].
[[nodiscard]] double hue_delta(double from, double to);
// Keeps OKLab L, adds a vector of length `amount` (OKLab units) in the direction of `hue_deg`.
[[nodiscard]] OkLab shift_toward_hue(const OkLab& c, double hue_deg, double amount);
// Turns the OKLCh hue toward `target_deg` the short way, by at most `max_deg`; L and C stay.
[[nodiscard]] OkLab turn_hue_toward(const OkLab& c, double target_deg, double max_deg);

[[nodiscard]] Lab to_lab(Rgb c);
[[nodiscard]] double delta_e2000(const Lab& x, const Lab& y);
[[nodiscard]] double delta_e2000(Rgb x, Rgb y);

// Machado, Oliveira and Fernandes (2009) at severity 1.0, applied in linear sRGB.
enum class Cvd { kNone, kProtan, kDeutan, kTritan };
[[nodiscard]] Rgb simulate(Rgb c, Cvd kind);
[[nodiscard]] const char* cvd_name(Cvd kind);  // "protanopia", ...

// ------------------------------------------------------------------ palette check
// The colours a theme check looks at, all opaque (translucent hover / selected composited onto
// the chrome first). Names follow the theme file: canvas, chrome, surface, foreground (interface
// text), muted, dim, accent, blocked, working, done, idle, and the terminal's background and
// foreground.
struct PaletteColors {
    Rgb canvas = 0, chrome = 0, surface = 0, selected = 0;
    Rgb foreground = 0, muted = 0, dim = 0, accent = 0;
    Rgb blocked = 0, working = 0, done = 0, idle = 0;
    Rgb term_bg = 0, term_fg = 0;
};

enum class CheckKind {
    kText,     // text on a background: AA is 4.5:1
    kGraphic,  // a status mark or accent outline: AA is 3:1
    kApart,    // two status colours under a colour-vision deficiency: CIEDE2000 distance
};
struct PaletteCheck {
    std::string what;  // "muted on canvas", "blocked/done (deuteranopia)"
    CheckKind kind = CheckKind::kText;
    double value = 0;
    double threshold = 0;
    [[nodiscard]] bool pass() const { return value >= threshold; }
};
// Contrast thresholds and the distance below which two status colours are flagged.
inline constexpr double kTextAA = 4.5;
inline constexpr double kGraphicAA = 3.0;
inline constexpr double kApartMin = 10.0;

// Every pair, in a fixed order (so two checks of related palettes line up index by index).
[[nodiscard]] std::vector<PaletteCheck> check_palette(const PaletteColors& p);
// Only the pairs below their threshold.
[[nodiscard]] std::vector<PaletteCheck> palette_problems(const PaletteColors& p);

// ------------------------------------------------------------------ ambient tint
// The colours an ambient tint moves: the backgrounds (canvas, chrome, surface, and hover /
// selected when opaque) keep their OKLab lightness and gain up to 0.010 of chroma toward `toward`'s
// hue; the accent turns up to 12 degrees toward it. k is the strength, 0..1.
inline constexpr double kTintChroma = 0.010;
inline constexpr double kTintTurnDeg = 12.0;
struct TintInput {
    PaletteColors base;
    Rgb hover = 0, selected = 0;  // the raw tokens
    bool hover_opaque = false, selected_opaque = false;
};
struct TintOutput {
    Rgb canvas = 0, chrome = 0, surface = 0, accent = 0, hover = 0, selected = 0;
    double k = 0;  // the strength actually used (lowered when a pair would fall below AA)
};
[[nodiscard]] TintOutput tint(const TintInput& in, Rgb toward, double k);
// tint(), with k lowered by bisection (6 steps) when a checked pair would fall below
// min(its value before, its AA threshold).
[[nodiscard]] TintOutput tint_protected(const TintInput& in, Rgb toward, double k);

}  // namespace agentdesk::ui::color
