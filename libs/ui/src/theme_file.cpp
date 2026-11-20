// Theme files and the colours derived from them; see theme_file.hpp.
#include "agentdesk/ui/theme_file.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace agentdesk::ui::theme {

namespace {
using Json = nlohmann::json;

// a toward b by t per channel, in float and rounded half away from zero: the arithmetic the
// interface has always used, so derived colours stay what they were.
Rgb mixf(Rgb a, Rgb b, float t) {
    Rgb out = 0;
    for (const int sh : {16, 8, 0}) {
        const float x = static_cast<float>((a >> sh) & 255), y = static_cast<float>((b >> sh) & 255);
        out |= static_cast<Rgb>(std::lround(x * (1.0f - t) + y * t)) << sh;
    }
    return out;
}

// Rec. 709 weights on the stored values, above one half: which side a theme counts as.
bool is_light(Rgb c) {
    auto ch = [&](int sh) { return static_cast<float>((c >> sh) & 255) / 255.0f; };
    return 0.2126f * ch(16) + 0.7152f * ch(8) + 0.0722f * ch(0) > 0.5f;
}

std::optional<Rgb> hex6(const Json& v) {
    if (!v.is_string())
        return std::nullopt;
    const std::string h = v.get<std::string>();
    if (h.size() != 7 || h[0] != '#')
        return std::nullopt;
    const auto t = parse_hex(h);
    return t ? std::optional<Rgb>(t->rgb) : std::nullopt;
}
}  // namespace

std::optional<Token> parse_hex(const std::string& h) {
    if ((h.size() != 7 && h.size() != 9) || h[0] != '#')
        return std::nullopt;
    if (!std::all_of(h.begin() + 1, h.end(),
                     [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }))
        return std::nullopt;
    const unsigned long v = std::strtoul(h.c_str() + 1, nullptr, 16);
    if (h.size() == 7)
        return Token{static_cast<Rgb>(v), 255};
    return Token{static_cast<Rgb>(v >> 8), static_cast<int>(v & 255)};
}

std::string hex(Rgb c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%06x", static_cast<unsigned>(c & 0xffffff));
    return buf;
}

std::string hex(const Token& t) {
    if (t.alpha >= 255)
        return hex(t.rgb);
    char buf[12];
    std::snprintf(buf, sizeof buf, "#%06x%02x", static_cast<unsigned>(t.rgb & 0xffffff),
                  static_cast<unsigned>(std::clamp(t.alpha, 0, 255)));
    return buf;
}

std::optional<ThemeFile> parse(const std::string& text, std::string* error) {
    auto fail = [&](const char* why) -> std::optional<ThemeFile> {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    };
    const Json j = Json::parse(text, nullptr, false);
    if (!j.is_object())
        return fail("not a JSON object");
    ThemeFile t;
    t.raw = j;
    const auto b = hex6(j.value("background", Json())), f = hex6(j.value("foreground", Json())),
               sel = hex6(j.value("selection", Json()));
    if (!b || !f || !sel)
        return fail("background, foreground and selection must be \"#rrggbb\"");
    t.background = *b;
    t.foreground = *f;
    t.selection = *sel;
    if (j.contains("cursor"))
        t.cursor = hex6(j["cursor"]);
    if (!j.contains("ansi") || !j["ansi"].is_array() || j["ansi"].size() != 16)
        return fail("ansi must list 16 colours");
    for (std::size_t i = 0; i < 16; ++i) {
        const auto c = hex6(j["ansi"][i]);
        if (!c)
            return fail("ansi colours must be \"#rrggbb\"");
        t.ansi[i] = *c;
    }
    if (j.contains("colors") && j["colors"].is_object())
        for (const auto& [k, v] : j["colors"].items())
            if (v.is_string())
                t.colors[k] = v.get<std::string>();
    return t;
}

std::string write(const ThemeFile& t) {
    Json j = t.raw.is_object() ? t.raw : Json::object();
    j["background"] = hex(t.background);
    j["foreground"] = hex(t.foreground);
    j["selection"] = hex(t.selection);
    if (t.cursor)
        j["cursor"] = hex(*t.cursor);
    else
        j.erase("cursor");
    Json ansi = Json::array();
    for (const Rgb c : t.ansi)
        ansi.push_back(hex(c));
    j["ansi"] = ansi;
    // Keys of "colors" this version does not use stay, as written.
    Json colors = j.contains("colors") && j["colors"].is_object() ? j["colors"] : Json::object();
    for (const auto& name : token_names())
        colors.erase(name);
    for (const auto& [k, v] : t.colors)
        colors[k] = v;
    if (colors.empty())
        j.erase("colors");
    else
        j["colors"] = colors;
    return j.dump(2) + "\n";
}

const std::vector<std::string>& token_names() {
    static const std::vector<std::string> k = {"background", "terminal_foreground",
                                               "selection",  "cursor",
                                               "canvas",     "chrome",
                                               "surface",    "foreground",
                                               "muted",      "dim",
                                               "accent",     "info",
                                               "border",     "hover",
                                               "selected",   "blocked",
                                               "working",    "done",
                                               "idle"};
    return k;
}

