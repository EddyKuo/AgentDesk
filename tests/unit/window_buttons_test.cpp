// The round window buttons' theme colours (libs/ui window_buttons): readable on the bar, three
// clearly different colours in every shipped theme, each near its usual red / amber / green.
#include "agentdesk/ui/window_buttons.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "agentdesk/ui/theme_file.hpp"

namespace wb = agentdesk::ui::window_buttons;
namespace th = agentdesk::ui::theme;
namespace c = agentdesk::ui::color;

namespace {
std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct Named {
    std::string name;
    wb::Input in;
};

// From a theme's derived colours; the buttons sit on the chrome.
wb::Input input_of(const th::Derived& d) {
    auto rgb = [&](const char* k) { return d.tokens.at(k).rgb; };
    return {rgb("blocked"), rgb("working"), rgb("done"),      rgb("accent"),
            rgb("chrome"),  rgb("border"),  rgb("foreground")};
}

// AgentDesk's own Midnight and Paper (apps/desktop themes.cpp), the graphite preset, and the
// shipped theme files the layouts suggest.
std::vector<Named> themes() {
    std::vector<Named> out;
    out.push_back({"midnight", {0xf0647a, 0xe9b949, 0x5ccb9a, 0x8b7cf6, 0x101521, 0x242c40, 0xe2e6f2}});
    out.push_back({"paper", {0xcc3448, 0xbe820a, 0x1e965a, 0x585cdc, 0xe2e2dc, 0xccccc4, 0x1e2028}});
    th::ThemeFile graphite;
    graphite.background = 0x17191d;
    graphite.foreground = 0xd2d4d9;
    graphite.selection = 0x394454;
    graphite.ansi = {0x45484f, 0xdc8c95, 0xa6bd91, 0xd8be87, 0x96b2d7, 0xb5a2ce, 0x8cbdc3, 0xd2d4d9,
                     0x818793, 0xf0a5ad, 0xbad5a2, 0xead2a1, 0xb0cbef, 0xcbb7e6, 0xa4d5db, 0xf0f0f2};
    graphite.status = std::array<th::Rgb, 3>{0xe5c07b, 0xf0705f, 0x76a8ff};
    out.push_back({"graphite", input_of(th::derive(graphite))});
    for (const char* name : {"mac-dusk", "mac-dawn", "cockpit-night", "cockpit-day", "phosphor-green",
                             "phosphor-amber", "phosphor-paper"}) {
        const auto t = th::parse(read_file(std::string(AGENTDESK_OWN_THEMES_DIR) + "/" + name + ".json"));
        EXPECT_TRUE(t) << name;
        if (t)
            out.push_back({name, input_of(th::derive(*t))});
    }
    return out;
}

double angle_to(double hue, double anchor) {
    const double d = c::hue_delta(anchor, hue);
    return d < 0 ? -d : d;
}
}  // namespace

// Each fill stands out from the bar at 3:1, as a graphic must.
TEST(WindowButtons, EveryFillReachesThreeToOneOnTheBar) {
    for (const auto& [name, in] : themes()) {
        const wb::Colors col = wb::derive(in);
        for (int i = 0; i < 3; ++i)
            EXPECT_GE(c::contrast(col.fill[static_cast<std::size_t>(i)], in.bar), wb::kMinContrast)
                << name << " button " << i;
    }
}

// Close, minimize and zoom can be told apart: every pair is a clear colour difference.
TEST(WindowButtons, TheThreeAreClearlyApart) {
    for (const auto& [name, in] : themes()) {
        const wb::Colors col = wb::derive(in);
        EXPECT_GE(c::delta_e2000(col.fill[0], col.fill[1]), wb::kMinApart) << name << " close/minimize";
        EXPECT_GE(c::delta_e2000(col.fill[1], col.fill[2]), wb::kMinApart) << name << " minimize/zoom";
        EXPECT_GE(c::delta_e2000(col.fill[0], col.fill[2]), wb::kMinApart) << name << " close/zoom";
    }
}

// Each keeps its role: its hue is nearest its own anchor (red, amber, green), within the window.
TEST(WindowButtons, EachStaysNearItsUsualHue) {
    for (const auto& [name, in] : themes()) {
        const wb::Colors col = wb::derive(in);
        for (std::size_t i = 0; i < 3; ++i) {
            const double h = c::hue_of(col.fill[i]);
            EXPECT_LE(angle_to(h, wb::kAnchorHue[i]), wb::kHueWindow + 2.0) << name << " button " << i;
            for (std::size_t j = 0; j < 3; ++j)
                if (j != i)
                    EXPECT_LT(angle_to(h, wb::kAnchorHue[i]), angle_to(h, wb::kAnchorHue[j]))
                        << name << " button " << i << " is nearer anchor " << j;
        }
    }
}

// The cross, bar and plus read on their fills.
TEST(WindowButtons, GlyphsReadOnTheirFills) {
    for (const auto& [name, in] : themes()) {
        const wb::Colors col = wb::derive(in);
        for (std::size_t i = 0; i < 3; ++i)
            EXPECT_GE(c::contrast(col.glyph[i], col.fill[i]), wb::kGlyphContrast) << name << " button " << i;
    }
}

// The buttons follow the theme: two themes with different status colours give different buttons,
// and a theme whose close colour is already a fine red keeps (close to) its own hue.
TEST(WindowButtons, FollowTheTheme) {
    const auto all = themes();
    const wb::Colors midnight = wb::derive(all[0].in);
    const wb::Colors paper = wb::derive(all[1].in);
    EXPECT_NE(midnight.fill[0], paper.fill[0]);
    EXPECT_NE(midnight.fill[2], paper.fill[2]);
    for (const auto& [name, in] : all) {
        if (name != "cockpit-night" && name != "midnight")
            continue;
        const wb::Colors col = wb::derive(in);
        EXPECT_LE(angle_to(c::hue_of(col.fill[0]), c::hue_of(in.blocked)), 12.0) << name;
        EXPECT_LE(angle_to(c::hue_of(col.fill[2]), c::hue_of(in.done)), 12.0) << name;
    }
    // Same input, same output.
    EXPECT_EQ(wb::derive(all[0].in).fill, midnight.fill);
}

// A monochrome theme (all status colours green) still gets three distinct buttons, and a light
// theme gets fills darker than its bar.
TEST(WindowButtons, MonochromeAndLightThemes) {
    for (const auto& [name, in] : themes()) {
        const wb::Colors col = wb::derive(in);
        if (name == "paper" || name == "mac-dawn" || name == "phosphor-paper" || name == "cockpit-day")
            for (const auto f : col.fill)
                EXPECT_LT(c::relative_luminance(f), c::relative_luminance(in.bar)) << name;
    }
    const wb::Input green{0xe6ffec, 0x7cf29a, 0xa8ffc0, 0xb8ffc8, 0x06100a, 0x1e5a2c, 0x8cffa8};
    const wb::Colors col = wb::derive(green);
    EXPECT_GE(c::delta_e2000(col.fill[0], col.fill[2]), wb::kMinApart);
    EXPECT_NE(col.muted, col.fill[0]);
}
