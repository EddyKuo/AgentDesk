// Colour arithmetic for the theme features; see color_math.hpp.
#include "agentdesk/ui/color_math.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace agentdesk::ui::color {

namespace {
constexpr double kPi = std::numbers::pi;

double deg(double rad) {
    return rad * 180.0 / kPi;
}
double rad(double d) {
    return d * kPi / 180.0;
}
double wrap360(double h) {
    h = std::fmod(h, 360.0);
    return h < 0 ? h + 360.0 : h;
}
int to8(double v) {
    return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
}
}  // namespace

Rgb rgb(int r, int g, int b) {
    return (static_cast<Rgb>(std::clamp(r, 0, 255)) << 16) | (static_cast<Rgb>(std::clamp(g, 0, 255)) << 8) |
           static_cast<Rgb>(std::clamp(b, 0, 255));
}
int red(Rgb c) {
    return static_cast<int>((c >> 16) & 255);
}
int green(Rgb c) {
    return static_cast<int>((c >> 8) & 255);
}
int blue(Rgb c) {
    return static_cast<int>(c & 255);
}

double srgb_to_linear(double v) {
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}
double linear_to_srgb(double v) {
    return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

Linear to_linear(Rgb c) {
    return {srgb_to_linear(red(c) / 255.0), srgb_to_linear(green(c) / 255.0),
            srgb_to_linear(blue(c) / 255.0)};
}

Rgb from_linear(const Linear& l) {
    auto ch = [](double v) { return to8(linear_to_srgb(std::clamp(v, 0.0, 1.0))); };
    return rgb(ch(l.r), ch(l.g), ch(l.b));
}

double relative_luminance(Rgb c) {
    const Linear l = to_linear(c);
    return 0.2126 * l.r + 0.7152 * l.g + 0.0722 * l.b;
}

double contrast(Rgb a, Rgb b) {
    const double x = relative_luminance(a), y = relative_luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

Rgb mix(Rgb a, Rgb b, double t) {
    auto ch = [&](int x, int y) { return static_cast<int>(std::lround(x + (y - x) * t)); };
    return rgb(ch(red(a), red(b)), ch(green(a), green(b)), ch(blue(a), blue(b)));
}

Rgb over(Rgb fg, int alpha, Rgb bg) {
    return mix(bg, fg, std::clamp(alpha, 0, 255) / 255.0);
}

Rgb best_on(Rgb bg) {
    return contrast(bg, 0x000000) >= contrast(bg, 0xffffff) ? 0x000000 : 0xffffff;
}

// ------------------------------------------------------------------ OKLab (Bjorn Ottosson, 2020)
OkLab linear_to_oklab(const Linear& c) {
    const double l = std::cbrt(0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b);
    const double m = std::cbrt(0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b);
    const double s = std::cbrt(0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

Linear oklab_to_linear(const OkLab& o) {
    const double l_ = o.L + 0.3963377774 * o.a + 0.2158037573 * o.b;
    const double m_ = o.L - 0.1055613458 * o.a - 0.0638541728 * o.b;
    const double s_ = o.L - 0.0894841775 * o.a - 1.2914855480 * o.b;
    const double l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    return {4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
            -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
            -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s};
}

OkLab to_oklab(Rgb c) {
    return linear_to_oklab(to_linear(c));
}
Rgb from_oklab(const OkLab& o) {
    return from_linear(oklab_to_linear(o));
}

OkLch to_oklch(const OkLab& o) {
    return {o.L, std::hypot(o.a, o.b), wrap360(deg(std::atan2(o.b, o.a)))};
}
OkLab from_oklch(const OkLch& o) {
    return {o.L, o.C * std::cos(rad(o.h)), o.C * std::sin(rad(o.h))};
}
double hue_of(Rgb c) {
    return to_oklch(to_oklab(c)).h;
}

double hue_delta(double from, double to) {
    double d = wrap360(to - from);  // 0..360
    if (d > 180.0)
        d -= 360.0;
    return d;
}

OkLab shift_toward_hue(const OkLab& c, double hue_deg, double amount) {
    return {c.L, c.a + amount * std::cos(rad(hue_deg)), c.b + amount * std::sin(rad(hue_deg))};
}

OkLab turn_hue_toward(const OkLab& c, double target_deg, double max_deg) {
    OkLch l = to_oklch(c);
    const double d = hue_delta(l.h, target_deg);
    l.h = wrap360(l.h + std::clamp(d, -max_deg, max_deg));
    return from_oklch(l);
}

// ------------------------------------------------------------------ CIELAB and CIEDE2000
Lab to_lab(Rgb c) {
    const Linear l = to_linear(c);
    const double x = (0.4124564 * l.r + 0.3575761 * l.g + 0.1804375 * l.b) / 0.95047;
    const double y = (0.2126729 * l.r + 0.7151522 * l.g + 0.0721750 * l.b) / 1.0;
    const double z = (0.0193339 * l.r + 0.1191920 * l.g + 0.9503041 * l.b) / 1.08883;
    auto f = [](double t) {
        constexpr double d = 6.0 / 29.0;
        return t > d * d * d ? std::cbrt(t) : t / (3 * d * d) + 4.0 / 29.0;
    };
    return {116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))};
}

// Sharma, Wu and Dalal (2005), with kL = kC = kH = 1.
double delta_e2000(const Lab& x, const Lab& y) {
    const double c1 = std::hypot(x.a, x.b), c2 = std::hypot(y.a, y.b);
    const double cbar = (c1 + c2) / 2;
    const double c7 = std::pow(cbar, 7);
    const double g = 0.5 * (1 - std::sqrt(c7 / (c7 + std::pow(25.0, 7))));
    const double a1 = (1 + g) * x.a, a2 = (1 + g) * y.a;
    const double cp1 = std::hypot(a1, x.b), cp2 = std::hypot(a2, y.b);
    auto hue = [](double b, double a) { return (a == 0 && b == 0) ? 0.0 : wrap360(deg(std::atan2(b, a))); };
    const double h1 = hue(x.b, a1), h2 = hue(y.b, a2);
    const double dL = y.L - x.L;
    const double dC = cp2 - cp1;
    double dh = 0;
    if (cp1 * cp2 != 0) {
        dh = h2 - h1;
        if (dh > 180)
            dh -= 360;
        else if (dh < -180)
            dh += 360;
    }
    const double dH = 2 * std::sqrt(cp1 * cp2) * std::sin(rad(dh / 2));
    const double Lbar = (x.L + y.L) / 2;
    const double Cbar = (cp1 + cp2) / 2;
    double hbar = h1 + h2;
    if (cp1 * cp2 != 0) {
        if (std::abs(h1 - h2) <= 180)
            hbar = (h1 + h2) / 2;
        else
            hbar = (h1 + h2 < 360) ? (h1 + h2 + 360) / 2 : (h1 + h2 - 360) / 2;
    }
    const double t = 1 - 0.17 * std::cos(rad(hbar - 30)) + 0.24 * std::cos(rad(2 * hbar)) +
                     0.32 * std::cos(rad(3 * hbar + 6)) - 0.20 * std::cos(rad(4 * hbar - 63));
    const double dtheta = 30 * std::exp(-std::pow((hbar - 275) / 25, 2));
    const double c7b = std::pow(Cbar, 7);
    const double rc = 2 * std::sqrt(c7b / (c7b + std::pow(25.0, 7)));
    const double l50 = (Lbar - 50) * (Lbar - 50);
    const double sl = 1 + 0.015 * l50 / std::sqrt(20 + l50);
    const double sc = 1 + 0.045 * Cbar;
    const double sh = 1 + 0.015 * Cbar * t;
    const double rt = -std::sin(rad(2 * dtheta)) * rc;
    const double tl = dL / sl, tc = dC / sc, th = dH / sh;
    return std::sqrt(tl * tl + tc * tc + th * th + rt * tc * th);
}

double delta_e2000(Rgb x, Rgb y) {
    return delta_e2000(to_lab(x), to_lab(y));
}

// ------------------------------------------------------------------ colour-vision deficiency
Rgb simulate(Rgb c, Cvd kind) {
    using M = std::array<double, 9>;
    static constexpr M kProtan = {0.152286, 1.052583,  -0.204868, 0.114503, 0.786281,
                                  0.099216, -0.003882, -0.048116, 1.051998};
    static constexpr M kDeutan = {0.367322, 0.860646,  -0.227968, 0.280085, 0.672501,
                                  0.047413, -0.011820, 0.042940,  0.968881};
    static constexpr M kTritan = {1.255528, -0.076749, -0.178779, -0.078411, 0.930809,
                                  0.147602, 0.004733,  0.691367,  0.303900};
    const M* m = nullptr;
    switch (kind) {
        case Cvd::kNone:
            return c;
        case Cvd::kProtan:
            m = &kProtan;
            break;
        case Cvd::kDeutan:
            m = &kDeutan;
            break;
        case Cvd::kTritan:
            m = &kTritan;
            break;
    }
    const Linear l = to_linear(c);
    const M& k = *m;
    return from_linear({k[0] * l.r + k[1] * l.g + k[2] * l.b, k[3] * l.r + k[4] * l.g + k[5] * l.b,
                        k[6] * l.r + k[7] * l.g + k[8] * l.b});
}

const char* cvd_name(Cvd kind) {
    switch (kind) {
        case Cvd::kProtan:
            return "protanopia";
        case Cvd::kDeutan:
            return "deuteranopia";
        case Cvd::kTritan:
            return "tritanopia";
        case Cvd::kNone:
            break;
    }
    return "normal vision";
}

// ------------------------------------------------------------------ palette check
std::vector<PaletteCheck> check_palette(const PaletteColors& p) {
    std::vector<PaletteCheck> out;
    auto add = [&](const std::string& what, CheckKind kind, double value, double threshold) {
        out.push_back({what, kind, value, threshold});
    };
    const std::pair<const char*, Rgb> backs[] = {
        {"canvas", p.canvas}, {"chrome", p.chrome}, {"surface", p.surface}};
    for (const auto& [fname, fg] :
         {std::pair<const char*, Rgb>{"foreground", p.foreground}, {"muted", p.muted}, {"dim", p.dim}})
        for (const auto& [bname, bg] : backs)
            add(std::string(fname) + " on " + bname, CheckKind::kText, contrast(fg, bg), kTextAA);
    add("foreground on selected", CheckKind::kText, contrast(p.foreground, p.selected), kTextAA);
    add("terminal text on terminal background", CheckKind::kText, contrast(p.term_fg, p.term_bg), kTextAA);
    for (const auto& [bname, bg] : backs)
        add(std::string("accent on ") + bname, CheckKind::kGraphic, contrast(p.accent, bg), kGraphicAA);
    const std::pair<const char*, Rgb> status[] = {
        {"blocked", p.blocked}, {"working", p.working}, {"done", p.done}, {"idle", p.idle}};
    for (const auto& [sname, sc] : status)
        for (const auto& [bname, bg] :
             {std::pair<const char*, Rgb>{"chrome", p.chrome}, {"surface", p.surface}})
            add(std::string(sname) + " on " + bname, CheckKind::kGraphic, contrast(sc, bg), kGraphicAA);
    for (const Cvd v : {Cvd::kNone, Cvd::kProtan, Cvd::kDeutan, Cvd::kTritan})
        for (std::size_t i = 0; i < 3; ++i)
            for (std::size_t j = i + 1; j < 3; ++j)
                add(std::string(status[i].first) + "/" + status[j].first + " (" + cvd_name(v) + ")",
                    CheckKind::kApart,
                    delta_e2000(simulate(status[i].second, v), simulate(status[j].second, v)), kApartMin);
    return out;
}

std::vector<PaletteCheck> palette_problems(const PaletteColors& p) {
    std::vector<PaletteCheck> out;
    for (auto& c : check_palette(p))
        if (!c.pass())
            out.push_back(std::move(c));
    return out;
}

// ------------------------------------------------------------------ ambient tint
TintOutput tint(const TintInput& in, Rgb toward, double k) {
    const double hue = hue_of(toward);
    auto shift = [&](Rgb c) { return from_oklab(shift_toward_hue(to_oklab(c), hue, kTintChroma * k)); };
    TintOutput o;
    o.k = k;
    o.canvas = shift(in.base.canvas);
    o.chrome = shift(in.base.chrome);
    o.surface = shift(in.base.surface);
    o.hover = in.hover_opaque ? shift(in.hover) : in.hover;
    o.selected = in.selected_opaque ? shift(in.selected) : in.selected;
    o.accent = from_oklab(turn_hue_toward(to_oklab(in.base.accent), hue, kTintTurnDeg * k));
    return o;
}

namespace {
// The tinted palette as the check sees it. A translucent selected keeps its overlay over the
// tinted chrome.
PaletteColors tinted_colors(const TintInput& in, const TintOutput& t) {
    PaletteColors p = in.base;
    p.canvas = t.canvas;
    p.chrome = t.chrome;
    p.surface = t.surface;
    p.accent = t.accent;
    if (in.selected_opaque)
        p.selected = t.selected;
    return p;
}

bool keeps_pairs(const std::vector<PaletteCheck>& before, const std::vector<PaletteCheck>& after) {
    for (std::size_t i = 0; i < before.size() && i < after.size(); ++i) {
        if (before[i].kind == CheckKind::kApart)
            continue;  // status colours do not move
        // Rounding to 8 bits can move a ratio by a hair; 0.005 keeps that from counting.
        if (after[i].value + 0.005 < std::min(before[i].value, before[i].threshold))
            return false;
    }
    return true;
}
}  // namespace

TintOutput tint_protected(const TintInput& in, Rgb toward, double k) {
    const auto before = check_palette(in.base);
    auto ok = [&](double kk) {
        return keeps_pairs(before, check_palette(tinted_colors(in, tint(in, toward, kk))));
    };
    if (k <= 0 || ok(k))
        return tint(in, toward, std::max(0.0, k));
    double lo = 0, hi = k;  // lo always passes (k = 0 is the palette as it was)
    for (int i = 0; i < 6; ++i) {
        const double mid = (lo + hi) / 2;
        (ok(mid) ? lo : hi) = mid;
    }
    return tint(in, toward, lo);
}

}  // namespace agentdesk::ui::color
