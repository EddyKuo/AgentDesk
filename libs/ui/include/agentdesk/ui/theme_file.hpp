#pragma once
// Theme files (themes\<name>.json): reading and writing them, and deriving the interface colours
// from a theme's background, foreground and ANSI table plus its "colors" overrides. Colours are
// 0xRRGGBB with a separate alpha; nothing here touches ImGui. Also the suggested theme pairs of
// the shipped layouts and the rules for a theme's file name.
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentdesk/ui/color_math.hpp"

namespace agentdesk::ui::theme {

using color::Rgb;

// A colour with its alpha (0..255).
struct Token {
    Rgb rgb = 0;
    int alpha = 255;
    bool operator==(const Token&) const = default;
};

struct ThemeFile {
    Rgb background = 0, foreground = 0, selection = 0;
    std::optional<Rgb> cursor;
    std::array<Rgb, 16> ansi{};
    std::optional<std::array<Rgb, 3>> status;       // working, blocked, done (the preset themes only)
    std::map<std::string, std::string> colors;      // "colors": overrides by token name, as written
    nlohmann::json raw = nlohmann::json::object();  // the whole file: unknown keys survive a save
};

// "#rrggbb" or "#rrggbbaa".
std::optional<Token> parse_hex(const std::string& h);
std::string hex(Rgb c);           // "#rrggbb"
std::string hex(const Token& t);  // "#rrggbb", or "#rrggbbaa" when not opaque

// {background, foreground, selection, cursor?, ansi[16], colors{}}; background, foreground,
// selection, cursor and ansi as "#rrggbb". nullopt (and why, in `error`) for anything else.
std::optional<ThemeFile> parse(const std::string& text, std::string* error = nullptr);
// The file as JSON text: `raw` with the known keys set from the fields; "colors" holds exactly
// `colors` (an empty one is left out).
std::string write(const ThemeFile& t);

// The token names a "colors" object may set, in the order an editor lists them.
const std::vector<std::string>& token_names();

struct Derived {
    bool light = false;
    std::map<std::string, Token> tokens;  // every name in token_names()
};
// The colours worked out from background, foreground and ANSI alone (no overrides).
Derived derive_base(const ThemeFile& t);
// derive_base() with the file's "colors" applied (values that do not parse are skipped).
Derived derive(const ThemeFile& t);
// The colours check_palette() looks at (hover / selected composited onto the chrome).
color::PaletteColors check_colors(const Derived& d);

// libvterm's 16 ANSI colours: what a terminal shows when a theme has no "ansi" list.
const std::array<Rgb, 16>& default_ansi();

// What is wrong with `name` as a theme file name ("" when it is fine): empty, too long (64),
// characters Windows does not allow, a reserved device name, the id of a built-in theme, or an
// ending that older settings read as a preset ("-graphite", "-ink", "-moss", "-paper").
std::string name_problem(const std::string& name, const std::vector<std::string>& builtin_ids);

// The theme pair (dark, light) a shipped layout was designed with, else nullopt.
std::optional<std::pair<std::string, std::string>> builtin_suggestion(const std::string& layout);

}  // namespace agentdesk::ui::theme
