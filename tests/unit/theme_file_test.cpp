// Theme files (libs/ui theme_file): parsing and writing, the derived interface colours, name
// rules, the shipped layouts' suggested pairs, and the palette check of AgentDesk's theme files.
#include "agentdesk/ui/theme_file.hpp"

#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

namespace th = agentdesk::ui::theme;
namespace c = agentdesk::ui::color;

namespace {
std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// herdr's graphite preset as a theme file.
th::ThemeFile graphite() {
    th::ThemeFile t;
    t.background = 0x17191d;
    t.foreground = 0xd2d4d9;
    t.selection = 0x394454;
    t.ansi = {0x45484f, 0xdc8c95, 0xa6bd91, 0xd8be87, 0x96b2d7, 0xb5a2ce, 0x8cbdc3, 0xd2d4d9,
              0x818793, 0xf0a5ad, 0xbad5a2, 0xead2a1, 0xb0cbef, 0xcbb7e6, 0xa4d5db, 0xf0f0f2};
    t.status = std::array<th::Rgb, 3>{0xe5c07b, 0xf0705f, 0x76a8ff};
    return t;
}

const char* kMinimal = R"({
  "background": "#21252b", "foreground": "#abb2bf", "selection": "#323844", "cursor": "#abb2bf",
  "ansi": ["#454c59", "#e06c75", "#98c379", "#e5c07b", "#61afef", "#c678dd", "#56b6c2", "#abb2bf",
           "#767676", "#e06c75", "#98c379", "#e5c07b", "#61afef", "#c678dd", "#56b6c2", "#abb2bf"],
  "colors": {"accent": "#ff8800", "later": "#123456"},
  "author": "someone"
})";
}  // namespace

TEST(ThemeFile, HexParsing) {
    EXPECT_EQ(th::parse_hex("#0d111b"), (th::Token{0x0d111b, 255}));
    EXPECT_EQ(th::parse_hex("#ffffff18"), (th::Token{0xffffff, 0x18}));
    for (const char* bad : {"0d111b", "#0d111", "#0d111bx", "#gg0000", "", "#12345678x"})
        EXPECT_FALSE(th::parse_hex(bad)) << bad;
    EXPECT_EQ(th::hex(0x0d111b), "#0d111b");
    EXPECT_EQ(th::hex(th::Token{0xffffff, 0x18}), "#ffffff18");
    EXPECT_EQ(th::hex(th::Token{0xabcdef, 255}), "#abcdef");
}

