// The Classic layout's rules; see classic_layout.hpp.
#include "agentdesk/ui/classic_layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "agentdesk/ui/input_mapping.hpp"

namespace agentdesk::ui::classic {

namespace {
// The next code point of `s` from byte `i` (moves `i` past it) and its byte length.
char32_t next_cp(std::string_view s, std::size_t& i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    const std::size_t len = b0 < 0x80 ? 1 : (b0 & 0xE0) == 0xC0 ? 2 : (b0 & 0xF0) == 0xE0 ? 3 : 4;
    char32_t cp = len == 1 ? b0 : len == 2 ? (b0 & 0x1Fu) : len == 3 ? (b0 & 0x0Fu) : (b0 & 0x07u);
    for (std::size_t m = 1; m < len && i + m < s.size(); ++m)
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + m]) & 0x3Fu);
    i = std::min(s.size(), i + len);
    return cp;
}

int cp_cols(char32_t cp) {
    return is_wide_codepoint(cp) ? 2 : 1;
}

// sRGB channel (0..255) to linear light, through a table.
const std::array<float, 256>& to_linear_table() {
    static const std::array<float, 256> t = [] {
        std::array<float, 256> a{};
        for (int i = 0; i < 256; ++i) {
            const float c = static_cast<float>(i) / 255.0f;
            a[static_cast<std::size_t>(i)] =
                c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return a;
    }();
    return t;
}

float lin(ImU32 c, int shift) {
    return to_linear_table()[(c >> shift) & 0xFF];
}

int to_srgb(float l) {
    l = std::clamp(l, 0.0f, 1.0f);
    const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
    return std::clamp(static_cast<int>(std::lround(c * 255.0f)), 0, 255);
}

// a..b at t, mixed in linear light; the ends come back exactly.
ImU32 mix_linear(ImU32 a, ImU32 b, float t) {
    if (t <= 0.0f)
        return a;
    if (t >= 1.0f)
        return b;
    auto ch = [&](int shift) { return to_srgb(lin(a, shift) + (lin(b, shift) - lin(a, shift)) * t); };
    const int alpha = static_cast<int>((a >> IM_COL32_A_SHIFT) & 0xFF);
    return IM_COL32(ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT), ch(IM_COL32_B_SHIFT), alpha);
}
}  // namespace

int Grid::cols(float width) const {
    return cell_w > 0 ? std::max(0, static_cast<int>(std::floor(width / cell_w + 0.001f))) : 0;
}

float Grid::remainder(float width) const {
    return std::max(0.0f, width - static_cast<float>(cols(width)) * cell_w);
}

Grid grid_for(float advance, float scale) {
    Grid g;
    g.cell_w = std::max(1.0f, std::round(advance));
    g.cell_h = std::max(1.0f, std::round(20.0f * scale));
    return g;
}

int text_cols(std::string_view utf8) {
    int n = 0;
    for (std::size_t i = 0; i < utf8.size();)
        n += cp_cols(next_cp(utf8, i));
    return n;
}

std::string cut_cols(std::string_view utf8, int cols) {
    if (cols <= 0)
        return {};
    if (text_cols(utf8) <= cols)
        return std::string(utf8);
    std::string out;
    int used = 0;
    for (std::size_t i = 0; i < utf8.size();) {
        const std::size_t start = i;
        const int w = cp_cols(next_cp(utf8, i));
        if (used + w > cols - 1)  // keep a cell for the ellipsis
            break;
        out.append(utf8.substr(start, i - start));
        used += w;
    }
    return out + "\xE2\x80\xA6";
}

const char* status_glyph(Attention a) {
    switch (a) {
        case Attention::kBlocked:
            return "[!]";
        case Attention::kWorking:
            return "[~]";
        case Attention::kDone:
            return "[+]";
        case Attention::kIdle:
            return "[.]";
        case Attention::kNone:
            break;
    }
    return "[?]";
}

char spinner_frame(long long ms) {
    static constexpr char kFrames[4] = {'|', '/', '-', '\\'};
    return kFrames[static_cast<std::size_t>((std::max(0LL, ms) / 125) % 4)];
}

std::vector<std::string> fit_tabs(const std::vector<std::string>& labels, int active, int cols, int sep) {
    std::vector<std::string> out = labels;
    auto total = [&] {
        int n = 0;
        for (const auto& l : out)
            n += text_cols(l);
        return n + sep * std::max(0, static_cast<int>(out.size()) - 1);
    };
    for (std::size_t i = 0; i < out.size() && total() > cols; ++i) {
        if (static_cast<int>(i) == active)
            continue;
        const auto space = labels[i].find(' ');
        out[i] = labels[i].substr(0, space) + "\xE2\x80\xA6";
    }
    return out;
}

MonoRamp mono_ramp(ImU32 background, ImU32 foreground) {
    return {background, mix_linear(foreground, IM_COL32(255, 255, 255, 255), 0.15f)};
}

float luminance(ImU32 c) {
    return 0.2126f * lin(c, IM_COL32_R_SHIFT) + 0.7152f * lin(c, IM_COL32_G_SHIFT) +
           0.0722f * lin(c, IM_COL32_B_SHIFT);
}

float contrast(ImU32 a, ImU32 b) {
    const float la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

ImU32 mono_color(ImU32 c, bool foreground, const MonoRamp& r) {
    // On a light theme the ramp runs from paper to ink: darkness, not lightness, moves along it.
    const bool paper = luminance(r.dark) > luminance(r.bright);
    const float l = paper ? 1.0f - luminance(c) : luminance(c);
    return mix_linear(r.dark, r.bright, foreground ? 0.35f + 0.65f * l : 0.45f * l);
}

CellPair mono_cell(ImU32 fg, ImU32 bg, bool fg_default, bool bg_default, bool reverse, const MonoRamp& r) {
    CellPair p{fg_default ? fg : mono_color(fg, true, r), bg_default ? bg : mono_color(bg, false, r)};
    const float lb = luminance(p.bg);
    if (std::abs(luminance(p.fg) - lb) < 0.35f)  // too close: the ramp's end farther from the background
        p.fg = std::abs(luminance(r.dark) - lb) > std::abs(luminance(r.bright) - lb) ? r.dark : r.bright;
    if (reverse)
        std::swap(p.fg, p.bg);
    return p;
}

std::vector<std::pair<float, float>> scanlines(float top, float bottom, float scale) {
    std::vector<std::pair<float, float>> out;
    const float step = 3.0f * scale;
    const float h = std::max(1.0f, std::round(scale));
    if (step <= 0.0f)
        return out;
    for (float y = top; y < bottom; y += step) {
        const float a = std::round(y);
        if (a + h > bottom)
            break;
        out.emplace_back(a, h);
    }
    return out;
}

BarFit fit_bar(int cols, int left_full, int left_short, int counts_cols, int clock_cols) {
    BarFit f;
    f.all_menus = cols >= 80;
    const int left = f.all_menus ? left_full : left_short;
    if (left + counts_cols + clock_cols > cols)
        f.clock = false;
    if (left + counts_cols > cols)
        f.counts = false;
    return f;
}

}  // namespace agentdesk::ui::classic
