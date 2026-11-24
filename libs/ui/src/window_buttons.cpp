// Theme-following colours for the round window buttons; see window_buttons.hpp.
#include "agentdesk/ui/window_buttons.hpp"

#include <algorithm>
#include <cmath>

namespace agentdesk::ui::window_buttons {

namespace {
using namespace color;

// A little slack on the linear-light range, so rounding does not count as out of gamut.
constexpr double kGamutEps = 1e-4;
// Chroma range of a fill (OKLCh): enough to read as a colour, not so much it glows.
constexpr double kMinChroma = 0.10;
constexpr double kMaxChroma = 0.18;
// Below this chroma a theme colour has no hue worth keeping.
constexpr double kGreyChroma = 0.04;
// How far each button turns toward the theme's accent.
constexpr double kAccentTurn = 8.0;

struct Lch {
    double L = 0, C = 0, h = 0;
};

double wrap(double h) {
    h = std::fmod(h, 360.0);
    return h < 0 ? h + 360.0 : h;
}

Lch lch_of(Rgb c) {
    const OkLch o = to_oklch(to_oklab(c));
    return {o.L, o.C, o.h};
}

bool in_gamut(const Lch& c) {
    const Linear l = oklab_to_linear(from_oklch({c.L, c.C, c.h}));
    auto ok = [](double v) { return v >= -kGamutEps && v <= 1.0 + kGamutEps; };
    return ok(l.r) && ok(l.g) && ok(l.b);
}

// The colour at L and h with as much of C as sRGB can show.
Rgb fit(const Lch& c) {
    if (in_gamut(c))
        return from_oklab(from_oklch({c.L, c.C, c.h}));
    double lo = 0, hi = c.C;
    for (int i = 0; i < 24; ++i) {
        const double mid = (lo + hi) * 0.5;
        (in_gamut({c.L, mid, c.h}) ? lo : hi) = mid;
    }
    return from_oklab(from_oklch({c.L, lo, c.h}));
}

// `h` kept within kHueWindow of `anchor`.
double near_anchor(double h, double anchor) {
    return wrap(anchor + std::clamp(hue_delta(anchor, h), -kHueWindow, kHueWindow));
}

bool warm(const Lch& c) {
    return c.C >= kGreyChroma && c.h >= 45.0 && c.h <= 115.0;
}

// Walks L away from the bar until the fill reaches the contrast it needs: first toward whichever
// of black and white stands out more on the bar, then the other way if that runs out.
Rgb settle(Lch c, Rgb bar) {
    const bool up_first = best_on(bar) == 0xffffff;
    Rgb best = fit(c);
    double best_contrast = contrast(best, bar);
    for (const bool up : {up_first, !up_first}) {
        Lch t = c;
        for (int i = 0; i < 90; ++i) {
            const Rgb f = fit(t);
            const double k = contrast(f, bar);
            if (k >= kMinContrast + 0.05)
                return f;
            if (k > best_contrast) {
                best = f;
                best_contrast = k;
            }
            t.L += up ? 0.01 : -0.01;
            if (t.L > 0.98 || t.L < 0.12)
                break;
        }
    }
    return best;  // the bar leaves no room either way: the most that could be had
}

// Dark ink like the usual round buttons whenever it reads, so the three glyphs match; white ink
// only on a fill too dark for it.
Rgb glyph_on(Rgb fill) {
    const Rgb ink = contrast(fill, 0x000000) >= kGlyphContrast + 0.6 ? 0x000000 : 0xffffff;
    for (double t = 0.55; t < 1.0; t += 0.05) {
        const Rgb g = mix(fill, ink, t);
        if (contrast(g, fill) >= kGlyphContrast + 0.2)
            return g;
    }
    return ink;
}

double apart(Rgb a, Rgb b) {
    return delta_e2000(a, b);
}

bool all_apart(const std::array<Rgb, 3>& f) {
    return apart(f[0], f[1]) >= kMinApart && apart(f[1], f[2]) >= kMinApart && apart(f[0], f[2]) >= kMinApart;
}
}  // namespace

Colors derive(const Input& in) {
    const Lch close = lch_of(in.blocked);
    const Lch zoom = lch_of(in.done);
    const Lch work = lch_of(in.working);
    // Minimize: the theme's working colour when it is warm, else halfway between close and zoom.
    const Lch minimize =
        warm(work) ? work : Lch{(close.L + zoom.L) * 0.5, (close.C + zoom.C) * 0.5, kAnchorHue[1]};
    std::array<Lch, 3> picks = {close, minimize, zoom};
    const Lch accent = lch_of(in.accent);
    for (std::size_t i = 0; i < 3; ++i) {
        Lch& p = picks[i];
        if (p.C < kGreyChroma)
            p.h = kAnchorHue[i];
        if (accent.C >= kGreyChroma)
            p.h += std::clamp(hue_delta(p.h, accent.h), -kAccentTurn, kAccentTurn);
        p.h = near_anchor(p.h, kAnchorHue[i]);
        p.C = std::clamp(p.C, kMinChroma, kMaxChroma);
        p.L = std::clamp(p.L, 0.45, 0.85);
    }

    Colors out;
    auto settle_all = [&] {
        for (std::size_t i = 0; i < 3; ++i)
            out.fill[i] = settle(picks[i], in.bar);
    };
    settle_all();
    // Too alike (a theme whose status colours sit close together): fall back to the anchor hues,
    // then also spread the lightness a little.
    if (!all_apart(out.fill)) {
        for (std::size_t i = 0; i < 3; ++i) {
            picks[i].h = kAnchorHue[i];
            picks[i].C = std::max(picks[i].C, 0.13);
        }
        settle_all();
    }
    if (!all_apart(out.fill)) {
        picks[0].L -= 0.05;
        picks[2].L += 0.05;
        settle_all();
    }
    for (std::size_t i = 0; i < 3; ++i)
        out.glyph[i] = glyph_on(out.fill[i]);
    out.muted = contrast(in.border, in.bar) >= 1.3 ? in.border : mix(in.bar, in.text, 0.25);
    return out;
}

}  // namespace agentdesk::ui::window_buttons