TEST(ThemeFile, ParseRejectsWrongShapes) {
    std::string why;
    EXPECT_FALSE(th::parse("[]", &why));
    EXPECT_FALSE(th::parse(R"({"background": "#000000", "foreground": "#ffffff"})", &why));
    EXPECT_FALSE(why.empty());
    EXPECT_FALSE(th::parse(R"({"background": "#000000", "foreground": "#ffffff", "selection": "#333333",
                              "ansi": ["#000000"]})"));
    const auto t = th::parse(kMinimal);
    ASSERT_TRUE(t);
    EXPECT_EQ(t->background, 0x21252bu);
    EXPECT_EQ(t->cursor, 0xabb2bfu);
    EXPECT_EQ(t->colors.at("accent"), "#ff8800");
}

TEST(ThemeFile, WriteKeepsUnknownKeysAndOnlyOverrides) {
    auto t = th::parse(kMinimal);
    ASSERT_TRUE(t);
    t->colors.erase("accent");
    t->colors["done"] = "#00ff00";
    const std::string text = th::write(*t);
    const auto j = nlohmann::json::parse(text);
    EXPECT_EQ(j["author"], "someone");           // a key this version does not know
    EXPECT_EQ(j["colors"]["later"], "#123456");  // an unknown colour name stays too
    EXPECT_FALSE(j["colors"].contains("accent"));
    EXPECT_EQ(j["colors"]["done"], "#00ff00");
    const auto back = th::parse(text);
    ASSERT_TRUE(back);
    EXPECT_EQ(back->background, t->background);
    EXPECT_EQ(back->ansi, t->ansi);
    EXPECT_EQ(back->colors.at("done"), "#00ff00");
    // No overrides at all: no "colors" object.
    th::ThemeFile plain = graphite();
    plain.status.reset();
    EXPECT_FALSE(nlohmann::json::parse(th::write(plain)).contains("colors"));
}

// The derivation the interface has always used, pinned value by value.
TEST(ThemeFile, DeriveMatchesTheExistingPresetColours) {
    const th::Derived d = th::derive(graphite());
    EXPECT_FALSE(d.light);
    EXPECT_EQ(d.tokens.at("canvas"), (th::Token{0x131518}));
    EXPECT_EQ(d.tokens.at("chrome"), (th::Token{0x101114}));
    EXPECT_EQ(d.tokens.at("surface"), (th::Token{0x17191d}));
    EXPECT_EQ(d.tokens.at("muted"), (th::Token{0x98999b}));
    EXPECT_EQ(d.tokens.at("foreground"), (th::Token{0xeeeeef}));
    EXPECT_EQ(d.tokens.at("hover"), (th::Token{0xffffff, 0x0e}));
    EXPECT_EQ(d.tokens.at("selected"), (th::Token{0xffffff, 0x17}));
    EXPECT_EQ(d.tokens.at("working"), (th::Token{0xe5c07b}));
    EXPECT_EQ(d.tokens.at("blocked"), (th::Token{0xf0705f}));
    EXPECT_EQ(d.tokens.at("accent"), (th::Token{0x96b2d7}));
    EXPECT_EQ(d.tokens.at("info"), d.tokens.at("accent"));
    EXPECT_EQ(d.tokens.at("cursor"), (th::Token{0xd2d4d9}));  // no cursor: the foreground
    for (const auto& name : th::token_names())
        EXPECT_TRUE(d.tokens.contains(name)) << name;
}

TEST(ThemeFile, OverridesAndInfo) {
    auto t = graphite();
    t.colors["accent"] = "#ff8800";
    t.colors["hover"] = "#ffffff20";
    t.colors["muted"] = "not a colour";
    th::Derived d = th::derive(t);
    EXPECT_EQ(d.tokens.at("accent"), (th::Token{0xff8800}));
    EXPECT_EQ(d.tokens.at("info"), (th::Token{0xff8800}));  // info follows the accent
    EXPECT_EQ(d.tokens.at("hover"), (th::Token{0xffffff, 0x20}));
    EXPECT_EQ(d.tokens.at("muted"), th::derive_base(t).tokens.at("muted"));
    t.colors["info"] = "#00aaff";
    d = th::derive(t);
    EXPECT_EQ(d.tokens.at("info"), (th::Token{0x00aaff}));
    // A light background makes a light theme.
    auto light = graphite();
    light.background = 0xffffff;
    EXPECT_TRUE(th::derive(light).light);
}

TEST(ThemeFile, NameRules) {
    const std::vector<std::string> builtin = {"midnight", "ember", "moss", "paper"};
    EXPECT_EQ(th::name_problem("my-theme", builtin), "");
    EXPECT_EQ(th::name_problem("\xe6\x88\x91\xe7\x9a\x84\xe4\xb8\xbb\xe9\xa1\x8c", builtin),
              "");  // Chinese is fine
    for (const char* bad : {"", "a/b", "a:b", "what?", "CON", "nul", "Com1", "paper", "Midnight",
                            "preset-ink", "dark-graphite", "x-paper", "trailing.", " lead"})
        EXPECT_NE(th::name_problem(bad, builtin), "") << bad;
    EXPECT_NE(th::name_problem(std::string(65, 'a'), builtin), "");
}

TEST(ThemeFile, BuiltinSuggestions) {
    EXPECT_EQ(th::builtin_suggestion("mac"), (std::pair<std::string, std::string>{"mac-dusk", "mac-dawn"}));
    EXPECT_EQ(th::builtin_suggestion("workspace"),
              (std::pair<std::string, std::string>{"midnight", "paper"}));
    EXPECT_EQ(th::builtin_suggestion("terminals"),
              (std::pair<std::string, std::string>{"preset-graphite", "preset-paper"}));
    EXPECT_EQ(th::builtin_suggestion("classic"),
              (std::pair<std::string, std::string>{"phosphor-green", "phosphor-paper"}));
    EXPECT_FALSE(th::builtin_suggestion("cockpit"));  // removed layouts suggest nothing
    EXPECT_FALSE(th::builtin_suggestion("tiling"));
    EXPECT_FALSE(th::builtin_suggestion("mine"));
}

// Every suggested theme that is not one of AgentDesk's palettes or a preset ships as a file that
// parses, dark first and light second.
TEST(ThemeFile, SuggestedThemeFilesShip) {
    for (const char* layout : {"workspace", "terminals", "mac", "classic"}) {
        const auto pair = th::builtin_suggestion(layout);
        ASSERT_TRUE(pair) << layout;
        for (const auto& [id, light] : {std::pair{pair->first, false}, std::pair{pair->second, true}}) {
            if (id == "midnight" || id == "paper" || id.starts_with("preset-"))
                continue;
            const auto t = th::parse(read_file(std::string(AGENTDESK_OWN_THEMES_DIR) + "/" + id + ".json"));
            ASSERT_TRUE(t) << layout << ": " << id;
            EXPECT_EQ(th::derive(*t).light, light) << id;
        }
    }
}

// AgentDesk's theme files: text at AA (4.5:1) or better on every background, status colours and
// the accent at 3:1 or better, and the three status colours apart under every colour vision.
TEST(ThemeFile, MacDuskAndDawnPassThePaletteCheck) {
    for (const char* name : {"mac-dusk", "mac-dawn"}) {
        const auto t = th::parse(read_file(std::string(AGENTDESK_OWN_THEMES_DIR) + "/" + name + ".json"));
        ASSERT_TRUE(t) << name;
        const th::Derived d = th::derive(*t);
        EXPECT_EQ(d.light, std::string(name) == "mac-dawn");
        for (const auto& x : c::check_palette(th::check_colors(d)))
            EXPECT_TRUE(x.pass()) << name << ": " << x.what << " " << x.value << " < " << x.threshold;
    }
}