// Mapping onto the interface: canvas is behind the panes, chrome the sidebar and bars, surface
// the terminal's card, foreground the interface text, muted and dim the quieter text, accent
// the focus colour (and info, unless the file sets its own).
Derived derive_base(const ThemeFile& h) {
    Derived d;
    const Rgb bg = h.background;
    d.light = is_light(bg);
    const Rgb fg = d.light ? 0x1b1b1e : 0xeeeeef;
    const auto st = h.status.value_or(std::array<Rgb, 3>{h.ansi[3], h.ansi[1], h.ansi[4]});
    auto& t = d.tokens;
    t["background"] = {bg};
    t["terminal_foreground"] = {h.foreground};
    t["foreground"] = {fg};
    t["surface"] = {bg};
    if (d.light) {
        const Rgb warm = 0xe8e8e2;
        t["hover"] = {0x000000, 0x0c};
        t["selected"] = {0x000000, 0x12};
        t["border"] = {0x000000, 0x17};
        t["canvas"] = {mixf(bg, warm, 0.2f)};
        t["chrome"] = {mixf(bg, warm, 0.6f)};
    } else {
        t["hover"] = {0xffffff, 0x0e};
        t["selected"] = {0xffffff, 0x17};
        t["border"] = {0xffffff, 0x18};
        t["canvas"] = {mixf(bg, 0, 0.16f)};
        t["chrome"] = {mixf(bg, 0, 0.32f)};
    }
    t["muted"] = {mixf(bg, fg, d.light ? 0.66f : 0.6f)};
    t["dim"] = {mixf(bg, fg, d.light ? 0.46f : 0.38f)};
    t["accent"] = {h.ansi[4]};
    t["info"] = {h.ansi[4]};
    t["selection"] = {h.selection};
    t["cursor"] = {h.cursor.value_or(h.foreground)};
    t["working"] = {st[0]};
    t["blocked"] = {st[1]};
    t["done"] = {st[2]};
    t["idle"] = {mixf(bg, fg, 0.38f)};
    return d;
}

Derived derive(const ThemeFile& h) {
    Derived d = derive_base(h);
    for (const auto& [k, v] : h.colors)
        if (const auto c = parse_hex(v); c && d.tokens.contains(k))
            d.tokens[k] = *c;
    if (!h.colors.contains("info") || !parse_hex(h.colors.at("info")))
        d.tokens["info"] = d.tokens["accent"];  // info follows the accent unless set itself
    return d;
}

color::PaletteColors check_colors(const Derived& d) {
    auto rgb = [&](const char* k) { return d.tokens.at(k).rgb; };
    auto flat = [&](const char* k, Rgb under) {
        const Token& t = d.tokens.at(k);
        return color::over(t.rgb, t.alpha, under);
    };
    color::PaletteColors p;
    p.canvas = flat("canvas", 0);
    p.chrome = flat("chrome", 0);
    p.surface = flat("surface", 0);
    p.selected = flat("selected", p.chrome);
    p.foreground = rgb("foreground");
    p.muted = rgb("muted");
    p.dim = rgb("dim");
    p.accent = rgb("accent");
    p.blocked = rgb("blocked");
    p.working = rgb("working");
    p.done = rgb("done");
    p.idle = rgb("idle");
    p.term_bg = flat("background", 0);
    p.term_fg = rgb("terminal_foreground");
    return p;
}

const std::array<Rgb, 16>& default_ansi() {
    static constexpr std::array<Rgb, 16> k = {0x000000, 0xe00000, 0x00e000, 0xe0e000, 0x0000e0, 0xe000e0,
                                              0x00e0e0, 0xe0e0e0, 0x808080, 0xff4040, 0x40ff40, 0xffff40,
                                              0x4040ff, 0xff40ff, 0x40ffff, 0xffffff};
    return k;
}

std::string name_problem(const std::string& name, const std::vector<std::string>& builtin_ids) {
    if (name.empty())
        return "Give the theme a name.";
    if (name.size() > 64)
        return "Names are at most 64 characters.";
    for (const char c : name)
        if (static_cast<unsigned char>(c) < 0x20 ||
            std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
            return "Names can't contain < > : \" / \\ | ? *";
    if (name.back() == '.' || name.back() == ' ' || name.front() == ' ')
        return "Names can't start with a space or end with a space or a dot.";
    std::string upper;
    for (const char c : name.substr(0, name.find('.')))
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    static const char* const kReserved[] = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                            "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                            "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const char* r : kReserved)
        if (upper == r)
            return "Windows keeps that name for itself.";
    std::string lower;
    for (const char c : name)
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (std::find(builtin_ids.begin(), builtin_ids.end(), lower) != builtin_ids.end() ||
        lower.starts_with("preset-"))
        return "A built-in theme already uses that name.";
    for (const char* end : {"-graphite", "-ink", "-moss", "-paper"})
        if (lower.ends_with(end))
            return std::string("Names can't end in \"") + end + "\" (older settings read that as a preset).";
    return {};
}

std::optional<std::pair<std::string, std::string>> builtin_suggestion(const std::string& layout) {
    if (layout == "workspace")
        return std::pair<std::string, std::string>{"midnight", "paper"};
    if (layout == "terminals")
        return std::pair<std::string, std::string>{"preset-graphite", "preset-paper"};
    if (layout == "mac")
        return std::pair<std::string, std::string>{"mac-dusk", "mac-dawn"};
    return std::nullopt;
}

}  // namespace agentdesk::ui::theme
