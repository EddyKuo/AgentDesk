// AgentDesk window contents. Feature set follows herdr's app (spaces, tabs, split panes, agent
// attention, notifications, command palette, worktrees; observed from its public guide
// defaults/AGENTS.md and keys.json), with AgentDesk's own look, names and code (ADR-12/ADR-13).
#include "app.hpp"
#include "win98_state.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <array>
#include <future>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <dwmapi.h>
#include <imgui.h>
#include <mmsystem.h>
#include <shellapi.h>

#include "agentdesk/host/git_tools.hpp"
#include "agentdesk/ipc/host_launcher.hpp"
#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/ui/chrome_logic.hpp"
#include "agentdesk/ui/git_info.hpp"
#include "agentdesk/ui/image_cache.hpp"
#include "agentdesk/ui/imgui_adapter.hpp"
#include "agentdesk/ui/programs.hpp"
#include "agentdesk/ui/keymap.hpp"
#include "agentdesk/ui/layout.hpp"
#include "agentdesk/ui/mods.hpp"
#include "agentdesk/ui/telemetry.hpp"
#include "agentdesk/ui/terminal_view.hpp"
#include "agentdesk/ui/updates.hpp"
#include "agentdesk/ui/workspace.hpp"

namespace agentdesk::desktop {

namespace {

namespace win = agentdesk::platform::win;
namespace ipc = agentdesk::ipc;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using ui::Attention;
using ui::Dir;

// ------------------------------------------------------------------ themes (our own palettes)
struct Palette {
    const char* id;
    const char* label;
    bool light;
    ImU32 bg, sidebar, card, border, focus, hover, selected, text, dim, faint;
    ImU32 needs_you, done, working, idle, info;
    ImU32 term_bg, term_fg, cursor, selection;
    bool layout_only = false;  // a layout's own theme (herdr: offered only there)
    const std::array<ImU32, 16>* ansi = nullptr;  // the theme's "ansi" table (herdr themes/*.json)
};

// herdr themes/win98.json "ansi" (0-7, then the bright 8-15).
constexpr std::array<ImU32, 16> kWin98Ansi = {
    IM_COL32(0x55, 0x55, 0x55, 255), IM_COL32(0xaa, 0x00, 0x00, 255), IM_COL32(0x00, 0xaa, 0x00, 255),
    IM_COL32(0xaa, 0x55, 0x00, 255), IM_COL32(0x00, 0x00, 0xbb, 255), IM_COL32(0xaa, 0x00, 0xaa, 255),
    IM_COL32(0x00, 0xaa, 0xaa, 255), IM_COL32(0xaa, 0xaa, 0xaa, 255), IM_COL32(0x55, 0x55, 0x55, 255),
    IM_COL32(0xff, 0x55, 0x55, 255), IM_COL32(0x55, 0xff, 0x55, 255), IM_COL32(0xff, 0xff, 0x55, 255),
    IM_COL32(0x55, 0x55, 0xff, 255), IM_COL32(0xff, 0x55, 0xff, 255), IM_COL32(0x55, 0xff, 0xff, 255),
    IM_COL32(0xff, 0xff, 0xff, 255)};

constexpr Palette kPalettes[] = {
    {"midnight",
     "Midnight",
     false,
     IM_COL32(13, 17, 27, 255),
     IM_COL32(16, 21, 33, 255),
     IM_COL32(18, 23, 36, 255),
     IM_COL32(36, 44, 64, 255),
     IM_COL32(139, 124, 246, 255),
     IM_COL32(27, 34, 52, 255),
     IM_COL32(35, 43, 66, 255),
     IM_COL32(226, 230, 242, 255),
     IM_COL32(136, 145, 170, 255),
     IM_COL32(86, 95, 120, 255),
     IM_COL32(240, 100, 122, 255),
     IM_COL32(92, 203, 154, 255),
     IM_COL32(233, 185, 73, 255),
     IM_COL32(136, 145, 170, 255),
     IM_COL32(110, 168, 250, 255),
     IM_COL32(15, 19, 30, 255),
     IM_COL32(222, 226, 238, 255),
     IM_COL32(200, 190, 255, 255),
     IM_COL32(139, 124, 246, 85)},
    {"ember",
     "Ember",
     false,
     IM_COL32(20, 17, 15, 255),
     IM_COL32(25, 21, 18, 255),
     IM_COL32(27, 23, 20, 255),
     IM_COL32(56, 46, 38, 255),
     IM_COL32(236, 140, 74, 255),
     IM_COL32(40, 33, 28, 255),
     IM_COL32(52, 42, 34, 255),
     IM_COL32(240, 232, 222, 255),
     IM_COL32(170, 152, 136, 255),
     IM_COL32(112, 98, 86, 255),
     IM_COL32(238, 98, 86, 255),
     IM_COL32(150, 200, 110, 255),
     IM_COL32(240, 196, 90, 255),
     IM_COL32(170, 152, 136, 255),
     IM_COL32(120, 176, 230, 255),
     IM_COL32(22, 19, 17, 255),
     IM_COL32(236, 228, 216, 255),
     IM_COL32(255, 200, 150, 255),
     IM_COL32(236, 140, 74, 80)},
    {"moss",
     "Moss",
     false,
     IM_COL32(12, 20, 18, 255),
     IM_COL32(15, 25, 22, 255),
     IM_COL32(17, 28, 25, 255),
     IM_COL32(34, 54, 48, 255),
     IM_COL32(64, 200, 170, 255),
     IM_COL32(24, 40, 35, 255),
     IM_COL32(30, 50, 44, 255),
     IM_COL32(222, 236, 230, 255),
     IM_COL32(132, 160, 150, 255),
     IM_COL32(84, 108, 100, 255),
     IM_COL32(236, 108, 118, 255),
     IM_COL32(120, 214, 140, 255),
     IM_COL32(226, 196, 88, 255),
     IM_COL32(132, 160, 150, 255),
     IM_COL32(110, 180, 230, 255),
     IM_COL32(14, 23, 21, 255),
     IM_COL32(218, 234, 228, 255),
     IM_COL32(160, 240, 220, 255),
     IM_COL32(64, 200, 170, 80)},
    {"paper",
     "Paper",
     true,
     IM_COL32(236, 236, 232, 255),
     IM_COL32(226, 226, 220, 255),
     IM_COL32(250, 250, 247, 255),
     IM_COL32(204, 204, 196, 255),
     IM_COL32(88, 92, 220, 255),
     IM_COL32(216, 216, 210, 255),
     IM_COL32(206, 208, 228, 255),
     IM_COL32(30, 32, 40, 255),
     IM_COL32(100, 102, 112, 255),
     IM_COL32(150, 150, 156, 255),
     IM_COL32(204, 52, 72, 255),
     IM_COL32(30, 150, 90, 255),
     IM_COL32(190, 130, 10, 255),
     IM_COL32(110, 112, 122, 255),
     IM_COL32(40, 110, 210, 255),
     IM_COL32(250, 250, 247, 255),
     IM_COL32(36, 38, 46, 255),
     IM_COL32(60, 60, 200, 255),
     IM_COL32(88, 92, 220, 70)},
    // herdr themes/win98.json, Herdr 98's own (settings.rs 99): a theme named like a layout is that
    // layout's own, so Settings and the palette do not offer it (AGENTS.md 338-339). Panel colors
    // from its "colors": surface/popover #c0c0c0, raised/hover #dfdfdf, border #808080, foreground
    // #000000, dim #808080, muted #404040, accent #000080; status blocked #ff0000, done #0000ff,
    // working #808000, idle #808080. Terminals: the pane's black (terminal_opacity 0 over
    // background #000000), foreground #c0c0c0, cursor #c0c0c0, selection #000080.
    {"win98",
     "AgentDesk 98",
     true,
     IM_COL32(0xc0, 0xc0, 0xc0, 255),
     IM_COL32(0xc0, 0xc0, 0xc0, 255),
     IM_COL32(0xc0, 0xc0, 0xc0, 255),
     IM_COL32(0x80, 0x80, 0x80, 255),
     IM_COL32(0x00, 0x00, 0x80, 255),
     IM_COL32(0xdf, 0xdf, 0xdf, 255),
     IM_COL32(0xdf, 0xdf, 0xdf, 255),
     IM_COL32(0x00, 0x00, 0x00, 255),
     IM_COL32(0x80, 0x80, 0x80, 255),
     IM_COL32(0x40, 0x40, 0x40, 255),
     IM_COL32(0xff, 0x00, 0x00, 255),
     IM_COL32(0x00, 0x00, 0xff, 255),
     IM_COL32(0x80, 0x80, 0x00, 255),
     IM_COL32(0x80, 0x80, 0x80, 255),
     IM_COL32(0x00, 0x00, 0x80, 255),
     IM_COL32(0x00, 0x00, 0x00, 255),
     IM_COL32(0xc0, 0xc0, 0xc0, 255),
     IM_COL32(0xc0, 0xc0, 0xc0, 255),
     IM_COL32(0x00, 0x00, 0x80, 0x80),  // drawn over the text here, so not opaque
     true,
     &kWin98Ansi},
};


// ------------------------------------------------------------------ herdr's themes
// The person chose to have herdr's themes beside ours (2026-11-19). herdr settings.rs: four
// presets (graphite, ink, moss, paper; 19-79), shipped theme files (atom-one-dark; 97-101) and
// the person's themes/<name>.json (background, foreground, selection, cursor?, ansi[16],
// colors{}; 103-113). The chrome colors come from the palette as herdr's resolve() derives them
// (306-375). Ids of herdr's presets get "herdr-" in front, since ours already use moss and paper.
struct HerdrPalette {
    std::uint32_t background = 0, foreground = 0, selection = 0;
    std::optional<std::uint32_t> cursor;
    std::array<std::uint32_t, 16> ansi{};
    std::optional<std::array<std::uint32_t, 3>> status;  // working, blocked, done
};

constexpr std::array<std::uint32_t, 3> kHerdrDarkStatus = {0xe5c07b, 0xf0705f, 0x76a8ff};  // settings.rs 31

const std::array<std::pair<const char*, HerdrPalette>, 4>& herdr_presets() {
    static const std::array<std::pair<const char*, HerdrPalette>, 4> k = {{
        {"graphite",
         {0x17191d, 0xd2d4d9, 0x394454, std::nullopt,
          {0x45484f, 0xdc8c95, 0xa6bd91, 0xd8be87, 0x96b2d7, 0xb5a2ce, 0x8cbdc3, 0xd2d4d9, 0x818793, 0xf0a5ad,
           0xbad5a2, 0xead2a1, 0xb0cbef, 0xcbb7e6, 0xa4d5db, 0xf0f0f2},
          kHerdrDarkStatus}},
        {"ink",
         {0x111720, 0xcdd6e4, 0x344861, std::nullopt,
          {0x414b5a, 0xe58c9a, 0x89c5aa, 0xd9be82, 0x91b6ee, 0xb9a0e2, 0x85c4d0, 0xcdd6e4, 0x7f91a7, 0xf5a5b2,
           0xa5dec3, 0xedd39c, 0xaccbfa, 0xd0b7f4, 0xa0dce5, 0xf0f5fc},
          kHerdrDarkStatus}},
        {"moss",
         {0x151c19, 0xcdd7ce, 0x354a40, std::nullopt,
          {0x435248, 0xdf978d, 0xa0c4a2, 0xd4bc8b, 0x99b9cd, 0xbba4c8, 0x8bbdb5, 0xcdd7ce, 0x82968a, 0xf1afa3,
           0xb8dab9, 0xe9d2a5, 0xb1d0e2, 0xd1bddc, 0xa4d6ca, 0xeff5ee},
          kHerdrDarkStatus}},
        {"paper",
         {0xffffff, 0x25272c, 0xcfdcf5, 0x25272c,
          {0x25272c, 0xc62a2a, 0x1a7f37, 0x8f5d00, 0x1f62d0, 0x7a45c8, 0x0f6f74, 0x5f6068, 0x7b7e87, 0xd1242f,
           0x1f883d, 0x9a6700, 0x0969da, 0x8250df, 0x1b7c83, 0x111214},
          std::array<std::uint32_t, 3>{0x9a6700, 0xc93c2c, 0x2f6fe4}}},
    }};
    return k;
}

ImU32 rgb_col(std::uint32_t c, int a = 255) {
    return IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, a);
}

// settings.rs 268-270 is_light, 292-297 mix.
bool herdr_is_light(std::uint32_t c) {
    auto ch = [&](int sh) { return static_cast<float>((c >> sh) & 255) / 255.0f; };
    return 0.2126f * ch(16) + 0.7152f * ch(8) + 0.0722f * ch(0) > 0.5f;
}
std::uint32_t herdr_mix(std::uint32_t a, std::uint32_t b, float t) {
    std::uint32_t out = 0;
    for (const int sh : {16, 8, 0}) {
        const float x = static_cast<float>((a >> sh) & 255), y = static_cast<float>((b >> sh) & 255);
        out |= static_cast<std::uint32_t>(std::lround(x * (1.0f - t) + y * t)) << sh;
    }
    return out;
}

std::optional<ImU32> parse_hex_color(const std::string& h) {
    if ((h.size() != 7 && h.size() != 9) || h[0] != '#')
        return std::nullopt;
    char* end = nullptr;
    const unsigned long v = std::strtoul(h.c_str() + 1, &end, 16);
    if (end != h.c_str() + h.size())
        return std::nullopt;
    if (h.size() == 7)
        return rgb_col(static_cast<std::uint32_t>(v));
    return rgb_col(static_cast<std::uint32_t>(v >> 8), static_cast<int>(v & 255));
}

struct OwnedPalette {
    std::string id, label;
    std::array<ImU32, 16> ansi{};
    Palette p{};
};
std::deque<OwnedPalette> g_herdr_palettes;  // stable addresses: g_pal may point into it

// herdr resolve(): our Palette's fields from herdr's derived colors. bg = canvas (behind the
// panes), sidebar = chrome, card = the terminal background (surface), text = foreground,
// dim = muted, faint = dim, focus/info = accent; theme files' "colors" override by herdr's names.
void add_herdr_palette(const std::string& id, const std::string& label, const HerdrPalette& h,
                       const std::map<std::string, std::string>& colors) {
    OwnedPalette& o = g_herdr_palettes.emplace_back();
    o.id = id;
    o.label = label;
    const std::uint32_t bg = h.background;
    const bool light = herdr_is_light(bg);
    const std::uint32_t fg = light ? 0x1b1b1e : 0xeeeeef;
    const auto st = h.status.value_or(std::array<std::uint32_t, 3>{h.ansi[3], h.ansi[1], h.ansi[4]});
    std::map<std::string, ImU32> t;
    t["background"] = rgb_col(bg);
    t["terminal_foreground"] = rgb_col(h.foreground);
    t["foreground"] = rgb_col(fg);
    t["surface"] = rgb_col(bg);
    if (light) {
        const std::uint32_t warm = 0xe8e8e2;
        t["hover"] = IM_COL32(0, 0, 0, 0x0c);
        t["selected"] = IM_COL32(0, 0, 0, 0x12);
        t["border"] = IM_COL32(0, 0, 0, 0x17);
        t["canvas"] = rgb_col(herdr_mix(bg, warm, 0.2f));
        t["chrome"] = rgb_col(herdr_mix(bg, warm, 0.6f));
    } else {
        t["hover"] = IM_COL32(255, 255, 255, 0x0e);
        t["selected"] = IM_COL32(255, 255, 255, 0x17);
        t["border"] = IM_COL32(255, 255, 255, 0x18);
        t["canvas"] = rgb_col(herdr_mix(bg, 0, 0.16f));
        t["chrome"] = rgb_col(herdr_mix(bg, 0, 0.32f));
    }
    t["muted"] = rgb_col(herdr_mix(bg, fg, light ? 0.66f : 0.6f));
    t["dim"] = rgb_col(herdr_mix(bg, fg, light ? 0.46f : 0.38f));
    t["accent"] = rgb_col(h.ansi[4]);
    t["selection"] = rgb_col(h.selection);
    t["cursor"] = rgb_col(h.cursor.value_or(h.foreground));
    t["working"] = rgb_col(st[0]);
    t["blocked"] = rgb_col(st[1]);
    t["done"] = rgb_col(st[2]);
    t["idle"] = rgb_col(herdr_mix(bg, fg, 0.38f));
    for (std::size_t i = 0; i < 16; ++i)
        o.ansi[i] = rgb_col(h.ansi[i]);
    for (const auto& [k, v] : colors)
        if (const auto c = parse_hex_color(v))
            t[k] = *c;
    o.p = Palette{o.id.c_str(), o.label.c_str(), light, t["canvas"], t["chrome"], t["surface"], t["border"],
                  t["accent"], t["hover"], t["selected"], t["foreground"], t["muted"], t["dim"], t["blocked"],
                  t["done"], t["working"], t["idle"], t["accent"], t["background"], t["terminal_foreground"],
                  t["cursor"], (t["selection"] & 0x00FFFFFFu) | (static_cast<ImU32>(light ? 0xa0 : 0x90) << 24), false, &o.ansi};
}

std::filesystem::path data_dir();

// herdr settings.rs 103-113: a theme file. A bad file is skipped (herdr: not listed).
std::optional<std::pair<HerdrPalette, std::map<std::string, std::string>>> read_theme_file(const std::filesystem::path& f) {
    std::ifstream in(f, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const auto j = nlohmann::json::parse(ss.str(), nullptr, false);
    if (!j.is_object())
        return std::nullopt;
    auto hex = [](const nlohmann::json& v) -> std::optional<std::uint32_t> {
        if (!v.is_string())
            return std::nullopt;
        const std::string h = v.get<std::string>();
        if (h.size() != 7 || h[0] != '#')
            return std::nullopt;
        return static_cast<std::uint32_t>(std::strtoul(h.c_str() + 1, nullptr, 16));
    };
    HerdrPalette p;
    const auto b = hex(j.value("background", nlohmann::json())), fgc = hex(j.value("foreground", nlohmann::json())),
               sel = hex(j.value("selection", nlohmann::json()));
    if (!b || !fgc || !sel || !j.contains("ansi") || !j["ansi"].is_array() || j["ansi"].size() != 16)
        return std::nullopt;
    p.background = *b;
    p.foreground = *fgc;
    p.selection = *sel;
    if (j.contains("cursor"))
        p.cursor = hex(j["cursor"]);
    for (std::size_t i = 0; i < 16; ++i) {
        const auto c = hex(j["ansi"][i]);
        if (!c)
            return std::nullopt;
        p.ansi[i] = *c;
    }
    std::map<std::string, std::string> colors;
    if (j.contains("colors") && j["colors"].is_object())
        for (const auto& [k, v] : j["colors"].items())
            if (v.is_string())
                colors[k] = v.get<std::string>();
    return std::make_pair(p, colors);
}

std::filesystem::path shipped_themes_dir() {
    wchar_t exe[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return std::filesystem::path(exe).parent_path() / L"themes";
}

std::filesystem::file_time_type g_themes_mtime{};
bool g_themes_loaded = false;

// The preset themes' ids. Settings written by 0.2.0 and earlier named them with another prefix:
// theme_id() reads "<anything>-<preset>" as the same preset.
constexpr std::string_view kPresetPrefix = "preset-";
std::string theme_id(std::string id) {
    const auto dash = id.find('-');
    if (dash == std::string::npos || id.starts_with(kPresetPrefix))
        return id;
    for (const auto& [name, pal] : herdr_presets())
        if (id.compare(dash + 1, std::string::npos, name) == 0)
            return std::string(kPresetPrefix) + name;
    return id;
}

// Presets, then the shipped files, then the person's (sorted by name), as herdr themes() lists
// them (settings.rs 272-286). win98 is Herdr 98's own and already one of ours.
void load_herdr_themes() {
    g_themes_loaded = true;
    std::error_code ec;
    g_themes_mtime = std::filesystem::last_write_time(data_dir() / L"themes", ec);
    if (!g_herdr_palettes.empty())
        return;  // addresses stay valid for g_pal; a changed file applies at the next start
    for (const auto& [name, pal] : herdr_presets())
        add_herdr_palette(std::string(kPresetPrefix) + name, name, pal, {});
    std::map<std::string, std::filesystem::path> files;
    for (const auto& dir : {shipped_themes_dir(), data_dir() / L"themes"})
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == L".json") {
                const auto stem = e.path().stem().u8string();
                const std::string name(stem.begin(), stem.end());
                if (name != "win98")
                    files[name] = e.path();  // the person's file wins over a shipped one
            }
    for (const auto& [name, file] : files)
        if (const auto t = read_theme_file(file)) {
            std::string label = name;
            std::replace(label.begin(), label.end(), '-', ' ');  // settings/main.js 367
            add_herdr_palette(name, label, t->first, t->second);
        }
}

// Ours, then herdr's.
std::vector<const Palette*> all_palettes() {
    if (!g_themes_loaded)
        load_herdr_themes();
    std::vector<const Palette*> out;
    for (const auto& p : kPalettes)
        out.push_back(&p);
    for (const auto& o : g_herdr_palettes)
        out.push_back(&o.p);
    return out;
}

const Palette* g_pal = &kPalettes[0];
float g_scale = 1.0f;  // DPI x zoom

float px(float v) {
    return std::round(v * g_scale);
}

ImVec4 v4(ImU32 c) {
    return ImGui::ColorConvertU32ToFloat4(c);
}

ImU32 alpha(ImU32 c, int a) {
    return (c & 0x00FFFFFF) | (static_cast<ImU32>(a) << 24);
}

const Palette* palette_by_id(const std::string& id) {
    for (const Palette* p : all_palettes())
        if (id == p->id)
            return p;
    return &kPalettes[0];
}

// Windows' app mode (Settings > Personalization > Colors), checked at most every 2 s.
bool system_light() {
    static auto checked = std::chrono::steady_clock::time_point{};
    static bool light = false;
    const auto now = std::chrono::steady_clock::now();
    if (now - checked > std::chrono::seconds(2)) {
        checked = now;
        DWORD v = 0;
        DWORD size = sizeof v;
        light = ::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                               L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS &&
                v != 0;
    }
    return light;
}

// herdr Appearance: System follows Windows; Light and Dark use the chosen theme of that kind. A
// layout file with a "theme" overrides it.
const Palette* effective_palette(const std::string& appearance, const std::string& dark, const std::string& light,
                                 const std::string& layout_theme) {
    if (!layout_theme.empty())
        return palette_by_id(layout_theme);
    const bool use_light = appearance == "light" || (appearance == "system" && system_light());
    return palette_by_id(use_light ? light : dark);
}

void apply_style(float dpi, float zoom) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    if (g_pal->light)
        ImGui::StyleColorsLight(&s);
    else
        ImGui::StyleColorsDark(&s);
    s.WindowRounding = 8;  // herdr overlays: radius 8, popover background, 1 px border
    s.ChildRounding = 10;
    s.FrameRounding = 7;
    s.PopupRounding = 8;
    s.GrabRounding = 7;
    s.ScrollbarRounding = 7;
    s.WindowBorderSize = 1;
    s.ChildBorderSize = 0;
    s.PopupBorderSize = 1;
    s.WindowPadding = ImVec2(18, 16);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(8, 8);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 10;
    s.SelectableTextAlign = ImVec2(0, 0.5f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = v4(g_pal->text);
    c[ImGuiCol_TextDisabled] = v4(g_pal->dim);
    c[ImGuiCol_WindowBg] = v4(g_pal->bg);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    {
        const ImVec4 a = v4(g_pal->card);
        const float t = g_pal->light ? -0.03f : 0.06f;  // popover = card mixed toward white (black on light)
        const float target = g_pal->light ? 0.0f : 1.0f;
        const float k = std::abs(t);
        c[ImGuiCol_PopupBg] = ImVec4(a.x + (target - a.x) * k, a.y + (target - a.y) * k, a.z + (target - a.z) * k, 1.0f);
    }
    c[ImGuiCol_Border] = v4(g_pal->border);
    c[ImGuiCol_FrameBg] = v4(g_pal->hover);
    c[ImGuiCol_FrameBgHovered] = v4(g_pal->selected);
    c[ImGuiCol_FrameBgActive] = v4(g_pal->selected);
    c[ImGuiCol_Button] = v4(g_pal->hover);
    c[ImGuiCol_ButtonHovered] = v4(g_pal->selected);
    c[ImGuiCol_ButtonActive] = v4(alpha(g_pal->focus, 120));
    c[ImGuiCol_Header] = v4(g_pal->selected);
    c[ImGuiCol_HeaderHovered] = v4(g_pal->hover);
    c[ImGuiCol_HeaderActive] = v4(g_pal->selected);
    c[ImGuiCol_CheckMark] = v4(g_pal->focus);
    c[ImGuiCol_SliderGrab] = v4(g_pal->focus);
    c[ImGuiCol_SliderGrabActive] = v4(g_pal->focus);
    c[ImGuiCol_Separator] = v4(g_pal->border);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = v4(g_pal->border);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.35f);  // herdr window.rs 4767: hsla(0, 0, 0, 0.35)
    c[ImGuiCol_TitleBg] = v4(g_pal->card);
    c[ImGuiCol_TitleBgActive] = v4(g_pal->card);
    c[ImGuiCol_NavCursor] = v4(g_pal->focus);
    c[ImGuiCol_TextSelectedBg] = v4(alpha(g_pal->focus, 90));
    s.ScaleAllSizes(dpi * zoom);
    s.FontScaleDpi = dpi;
    s.FontScaleMain = zoom;
}

// ------------------------------------------------------------------ settings (ui.json)
struct MachineConfig {
    std::string name;    // shown in the sidebar
    std::string target;  // ssh target: user@host or an ssh config alias
    std::string ctl;     // agentctl on that machine (empty: on PATH)
};

struct Settings {
    std::vector<MachineConfig> machines;
    std::set<std::string> mods_enabled;  // the examples stay off until turned on (Settings > Mods)
    std::string layout = "workspace";
    bool close_notice_shown = false;
    std::string last_cwd;
    std::string theme = "midnight";
    float zoom = 1.0f;
    float font_size = 14.0f;
    bool sounds = true;
    // Read from ui.json and written back, but ignored: herdr always shows the note (settings/main.js
    // 751: "A note shows in the corner either way"), so Settings no longer offers it.
    bool toasts = true;
    bool copy_on_select = true;
    float scroll_speed = 1.0f;
    bool sidebar = true;
    bool details = false;
    std::string worktree_dir;  // empty: %USERPROFILE%\.agentdesk\worktrees
    std::string shell_profile;  // new spaces/tabs/splits; empty: pwsh, powershell, cmd
    float ui_text = 15.0f;       // interface text size at zoom 1 (herdr: 13); scales all chrome
    float sidebar_width = 268;   // herdr design px, 44..480; under 160 = rail
    float sidebar_restore = 268; // width the rail toggle goes back to
    float spaces_share = 0.5f;   // spaces / agents split of the sidebar lists
    float panel_width = 368;     // right panel (Files / Changes), 280..900
    std::string agents_sort = "priority";  // or "space"
    std::vector<std::string> palette_recent;  // last 20 palette commands, newest first
    std::string terminal_font;   // font family; "" = JetBrains Mono (bundled)
    int terminal_weight = 500;   // herdr terminal font weight (JetBrains Mono Medium)
    std::string ui_font;         // interface font family; "" = Geist (bundled)
    std::string appearance = "system";  // "system", "light" or "dark" (herdr Appearance)
    std::string theme_dark = "midnight";
    std::string theme_light = "paper";
    // herdr window.rs 3620-3635: a layout is a look; each keeps its own dark and light theme
    // (layout -> {dark, light}). Without an entry the two above are used.
    std::map<std::string, std::pair<std::string, std::string>> layout_themes;
    float line_height = 1.0f;    // terminal cell height multiplier
    float cell_width = 1.0f;     // terminal cell width multiplier
    std::string sound_done;      // MP3 replacing the bundled sounds
    std::string sound_blocked;
    std::string welcomed;        // "", "finished" or "skipped"
    std::string seen_version;    // What's new was shown for this version (herdr: once after an update)
    std::vector<std::string> folded_machines;  // machine headers folded in the sidebar
    std::vector<std::string> panels = {"files", "changes"};  // panel buttons in the tab bar
    Json agents_changed = Json::object();  // session id -> {s: state, at: ms}: herdr agents.js 85-110
    // herdr settings.rs 718-726 / main.js 857-861: on unless the file says "telemetry": false;
    // turning it on again removes the key.
    bool telemetry = true;
};

std::filesystem::path data_dir() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = ::GetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", buf, MAX_PATH);
    std::filesystem::path dir;
    if (n > 0 && n < MAX_PATH) {
        dir = std::wstring(buf, n);
    } else {
        n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
        dir = (n > 0 ? std::filesystem::path(std::wstring(buf, n)) : std::filesystem::temp_directory_path()) /
              L"AgentDesk";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// herdr first run (settings.rs 778-802): zoom from Windows' text size (Accessibility
// TextScaleFactor, 100..225 %), x1.15 on a screen 2400 px or wider, rounded to tenths.
float first_run_zoom() {
    DWORD factor = 100, size = sizeof(factor);
    ::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Accessibility", L"TextScaleFactor", RRF_RT_REG_DWORD,
                   nullptr, &factor, &size);
    float z = std::clamp(static_cast<float>(factor) / 100.0f, 1.0f, 2.0f);
    if (::GetSystemMetrics(SM_CXSCREEN) >= 2400)
        z *= 1.15f;
    return std::clamp(std::round(z * 10.0f) / 10.0f, 0.5f, 3.0f);
}

// The shown layout's {dark, light} themes (herdr: on the layout's root), else the defaults.
std::pair<std::string, std::string> layout_theme_pair(const Settings& st) {
    if (const auto it = st.layout_themes.find(st.layout); it != st.layout_themes.end())
        return it->second;
    return {st.theme_dark, st.theme_light};
}

// ui.json parsed but a key has the wrong type: the defaults are used, and nothing is written over
// the person's file until they fix it (as when it does not parse).
bool g_settings_unreadable = false;

Settings load_settings_unchecked();
Settings load_settings() {
    try {
        return load_settings_unchecked();
    } catch (const Json::exception&) {
        g_settings_unreadable = true;
        return Settings{};
    }
}

Settings load_settings_unchecked() {
    Settings s;
    std::ifstream in(data_dir() / L"ui.json", std::ios::binary);
    if (!in) {
        s.zoom = first_run_zoom();
        return s;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const auto j = Json::parse(ss.str(), nullptr, false);
    // herdr settings.rs 718-726: even when the rest can't be used, the file's own "telemetry".
    if (j.is_object() && j.contains("telemetry") && j["telemetry"].is_boolean())
        s.telemetry = j["telemetry"].get<bool>();
    if (!j.is_object() || j.value("schema_version", 0) != 1)
        return s;
    s.close_notice_shown = j.value("close_notice_shown", false);
    s.last_cwd = j.value("last_cwd", "");
    s.theme = theme_id(j.value("theme", s.theme));
    s.terminal_weight = std::clamp(j.value("terminal_weight", s.terminal_weight), 100, 900);
    s.ui_font = j.value("ui_font", "");
    // Appearance: files from before it existed keep their one theme as the fixed choice.
    {
        const bool light = std::string(palette_by_id(s.theme)->id) == s.theme && palette_by_id(s.theme)->light;
        s.appearance = j.value("appearance", j.contains("theme") ? (light ? "light" : "dark") : "system");
        s.theme_dark = theme_id(j.value("theme_dark", light ? std::string("midnight") : s.theme));
        s.theme_light = theme_id(j.value("theme_light", light ? s.theme : std::string("paper")));
    }
    if (const auto lt = j.find("layout_themes"); lt != j.end() && lt->is_object())
        for (const auto& [name, pair] : lt->items())
            if (pair.is_object())
                s.layout_themes[name] = {theme_id(pair.value("dark", s.theme_dark)),
                                         theme_id(pair.value("light", s.theme_light))};
    s.zoom = std::clamp(j.value("zoom", 1.0f), 0.5f, 3.0f);
    s.font_size = std::clamp(j.value("font_size", 14.0f), 8.0f, 32.0f);  // the stepper's 8-32
    s.sounds = j.value("sounds", true);
    s.toasts = j.value("toasts", true);
    s.copy_on_select = j.value("copy_on_select", true);
    s.scroll_speed = std::clamp(j.value("scroll_speed", 1.0f), 0.1f, 5.0f);
    s.sidebar = j.value("sidebar", true);
    s.details = j.value("details", false);
    s.worktree_dir = j.value("worktree_dir", "");
    s.shell_profile = j.value("shell_profile", "");
    s.ui_text = std::clamp(j.value("ui_text", 15.0f), 11.0f, 24.0f);
    s.sidebar_width = std::clamp(j.value("sidebar_width", 268.0f), 44.0f, 480.0f);
    s.sidebar_restore = std::clamp(j.value("sidebar_restore", 268.0f), 160.0f, 480.0f);
    s.spaces_share = std::clamp(j.value("spaces_share", 0.5f), 0.1f, 0.9f);
    s.panel_width = std::clamp(j.value("panel_width", 368.0f), 280.0f, 900.0f);
    s.agents_sort = j.value("agents_sort", std::string("priority"));
    s.terminal_font = j.value("terminal_font", "");
    s.line_height = std::clamp(j.value("line_height", 1.0f), 0.8f, 2.0f);
    s.cell_width = std::clamp(j.value("cell_width", 1.0f), 0.8f, 2.0f);
    s.sound_done = j.value("sound_done", "");
    s.sound_blocked = j.value("sound_blocked", "");
    s.welcomed = j.value("welcomed", "");
    s.seen_version = j.value("seen_version", "");
    for (const auto& m : j.value("folded_machines", Json::array()))
        if (m.is_string())
            s.folded_machines.push_back(m.get<std::string>());
    if (const auto it = j.find("panels"); it != j.end() && it->is_array()) {
        s.panels.clear();
        for (const auto& p : *it)
            if (p.is_string())
                s.panels.push_back(p.get<std::string>());
    }
    for (const auto& r : j.value("palette_recent", Json::array()))
        if (r.is_string())
            s.palette_recent.push_back(r.get<std::string>());
    if (const auto it = j.find("agents_changed"); it != j.end() && it->is_object())
        s.agents_changed = *it;
    if (const auto it = j.find("mods_enabled"); it != j.end() && it->is_array()) {
        s.mods_enabled.clear();
        for (const auto& m : *it)
            if (m.is_string())
                s.mods_enabled.insert(m.get<std::string>());
    }
    s.layout = j.value("layout", s.layout);
    for (const auto& m : j.value("machines", Json::array())) {
        if (m.is_object() && !m.value("name", "").empty() && !m.value("target", "").empty())
            s.machines.push_back({m.value("name", ""), m.value("target", ""), m.value("ctl", "")});
    }
    return s;
}

// Interface text size relative to herdr's 13 px; the whole chrome scales with it and the zoom.
float ui_text_factor(const Settings& s) {
    return s.ui_text / 13.0f;
}
float ui_scale(const Settings& s) {
    return s.zoom * ui_text_factor(s);
}

std::string g_settings_problem;  // why the last write did not happen (shown once as a toast)

std::string read_text_file(const std::filesystem::path& p, bool* ok = nullptr) {
    std::ifstream in(p, std::ios::binary);
    if (ok != nullptr)
        *ok = static_cast<bool>(in);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// herdr settings.js 115-148: re-read the file, apply our keys onto it (keys we do not own stay),
// write only if nobody changed it meanwhile (3 tries), and never overwrite a file that does not
// parse.
void save_settings(const Settings& s) {
    const auto path = data_dir() / L"ui.json";
    const auto tmp = std::filesystem::path(path).concat(L".tmp");
    const Json ours = Json{
            {"schema_version", 1},
            {"close_notice_shown", s.close_notice_shown},
            {"last_cwd", s.last_cwd},
            {"theme", s.theme},
            {"terminal_weight", s.terminal_weight},
            {"ui_font", s.ui_font},
            {"appearance", s.appearance},
            {"theme_dark", s.theme_dark},
            {"theme_light", s.theme_light},
            {"layout_themes",
             [&] {
                 Json m = Json::object();
                 for (const auto& [name, pair] : s.layout_themes)
                     m[name] = Json{{"dark", pair.first}, {"light", pair.second}};
                 return m;
             }()},
            {"zoom", s.zoom},
            {"font_size", s.font_size},
            {"sounds", s.sounds},
            {"toasts", s.toasts},
            {"copy_on_select", s.copy_on_select},
            {"scroll_speed", s.scroll_speed},
            {"sidebar", s.sidebar},
            {"details", s.details},
            {"worktree_dir", s.worktree_dir},
            {"shell_profile", s.shell_profile},
            {"ui_text", s.ui_text},
            {"sidebar_width", s.sidebar_width},
            {"sidebar_restore", s.sidebar_restore},
            {"spaces_share", s.spaces_share},
            {"panel_width", s.panel_width},
            {"agents_sort", s.agents_sort},
            {"palette_recent", s.palette_recent},
            {"terminal_font", s.terminal_font},
            {"line_height", s.line_height},
            {"cell_width", s.cell_width},
            {"sound_done", s.sound_done},
            {"sound_blocked", s.sound_blocked},
            {"welcomed", s.welcomed},
            {"seen_version", s.seen_version},
            {"folded_machines", s.folded_machines},
            {"panels", s.panels},
            {"agents_changed", s.agents_changed},
            {"mods_enabled", std::vector<std::string>(s.mods_enabled.begin(), s.mods_enabled.end())},
            {"layout", s.layout},
            {"machines", [&] {
                 Json list = Json::array();
                 for (const auto& m : s.machines)
                     list.push_back({{"name", m.name}, {"target", m.target}, {"ctl", m.ctl}});
                 return list;
             }()}};
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::error_code ec;
        const bool exists = std::filesystem::exists(path, ec);
        bool read_ok = true;
        const std::string before = exists ? read_text_file(path, &read_ok) : std::string();
        if (exists && !read_ok) {
            g_settings_problem = "Cannot read ui.json";
            return;
        }
        if (g_settings_unreadable) {  // a value of the wrong type: the person's file stays as is
            g_settings_problem = "ui.json has errors: fix it first";
            return;
        }
        Json doc = Json::object();
        if (exists && !before.empty()) {
            doc = Json::parse(before, nullptr, false);
            if (!doc.is_object()) {
                g_settings_problem = "ui.json has errors: fix it first";
                return;
            }
        }
        for (const auto& [k, v] : ours.items())
            doc[k] = v;
        // herdr settings main.js 858-861: off writes false, on deletes the key.
        if (s.telemetry)
            doc.erase("telemetry");
        else
            doc["telemetry"] = false;
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << doc.dump(2, ' ', false, Json::error_handler_t::replace) << "\n";
            out.flush();
            if (!out) {  // a full or failing disk: never put a cut-off file in its place
                out.close();
                std::filesystem::remove(tmp, ec);
                g_settings_problem = "Not saved to ui.json";
                return;
            }
        }
        bool again_ok = true;
        const std::string now = exists ? read_text_file(path, &again_ok) : std::string();
        if (now != before) {  // changed while we wrote: start over from the new text
            std::filesystem::remove(tmp, ec);
            continue;
        }
        if (!::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(tmp, ec);
            g_settings_problem = "Not saved to ui.json";
        }
        return;
    }
    g_settings_problem = "Not saved to ui.json";
}

// ------------------------------------------------------------------ layouts
// herdr's layout engine (libs/ui layout.hpp): a layout is a tree of blocks (rows, columns,
// stacks; the sidebar, tab bar, panes, panel and status bar are blocks) in herdr's format
// (defaults/AGENTS.md "Layouts"). Built-ins are herdr's main.json (named workspace here),
// terminals.json and win98.json; <data>\layouts\<name>.json adds one or replaces a built-in of
// that name. The person's clicks on a layout (sizes, shown/hidden, a stack's child, where a float
// sits) live in <data>\layouts\.state\<name>.json (herdr window.rs 203-216, layout_sync.rs).
// AgentDesk's older files ({"sidebar", "tab_bar", "status_bar", "theme", "style", "font_size",
// "details"} and no tree) still load: read as the built-in of their style with those parts hidden
// (layout::from_legacy); the file itself is left as it is.
namespace lay = ui::layout;

struct LayoutSpec {
    std::string name;
    lay::Node file;  // as its file (or herdr) has it
    lay::Node tree;  // with the person's clicks: what is drawn
    bool builtin = false;
    bool from_file = false;
    bool legacy = false;
    std::optional<bool> details;  // AgentDesk's own details pane (old files only)
    std::string error;            // the file is broken: shown as shipped meanwhile (window.rs 1005-1018)
    std::filesystem::file_time_type mtime{};
    // From the tree.
    std::string theme;         // a palette AgentDesk has, else "" (the person's theme)
    bool look_applies = true;  // false when the layout names a theme AgentDesk lacks
    std::string style;         // "", "terminals" or "win98": which renderers its blocks name
    std::optional<float> font_size;
};

std::filesystem::path layouts_dir() {
    return data_dir() / L"layouts";
}

std::filesystem::path layout_state_path(const std::string& name) {
    return layouts_dir() / L".state" / win::to_wide(name + ".json");
}

bool palette_exists(const std::string& id) {
    const auto all = all_palettes();
    return std::any_of(all.begin(), all.end(), [&](const Palette* p) { return id == p->id; });
}

// The tree says how it looks and which of AgentDesk's renderers draw it.
void derive_layout(LayoutSpec& l) {
    const lay::Node& t = l.tree;
    std::string theme;
    if (t.look.contains("theme")) {
        const Json& th = t.look["theme"];
        if (th.is_string())
            theme = th.get<std::string>();
        else if (th.is_object())  // layout.rs 197-210: the system's appearance picks
            theme = th.value(system_light() ? "light" : "dark", std::string());
    }
    l.look_applies = theme.empty() || palette_exists(theme);
    l.theme = palette_exists(theme) ? theme : std::string();
    l.style.clear();
    for (const lay::Node* n : t.walk()) {
        const std::string m = n->mod();
        if (m == "win98")
            l.style = "win98";
        else if (m == "terminals-bar" && l.style.empty())
            l.style = "terminals";
        if (n->is_block() && n->content.type == lay::Content::Type::kTab && n->content.pane &&
            n->content.pane->header && n->content.pane->header->mod() == "terminals-pane" && l.style.empty())
            l.style = "terminals";
    }
    // layout.rs 150-151: the nearest node setting it wins; the panes' block decides.
    l.font_size.reset();
    for (const lay::Node* n : t.visible_blocks()) {
        if (n->content.type != lay::Content::Type::kTab)
            continue;
        if (const auto path = t.path_to(n->id))
            for (const auto& id : *path)
                if (const auto f = t.find(id)->font_size())
                    l.font_size = std::clamp(*f, 6.0f, 72.0f);
        break;
    }
    if (!l.font_size)
        l.font_size = t.font_size();
}

// Before this engine the workspace's sizes were in ui.json: the first time, they become its
// clicks (what herdr keeps in layouts/.state).
lay::State seed_from_settings(const Settings& st, const lay::Node& file) {
    lay::Node now = file;
    const Settings d;
    if (lay::Node* n = now.find("sidebar")) {
        if (st.sidebar_width != d.sidebar_width)
            n->size = lay::Span::pixels(std::clamp(st.sidebar_width, 44.0f, 480.0f));
        if (!st.sidebar)
            n->hidden = true;
    }
    if (lay::Node* n = now.find("panel"); n != nullptr && st.panel_width != d.panel_width)
        n->size = lay::Span::pixels(std::clamp(st.panel_width, 280.0f, 900.0f));
    lay::Node* sp = now.find("spaces");
    lay::Node* ag = now.find("agents");
    if (sp != nullptr && ag != nullptr && st.spaces_share != d.spaces_share) {
        sp->size = lay::Span::weight(2.0f * st.spaces_share);
        ag->size = lay::Span::weight(2.0f * (1.0f - st.spaces_share));
    }
    return lay::State::between(file, now);
}

// herdr window.rs 997-1043 load_layout: the file (or its shipped default when it is broken),
// with the clicks; a click on a field the file changed since gives way.
void apply_layout_state(LayoutSpec& l, const Settings& st) {
    l.tree = l.file;
    std::error_code ec;
    const auto path = layout_state_path(l.name);
    lay::State state;
    if (std::filesystem::exists(path, ec)) {
        state = lay::State::from_json(Json::parse(read_text_file(path), nullptr, false));
        if (l.error.empty())
            state.forget_since_saved(l.file);
    } else if (l.name == "workspace") {
        state = seed_from_settings(st, l.file);
    }
    state.apply(l.tree);
    derive_layout(l);
}

// herdr window.rs 1199-1220 write_state: the clicks, with the file they were made on; none, no
// file. Not for a broken file (it is not what shows).
void save_layout_state(const LayoutSpec& l) {
    if (!l.error.empty())
        return;
    lay::State state = lay::State::between(l.file, l.tree);
    std::error_code ec;
    const auto path = layout_state_path(l.name);
    if (state.empty()) {
        std::filesystem::remove(path, ec);
        return;
    }
    state.file = lay::to_json(l.file);
    Json j = state.to_json();
    lay::normalize(j);
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto tmp = std::filesystem::path(path).concat(L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << j.dump(2) << "\n";
    }
    ::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

std::vector<LayoutSpec> load_layouts(const Settings& st) {
    std::vector<LayoutSpec> list;
    for (const auto& name : lay::builtin_names()) {
        LayoutSpec l;
        l.name = name;
        l.builtin = true;
        l.file = lay::builtin(name);
        list.push_back(std::move(l));
    }
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(layouts_dir(), ec))
        if (e.is_regular_file() && e.path().extension() == L".json")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        LayoutSpec l;
        const auto stem = f.stem().u8string();
        l.name.assign(stem.begin(), stem.end());
        l.from_file = true;
        l.mtime = std::filesystem::last_write_time(f, ec);
        const Json j = Json::parse(read_text_file(f), nullptr, false);
        if (lay::is_legacy(j)) {
            l.legacy = true;
            l.name = j.value("name", l.name);
            l.file = lay::from_legacy(j);
            if (j.contains("details") && j["details"].is_boolean())
                l.details = j["details"].get<bool>();
        } else if (auto n = j.is_discarded() ? std::nullopt : lay::parse(j, &l.error)) {
            l.file = std::move(*n);
        } else {
            if (j.is_discarded())
                l.error = "not valid JSON";
            l.file = lay::builtin(l.name);  // window.rs 180-182: as shipped, main when not one
        }
        // window.rs 1222-1243 keep_ids: nodes without an id get one (here only in memory).
        int k = 0;
        std::set<std::string> taken;
        for (const lay::Node* n : l.file.walk())
            taken.insert(n->id);
        l.file.fill_ids([&] {
            std::string id;
            do
                id = "node-" + std::to_string(++k);
            while (taken.count(id) != 0);
            return id;
        });
        const auto it = std::find_if(list.begin(), list.end(), [&](const LayoutSpec& x) { return x.name == l.name; });
        if (it != list.end()) {
            l.builtin = it->builtin;
            *it = std::move(l);
        } else {
            list.push_back(std::move(l));
        }
    }
    for (auto& l : list)
        apply_layout_state(l, st);
    return list;
}

// ------------------------------------------------------------------ example mods (first run)
// Written to <data>\mods when that folder does not exist yet; ours, small, and meant to be read.
// The example mods (C++ DLLs built from mods\ in the source tree) ship next to agent-ui.exe in
// mods\<name>\. Copied into <data>\mods on first run, and into folders that still hold the old
// Lua example of the same name (its settings.json is kept). They stay off until enabled.
void write_example_mods() {
    wchar_t exe[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto shipped = std::filesystem::path(exe).parent_path() / L"mods";
    const auto dir = data_dir() / L"mods";
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(shipped, ec)) {
        if (!e.is_directory() || !std::filesystem::is_regular_file(e.path() / L"mod.dll", ec))
            continue;
        const auto to = dir / e.path().filename();
        if (std::filesystem::is_regular_file(to / L"mod.dll", ec))
            continue;  // already there (maybe the person's own build): leave it
        if (std::filesystem::exists(to, ec) && !std::filesystem::is_regular_file(to / L"main.lua", ec))
            continue;  // a folder of the person's own with that name
        std::filesystem::create_directories(to, ec);
        for (const wchar_t* f : {L"mod.dll", L"mod.json"})
            std::filesystem::copy_file(e.path() / f, to / f, std::filesystem::copy_options::overwrite_existing, ec);
        if (!std::filesystem::exists(to / L"settings.json", ec) &&
            std::filesystem::is_regular_file(e.path() / L"settings.json", ec))
            std::filesystem::copy_file(e.path() / L"settings.json", to / L"settings.json", ec);
        std::filesystem::remove(to / L"main.lua", ec);  // the Lua version it replaces
    }
}

// ------------------------------------------------------------------ programs
// herdr src/programs.rs: one table (defaults/programs.json, shipped in programs\ next to the exe)
// with the person's programs.json in the data folder on top; read again when that file changes,
// problems go to programs.error (herdr window.rs 982-991, 909-912).
ui::Programs g_programs;
std::filesystem::file_time_type g_programs_mtime{};
bool g_programs_loaded = false;

std::filesystem::path programs_dir() {
    wchar_t exe[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return std::filesystem::path(exe).parent_path() / L"programs";
}

// Returns the problem with the person's file, if any (empty: applied or no file).
std::string load_programs() {
    std::error_code ec;
    const auto mine = data_dir() / L"programs.json";
    g_programs_mtime = std::filesystem::last_write_time(mine, ec);
    std::string error;
    g_programs = ui::Programs::load(programs_dir() / L"programs.json", mine, &error);
    g_programs_loaded = true;
    const auto err_path = data_dir() / L"programs.error";
    if (error.empty())
        std::filesystem::remove(err_path, ec);
    else
        std::ofstream(err_path, std::ios::binary) << "programs.json was not applied: " << error << "\n";
    return error;
}

const ui::Programs& programs() {
    if (!g_programs_loaded)
        load_programs();
    return g_programs;
}

// What runs in a terminal (herdr programs::of): the agent's 7501 `app` first, else the
// foreground program's argv (its name alone from an older host).
ui::Program program_of(const ui::SessionView& v) {
    std::vector<std::string> argv = v.program_argv;
    if (argv.empty() && !v.program.empty())
        argv.push_back(v.program);
    return programs().of(v.agent_app, argv);
}

// The program a profile starts.
ui::Program program_of_profile(const std::string& executable, const std::string& id) {
    return programs().of({}, {executable.empty() ? id : executable});
}

// A person's icon (in the data folder) before the shipped one (herdr: both live in its home).
std::filesystem::path program_icon_file(const ui::Program& p) {
    if (p.icon.empty())
        return {};
    const auto rel = win::to_wide(p.icon);
    std::error_code ec;
    if (const auto mine = data_dir() / rel; std::filesystem::is_regular_file(mine, ec))
        return mine;
    return programs_dir() / rel;
}

ImU32 hex_color(const std::string& hex, ImU32 fallback) {
    if (hex.size() != 7 || hex[0] != '#')
        return fallback;
    const unsigned long v = std::strtoul(hex.c_str() + 1, nullptr, 16);
    return IM_COL32((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff, 255);
}

ui::ImageCache* g_program_images = nullptr;  // App::State's (set while the App lives)
ui::ImageCache& program_images() {
    static ui::ImageCache none;  // never used while an App lives
    return g_program_images != nullptr ? *g_program_images : none;
}

// herdr's icon(p, size, color) (pane/main.js 27-30, agents/main.js 78, tabs/main.js 120): a
// tinted entry is a one-color SVG in `color`, else the picture itself. `min` is its top left.
void program_icon(ImDrawList* dl, const ui::Program& p, ImVec2 min, float size, ImU32 color) {
    const auto file = program_icon_file(p);
    if (file.empty())
        return;
    const int side = std::max(1, static_cast<int>(size + 0.5f));
    const auto img = program_images().svg(file, side, side, p.tint);
    if (!img.ok())
        return;
    dl->AddImage(img.tex, min, ImVec2(min.x + size, min.y + size), ImVec2(0, 0), ImVec2(1, 1),
                 p.tint ? color : IM_COL32_WHITE);
}

bool is_shell(const std::string& program) {
    return program == "cmd" || program == "pwsh" || program == "powershell" || program == "bash" ||
           program == "wsl" || program == "nu";
}

// Profile ids that open a shell rather than an agent (WSL profiles are "wsl-<distribution>").
bool is_shell_profile(const std::string& id) {
    return is_shell(id) || id.starts_with("wsl-");
}

// ------------------------------------------------------------------ small helpers
std::string folder_name(const std::string& cwd) {
    std::string s = cwd;
    while (!s.empty() && (s.back() == '\\' || s.back() == '/'))
        s.pop_back();
    const auto p = s.find_last_of("\\/");
    return p == std::string::npos ? s : s.substr(p + 1);
}

std::string display_path(const std::string& cwd) {
    static const std::string home = [] {
        wchar_t buf[MAX_PATH]{};
        const DWORD n = ::GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
        return n > 0 ? win::to_utf8(std::wstring(buf, n)) : std::string();
    }();
    if (!home.empty() && cwd.size() >= home.size() && _strnicmp(cwd.c_str(), home.c_str(), home.size()) == 0)
        return "~" + cwd.substr(home.size());
    return cwd;
}

std::string elapsed_since(const std::string& iso) {
    std::tm tm{};
    std::istringstream in(iso);
    in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (in.fail())
        return {};
    const std::time_t t = _mkgmtime(&tm);
    const long long s = std::max<long long>(0, static_cast<long long>(std::time(nullptr) - t));
    if (s < 60)
        return std::to_string(s) + "s";
    if (s < 3600)
        return std::to_string(s / 60) + "m";
    if (s < 86400)
        return std::to_string(s / 3600) + "h";
    return std::to_string(s / 86400) + "d";
}

float text_clipped(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const std::string& text,
                   float max_w) {
    if (max_w <= 0 || text.empty())
        return 0;
    const ImVec2 full = font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str());
    if (full.x <= max_w) {
        dl->AddText(font, size, pos, col, text.c_str());
        return full.x;
    }
    const char* ell = "\xE2\x80\xA6";
    const float ell_w = font->CalcTextSizeA(size, FLT_MAX, 0, ell).x;
    std::size_t n = text.size();
    while (n > 0) {
        --n;
        while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
            --n;
        if (font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str(), text.c_str() + n).x + ell_w <= max_w)
            break;
    }
    const std::string cut = text.substr(0, n) + ell;
    dl->AddText(font, size, pos, col, cut.c_str());
    return font->CalcTextSizeA(size, FLT_MAX, 0, cut.c_str()).x;
}

ImU32 attention_color(Attention a) {
    switch (a) {
        case Attention::kBlocked:
            return g_pal->needs_you;
        case Attention::kDone:
            return g_pal->done;
        case Attention::kWorking:
            return g_pal->working;
        case Attention::kIdle:
            return g_pal->idle;
        case Attention::kNone:
            return g_pal->faint;
    }
    return g_pal->faint;
}

// Shape + color mark; never color alone (design doc 4.1). Working pulses gently.
void attention_mark(ImDrawList* dl, ImVec2 c, float r, Attention a) {
    const ImU32 col = attention_color(a);
    switch (a) {
        case Attention::kBlocked: {
            dl->AddCircleFilled(c, r * 1.15f, col);
            dl->AddLine(ImVec2(c.x, c.y - r * 0.55f), ImVec2(c.x, c.y + r * 0.15f), g_pal->card, px(1.6f));
            dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.55f), px(1.0f), g_pal->card);
            break;
        }
        case Attention::kDone:
            dl->AddCircleFilled(c, r * 1.1f, col);
            dl->AddLine(ImVec2(c.x - r * 0.5f, c.y), ImVec2(c.x - r * 0.1f, c.y + r * 0.45f), g_pal->card,
                        px(1.6f));
            dl->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.45f), ImVec2(c.x + r * 0.55f, c.y - r * 0.4f),
                        g_pal->card, px(1.6f));
            break;
        case Attention::kWorking: {
            const float t = static_cast<float>(ImGui::GetTime());
            const float a0 = t * 4.0f;
            dl->PathArcTo(c, r, a0, a0 + 4.2f, 16);
            dl->PathStroke(col, 0, px(1.8f));
            break;
        }
        case Attention::kIdle:
            dl->AddCircle(c, r * 0.8f, col, 0, px(1.4f));
            break;
        case Attention::kNone:
            (void)col;
            break;
    }
}

bool fuzzy_match(const std::string& text, const std::string& query) {
    std::size_t j = 0;
    for (std::size_t i = 0; i < text.size() && j < query.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) ==
            std::tolower(static_cast<unsigned char>(query[j])))
            ++j;
    }
    return j == query.size();
}

Dir parse_dir(const std::string& s) {
    if (s == "left")
        return Dir::kLeft;
    if (s == "up")
        return Dir::kUp;
    if (s == "down")
        return Dir::kDown;
    return Dir::kRight;
}

struct CommandInfo {
    const char* id;
    const char* label;
};

constexpr CommandInfo kCommands[] = {
    {"tab.new", "New terminal (tab)"},
    {"space.new", "New space (terminal)"},
    {"agent.new", "Start a program... (Claude Code, Codex, ...)"},
    {"pane.split:right", "Split right"},
    {"pane.split:down", "Split down"},
    {"pane.zoom", "Zoom / unzoom pane"},
    {"pane.close", "Close pane (ends its terminal)"},
    {"app.picker", "Find a space or agent"},
    {"sidebar.rail", "Collapse / expand the sidebar"},
    {"worktree.open", "Open worktree"},
    {"worktree.remove", "Remove worktree"},
    {"machine.add", "Add machine"},
    {"panel:files", "Show / hide the Files panel"},
    {"app.welcome", "Welcome"},
    {"app.whats_new", "What's new"},
    {"app.feedback", "Send feedback"},
    {"tab.settle", "Settle tab (put it away, it keeps running)"},
    {"tab.unsettle", "Bring back tabs put away"},
    {"terminal.right_click:program", "Send right-clicks to the pane"},
    {"terminal.right_click:menu", "Use the AgentDesk right-click menu"},
    {"layout.clone", "Duplicate layout"},
    {"layout.reset", "Reset layout"},
    {"mod.clone", "Duplicate mod"},
    {"app.reset", "Reset to the default screen"},
    {"terminal.kill", "End the focused terminal and its sub-agents"},
    {"panel:changes", "Show / hide the Changes panel"},
    {"pane.rename", "Rename pane"},
    {"tab.rename", "Rename tab"},
    {"space.rename", "Rename space"},
    {"tab.close", "Close tab (ends its terminals)"},
    {"space.close", "Close space (ends its terminals)"},
    {"tab.move:left", "Move tab left"},
    {"tab.move:right", "Move tab right"},
    {"tab.next", "Next tab"},
    {"tab.prev", "Previous tab"},
    {"space.next", "Next space"},
    {"space.prev", "Previous space"},
    {"agent.next", "Next agent"},
    {"agent.prev", "Previous agent"},
    {"pane.go:left", "Focus pane left"},
    {"pane.go:right", "Focus pane right"},
    {"pane.go:up", "Focus pane up"},
    {"pane.go:down", "Focus pane down"},
    {"pane.swap:left", "Swap pane left"},
    {"pane.swap:right", "Swap pane right"},
    {"pane.swap:up", "Swap pane up"},
    {"pane.swap:down", "Swap pane down"},
    {"terminal.search", "Find in terminal"},
    {"worktree.new", "New git worktree"},
    {"session.interrupt", "Send Ctrl+C to the focused session"},
    {"session.stop", "Stop the focused session"},
    {"session.force_stop", "Force stop the focused session"},
    {"session.restart", "Restart: new session in the same folder"},
    {"sidebar.toggle", "Show / hide sidebar"},
    {"details.toggle", "Show / hide details"},
    {"app.palette", "Command palette"},
    {"app.settings", "Settings"},
    {"app.help", "Keyboard shortcuts"},
    {"app.zoom_in", "Zoom in"},
    {"app.zoom_out", "Zoom out"},
    {"app.zoom_reset", "Reset zoom"},
    {"app.keys", "Edit keys.json"},
    {"layout.switch", "Next layout"},
    {"mods.reload", "Reload mods and layouts"},
    {"mods.folder", "Open the mods folder"},
    {"app.exit_all", "Exit and stop all sessions"},
};

bool known_command(std::string_view name);

// The commands AgentDesk itself has (herdr commands::COMMANDS).
bool builtin_command(std::string_view name) {
    static const std::set<std::string, std::less<>> kNames = {
        "agent.focus", "agent.keep", "agent.new", "agent.next", "agent.peek", "agent.prev", "app.exit_all",
        "app.feedback", "app.help", "app.keys", "app.palette", "app.picker", "app.reset", "app.settings",
        "app.welcome", "app.whats_new", "app.zoom_in", "app.zoom_out", "app.zoom_reset", "details.toggle",
        "launch", "layout", "layout.clone", "layout.reset", "layout.switch", "layout.toggle", "machine.add",
        "mod", "mod.clone", "mod.open", "mods.folder", "mods.reload", "pane.close", "pane.go", "pane.park",
        "pane.rename", "pane.resize", "pane.split", "pane.swap", "pane.zoom", "panel", "session.focus",
        "session.force_stop", "session.interrupt", "session.restart", "session.stop", "sidebar.rail",
        "sidebar.toggle", "space.close", "space.move", "space.new", "space.next", "space.prev", "space.rename",
        "space.select", "tab.close", "tab.move", "tab.new", "tab.next", "tab.prev", "tab.rename", "tab.select",
        "tab.settle", "tab.unsettle", "terminal.copy", "terminal.kill", "terminal.open", "terminal.park",
        "terminal.paste", "terminal.paste_image", "terminal.peek", "terminal.pin", "terminal.right_click",
        "terminal.scroll", "terminal.search", "terminal.select_all", "theme", "window.close", "window.maximize",
        "window.minimize", "worktree.new", "worktree.open", "worktree.remove", "app.quit", "overlay.close",
        // herdr commands.rs 98, 99, 105
        "app.check_updates", "app.update_core", "app.update"};
    return kNames.contains(name);
}

// Command names keys.json may bind (herdr validates actions and reports unknown ones). A
// "<mod>.<command>" action is accepted when that mod folder exists.
bool known_command(std::string_view name) {
    if (builtin_command(name))
        return true;
    // Herdr 98's commands (herdr-app defaults/mods/win98/README.md 12-17).
    static const std::set<std::string, std::less<>> kWin98 = {
        "win98.start",       "win98.taskman", "win98.new_tab", "win98.new_space", "win98.rename_tab", "win98.rename_space",
        "win98.run",         "win98.shutdown", "win98.about",  "win98.display",   "win98.welcome",    "win98.boot",
        "win98.mines",       "win98.sol",     "win98.mycomputer", "win98.network", "win98.bin",       "win98.datetime",
        "win98.logoff",      "win98.small_fonts", "win98.large_fonts", "win98.close_tab", "win98.close_space"};
    if (kWin98.contains(name))
        return true;
    const auto dot = name.find('.');
    std::error_code ec;
    return dot != std::string_view::npos &&
           std::filesystem::is_directory(data_dir() / L"mods" / win::to_wide(std::string(name.substr(0, dot))), ec);
}

const char* command_label(const std::string& id) {
    for (const auto& c : kCommands)
        if (id == c.id)
            return c.label;
    return nullptr;
}

}  // namespace

namespace {
// ------------------------------------------------------------------ git facts per folder
// herdr's spaces mod asks git for the checkout's top, its common dir (worktrees), the branch and
// ahead/behind of upstream; at most a few at a time, again when the folder changes and every
// 60 s (spaces.js 105-147, 229-264). Here one background thread runs git for queued folders.
// Like herdr's watchFolder (spaces.js 266-286) a second thread watches each local repo's git
// folder, so a commit or checkout made outside shows at once; the 60 s look again happens only
// while the window is in front (spaces.js 140).
struct GitInfo {
    bool repo = false;
    std::string top;         // checkout top (a worktree's own folder)
    std::string common_top;  // main checkout's top (differs for a linked worktree)
    std::string common_dir;  // the repo's own git folder (`git rev-parse --git-common-dir`)
    bool worktree = false;
    std::string branch;      // "HEAD" when detached
    int ahead = 0;
    int behind = 0;
};

class GitCache {
public:
    GitCache()
        : wake_(::CreateEventW(nullptr, FALSE, FALSE, nullptr)),
          worker_([this](std::stop_token st) { run(st); }),
          watcher_([this](std::stop_token st) { watch_loop(st); }) {}
    ~GitCache() {
        worker_.request_stop();
        watcher_.request_stop();
        cv_.notify_all();
        if (wake_ != nullptr)
            ::SetEvent(wake_);
        watcher_.join();
        worker_.join();
        if (wake_ != nullptr)
            ::CloseHandle(wake_);
    }
    GitCache(const GitCache&) = delete;
    GitCache& operator=(const GitCache&) = delete;

    // Another machine's folders: git through its agent-host (git.run), set by the app.
    std::function<std::optional<Json>(const std::string& machine, const std::string& method, const Json& params)> remote;

    // Cached facts for `dir` on `machine` ("" = here; empty until the first answer); schedules a
    // refresh when stale.
    GitInfo get(const std::string& dir, const std::string& machine = {}) {
        if (dir.empty())
            return {};
        const std::string key = machine.empty() ? dir : machine + "|" + dir;
        std::lock_guard lock(mutex_);
        auto& e = entries_[key];
        const auto now = Clock::now();
        e.used = now;
        // The fallback look (60 s) only while the window is in front (spaces.js 140-145).
        if (!e.queued && (e.at == Clock::time_point{} || (active_ && now - e.at > std::chrono::seconds(60)))) {
            e.queued = true;
            queue_.push_back(key);
            cv_.notify_one();
        }
        return e.info;
    }
    // Ask again soon (a command or an agent turn ended: a checkout or commit may have happened).
    void invalidate(const std::string& dir) {
        std::lock_guard lock(mutex_);
        if (auto it = entries_.find(dir); it != entries_.end())
            it->second.at = Clock::time_point{};
    }
    // `dir` and every other folder in its repo (spaces.js 149-153: a commit there moves their
    // ahead/behind too).
    void invalidate_repo(const std::string& dir, const std::string& machine = {}) {
        if (dir.empty())
            return;
        std::lock_guard lock(mutex_);
        const std::string key = machine.empty() ? dir : machine + "|" + dir;
        std::string common;
        if (const auto it = entries_.find(key); it != entries_.end()) {
            it->second.at = Clock::time_point{};
            common = it->second.info.common_top;
        } else {
            // A folder nobody asked about yet: the repo of a known checkout that holds it.
            for (const auto& [k, e] : entries_) {
                const std::string& top = e.info.top;
                if (machine_of(k) == machine && e.info.repo && dir.size() >= top.size() &&
                    _strnicmp(dir.c_str(), top.c_str(), top.size()) == 0 &&
                    (dir.size() == top.size() || dir[top.size()] == '\\' || dir[top.size()] == '/')) {
                    common = e.info.common_top;
                    break;
                }
            }
        }
        if (common.empty())
            return;
        for (auto& [k, e] : entries_)
            if (machine_of(k) == machine && e.info.repo && _stricmp(e.info.common_top.c_str(), common.c_str()) == 0)
                e.at = Clock::time_point{};
    }
    // The window is in front: the 60 s fallback runs only then.
    void set_active(bool active) { active_ = active; }

private:
    struct Entry {
        GitInfo info;
        Clock::time_point at{};
        bool queued = false;
        Clock::time_point used{};  // last asked for by the UI
    };

    static std::string machine_of(const std::string& key) {
        const auto bar = key.find('|');
        return bar == std::string::npos ? std::string() : key.substr(0, bar);
    }

    // Local repos' git folders, each with a change notification (spaces.js 266-286 watchFolder).
    // A change looks again at every folder of that repo; folders no longer asked about for
    // 10 minutes stop being watched. At most 62 at once (WaitForMultipleObjects).
    void watch_loop(std::stop_token st) {
        std::map<std::string, HANDLE> handles;
        while (!st.stop_requested()) {
            {
                std::set<std::string> wanted;
                {
                    std::lock_guard lock(mutex_);
                    const auto now = Clock::now();
                    for (const auto& [k, e] : entries_)
                        if (machine_of(k).empty() && e.info.repo && !e.info.common_dir.empty() &&
                            now - e.used < std::chrono::minutes(10))
                            wanted.insert(e.info.common_dir);
                }
                for (auto it = handles.begin(); it != handles.end();) {
                    if (!wanted.contains(it->first)) {
                        ::FindCloseChangeNotification(it->second);
                        it = handles.erase(it);
                    } else {
                        ++it;
                    }
                }
                for (const auto& dir : wanted) {
                    if (handles.size() >= 62 || handles.contains(dir))
                        continue;
                    const HANDLE h = ::FindFirstChangeNotificationW(
                            win::to_wide(dir).c_str(), TRUE,
                            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
                    if (h != INVALID_HANDLE_VALUE)
                        handles[dir] = h;
                }
            }
            std::vector<HANDLE> wait{wake_};
            std::vector<std::string> dirs{""};
            for (const auto& [d, h] : handles) {
                wait.push_back(h);
                dirs.push_back(d);
            }
            const DWORD r = ::WaitForMultipleObjects(static_cast<DWORD>(wait.size()), wait.data(), FALSE, 2000);
            if (st.stop_requested())
                break;
            if (r == WAIT_FAILED)
                std::this_thread::sleep_for(std::chrono::seconds(1));
            if (r <= WAIT_OBJECT_0 || r >= WAIT_OBJECT_0 + wait.size())
                continue;
            const std::string dir = dirs[r - WAIT_OBJECT_0];
            // A git command writes many files: let it finish, then look once.
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            ::FindNextChangeNotification(handles[dir]);
            std::lock_guard lock(mutex_);
            for (auto& [k, e] : entries_) {
                if (!machine_of(k).empty() || _stricmp(e.info.common_dir.c_str(), dir.c_str()) != 0 || e.queued)
                    continue;
                e.queued = true;
                queue_.push_back(k);
            }
            cv_.notify_one();
        }
        for (const auto& [d, h] : handles)
            ::FindCloseChangeNotification(h);
    }

    static std::string trim(std::string s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
            s.pop_back();
        return s;
    }
    static std::string norm(std::string p) {
        for (auto& c : p)
            if (c == '/')
                c = '\\';
        while (p.size() > 3 && p.back() == '\\')
            p.pop_back();
        return p;
    }

    GitInfo query(const std::string& key) {
        GitInfo g;
        const auto bar = key.find('|');
        const std::string machine = bar == std::string::npos ? std::string() : key.substr(0, bar);
        const std::string dir = bar == std::string::npos ? key : key.substr(bar + 1);
        const std::filesystem::path cwd(win::to_wide(dir));
        std::error_code ec;
        if (machine.empty() && !std::filesystem::is_directory(cwd, ec))
            return g;
        auto git = [&](std::vector<std::wstring> args) {
            if (!machine.empty()) {  // the remote host adds its own safety switches and -C
                host::ToolResult r;
                Json a = Json::array();
                for (const auto& x : args)
                    a.push_back(win::to_utf8(x));
                if (const auto res = remote ? remote(machine, "git.run", Json{{"cwd", dir}, {"args", a}}) : std::nullopt) {
                    r.started = true;
                    r.exit_code = res->value("exit_code", 1ul);
                    r.output = res->value("output", "");
                    r.error = res->value("error", "");
                }
                return r;
            }
            args.insert(args.begin(), {L"git", L"--no-optional-locks", L"-C", cwd.native()});
            return host::run_tool(args, cwd, std::chrono::seconds(10));
        };
        const auto r = git({L"rev-parse", L"--show-toplevel", L"--git-common-dir", L"--abbrev-ref", L"HEAD"});
        if (!r.started || r.exit_code != 0)
            return g;
        std::vector<std::string> lines;
        std::stringstream ss(r.output);
        for (std::string l; std::getline(ss, l);)
            lines.push_back(trim(l));
        if (lines.size() < 3)
            return g;
        g.repo = true;
        g.top = norm(lines[0]);
        std::filesystem::path common(win::to_wide(lines[1]));
        if (common.is_relative())
            common = cwd / common;
        common = machine.empty() ? std::filesystem::weakly_canonical(common, ec) : common.lexically_normal();
        g.common_top = norm(win::to_utf8(common.parent_path().native()));
        g.common_dir = norm(win::to_utf8(common.native()));
        g.worktree = _stricmp(g.common_top.c_str(), g.top.c_str()) != 0;
        g.branch = lines[2];
        const auto c = git({L"rev-list", L"--left-right", L"--count", L"HEAD...@{upstream}"});
        if (c.started && c.exit_code == 0) {
            std::stringstream cs(c.output);
            cs >> g.ahead >> g.behind;
        }
        return g;
    }

    void run(std::stop_token st) {
        while (!st.stop_requested()) {
            std::string dir;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, st, [&] { return !queue_.empty(); });
                if (st.stop_requested())
                    return;
                dir = queue_.front();
                queue_.pop_front();
            }
            const GitInfo info = query(dir);
            std::lock_guard lock(mutex_);
            auto& e = entries_[dir];
            const bool new_dir = e.info.common_dir != info.common_dir;
            e.info = info;
            e.at = Clock::now();
            e.queued = false;
            if (new_dir && wake_ != nullptr)
                ::SetEvent(wake_);  // a repo to watch
        }
    }

    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<std::string> queue_;
    std::map<std::string, Entry> entries_;
    std::atomic<bool> active_{true};
    HANDLE wake_ = nullptr;
    std::jthread worker_;
    std::jthread watcher_;
};
// ------------------------------------------------------------------ Files / Changes panels
// herdr-app mods/files and mods/changes (docs/research/herdr-app/spec-files-changes.md): both
// follow the focused pane's live folder, refresh every 10 s and when the folder changes on
// disk, and never write under .git while reading. Git and file reads run on one worker thread.
class SidePanels {
public:
    struct FileEntry {
        std::string name;
        bool dir = false;
    };
    struct Change {
        char letter = 'M';
        std::string path;  // relative to the repo top, '/' separators
        int add = 0;
        int del = 0;
        bool binary = false;
        bool untracked = false;
    };
    struct DiffRow {
        enum Kind { kContext, kAdd, kDel, kHunk, kOther } kind = kContext;
        int number = 0;
        std::string text;
    };

    SidePanels() : worker_([this](std::stop_token st) { run(st); }) {}
    ~SidePanels() {
        worker_.request_stop();
        cv_.notify_all();
    }
    SidePanels(const SidePanels&) = delete;
    SidePanels& operator=(const SidePanels&) = delete;

    // Folders on another machine go through its agent-host (fs.list, fs.read, git.run). Runs on
    // the panel worker; returns the result, or nullopt when the machine did not answer.
    using RemoteCall = std::function<std::optional<Json>(const std::string& machine, const std::string& method,
                                                          const Json& params)>;
    RemoteCall remote;

    std::mutex mutex;  // guards everything below
    // Files
    std::string files_root;
    std::string files_machine;  // "" = this computer
    std::set<std::string> expanded;                  // relative folder paths ("" = root)
    std::map<std::string, std::vector<FileEntry>> listing;
    std::map<std::string, char> marks;               // relative file path (lower case) -> A/M/D/U
    std::string files_error;
    bool files_loading = false;
    bool files_limited = false;
    std::string preview_path;  // relative; empty = tree
    std::string preview_text;
    std::string preview_message;
    // Selection in the read-only preview: (line, byte) ends; line -1 = none.
    int sel_line_a = -1;
    std::size_t sel_byte_a = 0;
    int sel_line_b = -1;
    std::size_t sel_byte_b = 0;
    bool sel_dragging = false;
    // Changes
    std::string changes_cwd;
    std::string changes_machine;
    std::string repo_top;
    bool not_repo = false;
    std::vector<Change> changes;
    bool changes_more = false;
    std::string changes_error;
    bool changes_loading = false;
    std::string open_diff;  // path of the open file
    bool user_closed = false;
    std::vector<DiffRow> diff;
    std::string diff_message;
    std::string commit_note;
    bool committing = false;
    char commit_msg[512] = {};

    // Called every frame with the current folder; refreshes when it changed or every 10 s.
    void track_files(const std::string& root, const std::string& machine = {}) {
        std::lock_guard lock(mutex);
        const auto now = Clock::now();
        if (root != files_root || machine != files_machine) {
            files_contexts_[files_machine + "|" + files_root] = {expanded, preview_path};
            files_root = root;
            files_machine = machine;
            const auto it = files_contexts_.find(machine + "|" + root);
            expanded = it != files_contexts_.end() ? it->second.first : std::set<std::string>{""};
            expanded.insert("");
            preview_path = it != files_contexts_.end() ? it->second.second : std::string();
            listing.clear();
            marks.clear();
            files_error.clear();
            files_at_ = {};
            watch(machine.empty() ? root : std::string());  // remote folders refresh every 10 s
        }
        if (!files_loading && (files_at_ == Clock::time_point{} || now - files_at_ > std::chrono::seconds(10) ||
                               files_changed_.exchange(false))) {
            files_loading = true;
            files_at_ = now;
            post([this, mc = machine, root, folders = expanded, preview = preview_path] {
                load_files(mc, root, folders, preview);
            });
        }
    }
    void track_changes(const std::string& cwd, const std::string& machine = {}) {
        std::lock_guard lock(mutex);
        const auto now = Clock::now();
        if (cwd != changes_cwd || machine != changes_machine) {
            changes_cwd = cwd;
            changes_machine = machine;
            repo_top.clear();
            not_repo = false;
            changes.clear();
            open_diff.clear();
            user_closed = false;
            diff.clear();
            diff_message.clear();
            commit_note.clear();
            commit_msg[0] = 0;
            changes_at_ = {};
            watch(machine.empty() ? cwd : std::string());
        }
        if (!changes_loading && !committing &&
            (changes_at_ == Clock::time_point{} || now - changes_at_ > std::chrono::seconds(10) ||
             changes_changed_.exchange(false))) {
            changes_loading = true;
            changes_at_ = now;
            post([this, mc = machine, cwd, open = open_diff, closed = user_closed] { load_changes(mc, cwd, open, closed); });
        }
    }
    void toggle_folder(const std::string& rel) {
        std::lock_guard lock(mutex);
        if (expanded.contains(rel)) {
            // Collapse it and every expanded folder below it; drop their listings.
            for (auto it = expanded.begin(); it != expanded.end();) {
                if (*it == rel || it->rfind(rel + "/", 0) == 0) {
                    listing.erase(*it);
                    it = expanded.erase(it);
                } else {
                    ++it;
                }
            }
            return;
        }
        expanded.insert(rel);
        post([this, mc = files_machine, root = files_root, rel] {
            auto entries = list_folder(mc, root, rel);
            std::lock_guard l(mutex);
            if (root == files_root && mc == files_machine)
                listing[rel] = std::move(entries.first);
        });
    }
    void open_file(const std::string& rel) {
        std::lock_guard lock(mutex);
        preview_path = rel;
        preview_text.clear();
        preview_message = "Loading file\xE2\x80\xA6";
        sel_line_a = sel_line_b = -1;
        sel_dragging = false;
        post([this, mc = files_machine, root = files_root, rel] { load_preview(mc, root, rel); });
    }
    void close_preview() {
        std::lock_guard lock(mutex);
        preview_path.clear();
    }
    void select_change(const std::string& path) {
        std::lock_guard lock(mutex);
        if (open_diff == path) {
            open_diff.clear();
            user_closed = true;
            diff.clear();
            diff_message.clear();
            return;
        }
        open_diff = path;
        user_closed = false;
        diff.clear();
        diff_message = "Loading\xE2\x80\xA6";
        Change c;
        for (const auto& x : changes)
            if (x.path == path)
                c = x;
        post([this, mc = changes_machine, top = repo_top, c] { load_diff(mc, top, c); });
    }
    void commit() {
        std::lock_guard lock(mutex);
        // herdr changes/main.js commit(): this.message.trim()
        std::string msg = commit_msg;
        const auto first = msg.find_first_not_of(" \t\r\n");
        msg = first == std::string::npos ? std::string() : msg.substr(first, msg.find_last_not_of(" \t\r\n") - first + 1);
        if (msg.empty() || repo_top.empty() || committing || changes_loading)
            return;
        committing = true;
        commit_note.clear();
        post([this, mc = changes_machine, top = repo_top, msg] {
            auto r = git_run(mc, top, {L"add", L"-A"});
            if (r.started && r.exit_code == 0)
                r = git_run(mc, top, {L"commit", L"-m", win::to_wide(msg)});
            std::lock_guard l(mutex);
            committing = false;
            if (r.started && r.exit_code == 0) {
                commit_msg[0] = 0;
            } else {
                // (stderr || stdout).trim(), as herdr shows it
                commit_note = "Commit failed: " + (r.started ? r.message() : std::string("git not found"));
            }
            changes_at_ = {};
        });
    }

private:
    static std::string join(const std::string& root, const std::string& rel) {
        if (rel.empty())
            return root;
        std::string p = root;
        if (!p.empty() && p.back() != '\\')
            p += '\\';
        for (const char c : rel)
            p += c == '/' ? '\\' : c;
        return p;
    }
    static std::string lower(std::string s) {
        for (auto& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    static constexpr std::size_t kPerFolder = 500;
    static constexpr std::size_t kTotal = 5000;

    // Hidden and system files included, only .git skipped, folders first then by name.
    std::pair<std::vector<FileEntry>, bool> list_folder(const std::string& mc, const std::string& root,
                                                        const std::string& rel, bool* failed = nullptr) {
        std::vector<FileEntry> out;
        bool more = false;
        if (!mc.empty()) {
            const auto r = remote ? remote(mc, "fs.list", Json{{"path", join(root, rel)}, {"max", kPerFolder}})
                                  : std::nullopt;
            if (!r) {
                if (failed != nullptr)
                    *failed = true;
                return {out, false};
            }
            for (const auto& e : r->value("entries", Json::array()))
                out.push_back({e.value("name", ""), e.value("dir", false)});
            more = r->value("more", false);
            std::sort(out.begin(), out.end(), [](const FileEntry& a, const FileEntry& b) {
                if (a.dir != b.dir)
                    return a.dir;
                return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
            });
            return {out, more};
        }
        std::error_code ec;
        std::filesystem::directory_iterator it(win::to_wide(join(root, rel)),
                                               std::filesystem::directory_options::skip_permission_denied, ec);
        for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            const std::string name = win::to_utf8(it->path().filename().native());
            if (name == ".git")
                continue;
            if (out.size() >= kPerFolder) {
                more = true;
                break;
            }
            std::error_code e2;
            out.push_back({name, it->is_directory(e2)});
        }
        std::sort(out.begin(), out.end(), [](const FileEntry& a, const FileEntry& b) {
            if (a.dir != b.dir)
                return a.dir;
            return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
        });
        return {out, more};
    }

    // git in `cwd`, here or on machine `mc` (its agent-host runs it with the same switches).
    host::ToolResult git_run(const std::string& mc, const std::string& cwd, std::vector<std::wstring> args) {
        if (!mc.empty()) {
            host::ToolResult r;
            Json a = Json::array();
            for (const auto& x : args)
                a.push_back(win::to_utf8(x));
            const auto res = remote ? remote(mc, "git.run", Json{{"cwd", cwd}, {"args", a}}) : std::nullopt;
            if (res) {
                r.started = true;
                r.exit_code = res->value("exit_code", 1ul);
                r.output = res->value("output", "");
                r.error = res->value("error", "");
            }
            return r;
        }
        const std::filesystem::path p(win::to_wide(cwd));
        args.insert(args.begin(), {L"git", L"--no-optional-locks", L"-c", L"core.fsmonitor=false", L"-c",
                                   L"diff.autoRefreshIndex=false", L"-C", p.native()});
        return host::run_tool(args, p, std::chrono::seconds(60));
    }
    std::string git_out(const std::string& mc, const std::string& cwd, std::vector<std::wstring> args,
                        bool* ok = nullptr) {
        const auto r = git_run(mc, cwd, std::move(args));
        if (ok != nullptr)
            *ok = r.started && r.exit_code == 0;
        return r.output;
    }
    std::string top_of(const std::string& mc, const std::string& cwd) {
        bool ok = false;
        std::string out = git_out(mc, cwd, {L"rev-parse", L"--show-toplevel"}, &ok);
        if (!ok)
            return {};
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
            out.pop_back();
        for (auto& c : out)
            if (c == '/')
                c = '\\';
        return out;
    }
    // porcelain v1 -z: "XY path\0"
    std::vector<std::pair<std::string, std::string>> status(const std::string& mc, const std::string& top, bool* ok) {
        const std::string out = git_out(mc, top, {L"status", L"--porcelain=v1", L"-z", L"--no-renames",
                                              L"--untracked-files=all"},
                                        ok);
        std::vector<std::pair<std::string, std::string>> r;
        std::size_t i = 0;
        while (i + 3 < out.size()) {
            const std::size_t z = out.find('\0', i);
            if (z == std::string::npos)
                break;
            r.emplace_back(out.substr(i, 2), out.substr(i + 3, z - i - 3));
            i = z + 1;
        }
        return r;
    }
    // Changes list (herdr changes/main.js): conflicts first.
    static char letter_of(const std::string& xy) {
        if (xy.find('U') != std::string::npos || xy == "AA" || xy == "DD")
            return 'U';
        if (xy.find('D') != std::string::npos)
            return 'D';
        if (xy == "??" || xy.find('A') != std::string::npos)
            return 'A';
        return 'M';
    }
    // Files tree marks (herdr files/main.js:99): added first, then U, D, M.
    static char file_mark_of(const std::string& xy) {
        if (xy == "??" || xy.find('A') != std::string::npos)
            return 'A';
        if (xy.find('U') != std::string::npos)
            return 'U';
        if (xy.find('D') != std::string::npos)
            return 'D';
        return 'M';
    }

    void load_files(const std::string& mc, const std::string& root, std::set<std::string> folders,
                    const std::string& preview) {
        std::map<std::string, std::vector<FileEntry>> got;
        std::string error;
        bool limited = false;
        std::size_t total = 0;
        for (const auto& rel : folders) {
            bool failed = false;
            auto [entries, more] = list_folder(mc, root, rel, &failed);
            if (rel.empty() && entries.empty()) {
                std::error_code ec;
                if (failed || (mc.empty() && !std::filesystem::is_directory(win::to_wide(root), ec)))
                    error = mc.empty() ? "Cannot list this folder" : "Cannot list this folder on " + mc;
            }
            limited = limited || more || total + entries.size() > kTotal;
            if (total + entries.size() > kTotal)
                entries.resize(kTotal - std::min(total, kTotal));
            total += entries.size();
            got[rel] = std::move(entries);
        }
        // Git marks: keyed by path relative to `root` (normalised; herdr's string join misses on Windows).
        std::map<std::string, char> m;
        const std::string top = top_of(mc, root);
        if (!top.empty()) {
            bool ok = false;
            const auto st = status(mc, top, &ok);
            std::string prefix = lower(root.size() > top.size() ? root.substr(top.size() + 1) : std::string());
            for (auto& c : prefix)
                if (c == '\\')
                    c = '/';
            std::size_t n = 0;
            for (const auto& [xy, path] : st) {
                if (++n > 5000)
                    break;
                std::string rel = lower(path);
                if (!prefix.empty()) {
                    if (rel.rfind(prefix + "/", 0) != 0)
                        continue;
                    rel = rel.substr(prefix.size() + 1);
                }
                m[rel] = file_mark_of(xy);
            }
        }
        std::lock_guard lock(mutex);
        if (root != files_root || mc != files_machine) {
            files_loading = false;
            return;
        }
        for (auto& [rel, entries] : got)
            if (expanded.contains(rel))
                listing[rel] = std::move(entries);
        marks = std::move(m);
        files_error = error;
        files_limited = limited;
        files_loading = false;
        if (!preview.empty() && preview == preview_path && preview_text.empty())
            post([this, mc, root, preview] { load_preview(mc, root, preview); });
    }

    bool read_text(const std::string& mc, const std::string& path, std::string& out, std::string& message) {
        if (!mc.empty()) {
            const auto r = remote ? remote(mc, "fs.read", Json{{"path", path}}) : std::nullopt;
            if (!r || r->value("missing", false)) {
                message = r ? "This file is no longer available" : "No answer from " + mc;
                return false;
            }
            if (r->value("too_large", false)) {
                message = "Cannot preview this file (binary, too large, or unreadable)";
                return false;
            }
            if (r->value("binary", false)) {
                message = "Binary file \xC2\xB7 no text preview";
                return false;
            }
            out = r->value("text", "");
            return true;
        }
        std::error_code ec;
        const auto size = std::filesystem::file_size(win::to_wide(path), ec);
        if (ec) {
            message = "This file is no longer available";
            return false;
        }
        if (size > 1024 * 1024) {
            message = "Cannot preview this file (binary, too large, or unreadable)";
            return false;
        }
        std::ifstream in(std::filesystem::path(win::to_wide(path)), std::ios::binary);
        out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (out.find('\0') != std::string::npos) {
            message = "Binary file \xC2\xB7 no text preview";
            return false;
        }
        return true;
    }

    void load_preview(const std::string& mc, const std::string& root, const std::string& rel) {
        std::string text, message;
        if (read_text(mc, join(root, rel), text, message) && text.size() > 400000) {
            text.resize(400000);
            text += "\n\xE2\x80\xA6 (file truncated)";
        }
        std::lock_guard lock(mutex);
        if (root == files_root && mc == files_machine && rel == preview_path) {
            preview_text = std::move(text);
            preview_message = std::move(message);
        }
    }

    void load_changes(const std::string& mc, const std::string& cwd, std::string open, bool closed) {
        const std::string top = top_of(mc, cwd);
        if (top.empty()) {
            std::lock_guard lock(mutex);
            if (cwd == changes_cwd && mc == changes_machine) {
                not_repo = true;
                repo_top.clear();
                changes.clear();
                changes_loading = false;
            }
            return;
        }
        bool ok = false;
        const auto st = status(mc, top, &ok);
        std::string error = ok ? std::string() : std::string("git status failed");
        bool has_head = false;
        git_out(mc, top, {L"rev-parse", L"--verify", L"HEAD"}, &has_head);
        std::map<std::string, std::pair<int, int>> counts;
        std::set<std::string> binary;
        auto numstat = [&](std::vector<std::wstring> args) {
            const std::string out = git_out(mc, top, std::move(args));
            std::size_t i = 0;
            while (i < out.size()) {
                const std::size_t z = out.find('\0', i);
                if (z == std::string::npos)
                    break;
                const std::string rec = out.substr(i, z - i);
                i = z + 1;
                std::stringstream ss(rec);
                std::string a, d, p;
                std::getline(ss, a, '\t');
                std::getline(ss, d, '\t');
                std::getline(ss, p);
                if (a == "-") {
                    binary.insert(p);
                    continue;
                }
                counts[p].first += std::atoi(a.c_str());
                counts[p].second += std::atoi(d.c_str());
            }
        };
        if (has_head) {
            numstat({L"diff", L"HEAD", L"--numstat", L"-z", L"--no-renames"});
        } else {
            numstat({L"diff", L"--cached", L"--numstat", L"-z", L"--no-renames"});
            numstat({L"diff", L"--numstat", L"-z", L"--no-renames"});
        }
        std::vector<Change> list;
        bool more = false;
        for (const auto& [xy, path] : st) {
            if (list.size() >= 1000) {
                more = true;
                break;
            }
            Change c;
            c.letter = letter_of(xy);
            c.path = path;
            c.binary = binary.contains(path);
            c.untracked = xy == "??" || (!has_head && xy.find('A') != std::string::npos);
            if (c.untracked) {
                std::string text, msg;
                if (read_text(mc, join(top, path), text, msg))
                    c.add = static_cast<int>(std::count(text.begin(), text.end(), '\n') +
                                             (!text.empty() && text.back() != '\n' ? 1 : 0));
            } else if (const auto it = counts.find(path); it != counts.end()) {
                c.add = it->second.first;
                c.del = it->second.second;
            }
            list.push_back(std::move(c));
        }
        // Keep the open diff; open the first file when nothing is open and the user did not close it.
        Change to_open;
        bool load = false;
        if (!open.empty()) {
            for (const auto& c : list)
                if (c.path == open) {
                    to_open = c;
                    load = true;
                }
        } else if (!closed && !list.empty()) {
            to_open = list.front();
            load = true;
        }
        {
            std::lock_guard lock(mutex);
            if (cwd != changes_cwd || mc != changes_machine) {
                changes_loading = false;
                return;
            }
            repo_top = top;
            not_repo = false;
            changes = std::move(list);
            changes_more = more;
            changes_error = error;
            changes_loading = false;
            if (!load) {
                if (!open.empty()) {
                    open_diff.clear();
                    diff.clear();
                    diff_message.clear();
                }
                return;
            }
            open_diff = to_open.path;
        }
        load_diff(mc, top, to_open);
    }

    static std::string untab(const std::string& s) {
        std::string out;
        int col = 0;
        for (const char c : s) {
            if (c == '\t') {
                do {
                    out += ' ';
                    ++col;
                } while (col % 4 != 0);
            } else {
                out += c;
                if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
                    ++col;
            }
        }
        return out.empty() ? std::string(" ") : out;
    }
    static std::string cut(std::string s) {
        if (s.size() > 4000) {
            s.resize(4000);
            s += "\xE2\x80\xA6 (line truncated)";
        }
        return s;
    }

    void load_diff(const std::string& mc, const std::string& top, const Change& c) {
        std::vector<DiffRow> rows;
        std::string message;
        if (c.binary) {
            message = "Binary file changed";
        } else if (c.untracked) {
            std::string text, msg;
            if (!read_text(mc, join(top, c.path), text, msg)) {
                message = msg.find("Binary") != std::string::npos ? "Binary file \xC2\xB7 no text diff" : msg;
            } else {
                if (text.size() > 400000)
                    text.resize(400000);
                std::stringstream ss(text);
                int n = 0;
                std::vector<std::string> lines;
                for (std::string l; std::getline(ss, l) && lines.size() < 1500;)
                    lines.push_back(l);
                rows.push_back({DiffRow::kHunk, 0, "@@ -0,0 +1," + std::to_string(lines.size()) + " @@"});
                for (auto& l : lines) {
                    if (!l.empty() && l.back() == '\r')
                        l.pop_back();
                    rows.push_back({DiffRow::kAdd, ++n, cut(untab(l))});
                }
            }
        } else {
            bool ok = false;
            bool has_head = false;
            git_out(mc, top, {L"rev-parse", L"--verify", L"HEAD"}, &has_head);
            std::vector<std::wstring> args = {L"diff"};
            if (has_head)
                args.push_back(L"HEAD");
            for (const wchar_t* a : {L"--no-ext-diff", L"--no-textconv", L"--no-color", L"--"})
                args.push_back(a);
            args.push_back(win::to_wide(c.path));
            const std::string out = git_out(mc, top, args, &ok);
            if (!ok) {
                message = "Cannot preview this diff (too large or unreadable)";
            } else {
                std::stringstream ss(out);
                bool body = false;
                int old_n = 0, new_n = 0;
                for (std::string l; std::getline(ss, l) && rows.size() < 1500;) {
                    if (!l.empty() && l.back() == '\r')
                        l.pop_back();
                    if (l.rfind("@@", 0) == 0) {
                        body = true;
                        int a = 0, b2 = 0;
                        sscanf_s(l.c_str(), "@@ -%d%*[^+]+%d", &a, &b2);
                        old_n = a;
                        new_n = b2;
                        rows.push_back({DiffRow::kHunk, 0, l});
                        continue;
                    }
                    if (!body)
                        continue;
                    if (!l.empty() && l[0] == '+')
                        rows.push_back({DiffRow::kAdd, new_n++, cut(untab(l.substr(1)))});
                    else if (!l.empty() && l[0] == '-')
                        rows.push_back({DiffRow::kDel, old_n++, cut(untab(l.substr(1)))});
                    else if (!l.empty() && l[0] == ' ') {
                        rows.push_back({DiffRow::kContext, new_n++, cut(untab(l.substr(1)))});
                        ++old_n;
                    } else
                        rows.push_back({DiffRow::kOther, 0, l});
                }
                if (rows.empty())
                    message = "No text diff available";
            }
        }
        std::lock_guard lock(mutex);
        if (top == repo_top && c.path == open_diff) {
            diff = std::move(rows);
            diff_message = std::move(message);
        }
    }

    // A recursive change notification on the watched folder (150 ms batching).
    void watch(const std::string& dir) {
        watch_dir_ = dir;
        cv_.notify_all();
    }

    void post(std::function<void()> fn) {
        tasks_.push_back(std::move(fn));
        cv_.notify_all();
    }

    void run(std::stop_token st) {
        std::string watching;
        HANDLE change = INVALID_HANDLE_VALUE;
        while (!st.stop_requested()) {
            std::function<void()> task;
            std::string want;
            {
                std::unique_lock lock(mutex);
                cv_.wait_for(lock, st, std::chrono::milliseconds(150), [&] { return !tasks_.empty(); });
                if (st.stop_requested())
                    break;
                want = watch_dir_;
                if (!tasks_.empty()) {
                    task = std::move(tasks_.front());
                    tasks_.pop_front();
                }
            }
            if (want != watching) {
                if (change != INVALID_HANDLE_VALUE)
                    ::FindCloseChangeNotification(change);
                change = want.empty() ? INVALID_HANDLE_VALUE
                                      : ::FindFirstChangeNotificationW(
                                            win::to_wide(want).c_str(), TRUE,
                                            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
                watching = want;
            }
            if (change != INVALID_HANDLE_VALUE && ::WaitForSingleObject(change, 0) == WAIT_OBJECT_0) {
                files_changed_ = true;
                changes_changed_ = true;
                ::FindNextChangeNotification(change);
            }
            if (task)
                task();
        }
        if (change != INVALID_HANDLE_VALUE)
            ::FindCloseChangeNotification(change);
    }

    std::map<std::string, std::pair<std::set<std::string>, std::string>> files_contexts_;
    Clock::time_point files_at_{};
    Clock::time_point changes_at_{};
    std::atomic<bool> files_changed_{false};
    std::atomic<bool> changes_changed_{false};
    std::string watch_dir_;
    std::deque<std::function<void()>> tasks_;
    std::condition_variable_any cv_;
    std::jthread worker_;
};
}  // namespace

// ------------------------------------------------------------------ state
struct ViewState {
    ui::TerminalView view;
    bool focus_next = false;
};

struct Toast {
    std::string title;
    std::string body;
    std::string session;
    ImU32 color = 0;
    Clock::time_point until;
};

enum class LaunchTarget { kTab, kSpace, kSplitRight, kSplitDown };

struct Placement {
    std::string machine;  // "" = this PC
    std::string session;
    LaunchTarget target = LaunchTarget::kTab;
    int space = -1;
    std::string split_of;
    std::string replace;  // restart in place
    std::string space_name;
    std::string tab_name;  // herdr tab.new {name} (Herdr 98 names its tabs)
};

struct WorktreeEntry {
    std::string path;
    std::string branch;  // empty: detached
    bool main = false;
    long long time = 0;  // last commit (unix seconds)
    std::string age;     // "5m", "3h", "2d", "4mo"
};

struct Remote {
    MachineConfig cfg;
    std::shared_ptr<ui::HostClient> host;  // shared: the panel worker may hold it during a request
};

struct App::State {
    AppConfig config;
    std::unique_ptr<ui::HostClient> host;           // this PC
    std::vector<Remote> remotes;                    // other machines over ssh
    std::mutex remotes_mutex;                       // remotes is replaced on the UI thread, read by the panel worker
    std::map<std::string, std::string> machine_of;  // session id -> machine name ("" here)
    std::string launch_machine;
    Settings settings;
    ui::Keymap keymap;
    std::vector<std::string> keymap_errors;
    std::filesystem::file_time_type keys_mtime{};
    Clock::time_point keys_checked{};
    ui::FontSet fonts;
    float dpi = 1.0f;
    bool window_focused = true;
    bool exit_now = false;
    // Updates and Privacy (herdr update.rs, telemetry.rs, settings main.js 760-882).
    std::optional<Clock::time_point> updates_asked;  // main.js 65-71 `this.asked`
    bool show_data = false;                          // main.js 870 `this.showData`
    std::optional<std::string> update_dismissed;     // window.rs 4840
    std::optional<std::string> core_update_error_dismissed;  // window.rs 3974
    std::string noted_host;                          // host instance whose version updates.cpp has
    bool was_connected = false;
    bool window_opened = false;
    Clock::time_point observed_at{};
    ImFont* report_font = nullptr;  // JetBrains Mono for the report lines (main.js 840)

    ui::Workspace ws;
    std::set<std::string> hidden;  // sessions the user closed the pane of (still running)
    std::set<std::string> known;   // placed, hidden, or deliberately skipped
    bool doc_loaded = false;
    std::string last_doc;
    Clock::time_point doc_dirty_at{};
    bool doc_dirty = false;
    std::string last_sent_doc;

    std::map<std::string, ViewState> views;
    ui::AttentionTracker attention;
    std::map<std::string, std::string> prev_process_state;
    std::map<std::string, Clock::time_point> launched_here;
    // Created by this window but not yet in the host's session list (the create reply comes
    // before the next list refresh): kept in the layout for a while.
    std::map<std::string, Clock::time_point> awaiting_list;
    std::deque<Toast> toasts;
    std::vector<std::string> agent_order;
    std::string peek;  // sub-agent shown in the floating peek window
    std::unique_ptr<ui::ModHost> mods;
    std::vector<std::string> pending_mod_commands;
    std::vector<LayoutSpec> layouts;
    std::map<std::string, std::string> last_agent_state;  // for mod events
    Clock::time_point next_tick{};
    [[nodiscard]] const LayoutSpec& layout() const {
        for (const auto& l : layouts)
            if (l.name == settings.layout)
                return l;
        return layouts.front();
    }
    LayoutSpec& layout() {
        return const_cast<LayoutSpec&>(std::as_const(*this).layout());
    }
    ui::layout::Placement placed;  // the shown layout, as this frame drew it
    struct LayoutDrag {
        std::string container;
        int index = 0;
        ui::layout::Axis axis = ui::layout::Axis::kRow;
    };
    std::optional<LayoutDrag> layout_drag;  // window.rs Drag: a separator held down

    std::mutex placements_mutex;
    std::vector<Placement> placements;      // completed creates waiting for the UI thread
    std::vector<std::string> async_errors;  // from worker callbacks

    // per-frame
    std::vector<std::shared_ptr<ui::RemoteSession>> sessions;
    std::map<std::string, ui::SessionView> views_by_id;
    std::map<std::string, std::shared_ptr<ui::RemoteSession>> by_id;
    ui::Rect pane_area{};
    bool started_dev = false;
    std::size_t next_dev_command = 0;
    Clock::time_point next_dev_at{};
    std::size_t next_input = 0;  // --input step
    Clock::time_point input_at{};
    bool input_shot_wait = false;
    std::optional<ImVec2> input_mouse;  // last scripted mouse position, re-applied every frame
    int input_up = -1;                  // button to release on a later frame (a real click spans frames)
    int input_clicks_left = 0;          // dblclick: the second press
    Clock::time_point input_shot_since{};

    // overlays
    bool show_palette = false;
    char palette_buf[256] = {};
    int palette_sel = 0;
    bool show_help = false;
    bool show_settings = false;
    bool show_exit = false;
    std::string exit_error;
    bool show_close_notice = false;
    bool show_launcher = false;
    LaunchTarget launch_target = LaunchTarget::kTab;
    std::string launch_split_of;
    char launch_cwd[512] = {};
    char launch_name[128] = {};
    int launcher_sel = 0;
    std::string launch_error;
    bool launch_busy = false;
    bool show_rename = false;
    std::string rename_kind;  // tab, space, pane
    std::string rename_target;
    char rename_buf[128] = {};
    bool rename_open = false;   // the prompt is showing
    bool rename_close = false;  // close it on the next frame (rename pressed again)
    bool show_worktree = false;
    char wt_repo[512] = {};
    char wt_branch[128] = {};
    int wt_profile = 0;
    std::string wt_error;
    bool wt_busy = false;
    bool open_popup_next = false;
    std::string popup_to_open;
    // herdr chrome
    GitCache git;
    std::unique_ptr<SidePanels> panels = std::make_unique<SidePanels>();
    std::set<std::string> folded_machines;
    ImFont* base_ui = nullptr;  // the bundled interface fonts (Geist), restored when ui_font is cleared
    ImFont* base_ui_medium = nullptr;
    ImFont* base_ui_bold = nullptr;
    struct TabWheel {
        float acc = 0;
        int steps = 0;
        Clock::time_point last{}, switched{};
    } tab_wheel;
    std::set<std::string> folded_groups;
    std::set<std::string> folded_agents;
    // Each agent's last state and when it changed (ms since 1970), kept across restarts in
    // ui.json (herdr agents.js 85-110).
    struct Changed {
        std::string s;
        long long at = 0;
    };
    std::map<std::string, Changed> changed;
    bool changed_loaded = false;
    std::map<std::string, bool> busy_terminal;  // a command or an agent turn running (spaces.js 128-133)
    std::string spaces_revealed;                // active space last brought into view (spaces.js 533-539)
    int spaces_revealed_at = -1;
    float spaces_list_h = 0;
    int drag_space = -1;
    int drag_tab = -1;
    std::string drag_pane;          // pane being dragged by its grip (herdr pane drag)
    ImVec2 drag_pane_from{};
    std::map<std::string, ui::Rect> pane_rects;  // panes of the shown tab, this frame
    float tab_scroll = 0;
    bool show_confirm = false;
    bool show_picker = false;
    char picker_buf[256] = {};
    int picker_sel = -1;
    bool picker_reset = true;
    bool picker_earlier = false;
    char help_buf[128] = {};
    std::string settings_section;
    float settings_row_end = 0;
    float settings_row_x = 0;
    bool show_welcome = false;
    bool welcome_panels_seen = false;
    bool welcome_copied = false;
    bool show_notes = false;  // What's new / Send feedback dialog
    std::string notes_kind;   // "whats_new" or "feedback"
    char feedback_buf[4096] = {};
    std::string feedback_note;
    int welcome_page = 0;
    ImVec2 welcome_pos{-1.0f, -1.0f};  // top-left once the person dragged it (herdr: Drag to move)
    bool show_machine_add = false;
    std::vector<std::pair<std::string, std::string>> ssh_hosts;  // alias, what it reaches
    // Add a machine (herdr machine-add): target -> "adding" or why it could not be added.
    std::map<std::string, std::string> machine_add_state;
    std::set<std::string> machine_add_pending;  // needs the person to answer something first
    std::string machine_add_copied;
    char machine_buf[256] = {};
    std::string confirm_title, confirm_detail, confirm_ok, confirm_command;
    std::string settings_tab;  // tab to select when the settings window opens
    bool show_worktree_open = false;
    std::vector<WorktreeEntry> worktree_list;
    std::string worktree_not_repo;  // Open worktree outside git: the folder (shown in the dialog)
    std::string worktree_machine;  // machine the listed worktrees are on
    char wt_filter[128] = {};
    int wt_sel = 0;
    std::vector<std::string> chord_commands;  // window chords Windows reserves (main.cpp), run next frame
    ui::ImageCache program_images;  // herdr's program icons (programs\icons), as textures
    // Herdr 98 (win98_*.inl): its menus, dialogs, games and shared values.
    std::unique_ptr<w98state::W98> win98 = std::make_unique<w98state::W98>();
    bool fonts_win98 = false;  // Herdr 98's fonts are the ones in use

    // ---------------------------------------------------------------- helpers
    [[nodiscard]] std::string title_of(const ui::SessionView& v) const {
        if (!v.name.empty())
            return v.name;
        return program_of(v).name;
    }
    [[nodiscard]] std::string pane_title(const std::string& id) const {
        const auto loc = ws.find(id);
        if (loc.space >= 0) {
            const auto& t =
                ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
            if (const auto it = t.pane_names.find(id); it != t.pane_names.end() && !it->second.empty())
                return it->second;
        }
        const auto it = views_by_id.find(id);
        return it == views_by_id.end() ? std::string("terminal") : title_of(it->second);
    }
    [[nodiscard]] Attention attention_of(const std::string& id) const {
        const auto it = views_by_id.find(id);
        if (it != views_by_id.end() &&
            (it->second.lost || it->second.process_state != "Running"))
            return Attention::kNone;
        return attention.get(id);
    }
    // Folder that names an unnamed space (herdr model.rs 271-279): its first terminal's git top
    // (a worktree's own folder), on whichever machine it runs, else that terminal's folder.
    [[nodiscard]] std::map<std::string, std::string> session_cwds() {
        std::map<std::string, std::string> m;
        for (const auto& sp : ws.spaces) {
            if (sp.tabs.empty())
                continue;
            const auto ids = ws.sessions_in(sp.tabs.front());
            if (ids.empty())
                continue;
            const auto it = views_by_id.find(ids.front());
            if (it == views_by_id.end() || it->second.cwd.empty())
                continue;
            const auto mi = machine_of.find(ids.front());
            const std::string machine = mi != machine_of.end() ? mi->second : sp.machine;
            const GitInfo g = git.get(it->second.cwd, machine);
            std::string top = g.repo ? g.top : std::string();
            if (top.empty() && machine.empty())
                top = ui::git_top(it->second.cwd);  // until git answers
            m[ids.front()] = top.empty() ? it->second.cwd : top;
        }
        return m;
    }
    [[nodiscard]] std::string focused_cwd() const {
        const auto id = ws.focused_session();
        const auto it = views_by_id.find(id);
        if (it != views_by_id.end())
            return it->second.cwd;
        return settings.last_cwd;
    }
    // herdr toasts: 5 s, 8 s for errors and warnings.
    void toast(std::string title, std::string body, std::string session, ImU32 color, int seconds = 0) {
        if (seconds <= 0)
            seconds = color == g_pal->needs_you || color == g_pal->working ? 8 : 5;
        // No cap: herdr stacks every toast (window.rs 4794-4796).
        toasts.push_back({std::move(title), std::move(body), std::move(session), color,
                          Clock::now() + std::chrono::seconds(seconds)});
    }
    void focus(const std::string& id) {
        if (id.empty())
            return;
        if (ws.find(id).space < 0) {
            hidden.erase(id);
            const std::string m = machine_of.count(id) ? machine_of[id] : std::string();
            const int sp = space_for_cwd(views_by_id.count(id) ? views_by_id[id].cwd : std::string(), m);
            if (sp >= 0) {
                ws.add_tab(id, sp);
            } else {
                ws.add_space(id);
                ws.spaces.back().machine = m;
            }
        }
        ws.focus(id);
        views[id].focus_next = true;
        attention.seen(id);
    }
    ui::HostClient* host_for(const std::string& machine) {
        for (auto& r : remotes)
            if (r.cfg.name == machine)
                return r.host.get();
        return host.get();
    }
    [[nodiscard]] bool machine_connected(const std::string& machine) const {
        if (machine.empty())
            return host->connected();
        for (const auto& r : remotes)
            if (r.cfg.name == machine)
                return r.host->connected();
        return false;
    }
    [[nodiscard]] std::string machine_label(const std::string& machine) const {
        return machine.empty() ? std::string("This PC") : machine;
    }
    // herdr spaces/main.js 347-351 loading(): the machine's layout is still on its way. Here:
    // the machine is connecting (not connected, no error yet), or this computer's document has
    // not arrived. Disconnected or failed is not loading.
    [[nodiscard]] bool machine_loading(const std::string& machine) const {
        const ui::HostClient* h = host.get();
        for (const auto& r : remotes)
            if (r.cfg.name == machine && !machine.empty())
                h = r.host.get();
        if (!machine.empty() && h == host.get())
            return false;  // a machine no longer configured
        if (h->connected())
            return machine.empty() && !doc_loaded;
        return h->last_error().empty();
    }
    // The state herdr writes after a machine's name (spaces/main.js 370, 385).
    [[nodiscard]] std::string machine_state(const std::string& machine) const {
        if (machine_connected(machine))
            return "connected";
        return machine_loading(machine) ? "connecting" : "unreachable";
    }
    int space_for_cwd(const std::string& cwd, const std::string& machine = {}) {
        if (cwd.empty()) {
            if (ws.spaces.empty())
                return -1;
            const auto& a = ws.spaces[static_cast<std::size_t>(
                std::clamp(ws.active_space, 0, static_cast<int>(ws.spaces.size()) - 1))];
            return a.machine == machine ? ws.active_space : -1;
        }
        for (int s = 0; s < static_cast<int>(ws.spaces.size()); ++s) {
            if (ws.spaces[static_cast<std::size_t>(s)].machine != machine)
                continue;
            for (const auto& t : ws.spaces[static_cast<std::size_t>(s)].tabs) {
                for (const auto& id : ws.sessions_in(t)) {
                    const auto it = views_by_id.find(id);
                    if (it != views_by_id.end() && _stricmp(it->second.cwd.c_str(), cwd.c_str()) == 0)
                        return s;
                }
            }
        }
        return -1;
    }
    void focus_view(const std::string& id) {
        if (!id.empty())
            views[id].focus_next = true;
    }
};

// ------------------------------------------------------------------ construction
namespace {
void start_remotes(App::State& s);
void apply_terminal_font(App::State& s);
void load_keys(App::State& s);
void apply_ui_font(App::State& s);
}  // namespace

namespace {
// keys.json (herdr keys.rs): installed from the defaults when missing; read again whenever it
// changes on disk; problems go to keys.error (removed once the file applies) and the defaults
// stay in force.
void load_keys(App::State& s) {
    const auto path = data_dir() / L"keys.json";
    const auto err_path = data_dir() / L"keys.error";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream(path, std::ios::binary) << ui::Keymap::default_file().dump(2) << "\n";
    }
    s.keys_mtime = std::filesystem::last_write_time(path, ec);
    s.keymap = ui::Keymap::defaults();
    s.keymap_errors.clear();
    const auto j = Json::parse(read_text_file(path), nullptr, false);
    if (j.is_discarded())
        s.keymap_errors.emplace_back("keys.json is not valid JSON");
    else
        s.keymap_errors = s.keymap.apply(j, known_command);
    if (s.keymap_errors.empty()) {
        std::filesystem::remove(err_path, ec);
    } else {
        std::ofstream out(err_path, std::ios::binary);
        for (const auto& e : s.keymap_errors)
            out << e << "\n";
    }
}

// What telemetry.cpp asks the app (herdr reads these from its own modules).
ui::telemetry::Config telemetry_config(App::State& s) {
    ui::telemetry::Config c;
    c.data_dir = data_dir();
    c.app_version = AGENTDESK_VERSION_STRING;
    // window.rs 171-173 shipped_layouts(): herdr ships main and terminals; ours are named
    // workspace and terminals (load_layouts).
    c.shipped_layouts = {"workspace", "terminals"};
    // settings.rs 80, 101: the presets and shipped themes; ours are the palettes.
    for (const auto& p : kPalettes)
        c.shipped_themes.emplace_back(p.id);
    for (const auto& [name, pal] : herdr_presets())  // herdr's presets and shipped file
        c.shipped_themes.emplace_back(std::string(kPresetPrefix) + name);
    c.shipped_themes.emplace_back("atom-one-dark");
    // mods.rs 281-299 kinds(): shipped ones as shipped (by name), shipped but edited, the
    // person's own. Shipped: the example mods next to agent-ui.exe (write_example_mods); as
    // shipped: the same mod.dll and mod.json bytes.
    c.mod_kinds = [](const std::set<std::string>& names) {
        wchar_t exe[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const auto shipped = std::filesystem::path(exe).parent_path() / L"mods";
        std::vector<std::string> builtin;
        std::size_t edited = 0;
        std::size_t custom = 0;
        for (const auto& name : names) {
            std::error_code ec;
            const auto from = shipped / win::to_wide(name);
            if (!std::filesystem::is_regular_file(from / L"mod.dll", ec)) {
                ++custom;
                continue;
            }
            const auto have = data_dir() / L"mods" / win::to_wide(name);
            bool same = true;
            for (const wchar_t* f : {L"mod.dll", L"mod.json"}) {
                bool a_ok = false;
                bool b_ok = false;
                const std::string a = read_text_file(from / f, &a_ok);
                const std::string b = read_text_file(have / f, &b_ok);
                same = same && a_ok == b_ok && a == b;
            }
            if (same)
                builtin.push_back(name);
            else
                ++edited;
        }
        return std::make_tuple(builtin, edited, custom);
    };
    // telemetry.rs 317-324: keys.json differs from the shipped one.
    c.keys_custom = [] {
        bool ok = false;
        const std::string text = read_text_file(data_dir() / L"keys.json", &ok);
        if (!ok)
            return false;
        return Json::parse(text, nullptr, false) != ui::Keymap::default_file();
    };
    c.core_version = [] { return ui::updates::core().version; };
    c.refresh = s.config.wake;
    return c;
}

// window.rs 834-856: what telemetry.rs counts: how much is open, never what's in it.
void observe(App::State& s) {
    if (!ui::telemetry::collecting())
        return;
    ui::telemetry::Observation o;
    o.foreground = s.window_focused;
    // Every terminal reporting an agent. herdr names it by its 7501 `app`; agent-host does not
    // pass that on, so it is what runs there (`claude`, `codex`), named by the same table.
    for (const auto& [id, v] : s.views_by_id) {
        if (v.agent_state == "Unknown" || v.lost)
            continue;
        o.agents.push_back(v.program.empty() ? std::nullopt : std::optional<std::string>(v.program));
        if (v.agent_state == "Working")
            ++o.agents_working;
    }
    o.spaces = s.ws.spaces.size();
    for (const auto& sp : s.ws.spaces) {
        o.tabs += sp.tabs.size();
        for (const auto& t : sp.tabs)
            o.terminals += s.ws.sessions_in(t).size();
    }
    o.layout = s.settings.layout;
    o.mods.assign(s.settings.mods_enabled.begin(), s.settings.mods_enabled.end());
    o.theme = g_pal->id;
    if (!s.settings.welcomed.empty())
        o.welcome = s.settings.welcomed;
    ui::telemetry::observe(std::move(o));
}

// window.rs 5455-5459
void reconcile_core_update_error(std::optional<std::string>& dismissed, const ui::updates::CoreInfo& core) {
    if ((core.error && dismissed != core.error) || (!core.updating && !core.error))
        dismissed.reset();
}

}  // namespace

App::App(AppConfig config, std::unique_ptr<ui::HostClient> host) : s_(std::make_unique<State>()) {
    g_program_images = &s_->program_images;
    s_->config = std::move(config);
    s_->host = std::move(host);
    s_->settings = load_settings();
    g_pal = palette_by_id(s_->settings.theme);
    s_->folded_machines.insert(s_->settings.folded_machines.begin(), s_->settings.folded_machines.end());
    s_->keymap = ui::Keymap::defaults();
    load_keys(*s_);
    std::string cwd = s_->settings.last_cwd;
    if (cwd.empty()) {
        const auto u8 = std::filesystem::current_path().u8string();
        cwd.assign(u8.begin(), u8.end());
    }
    std::snprintf(s_->launch_cwd, sizeof s_->launch_cwd, "%s", cwd.c_str());
    start_remotes(*s_);
    write_example_mods();
    s_->layouts = load_layouts(s_->settings);
    {
        // ui.json kept the workspace's sidebar, panel and list sizes before this engine; they
        // are now its clicks in layouts/.state/workspace.json (seed_from_settings). Written once,
        // then ui.json forgets them so a later reload does not bring them back.
        const Settings d;
        Settings& st = s_->settings;
        if (st.sidebar_width != d.sidebar_width || st.panel_width != d.panel_width ||
            st.spaces_share != d.spaces_share || st.sidebar != d.sidebar) {
            for (const auto& l : s_->layouts)
                if (l.name == "workspace")
                    save_layout_state(l);
            st.sidebar_width = d.sidebar_width;
            st.panel_width = d.panel_width;
            st.spaces_share = d.spaces_share;
            st.sidebar = d.sidebar;
            save_settings(st);
        }
    }
    // Files / Changes for a pane on another machine: ask that machine's agent-host.
    s_->panels->remote = [st = s_.get()](const std::string& machine, const std::string& method,
                                        const Json& params) -> std::optional<Json> {
        std::shared_ptr<ui::HostClient> h;
        {
            std::lock_guard lock(st->remotes_mutex);
            for (const auto& r : st->remotes)
                if (r.cfg.name == machine)
                    h = r.host;
        }
        if (!h || !h->connected())
            return std::nullopt;
        auto done = std::make_shared<std::promise<std::optional<Json>>>();
        auto result = done->get_future();
        // git worktree add/remove may take minutes (herdr worktree/main.js:205-207, 243).
        const auto args = params.find("args");
        const bool slow = method == "git.run" && args != params.end() && args->is_array() && !args->empty() &&
                          (*args)[0] == "worktree";
        const auto wait = std::chrono::seconds(slow ? 600 : 60);
        h->call(
            method, params,
            [done](Json r, std::string error) {
                done->set_value(error.empty() ? std::optional<Json>(std::move(r)) : std::nullopt);
            },
            wait);
        if (result.wait_for(wait + std::chrono::seconds(5)) != std::future_status::ready)
            return std::nullopt;
        return result.get();
    };
    s_->git.remote = s_->panels->remote;
    s_->mods = std::make_unique<ui::ModHost>(data_dir() / L"mods");
    s_->mods->load(s_->settings.mods_enabled);
    // herdr main.rs 154-158: after an update, then the checker, then telemetry.
    ui::updates::after_update();
    ui::updates::start();
    ui::telemetry::start(telemetry_config(*s_), s_->settings.telemetry);
    // herdr: the welcome opens on the first run (until it is finished or skipped).
    if (s_->settings.welcomed.empty() && !s_->config.automated) {
        s_->show_welcome = true;
        s_->settings.seen_version = AGENTDESK_VERSION_STRING;  // nothing is new on a first run
        save_settings(s_->settings);
    } else if (s_->settings.seen_version != AGENTDESK_VERSION_STRING && !s_->config.automated) {
        s_->show_notes = true;  // herdr: What's new once after an update
        s_->notes_kind = "whats_new";
        s_->settings.seen_version = AGENTDESK_VERSION_STRING;
        save_settings(s_->settings);
    }
}

namespace {
void start_remotes(App::State& s) {
    std::vector<Remote> keep;
    for (const auto& cfg : s.settings.machines) {
        auto it = std::find_if(s.remotes.begin(), s.remotes.end(), [&](const Remote& r) {
            return r.cfg.name == cfg.name && r.cfg.target == cfg.target && r.cfg.ctl == cfg.ctl;
        });
        if (it != s.remotes.end()) {
            keep.push_back(std::move(*it));
            continue;
        }
        ui::HostClientOptions o;
        o.wake = s.config.wake;
        o.connector = [cfg] {
            // "pipe:<name>": another agent-host on this PC through a local bridge (tests, a second
            // host under another data folder); anything else is an ssh target.
            if (cfg.target.rfind("pipe:", 0) == 0) {
                return ipc::connect_over_command(
                    {ipc::sibling_executable(L"agentctl.exe").native(), L"--pipe",
                     win::to_wide(cfg.target.substr(5)), L"--no-start", L"bridge"},
                    std::chrono::seconds(10));
            }
            return ipc::connect_over_command(ipc::ssh_bridge_argv(cfg.target, cfg.ctl),
                                             std::chrono::seconds(20));
        };
        keep.push_back({cfg, std::make_unique<ui::HostClient>(o)});
    }
    std::vector<Remote> old;
    {
        std::lock_guard lock(s.remotes_mutex);
        old = std::exchange(s.remotes, std::move(keep));
    }
    old.clear();  // removed machines disconnect here, outside the lock
}
}  // namespace

bool App::binds_chord(int chord) const {
    return s_->keymap.global(static_cast<ImGuiKeyChord>(chord)).has_value();
}

void App::run_chord(int chord) {
    if (const auto cmd = s_->keymap.global(static_cast<ImGuiKeyChord>(chord)))
        s_->chord_commands.push_back(*cmd);
}

App::~App() {
    g_program_images = nullptr;
}

std::filesystem::path data_folder() {
    return data_dir();
}

// herdr main.rs 177-192 on_app_quit: telemetry's clean quit, then the waiting update goes in
// place (not restarted for it).
void App::on_quit() {
    ui::telemetry::quit();
    std::string error;
    ui::updates::apply(&error);
    ui::telemetry::stop();
}

ui::HostClient& App::host() {
    return *s_->host;
}

void App::load_fonts() {
    s_->fonts = ui::load_fonts(s_->settings.font_size, 13.0f);  // herdr UI base 13, scaled by ui_scale()
    ImGui::GetIO().FontDefault = s_->fonts.ui;
    apply_terminal_font(*s_);
    apply_ui_font(*s_);
}

void App::apply_scale(float dpi) {
    s_->dpi = dpi;
    g_scale = dpi * ui_scale(s_->settings);
    apply_style(dpi, ui_scale(s_->settings));
}

void App::on_close_requested() {
    if (s_->settings.close_notice_shown || s_->config.automated)
        s_->exit_now = true;
    else
        s_->show_close_notice = true;
}

void App::on_focus_changed(bool focused) {
    s_->window_focused = focused;
}

void App::on_files_dropped(std::vector<std::string> paths) {
    std::string target;
    for (const auto& [id, vs] : s_->views) {
        if (vs.view.hovered())
            target = id;
    }
    if (target.empty())
        target = s_->ws.focused_session();
    if (const auto it = s_->views.find(target); it != s_->views.end())
        it->second.view.drop_paths(std::move(paths));
}

bool App::exit_requested() const {
    return s_->exit_now;
}

long long App::next_wakeup_ms() const {
    long long ms = 1000;
    for (const auto& [id, vs] : s_->views) {
        if (const auto d = vs.view.next_deadline())
            ms = std::min<long long>(
                ms, std::max<long long>(
                        0, std::chrono::duration_cast<std::chrono::milliseconds>(*d - Clock::now()).count()) +
                        1);
    }
    if (s_->doc_dirty)
        ms = std::min<long long>(ms, 450);
    if (!s_->toasts.empty())
        ms = std::min<long long>(ms, 250);
    if (s_->updates_asked)
        ms = std::min<long long>(ms, 300);  // settings main.js 66: every 300 ms
    for (const auto& [id, v] : s_->views_by_id) {
        if (v.agent_state == "Working")
            ms = std::min<long long>(ms, 50);  // the working mark spins
    }
    if (s_->layout().style == "win98")  // Herdr 98's clock, carets, badges, games and screens
        ms = std::min<long long>(ms, s_->win98->wakeup_ms);
    return ms;
}

Json App::report() const {
    Json out = {{"connected", s_->host->connected()}, {"host_instance_id", s_->host->host_instance_id()}};
    Json list = Json::array();
    for (const auto& s : s_->host->sessions()) {
        const auto v = s->view();
        const auto snap = s->snapshot();
        Json screen = Json::array();
        for (int r = 0; r < snap->rows; ++r)
            screen.push_back(snap->row_text(r));
        list.push_back({{"id", v.id},
                        {"name", v.name},
                        {"process_state", v.process_state},
                        {"pid", v.pid},
                        {"program", v.program},
                        {"agent_state", v.agent_state},
                        {"agent_source", v.agent_source},
                        {"is_controller", v.is_controller},
                        {"lost", v.lost},
                        {"cols", snap->cols},
                        {"rows", snap->rows},
                        {"screen", screen}});
    }
    out["sessions"] = list;
    std::size_t tabs = 0;
    for (const auto& sp : s_->ws.spaces)
        tabs += sp.tabs.size();
    out["spaces"] = s_->ws.spaces.size();
    out["tabs"] = tabs;
    out["selected"] = s_->ws.focused_session();
    out["workspace"] = s_->ws.to_json();

    return out;
}

// ------------------------------------------------------------------ sessions -> workspace
namespace {

Json make_doc(const App::State& s) {
    Json doc = s.ws.to_json();
    doc["hidden"] = Json(std::vector<std::string>(s.hidden.begin(), s.hidden.end()));
    return doc;
}

void adopt_doc(App::State& s, const Json& doc) {
    s.ws = ui::Workspace::from_json(doc);
    s.hidden.clear();
    for (const auto& h : doc.value("hidden", Json::array()))
        if (h.is_string())
            s.hidden.insert(h.get<std::string>());
    s.known.clear();
    for (const auto& id : s.ws.all_sessions())
        s.known.insert(id);
    s.known.insert(s.hidden.begin(), s.hidden.end());
    s.last_doc = make_doc(s).dump();
    s.last_sent_doc = s.last_doc;
}

// `args` go after the profile's own (session.create "args"; Herdr 98's Run line).
void create_session(App::State& s, const std::string& profile, const std::string& cwd,
                    const std::string& name, Placement placement, const std::vector<std::string>& args = {}) {
    Json params{{"profile_id", profile}, {"cwd", cwd}};
    if (!name.empty())
        params["name"] = name;
    if (!args.empty())
        params["args"] = args;
    s.launch_busy = true;
    s.host_for(placement.machine)
        ->call(
            "session.create", params,
            [st = &s, placement](Json result, std::string error) mutable {
                std::lock_guard lock(st->placements_mutex);
                if (!error.empty()) {
                    st->async_errors.push_back(error);
                    return;
                }
                placement.session = result.value("session_id", "");
                st->placements.push_back(placement);
            },
            std::chrono::seconds(15));
}

void place(App::State& s, const Placement& p) {
    s.known.insert(p.session);
    s.launched_here[p.session] = Clock::now();
    s.awaiting_list[p.session] = Clock::now();
    if (!p.replace.empty() && s.ws.replace(p.replace, p.session)) {
        s.focus(p.session);
        return;
    }
    switch (p.target) {
        case LaunchTarget::kSplitRight:
        case LaunchTarget::kSplitDown:
            if (!p.split_of.empty() &&
                s.ws.split(p.split_of, p.session,
                           p.target == LaunchTarget::kSplitRight ? Dir::kRight : Dir::kDown)) {
                s.focus(p.session);
                return;
            }
            [[fallthrough]];
        case LaunchTarget::kTab: {
            int sp = p.space >= 0 && p.space < static_cast<int>(s.ws.spaces.size()) &&
                             s.ws.spaces[static_cast<std::size_t>(p.space)].machine == p.machine
                         ? p.space
                         : -1;
            if (sp < 0) {
                for (int i = 0; i < static_cast<int>(s.ws.spaces.size()) && sp < 0; ++i)
                    if (s.ws.spaces[static_cast<std::size_t>(i)].machine == p.machine)
                        sp = i;
            }
            if (sp >= 0) {
                s.ws.add_tab(p.session, sp, p.tab_name);
                s.focus(p.session);
                return;
            }
            [[fallthrough]];
        }
        case LaunchTarget::kSpace:
            s.ws.add_space(p.session, p.space_name);
            s.ws.spaces.back().machine = p.machine;
            s.focus(p.session);
            return;
    }
}

// Places sessions that appeared without a placement (agentctl, another window, first start):
// into the space of the same folder, else a new space; never steals focus once loaded.
void sync_sessions(App::State& s) {
    {
        std::lock_guard lock(s.placements_mutex);
        for (const auto& p : s.placements)
            if (!p.session.empty())
                place(s, p);
        if (!s.placements.empty())
            s.launch_busy = false;
        s.placements.clear();
        for (auto& e : s.async_errors) {
            s.launch_error = e;
            s.launch_busy = false;
            // The launcher may be closed (new terminal, split, space): say it where it is seen.
            s.toasts.push_back({"Could not start", e, "", g_pal->needs_you, Clock::now() + std::chrono::seconds(10)});
        }
        s.async_errors.clear();
    }
    std::set<std::string> present;
    for (const auto& sess : s.sessions) {
        s.awaiting_list.erase(sess->id());
        // herdr window.rs 2262-2283, 2695-2719 (reconcile / checked_closed): a pane whose terminal
        // ended (exited, killed) or is gone is removed; its tab, then its space, close when
        // they empty (model.rs 403-422). Lost: the agent-host that ran it is gone.
        const auto& v = s.views_by_id[sess->id()];
        if (v.process_state == "Exited" || v.lost)
            continue;
        present.insert(sess->id());
    }
    // A machine that is not connected right now keeps its panes (its sessions still run there).
    for (const auto& sp : s.ws.spaces)
        if (!s.machine_connected(sp.machine))
            for (const auto& t : sp.tabs)
                for (const auto& id : s.ws.sessions_in(t))
                    present.insert(id);
    for (auto it = s.awaiting_list.begin(); it != s.awaiting_list.end();) {
        if (Clock::now() - it->second > std::chrono::seconds(15)) {
            it = s.awaiting_list.erase(it);
        } else {
            present.insert(it->first);
            ++it;
        }
    }
    const bool initial = s.ws.spaces.empty();
    for (const auto& sess : s.sessions) {
        const auto& id = sess->id();
        if (s.known.contains(id))
            continue;
        s.known.insert(id);
        const auto& v = s.views_by_id[id];
        if (v.process_state == "Exited" || v.lost)
            continue;  // ended: never opened (herdr places live terminals only, window.rs 2277-2283)
        if (!v.parent.empty() && s.views_by_id.count(v.parent))
            continue;  // a sub-agent: listed under its parent, peeked or opened on demand
        const int before_space = s.ws.active_space;
        const int before_tab = s.ws.spaces.empty() ? 0 : s.ws.active()->active_tab;
        const std::string m = s.machine_of.count(id) ? s.machine_of[id] : std::string();
        const int sp = s.space_for_cwd(v.cwd, m);
        if (sp >= 0) {
            s.ws.add_tab(id, sp);
        } else {
            s.ws.add_space(id);
            s.ws.spaces.back().machine = m;
        }
        if (!initial && before_space < static_cast<int>(s.ws.spaces.size())) {
            s.ws.active_space = before_space;  // keep what the user looks at
            if (auto* a = s.ws.active())
                a->active_tab = std::min(before_tab, static_cast<int>(a->tabs.size()) - 1);
        }
    }
    s.ws.prune(present);
    for (auto it = s.hidden.begin(); it != s.hidden.end();)
        it = present.contains(*it) ? std::next(it) : s.hidden.erase(it);
    for (auto it = s.views.begin(); it != s.views.end();)
        it = present.contains(it->first) ? std::next(it) : s.views.erase(it);
}

void persist(App::State& s) {
    const std::string doc = make_doc(s).dump();
    if (doc != s.last_doc) {
        s.last_doc = doc;
        s.doc_dirty = true;
        s.doc_dirty_at = Clock::now();
    }
    if (s.doc_dirty && Clock::now() - s.doc_dirty_at > std::chrono::milliseconds(400) &&
        s.host->connected()) {
        s.doc_dirty = false;
        if (doc != s.last_sent_doc) {
            s.last_sent_doc = doc;
            s.host->set_ui_state(Json::parse(doc));
        }
    }
}

// Agents that need the user / finished unseen: sound, toast, taskbar flash.
// Events for mods, then whatever they asked for.
void run_mods(App::State& s) {
    std::vector<ui::ModSession> list;
    const auto focused = s.ws.focused_session();
    for (const auto& [id, v] : s.views_by_id) {
        ui::ModSession m;
        m.id = id;
        m.title = s.pane_title(id);
        m.program = v.program;
        m.cwd = v.cwd;
        m.agent_state = v.agent_state;
        m.process_state = v.lost ? std::string("Lost") : v.process_state;
        m.machine = s.machine_of.count(id) ? s.machine_of[id] : std::string();
        m.parent = v.parent;
        m.created_at = v.created_at;
        m.labels = v.labels;
        m.focused = id == focused;
        list.push_back(std::move(m));
    }
    s.mods->set_sessions(list);
    for (const auto& m : list) {
        auto it = s.last_agent_state.find(m.id);
        if (it == s.last_agent_state.end()) {
            s.mods->emit("session_added", {{"session", m.id}, {"title", m.title}});
            s.last_agent_state[m.id] = m.agent_state;
            if (m.agent_state != "Unknown")
                s.mods->emit("agent_state", {{"session", m.id},
                                             {"state", m.agent_state},
                                             {"previous", "Unknown"},
                                             {"title", m.title}});
        } else if (it->second != m.agent_state) {
            s.mods->emit(
                "agent_state",
                {{"session", m.id}, {"state", m.agent_state}, {"previous", it->second}, {"title", m.title}});
            it->second = m.agent_state;
        }
        if (m.process_state == "Exited" && s.prev_process_state[m.id] != "Exited")
            s.mods->emit("session_exited", {{"session", m.id}, {"title", m.title}});
    }
    if (Clock::now() >= s.next_tick) {
        s.next_tick = Clock::now() + std::chrono::seconds(1);
        s.mods->emit("tick", Json::object());
    }
    auto out = s.mods->take_output();
    for (const auto& n : out.notes)
        s.toast(n.title, n.body, "", g_pal->info);
    for (const auto& snd : out.sends) {
        if (const auto it = s.by_id.find(snd.session); it != s.by_id.end()) {
            it->second->send(host::PasteInput{snd.text});
            if (snd.enter)
                it->second->send(host::RawInput{"\r"});
        }
    }
    for (const auto& argv : out.execs) {
        // Started directly, never through a shell; no window; not waited for.
        wchar_t exe[MAX_PATH]{};
        if (::SearchPathW(nullptr, argv[0].c_str(), L".exe", MAX_PATH, exe, nullptr) == 0)
            continue;
        std::wstring cmd =
            win::build_command_line(exe, std::vector<std::wstring>(argv.begin() + 1, argv.end()));
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (::CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                             &si, &pi)) {
            ::CloseHandle(pi.hThread);
            ::CloseHandle(pi.hProcess);
        }
    }
    for (const auto& c : out.commands)
        s.pending_mod_commands.push_back(c);
}

void play_alert(App::State& s, bool blocked);

void update_attention(App::State& s) {
    const ui::Tab* tab = s.ws.active_tab();
    std::set<std::string> visible;
    if (tab != nullptr) {
        if (!tab->zoomed.empty())
            visible.insert(tab->zoomed);
        else
            for (const auto& id : s.ws.sessions_in(*tab))
                visible.insert(id);
    }
    for (const auto& [id, v] : s.views_by_id) {
        const bool looking = s.window_focused && visible.contains(id);
        if (s.attention.update(id, v.agent_state, looking) && !s.config.automated) {
            // herdr: the sound plays either way; the note shows when you were not looking.
            const bool blocked = v.agent_state == "Blocked";
            const std::string title = s.pane_title(id);
            if (s.settings.sounds)
                play_alert(s, blocked);
            if (s.ws.unsettle_session(id)) {  // herdr: an alert brings a tab put away back
                s.doc_dirty = true;
                s.doc_dirty_at = Clock::now();
            }
            if (!looking) {
                std::string where;
                const auto loc = s.ws.find(id);
                if (loc.space >= 0) {
                    const auto& t = s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
                    where = s.ws.space_title(loc.space, s.session_cwds()) + " / " +
                            (t.name.empty() ? std::to_string(loc.tab + 1) : t.name);
                }
                s.toast(blocked ? title + " needs you" : title + " finished",
                        where, id, blocked ? g_pal->needs_you : g_pal->focus);  // herdr: "<space> / <tab>"
            }
        }
        // herdr window.rs 142-157, 2463-2481: a terminal that exited by itself with an error
        // within 2 s of starting here (any time for 127) -> "Terminal failed to start", the exit
        // code and its last 5 lines. One the person closed (killed) says nothing.
        const auto prev = s.prev_process_state[id];
        if (prev != v.process_state) {
            s.prev_process_state[id] = v.process_state;
            const auto l = s.launched_here.find(id);
            const bool closed = s.hidden.contains(id);
            if (v.process_state == "Exited" && v.exit_code && *v.exit_code != 0 && !closed &&
                l != s.launched_here.end() &&
                (Clock::now() - l->second <= std::chrono::seconds(2) || *v.exit_code == 127)) {
                std::string body = "Exit code " + std::to_string(*v.exit_code);
                if (const auto it = s.by_id.find(id); it != s.by_id.end()) {
                    const auto snap = it->second->snapshot();
                    std::vector<std::string> lines;
                    for (int r = 0; r < snap->rows; ++r) {
                        auto t = snap->row_text(r);
                        if (t.find_first_not_of(' ') != std::string::npos)
                            lines.push_back(t);
                    }
                    for (std::size_t i = lines.size() > 5 ? lines.size() - 5 : 0; i < lines.size(); ++i)
                        body += "\n" + lines[i];
                }
                s.toast("Terminal failed to start", body, id, g_pal->needs_you, 10);
            }
        }
    }
    // Looking at a pane clears its "done".
    if (s.window_focused)
        for (const auto& id : visible)
            s.attention.seen(id);
}

}  // namespace

// ------------------------------------------------------------------ commands
namespace {

// Profile used for new spaces/tabs/splits: the configured one if it exists, else the first
// shell found (PowerShell 7, Windows PowerShell, Command Prompt), else empty.
std::string pick_shell(const std::string& wanted, const std::vector<std::pair<std::string, std::string>>& profiles) {
    const auto has = [&](const std::string& id) {
        return std::any_of(profiles.begin(), profiles.end(), [&](const auto& p) { return p.first == id; });
    };
    if (!wanted.empty() && has(wanted))
        return wanted;
    for (const char* id : {"pwsh", "powershell", "cmd"})
        if (has(id))
            return id;
    return {};
}

void open_confirm(App::State& s, std::string title, std::string detail, std::string ok, std::string command);

// git in `cwd` on `machine` ("" = here); other machines run it through their agent-host's
// git.run (herdr worktree.js runs git on the space's machine).
host::ToolResult git_at(App::State& s, const std::string& machine, const std::string& cwd, std::vector<std::wstring> args,
                        std::chrono::seconds timeout = std::chrono::seconds(30)) {
    if (machine.empty()) {
        const std::filesystem::path p(win::to_wide(cwd));
        args.insert(args.begin(), {L"git", L"-C", p.native()});
        return host::run_tool(args, p, timeout);
    }
    host::ToolResult r;
    Json a = Json::array();
    for (const auto& x : args)
        a.push_back(win::to_utf8(x));
    if (const auto res = s.panels->remote ? s.panels->remote(machine, "git.run", Json{{"cwd", cwd}, {"args", a}})
                                          : std::nullopt) {
        r.started = true;
        r.exit_code = res->value("exit_code", 1ul);
        r.output = res->value("output", "");
        r.error = res->value("error", "");
    }
    return r;
}

// Text without surrounding whitespace (JavaScript's trim()).
std::string trimmed(const std::string& t) {
    const auto a = t.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    return t.substr(a, t.find_last_not_of(" \t\r\n") - a + 1);
}

// What removing a linked worktree would lose (herdr worktree.js): uncommitted changes, and
// submodules with commits their upstream does not have. Plain git, no shell.
struct WorktreeRisk {
    bool dirty = false;
    bool submodules = false;
    std::vector<std::string> unpushed;  // submodule paths
    [[nodiscard]] std::string signature() const {
        std::string sig = std::string(dirty ? "d" : "-") + (submodules ? "s" : "-");
        for (const auto& u : unpushed)
            sig += "," + u;
        return sig;
    }
};

WorktreeRisk worktree_risk(App::State& s, const std::string& machine, const std::string& top) {
    WorktreeRisk r;
    const std::filesystem::path p(win::to_wide(top));
    const auto st = git_at(s, machine, top, {L"status", L"--porcelain"});
    r.dirty = st.started && st.exit_code == 0 && !st.output.empty();
    const auto subs = git_at(s, machine, top, {L"submodule", L"status", L"--recursive"});
    std::stringstream ss(subs.started && subs.exit_code == 0 ? subs.output : std::string());
    std::vector<std::string> paths;
    for (std::string line; std::getline(ss, line);) {
        // "<flag><sha> <path> (<describe>)"; '-': never checked out (herdr skips those too)
        if (line.empty() || line[0] == '-')
            continue;
        std::stringstream ls(line.substr(1));
        std::string sha, path;
        ls >> sha >> path;
        if (!path.empty())
            paths.push_back(path);
    }
    for (const auto& path : paths) {
        r.submodules = true;
        // herdr defaults/mods/worktree/main.js:100-110: commits that exist nowhere else, i.e. on
        // no remote and not the commit the parent pins (git fetches that by hash, so it is often
        // on no remote branch). The parent is the deepest other submodule containing it.
        std::string parent;
        for (const auto& other : paths) {
            if (other.size() > parent.size() && path.size() > other.size() && path.compare(0, other.size(), other) == 0 &&
                path[other.size()] == '/')
                parent = other;
        }
        const auto parent_dir = parent.empty() ? p : p / win::to_wide(parent);
        const std::string inner = parent.empty() ? path : path.substr(parent.size() + 1);
        const auto pinned = git_at(s, machine, win::to_utf8(parent_dir.native()),
                                   {L"rev-parse", L"HEAD:" + win::to_wide(inner)});
        std::vector<std::wstring> args = {L"rev-list", L"-n1", L"--all", L"--not", L"--remotes"};
        std::string pin = pinned.started && pinned.exit_code == 0 ? trimmed(pinned.output) : std::string();
        if (!pin.empty())
            args.push_back(win::to_wide(pin));
        const auto local = git_at(s, machine, win::to_utf8((p / win::to_wide(path)).native()), args);
        if (!local.started || local.exit_code != 0 || !trimmed(local.output).empty())
            r.unpushed.push_back(path);  // a check that failed counts as at risk
    }
    return r;
}

std::string worktree_remove_text(const std::string& top, const WorktreeRisk& r) {
    std::string t = "Closes the space and deletes " + display_path(top) + ". The branch stays.";
    if (r.dirty)
        t += "\n\nIt has uncommitted changes: they will be lost.";
    if (!r.unpushed.empty()) {  // herdr worktree/main.js render()
        t += "\n\nSubmodule ";
        for (std::size_t i = 0; i < r.unpushed.size(); ++i)
            t += (i ? ", " : "") + r.unpushed[i];
        t += " has commits that aren't pushed anywhere: they will be lost.";
    }
    return t;
}
void confirm_close_space(App::State& s, int index);

void open_launcher(App::State& s, LaunchTarget target) {
    s.show_launcher = true;
    s.launch_machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
    if (target == LaunchTarget::kSplitRight || target == LaunchTarget::kSplitDown) {
        const auto f = s.ws.focused_session();
        if (s.machine_of.count(f))
            s.launch_machine = s.machine_of[f];
    }
    s.launch_target = target;
    s.launch_split_of = s.ws.focused_session();
    s.launch_error.clear();
    s.launch_name[0] = '\0';
    s.launcher_sel = 0;
    const auto cwd = s.focused_cwd();
    if (!cwd.empty())
        std::snprintf(s.launch_cwd, sizeof s.launch_cwd, "%s", cwd.c_str());
}

void open_rename(App::State& s, const std::string& kind, const std::string& target,
                 const std::string& current) {
    // herdr window.rs 3047-3051: a second rename press closes the open prompt instead of
    // stacking another.
    if (s.rename_open) {
        s.rename_close = true;
        return;
    }
    s.show_rename = true;
    s.rename_kind = kind;
    s.rename_target = target;
    std::snprintf(s.rename_buf, sizeof s.rename_buf, "%s", current.c_str());
}

// ------------------------------------------------------------------ layout commands
// herdr tabs/main.js 62-80 syncPanel: the side panel is remembered per space (here the space's
// `panel`, kept in the UI document): the shown space's choice opens or closes the layout's
// "panel" node and picks the "panel-body" child.
void sync_panel(App::State& s, LayoutSpec& l) {
    lay::Node* panel = l.tree.find("panel");
    if (panel == nullptr)
        return;
    const auto* sp = s.ws.active();
    panel->hidden = sp == nullptr || sp->panel.empty();
    if (sp != nullptr && !sp->panel.empty())
        l.tree.select("panel-body", sp->panel);
}

// The arguments herdr's layout.* commands take ({"id": ..., "px": ...}) as JSON, or the short
// spellings a key or the palette can carry: "id", "id=300" (px), "stack=child".
Json layout_args(const std::string& arg, const char* first, const char* second) {
    if (!arg.empty() && arg.front() == '{') {
        Json j = Json::parse(arg, nullptr, false);
        return j.is_object() ? j : Json::object();
    }
    Json j = Json::object();
    const auto eq = arg.find('=');
    j[first] = arg.substr(0, eq);
    if (eq != std::string::npos) {
        const std::string v = arg.substr(eq + 1);
        if (std::string(second) == "px") {
            char* end = nullptr;
            const double d = std::strtod(v.c_str(), &end);
            if (end != v.c_str())
                j["px"] = d;
        } else {
            j[second] = v;
        }
    }
    return j;
}

// herdr window.rs 3496-3538: layout.toggle / show / hide {id}, layout.resize {id, px | weight},
// layout.select {stack, child}; the change is a click, kept in layouts/.state.
std::optional<std::string> layout_command(App::State& s, const std::string& name, const std::string& arg) {
    LayoutSpec& l = s.layout();
    if (name == "layout.toggle" || name == "layout.show" || name == "layout.hide") {
        const std::string id = layout_args(arg, "id", "").value("id", std::string());
        lay::Node* n = l.tree.find(id);
        if (n == nullptr)
            return "no node \"" + id + "\" in the shown layout";
        const bool hide = name == "layout.toggle" ? !n->hidden : name == "layout.hide";
        if (id == "panel") {  // the space's choice (tabs/main.js 136-141)
            if (auto* sp = s.ws.active()) {
                const lay::Node* body = l.tree.find("panel-body");
                const lay::Node* shown = body != nullptr ? body->active_child() : nullptr;
                sp->panel = hide ? std::string() : shown != nullptr ? shown->id : std::string("files");
                s.doc_dirty = true;
                s.doc_dirty_at = Clock::now();
            }
        }
        n->hidden = hide;
    } else if (name == "layout.resize") {
        const Json a = layout_args(arg, "id", "px");
        const std::string id = a.value("id", std::string());
        lay::Node* n = l.tree.find(id);
        if (n == nullptr)
            return "no node \"" + id + "\"";
        if (a.contains("px") && a["px"].is_number())
            n->size = lay::Span::pixels(a["px"].get<float>());
        else if (a.contains("weight") && a["weight"].is_number())
            n->size = lay::Span::weight(a["weight"].get<float>());
    } else if (name == "layout.select") {
        const Json a = layout_args(arg, "stack", "child");
        const std::string st = a.value("stack", std::string());
        const std::string ch = a.value("child", std::string());
        if (!l.tree.select(st, ch))
            return "no stack \"" + st + "\" with a child \"" + ch + "\"";
        if (st == "panel-body")
            if (auto* sp = s.ws.active(); sp != nullptr && !sp->panel.empty()) {
                sp->panel = ch;
                s.doc_dirty = true;
                s.doc_dirty_at = Clock::now();
            }
    } else {
        return std::nullopt;
    }
    save_layout_state(l);
    derive_layout(l);
    return std::string();
}

// person: the person ran it (palette, keys, a click), not a mod or --run (window.rs 2995).
void run_command(App::State& s, const std::string& cmd, bool person = true);
namespace w98 {
void command(App::State& s, const std::string& cmd);  // win98_chrome.inl
}  // namespace w98

// Runs a command and says why it failed ("" when it ran): herdr window.rs run_as() answers
// {"error": ...} for an unknown command, a missing target, or a git step that failed.
std::string exec_command(App::State& s, const std::string& cmd) {
    const auto colon = cmd.find(':');
    const std::string name = cmd.substr(0, colon);
    const std::string arg = colon == std::string::npos ? std::string() : cmd.substr(colon + 1);
    const std::string focused = s.ws.focused_session();
    auto session = [&]() -> std::shared_ptr<ui::RemoteSession> {
        const auto it = s.by_id.find(focused);
        return it == s.by_id.end() ? nullptr : it->second;
    };
    // herdr command names (keys.json written for herdr keeps working).
    static const std::map<std::string, std::string> kHerdrNames = {
        {"terminal.park", "pane.park"}, {"terminal.peek", "agent.peek"}, {"terminal.open", "tab.new"},
        {"app.quit", "window.close"}};  // herdr Quit: the window closes, agents keep running
    if (name == "layout.toggle" && arg.empty())  // the panel (AgentDesk's older spelling)
        return exec_command(s, "panel");
    if (auto r = layout_command(s, name, arg))
        return *r;
    if (const auto al = kHerdrNames.find(name); al != kHerdrNames.end())
        return exec_command(s, arg.empty() ? al->second : al->second + ":" + arg);
    if (name == "mod.open" && arg == "picker")
        return exec_command(s, "app.picker");
    if (name == "overlay.close") {  // herdr: closes the overlays (here: the floating ones)
        s.show_palette = s.show_picker = s.show_help = false;
        s.peek.clear();
        return {};
    }
    if (name == "terminal.pin") {  // herdr terminal.pin {pinned}: the focused agent
        if (!focused.empty()) {
            if (arg == "false")
                s.ws.pinned.erase(focused);
            else if (arg == "true" || !s.ws.pinned.erase(focused))
                s.ws.pinned.insert(focused);
            s.doc_dirty = true;
            s.doc_dirty_at = Clock::now();
        }
        return {};
    }
    if (name == "space.move") {  // herdr space.move {index} (1 = first): the active space
        const int to = std::atoi(arg.c_str()) - 1;
        if (to >= 0 && s.ws.move_space(s.ws.active_space, to)) {
            s.doc_dirty = true;
            s.doc_dirty_at = Clock::now();
        }
        return {};
    }
    if (name == "launch") {
        const auto c2 = arg.find(':');
        const std::string target = arg.substr(0, c2);
        const std::string profile = c2 == std::string::npos ? std::string() : arg.substr(c2 + 1);
        Placement p;
        p.target = target == "space"   ? LaunchTarget::kSpace
                   : target == "right" ? LaunchTarget::kSplitRight
                   : target == "down"  ? LaunchTarget::kSplitDown
                                       : LaunchTarget::kTab;
        p.split_of = focused;
        p.space = s.ws.active_space;
        p.machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
        const std::string cwd = s.focused_cwd().empty() ? std::string(s.launch_cwd) : s.focused_cwd();
        if (!profile.empty())
            create_session(s, profile, cwd, "", p);
    } else if (name == "tab.new" || name == "space.new" || name == "pane.split") {
        // New spaces, tabs and splits open a shell in the focused folder: cd there, then run
        // claude / codex yourself (the pane follows the program running in it).
        Placement p;
        p.target = name == "space.new"                 ? LaunchTarget::kSpace
                   : name == "tab.new" || focused.empty() ? LaunchTarget::kTab
                   : arg == "down"                      ? LaunchTarget::kSplitDown
                                                        : LaunchTarget::kSplitRight;
        p.split_of = focused;
        p.space = s.ws.active_space;
        p.machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
        if (s.machine_of.count(focused) && p.target != LaunchTarget::kSpace)
            p.machine = s.machine_of[focused];
        ui::HostClient* h = s.host_for(p.machine);
        const std::string shell = pick_shell(s.settings.shell_profile, h->profiles());
        if (!h->connected() || shell.empty()) {
            open_launcher(s, p.target);
        } else {
            std::string cwd = s.focused_cwd();
            if (cwd.empty())
                cwd = s.launch_cwd;
            create_session(s, shell, cwd, "", p);
        }
    } else if (name == "agent.new") {
        if (focused.empty())
            open_launcher(s, LaunchTarget::kTab);
        else
            open_launcher(s, arg == "space" ? LaunchTarget::kSpace : LaunchTarget::kTab);
    } else if (name == "pane.close") {
        // herdr: ends the terminal; the last pane closes the tab (and maybe asks about the space).
        if (focused.empty())
            return "no pane to close";
        {
            const auto* t = s.ws.active_tab();
            if (t != nullptr && s.ws.sessions_in(*t).size() == 1) {
                return exec_command(s, "tab.close");
            } else {
                if (auto it = s.by_id.find(focused); it != s.by_id.end())
                    it->second->stop(true);
                s.hidden.insert(focused);
                s.ws.remove(focused);
                s.focus_view(s.ws.focused_session());
            }
        }
    } else if (name == "terminal.kill") {
        // herdr: end a terminal and its sub-agents.
        const std::string target = arg.empty() ? focused : arg;
        for (const auto& [id, v] : s.views_by_id)
            if (id == target || v.parent == target)
                if (auto it = s.by_id.find(id); it != s.by_id.end())
                    it->second->stop(true);
    } else if (name == "app.reset") {
        // herdr escape hatch: keys.json set aside, default layout, sidebar back.
        const auto keys = data_dir() / L"keys.json";
        std::error_code ec;
        if (std::filesystem::exists(keys, ec))
            std::filesystem::rename(keys, std::filesystem::path(keys).concat(L".aside"), ec);
        s.keymap = ui::Keymap::defaults();
        s.keymap_errors.clear();
        s.settings.layout = "workspace";
        save_settings(s.settings);
        // The workspace's sidebar back as shipped (shown, 268 px).
        if (lay::Node* side = s.layout().tree.find("sidebar")) {
            const lay::Node* shipped = s.layout().file.find("sidebar");
            side->hidden = shipped != nullptr && shipped->hidden;
            side->size = shipped != nullptr ? shipped->size : side->size;
            save_layout_state(s.layout());
        }
        s.toast("Back to the default screen", "keys.json was set aside as keys.json.aside.", "", g_pal->info);
    } else if (name == "pane.park") {
        // herdr terminal.park: a sub-agent's pane goes away; it keeps running under its parent
        // (agents list, peek).
        if (!focused.empty()) {
            s.hidden.insert(focused);
            s.ws.remove(focused);
            s.focus_view(s.ws.focused_session());
        }
    } else if (name == "pane.zoom") {
        s.ws.toggle_zoom();
    } else if (name == "pane.go") {
        if (s.ws.focus_dir(parse_dir(arg), s.pane_area))
            s.focus_view(s.ws.focused_session());
    } else if (name == "pane.swap") {
        s.ws.swap_dir(parse_dir(arg), s.pane_area);
    } else if (name == "pane.resize") {
        s.ws.resize(parse_dir(arg), px(40), s.pane_area);
    } else if (name == "pane.rename") {
        if (focused.empty())
            return "no pane to rename";
        open_rename(s, "pane", focused, s.pane_title(focused));
    } else if (name == "tab.rename") {
        // herdr tab.rename {id}: that tab (default: the shown one), without selecting it.
        const auto loc = arg.empty() ? ui::PaneLoc{} : ui::find_tab(s.ws, arg);
        const ui::Tab* t = !arg.empty() ? (loc.space < 0 ? nullptr
                                                          : &s.ws.spaces[static_cast<std::size_t>(loc.space)]
                                                                 .tabs[static_cast<std::size_t>(loc.tab)])
                                        : s.ws.active_tab();
        if (t == nullptr)
            return arg.empty() ? std::string("no tab to rename") : "no tab \"" + arg + "\"";
        open_rename(s, "tab", t->id, t->name);
    } else if (name == "space.rename") {
        auto* sp = s.ws.active();
        if (sp == nullptr)
            return "no space to rename";
        open_rename(s, "space", sp->id, sp->name);
    } else if (name == "tab.close") {
        // herdr tab.close {id} (window.rs 3122-3132): by id, the shown tab and space stay; the
        // last tab takes its space with it, so that asks like closing the space.
        ui::PaneLoc loc;
        if (arg.empty()) {
            if (s.ws.active_tab() != nullptr)
                loc = {s.ws.active_space, s.ws.active()->active_tab};
        } else {
            loc = ui::find_tab(s.ws, arg);
        }
        if (loc.space < 0)
            return arg.empty() ? std::string("no tab to close") : "no tab \"" + arg + "\"";
        auto& sp = s.ws.spaces[static_cast<std::size_t>(loc.space)];
        if (sp.tabs.size() == 1) {
            confirm_close_space(s, loc.space);
        } else {
            for (const auto& id : s.ws.sessions_in(sp.tabs[static_cast<std::size_t>(loc.tab)])) {
                if (auto it = s.by_id.find(id); it != s.by_id.end())  // herdr: ends the tab's terminals
                    it->second->stop(true);
                s.hidden.insert(id);
            }
            s.ws.close_tab(loc.space, loc.tab);
            s.focus_view(s.ws.focused_session());
        }
    } else if (name == "space.close") {
        // herdr asks first ("Close space?"), then ends its terminals.
        if (s.ws.active() == nullptr)
            return "no space to close";
        confirm_close_space(s, s.ws.active_space);
    } else if (name == "space.close_now") {
        for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i) {
            const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
            if (sp.id != arg)
                continue;
            for (const auto& t : sp.tabs)
                for (const auto& id : s.ws.sessions_in(t))
                    if (auto it = s.by_id.find(id); it != s.by_id.end())
                        it->second->stop(true);
            s.ws.close_space(i);
            s.focus_view(s.ws.focused_session());
            break;
        }
    } else if (name == "panel") {
        // herdr layout.toggle:panel / the Files and Changes buttons: a per-space choice.
        if (auto* sp = s.ws.active()) {
            const std::string which = arg.empty() ? std::string("files") : arg;
            sp->panel = sp->panel == which ? std::string() : which;
            s.doc_dirty = true;
            s.doc_dirty_at = Clock::now();
        }
    } else if (name == "sidebar.rail") {
        // herdr workspace-chrome/main.js 18-24 toggleSidebar(): remember the width and
        // layout.resize the sidebar to 44, or back to it (a hidden one is shown first).
        LayoutSpec& l = s.layout();
        lay::Node* side = l.tree.find("sidebar");
        if (side == nullptr)
            return "no node \"sidebar\" in the shown layout";
        const float width = side->size && side->size->px ? side->size->value : 268.0f;
        if (side->hidden) {
            side->hidden = false;
        } else if (width >= 160) {
            s.settings.sidebar_restore = width;
            save_settings(s.settings);
            side->size = lay::Span::pixels(44);
        } else {
            side->size = lay::Span::pixels(std::max(160.0f, s.settings.sidebar_restore));
        }
        save_layout_state(l);
    } else if (name == "app.picker") {
        s.show_picker = !s.show_picker;
        s.picker_buf[0] = '\0';
        s.picker_sel = -1;
        s.picker_reset = true;
    } else if (name == "tab.goto" || name == "tab.close_at") {
        const auto c = arg.find(':');
        const int sp = std::atoi(arg.substr(0, c).c_str());
        const int t = c == std::string::npos ? 0 : std::atoi(arg.substr(c + 1).c_str());
        if (sp < 0 || sp >= static_cast<int>(s.ws.spaces.size()) || t < 0 ||
            t >= static_cast<int>(s.ws.spaces[static_cast<std::size_t>(sp)].tabs.size()))
            return "no such tab";
        if (name == "tab.close_at")  // by id: the shown tab and space stay (herdr picker ctrl+d)
            return exec_command(s, "tab.close:" + s.ws.spaces[static_cast<std::size_t>(sp)].tabs[static_cast<std::size_t>(t)].id);
        s.ws.select_space(sp);
        s.ws.select_tab(t);
        s.focus_view(s.ws.focused_session());
    } else if (name == "space.new_on") {
        Placement p;
        p.target = LaunchTarget::kSpace;
        p.machine = arg;
        ui::HostClient* h = s.host_for(arg);
        const std::string shell = pick_shell(s.settings.shell_profile, h->profiles());
        if (h->connected() && !shell.empty())
            create_session(s, shell, "", "", p);
    } else if (name == "window.minimize") {
        ::ShowWindow(s.config.hwnd, SW_MINIMIZE);
    } else if (name == "window.maximize") {
        ::ShowWindow(s.config.hwnd, ::IsZoomed(s.config.hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    } else if (name == "window.close") {
        ::PostMessageW(s.config.hwnd, WM_CLOSE, 0, 0);
    } else if (name == "machine.add") {
        s.show_machine_add = true;
    } else if (name == "app.welcome") {
        s.show_welcome = true;
    } else if (name == "worktree.open") {
        // Existing worktrees of the focused repo (git worktree list --porcelain, bare and prunable
        // skipped): main checkout first, then by the branch's last commit, newest first.
        s.worktree_list.clear();
        s.worktree_not_repo.clear();
        const std::string cwd = s.focused_cwd();
        const std::string machine = s.machine_of[s.ws.focused_session()];
        s.worktree_machine = machine;
        const auto g = s.git.get(cwd, machine);
        // herdr worktree/main.js checkouts(): each branch's last commit date, from its ref.
        std::map<std::string, long long> when;
        if (!cwd.empty()) {
            const auto refs = git_at(s, machine, cwd,
                                     {L"for-each-ref", L"--format=%(refname:short)%09%(committerdate:unix)", L"refs/heads"});
            std::stringstream rs(refs.started && refs.exit_code == 0 ? refs.output : std::string());
            for (std::string l; std::getline(rs, l);) {
                if (!l.empty() && l.back() == '\r')
                    l.pop_back();
                const auto tab = l.find('\t');
                if (tab != std::string::npos)
                    when[l.substr(0, tab)] = std::atoll(l.c_str() + tab + 1);
            }
        }
        if (!cwd.empty()) {
            const auto r = git_at(s, machine, cwd, {L"worktree", L"list", L"--porcelain"});
            std::stringstream ss(r.output + "\n");
            WorktreeEntry e;
            bool skip = false;
            for (std::string l; std::getline(ss, l);) {
                if (!l.empty() && l.back() == '\r')
                    l.pop_back();
                if (l.rfind("worktree ", 0) == 0) {
                    e = {};
                    skip = false;
                    e.path = l.substr(9);
                    for (auto& c : e.path)
                        if (c == '/')
                            c = '\\';
                } else if (l.rfind("branch ", 0) == 0) {
                    e.branch = l.substr(7);
                    if (e.branch.rfind("refs/heads/", 0) == 0)
                        e.branch = e.branch.substr(11);
                } else if (l == "bare" || l.rfind("prunable", 0) == 0) {
                    skip = true;
                } else if (l.empty() && !e.path.empty()) {
                    if (!skip) {
                        e.main = s.worktree_list.empty();  // git lists the main checkout first
                        // A detached checkout has no branch, so no date (herdr: at 0, no age).
                        const auto it = e.branch.empty() ? when.end() : when.find(e.branch);
                        e.time = it != when.end() ? it->second : 0;
                        // herdr ago(): rounded, minutes at least 1.
                        const double d = static_cast<double>(static_cast<long long>(std::time(nullptr)) - e.time);
                        auto round_to = [](double v) { return std::to_string(static_cast<long long>(std::floor(v + 0.5))); };
                        e.age = e.time <= 0           ? std::string()
                                : d < 3600.0          ? std::to_string(std::max(1LL, static_cast<long long>(std::floor(d / 60.0 + 0.5)))) + "m"
                                : d < 86400.0         ? round_to(d / 3600.0) + "h"
                                : d < 86400.0 * 60.0  ? round_to(d / 86400.0) + "d"
                                                      : round_to(d / 86400.0 / 30.0) + "mo";
                        s.worktree_list.push_back(e);
                    }
                    e = {};
                }
            }
            std::stable_sort(s.worktree_list.begin(), s.worktree_list.end(),
                             [](const WorktreeEntry& a2, const WorktreeEntry& b2) {
                                 if (a2.main != b2.main)
                                     return a2.main;
                                 return a2.time > b2.time;
                             });
        }
        // Outside git the dialog itself says so (herdr renderOpen).
        if (s.worktree_list.empty())
            s.worktree_not_repo = cwd.empty() ? std::string("This folder") : display_path(cwd);
        s.show_worktree_open = true;
    } else if (name == "worktree.open_at") {
        // arg: "<machine>|<path>": a space on that machine already in the checkout, else a new one.
        const auto bar = arg.find('|');
        const std::string machine = bar == std::string::npos ? std::string() : arg.substr(0, bar);
        const std::string path = bar == std::string::npos ? arg : arg.substr(bar + 1);
        int best = -1;
        for (int i = 0; i < static_cast<int>(s.ws.spaces.size()) && best < 0; ++i) {
            const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
            if (sp.machine != machine || sp.tabs.empty())
                continue;
            const auto ids = s.ws.sessions_in(sp.tabs.front());
            if (!ids.empty() && s.views_by_id.count(ids.front())) {
                const auto top = s.git.get(s.views_by_id[ids.front()].cwd, machine).top;
                if (_stricmp(top.c_str(), path.c_str()) == 0)
                    best = i;
            }
        }
        if (best >= 0) {
            s.ws.select_space(best);
            s.focus_view(s.ws.focused_session());
            return {};
        }
        Placement p;
        p.target = LaunchTarget::kSpace;
        p.machine = machine;
        ui::HostClient* h = s.host_for(machine);
        const std::string shell = pick_shell(s.settings.shell_profile, h->profiles());
        if (!shell.empty())
            create_session(s, shell, path, "", p);
    } else if (name == "worktree.remove") {
        const std::string machine = s.machine_of[s.ws.focused_session()];
        const auto g = s.git.get(s.focused_cwd(), machine);
        if (!g.repo)
            return "not a git checkout";
        if (!g.worktree) {
            return "not a worktree: this is the repository's main checkout";
        } else {
            const auto risk = worktree_risk(s, machine, g.top);
            const auto loc = s.ws.find(s.ws.focused_session());
            open_confirm(s, "Remove worktree " + (loc.space >= 0 ? s.ws.space_title(loc.space, s.session_cwds()) : g.top) + "?",
                         worktree_remove_text(g.top, risk), "Remove",
                         "worktree.remove_checked:" + risk.signature() + "|" + machine + "|" + g.top);
        }
    } else if (name == "worktree.remove_checked") {
        // herdr worktree.js 228-245: Enter re-checks; a risk that appeared since the dialog
        // opened asks once more ("Enter again removes it anyway").
        const auto bar = arg.find('|');
        const auto bar2 = bar == std::string::npos ? std::string::npos : arg.find('|', bar + 1);
        const std::string seen = arg.substr(0, bar);
        const std::string machine = bar2 == std::string::npos ? std::string() : arg.substr(bar + 1, bar2 - bar - 1);
        const std::string top = bar2 == std::string::npos ? std::string() : arg.substr(bar2 + 1);
        const auto risk = worktree_risk(s, machine, top);
        const std::string force = risk.dirty || risk.submodules ? "worktree.remove_force:" : "worktree.remove_now:";
        if (risk.signature() != seen) {
            open_confirm(s, "Remove worktree?",
                         worktree_remove_text(top, risk) + "\n\nIt changed since this opened. Enter again removes it anyway.",
                         "Remove", force + machine + "|" + top);
        } else {
            return exec_command(s, force + machine + "|" + top);
        }
    } else if (name == "worktree.remove_now" || name == "worktree.remove_force") {
        // arg: "<machine>|<top>" (an older "<top>" means this computer)
        const auto bar = arg.find('|');
        const std::string machine = bar == std::string::npos ? std::string() : arg.substr(0, bar);
        const std::string top = bar == std::string::npos ? arg : arg.substr(bar + 1);
        const auto g = s.git.get(top, machine);
        const std::string main = g.common_top.empty() ? top : g.common_top;
        // End the terminals of spaces in that checkout first (they hold the folder open).
        for (int i = static_cast<int>(s.ws.spaces.size()) - 1; i >= 0; --i) {
            bool inside = false;
            for (const auto& t : s.ws.spaces[static_cast<std::size_t>(i)].tabs)
                for (const auto& id : s.ws.sessions_in(t))
                    if (s.views_by_id.count(id) && s.machine_of[id] == machine &&
                        _strnicmp(s.views_by_id[id].cwd.c_str(), top.c_str(), top.size()) == 0)
                        inside = true;
            if (inside)
                run_command(s, "space.close_now:" + s.ws.spaces[static_cast<std::size_t>(i)].id);
        }
        std::vector<std::wstring> argv = {L"worktree", L"remove"};
        if (name == "worktree.remove_force")
            argv.push_back(L"--force");
        argv.push_back(win::to_wide(top));
        const auto r = git_at(s, machine, main, argv, std::chrono::seconds(600));  // herdr worktree.js 243
        s.git.invalidate(top);
        if (!r.started || r.exit_code != 0)  // shown as "worktree.remove failed: ..."
            return r.started ? r.message() : std::string("git did not run");
    } else if (name == "tab.move") {
        if (auto* sp = s.ws.active())
            s.ws.move_tab(s.ws.active_space, sp->active_tab, sp->active_tab + (arg == "left" ? -1 : 1));
    } else if (name == "tab.settle" || name == "tab.unsettle") {
        // herdr: put a tab away (out of the bar, still running) until shown or it needs you.
        if (auto* sp = s.ws.active()) {
            if (name == "tab.unsettle") {
                for (int i = 0; i < static_cast<int>(sp->tabs.size()); ++i)
                    s.ws.unsettle_tab(s.ws.active_space, i);
            } else if (!s.ws.settle_tab(s.ws.active_space, sp->active_tab)) {
                s.toast("Nothing to put away", "The last tab shown in a space stays.", "", g_pal->info);
            }
            s.doc_dirty = true;
            s.doc_dirty_at = Clock::now();
            s.focus_view(s.ws.focused_session());
        }
    } else if (name == "terminal.right_click") {
        if (!focused.empty()) {
            if (arg == "program")
                s.ws.right_click_program.insert(focused);
            else
                s.ws.right_click_program.erase(focused);
            s.doc_dirty = true;
            s.doc_dirty_at = Clock::now();
        }
    } else if (name == "tab.next" || name == "tab.prev") {
        s.ws.cycle_tab(name == "tab.next" ? 1 : -1);
        s.focus_view(s.ws.focused_session());
    } else if (name == "tab.select") {
        s.ws.select_shown_tab(std::atoi(arg.c_str()));
        s.focus_view(s.ws.focused_session());
    } else if (name == "space.next" || name == "space.prev") {
        s.ws.cycle_space(name == "space.next" ? 1 : -1);
        s.focus_view(s.ws.focused_session());
    } else if (name == "space.select") {
        s.ws.select_space(std::atoi(arg.c_str()) - 1);
        s.focus_view(s.ws.focused_session());
    } else if (name == "agent.next" || name == "agent.prev") {
        if (!s.agent_order.empty()) {
            auto it = std::find(s.agent_order.begin(), s.agent_order.end(), focused);
            int i = it == s.agent_order.end() ? -1 : static_cast<int>(it - s.agent_order.begin());
            const int n = static_cast<int>(s.agent_order.size());
            i = name == "agent.next" ? (i + 1) % n : (i <= 0 ? n - 1 : i - 1);
            s.focus(s.agent_order[static_cast<std::size_t>(i)]);
        }
    } else if (name == "agent.focus" || name == "session.focus") {
        s.focus(arg);
    } else if (name == "agent.peek") {
        s.peek = arg;
        s.views[arg].focus_next = true;
    } else if (name == "agent.keep") {
        // Give a sub-agent a pane: beside its parent when the parent is shown, else a new tab.
        const auto it = s.views_by_id.find(arg);
        if (it != s.views_by_id.end() && s.ws.find(arg).space < 0) {
            s.hidden.erase(arg);
            if (!it->second.parent.empty() && s.ws.find(it->second.parent).space >= 0)
                s.ws.split(it->second.parent, arg, Dir::kRight);
            else
                s.ws.add_tab(arg, s.space_for_cwd(it->second.cwd));
        }
        s.peek.clear();
        s.focus(arg);
    } else if (name == "terminal.search") {
        if (!focused.empty())
            s.views[focused].view.open_search();
    } else if (name == "terminal.copy" || name == "terminal.paste" || name == "terminal.paste_image" ||
               name == "terminal.select_all" || name == "terminal.scroll") {
        // Run by the focused terminal on its next frame (palette, keys.json in global).
        if (!focused.empty())
            s.views[focused].view.request(cmd);
    } else if (name == "worktree.new") {
        s.show_worktree = true;
        s.wt_error.clear();
        std::snprintf(s.wt_repo, sizeof s.wt_repo, "%s", s.focused_cwd().c_str());
        s.wt_branch[0] = '\0';
    } else if (name == "session.interrupt") {
        if (auto r = session())
            r->send(host::RawInput{std::string("\x03", 1)});
    } else if (name == "session.stop") {
        if (auto r = session())
            r->stop(false);
    } else if (name == "session.force_stop") {
        if (auto r = session())
            r->stop(true);
    } else if (name == "session.restart") {
        const auto it = s.views_by_id.find(focused);
        if (it != s.views_by_id.end()) {
            Placement p;
            p.replace = focused;
            p.machine = s.machine_of.count(focused) ? s.machine_of[focused] : std::string();
            create_session(s, it->second.profile_id, it->second.cwd, it->second.name, p);
        }
    } else if (name == "sidebar.toggle") {
        // herdr ctrl-shift-b: layout.toggle {"id": "sidebar"} (nothing when the layout has none).
        if (s.layout().tree.find("sidebar") != nullptr)
            return *layout_command(s, "layout.toggle", "sidebar");
    } else if (name == "details.toggle") {
        s.settings.details = !s.settings.details;
        save_settings(s.settings);
    } else if (name == "app.palette") {
        s.show_palette = !s.show_palette;
        s.palette_buf[0] = '\0';
        s.palette_sel = 0;
    } else if (name == "app.help") {
        s.show_help = !s.show_help;
    } else if (name == "app.settings") {
        s.show_settings = true;
        // herdr app.settings {section}: open on that page ("mods", "alerts", ...).
        for (const char* sec : {"Look", "Machines", "Alerts", "Mods", "Updates", "Privacy"})
            if (_stricmp(arg.c_str(), sec) == 0)
                s.settings_tab = sec;
    } else if (name == "app.zoom_in" || name == "app.zoom_out" || name == "app.zoom_reset") {
        s.settings.zoom =
            name == "app.zoom_reset"
                ? 1.0f
                : std::clamp(std::round((s.settings.zoom + (name == "app.zoom_in" ? 0.1f : -0.1f)) * 10.0f) / 10.0f,
                             0.5f, 3.0f);
        save_settings(s.settings);
        g_scale = s.dpi * ui_scale(s.settings);
        apply_style(s.dpi, ui_scale(s.settings));
    } else if (name == "app.keys") {
        const auto path = data_dir() / L"keys.json";
        load_keys(s);  // writes the defaults when the file is missing
        ::ShellExecuteW(nullptr, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
        s.toast("keys.json opened", "Saved changes apply right away.", "", g_pal->info);
    } else if (name == "app.exit_all") {
        s.show_exit = true;
        s.exit_error.clear();
    } else if (name == "theme") {
        // herdr app.theme (window.rs 3620-3635): the theme goes on the shown layout, as its light
        // or its dark half; the other half stays. Appearance is not changed.
        const Palette* p = palette_by_id(arg);
        auto pair = layout_theme_pair(s.settings);
        (p->light ? pair.second : pair.first) = p->id;
        s.settings.layout_themes[s.settings.layout] = pair;
        s.settings.theme = p->id;
        save_settings(s.settings);
    } else if (name == "layout.switch" || name == "layout") {
        std::string next = arg;
        if (next.empty()) {  // cycle
            for (std::size_t i = 0; i < s.layouts.size(); ++i)
                if (s.layouts[i].name == s.settings.layout)
                    next = s.layouts[(i + 1) % s.layouts.size()].name;
        }
        if (next.empty() && !s.layouts.empty())
            next = s.layouts.front().name;
        s.settings.layout = next;
        save_settings(s.settings);
        s.toast("Layout: " + next, "", "", g_pal->info, 2);
        s.focus_view(s.ws.focused_session());
    } else if (name == "app.check_updates") {
        ui::updates::check_now();  // window.rs 3603
    } else if (name == "app.update_core") {
        // window.rs 3604-3609: nothing waiting (and nothing failed) is an error; else
        // `agentctl update` (herdr: `hc update`) in the background. agent-host hands its
        // terminals to the new one; they keep running (design doc 7.4 OQ-09).
        const auto c = ui::updates::core();
        if (!c.ready && !c.error)
            return "agent-host is up to date";
        ui::updates::update_core();
    } else if (name == "app.update") {
        // window.rs 3645-3657
        std::string error;
        if (!ui::updates::apply(&error)) {
            if (error.empty())
                return "no update is ready yet";
            return "Close AgentDesk's terminals (or restart Windows) and try again. Couldn't replace "
                   "AgentDesk's files: " +
                   error + ".";
        }
        ui::updates::relaunch();
        s.exit_now = true;
    } else if (name == "app.whats_new" || name == "app.feedback") {
        s.show_notes = true;
        s.notes_kind = name == "app.whats_new" ? "whats_new" : "feedback";
        s.feedback_note.clear();
        if (name == "app.feedback" && !arg.empty() && s.feedback_buf[0] == '\0')
            std::snprintf(s.feedback_buf, sizeof s.feedback_buf, "%s", arg.c_str());
    } else if (name == "layout.clone") {
        // herdr window.rs 3443-3454 and 266-290 clone_layout: the layout (default: the shown one)
        // as `as` (default <name>-copy), never over a file that is there, marked cloned_from, with
        // its clicks; then shown. Ours is written in herdr's format, also from an old file.
        const Json a = layout_args(arg, "name", "as");
        const std::string from = a.value("name", std::string()).empty() ? s.settings.layout : a.value("name", std::string());
        const std::string to = a.value("as", std::string()).empty() ? from + "-copy" : a.value("as", std::string());
        auto valid = [](const std::string& n) {  // mods.rs valid_name: letters, digits, - or _
            return !n.empty() && std::all_of(n.begin(), n.end(), [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_';
            });
        };
        if (!valid(from) || !valid(to))
            return "cannot clone layout: names use letters, digits, - or _";
        const auto it =
            std::find_if(s.layouts.begin(), s.layouts.end(), [&](const LayoutSpec& l) { return l.name == from; });
        if (it == s.layouts.end())
            return "cannot clone layout: no layout \"" + from + "\"";
        save_layout_state(*it);
        std::error_code ec;
        std::filesystem::create_directories(layouts_dir(), ec);
        const auto dest = layouts_dir() / win::to_wide(to + ".json");
        if (std::filesystem::exists(dest, ec) ||
            std::any_of(s.layouts.begin(), s.layouts.end(), [&](const LayoutSpec& l) { return l.name == to; }))
            return "cannot clone layout: layouts\\" + to + ".json is already there";
        lay::Node node = it->file;
        node.cloned_from = Json{{"name", from}, {"version", AGENTDESK_VERSION_STRING}};
        {
            std::ofstream out(dest, std::ios::binary);
            out << lay::text_of(node);
        }
        if (std::filesystem::exists(layout_state_path(from), ec))
            std::filesystem::copy_file(layout_state_path(from), layout_state_path(to),
                                       std::filesystem::copy_options::overwrite_existing, ec);
        s.layouts = load_layouts(s.settings);
        run_command(s, "layout:" + to);
        s.toast("Layout duplicated", "layouts\\" + to + ".json", "", g_pal->info);
    } else if (name == "layout.reset") {
        // herdr window.rs 3514-3533: only a layout herdr ships; a file of that name is set aside
        // (<name>.json.old-<time>, never over an older one, window.rs 218-224) and its clicks go.
        const std::string which = arg.empty() ? s.settings.layout : arg;
        if (lay::builtin_text(which).empty())
            return "\"" + which + "\" isn't a layout AgentDesk ships";
        std::error_code ec;
        int aside = 0;
        std::vector<std::filesystem::path> files;
        for (const auto& e : std::filesystem::directory_iterator(layouts_dir(), ec))
            if (e.is_regular_file() && e.path().extension() == L".json")
                files.push_back(e.path());
        const long long stamp = static_cast<long long>(std::time(nullptr));
        for (const auto& f : files) {
            const auto j = Json::parse(read_text_file(f), nullptr, false);
            const auto stem = f.stem().u8string();
            const std::string file_name(stem.begin(), stem.end());
            // An old AgentDesk file says its name inside.
            const std::string n = lay::is_legacy(j) ? j.value("name", file_name) : file_name;
            if (n != which)
                continue;
            std::filesystem::path to;
            for (int i = 1;; ++i) {
                to = f;
                to += win::to_wide(".old-" + std::to_string(stamp) + (i == 1 ? "" : "-" + std::to_string(i)));
                if (!std::filesystem::exists(to, ec))
                    break;
            }
            std::filesystem::rename(f, to, ec);
            if (ec)
                return "cannot set layouts\\" + file_name + ".json aside: " + ec.message();
            ++aside;
        }
        std::filesystem::remove(layout_state_path(which), ec);
        s.layouts = load_layouts(s.settings);
        if (std::none_of(s.layouts.begin(), s.layouts.end(),
                         [&](const LayoutSpec& l) { return l.name == s.settings.layout; }))
            run_command(s, "layout:workspace");
        s.toast("Layout reset: " + which, aside > 0 ? "Your file was kept aside as <name>.json.old-<time>." : "", "",
                g_pal->info);
    } else if (name == "mod.clone") {
        if (arg.empty()) {
            open_rename(s, "mod.clone", "", "");
        } else {
            const auto dir = data_dir() / L"mods";
            const auto src = dir / win::to_wide(arg);
            std::error_code ec;
            if (arg.find_first_of("/\\:") != std::string::npos || arg == "." || arg == ".." ||
                !std::filesystem::is_directory(src, ec)) {
                s.toast("No mod named " + arg, "Mods are folders in the mods folder.", "", g_pal->info);
            } else {
                std::string to = arg + "-copy";
                for (int n = 2; std::filesystem::exists(dir / win::to_wide(to), ec); ++n)
                    to = arg + "-copy-" + std::to_string(n);
                std::filesystem::copy(src, dir / win::to_wide(to), std::filesystem::copy_options::recursive, ec);
                if (ec)
                    s.toast("Could not duplicate " + arg, ec.message(), "", g_pal->needs_you);
                else
                    s.toast("Mod duplicated", "mods\\" + to + " (off until you enable it)", "", g_pal->info);
            }
        }
    } else if (name == "mods.reload") {
        s.layouts = load_layouts(s.settings);
        s.mods->load(s.settings.mods_enabled);
        s.toast("Mods reloaded", "", "", g_pal->info, 2);
    } else if (name == "mods.folder") {
        const auto dir = data_dir() / L"mods";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        ::ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (name == "mod") {
        const auto c2 = arg.find(':');
        if (c2 == std::string::npos)
            return "mod needs <mod>:<command>";
        s.mods->run_command(arg.substr(0, c2), arg.substr(c2 + 1));
    } else if (name.rfind("win98.", 0) == 0) {
        // Herdr 98's key commands (README.md 12-17), delivered to its taskbar (taskbar.js 126-176)
        // while the layout shows.
        if (s.layout().style == "win98")
            w98::command(s, name.substr(6));
    } else if (const auto dot = name.find('.');
               dot != std::string::npos && [&] {
                   std::error_code ec;  // "<mod>.<command>": keys.json accepts it when that mod folder exists
                   return std::filesystem::is_directory(data_dir() / L"mods" / win::to_wide(name.substr(0, dot)), ec);
               }()) {
        s.mods->run_command(name.substr(0, dot), name.substr(dot + 1));
    } else {
        // herdr window.rs 2975-2976.
        return "unknown command \"" + name + "\"";
    }
    return {};
}

// herdr window.rs 2931-2943: a command run for the person (a click, a key, a menu, a mod) has
// nobody to read the reply, so a failure shows as an error toast "<command> failed".
void run_command(App::State& s, const std::string& cmd, bool person) {
    // herdr window.rs 2995-2997 / telemetry.rs 124: a command the person ran, by its name.
    if (const std::string name = cmd.substr(0, cmd.find(':')); person && builtin_command(name))
        ui::telemetry::command(name);
    const std::string error = exec_command(s, cmd);
    if (!error.empty())
        s.toast(cmd.substr(0, cmd.find(':')) + " failed", error, "", g_pal->needs_you, 8);
}

}  // namespace

// ------------------------------------------------------------------ drawing
namespace {

ImU32 mod_color(const std::string& c) {
    if (c == "dim")
        return g_pal->dim;
    if (c == "accent")
        return g_pal->focus;
    if (c == "good")
        return g_pal->done;
    if (c == "warn")
        return g_pal->working;
    if (c == "bad")
        return g_pal->needs_you;
    return g_pal->text;
}

float fpx(float v);
float px(float v);

// Mod panels, inset and titled like the sidebar's own sections (herdr spaces/agents headers).
// only_mod: just that mod's panels (a layout block naming it).
void draw_mod_panels(App::State& s, const std::string& where, const std::string& only_mod = {}) {
    const float inset = px(18);
    for (const auto& panel : s.mods->panels(where)) {
        if (!only_mod.empty() && panel.mod != only_mod)
            continue;
        ImGui::PushID((panel.mod + "/" + panel.id).c_str());
        ImGui::Dummy(ImVec2(0, px(8)));
        ImGui::Indent(inset);
        if (!panel.title.empty()) {
            ImGui::PushFont(s.fonts.ui_medium, 11.5f);
            ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
            ImGui::TextUnformatted(panel.title.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, px(2)));
        }
        ImGui::PushFont(s.fonts.ui, 12.5f);
        ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - inset);
        for (std::size_t i = 0; i < panel.widgets.size(); ++i) {
            const auto& w = panel.widgets[i];
            ImGui::PushID(static_cast<int>(i));
            switch (w.kind) {
                case ui::ModWidget::Kind::kText:
                    ImGui::PushStyleColor(ImGuiCol_Text, v4(mod_color(w.color)));
                    ImGui::TextWrapped("%s", w.text.c_str());
                    ImGui::PopStyleColor();
                    break;
                case ui::ModWidget::Kind::kButton:
                    if (ImGui::SmallButton(w.text.c_str()))
                        s.mods->click(panel.mod, panel.id, w.id);
                    break;
                case ui::ModWidget::Kind::kSeparator:
                    ImGui::Separator();
                    break;
                case ui::ModWidget::Kind::kProgress:
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, v4(g_pal->focus));
                    ImGui::ProgressBar(w.value, ImVec2(ImGui::GetContentRegionAvail().x - inset, px(6)), "");
                    ImGui::PopStyleColor();
                    if (!w.text.empty())
                        ImGui::TextDisabled("%s", w.text.c_str());
                    break;
                case ui::ModWidget::Kind::kRow: {
                    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
                    ImGui::TextUnformatted(w.text.c_str());
                    ImGui::PopStyleColor();
                    const float rw = ImGui::CalcTextSize(w.text2.c_str()).x;
                    ImGui::SameLine(ImGui::GetContentRegionMax().x - rw - inset);
                    ImGui::PushStyleColor(ImGuiCol_Text, v4(mod_color(w.color)));
                    ImGui::TextUnformatted(w.text2.c_str());
                    ImGui::PopStyleColor();
                    break;
                }
                case ui::ModWidget::Kind::kBadge: {
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    const ImVec2 t = ImGui::CalcTextSize(w.text.c_str());
                    ImGui::GetWindowDrawList()->AddRectFilled(p,
                                                              ImVec2(p.x + t.x + px(12), p.y + t.y + px(4)),
                                                              alpha(mod_color(w.color), 60), px(8));
                    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + px(6), p.y + px(2)), mod_color(w.color),
                                                        w.text.c_str());
                    ImGui::Dummy(ImVec2(t.x + px(12), t.y + px(4)));
                    break;
                }
            }
            ImGui::PopID();
        }
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::Unindent(inset);
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, px(4)));
    }
}

// herdr tabs.js 25-29: a tab is shown by its lead pane: the first agent, else the first pane.
std::string lead_pane(App::State& s, const std::vector<std::string>& ids);

// ------------------------------------------------------------------ herdr window chrome
// Layout, sizes and behaviour follow herdr-app's default layout and mods (layouts/main.json,
// mods/workspace-chrome, spaces, agents, tabs, pane, workspace-status; Apache-2.0, see
// docs/research/herdr-app/spec-sidebar-layout.md). Sizes are herdr design px through px().

float fpx(float v) {
    return v * g_scale;
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a);
    const ImVec4 y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t,
                                                 x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

// herdr tokens derived from our palettes: raised (active tab), popover (menus).
ImU32 raised_color() {
    return g_pal->light ? mix(g_pal->card, IM_COL32(0, 0, 0, 255), 0.05f)
                        : mix(g_pal->card, IM_COL32(255, 255, 255, 255), 0.09f);
}

// herdr's accent_foreground: text on the accent fill.
ImU32 accent_foreground() {
    return IM_COL32(255, 255, 255, 255);
}

// Avatar colours: herdr's fixed table (spaces.js MARK), the same in every theme.
ImU32 avatar_color(const std::string& name) {
    if (name == "ansi3")
        return IM_COL32(0xe3, 0x81, 0x4f, 255);
    if (name == "ansi4")
        return IM_COL32(0x5b, 0x8d, 0xef, 255);
    if (name == "ansi5")
        return IM_COL32(0x8b, 0x7c, 0xf6, 255);
    if (name == "ansi2")
        return IM_COL32(0x3f, 0xae, 0x6a, 255);
    if (name == "ansi1")
        return IM_COL32(0xd9, 0x64, 0x9a, 255);
    return IM_COL32(0x22, 0xa9, 0x9a, 255);  // ansi6
}

void draw_icon(App::State& s, ImDrawList* dl, const char* glyph, float size, ImVec2 center, ImU32 col) {
    const ImVec2 t = s.fonts.icons->CalcTextSizeA(size, FLT_MAX, 0, glyph);
    dl->AddText(s.fonts.icons, size, ImVec2(std::round(center.x - t.x * 0.5f), std::round(center.y - t.y * 0.5f)),
                col, glyph);
}

// herdr icon button: square, radius 6, muted icon; hover background + foreground.
bool icon_button(App::State& s, const char* id, const char* glyph, ImVec2 pos, float side, float icon,
                 const char* tip, ImU32 col, bool on = false) {
    ImGui::SetCursorScreenPos(pos);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(side, side));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hov || on)
        dl->AddRectFilled(pos, ImVec2(pos.x + side, pos.y + side), on ? g_pal->selected : g_pal->hover, px(6));
    draw_icon(s, dl, glyph, icon, ImVec2(pos.x + side * 0.5f, pos.y + side * 0.5f), hov || on ? g_pal->text : col);
    if (hov && tip != nullptr && *tip != 0)
        ImGui::SetTooltip("%s", tip);
    return clicked;
}

// herdr context menu (mods/menu): padding 5, rows 31 px with a 14 px muted icon, the label and
// the bound key right-aligned in 11 px dim. Use between begin_menu / end_menu.
// Keyboard in the open menu (herdr menu/main.js 131-137): one menu is open at a time.
ui::MenuNav g_menu_nav;

// Call right after a menu popup begins: a fresh menu highlights its first row; Up/Down move,
// Enter runs the highlighted row (menu_row), Esc closes.
void menu_keys() {
    if (ImGui::IsWindowAppearing())
        g_menu_nav.open();
    g_menu_nav.frame(ImGui::IsKeyPressed(ImGuiKey_UpArrow), ImGui::IsKeyPressed(ImGuiKey_DownArrow),
                     ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ImGui::CloseCurrentPopup();
}

bool begin_menu(const char* id) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(5), px(5)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(px(200), 0), ImVec2(FLT_MAX, FLT_MAX));
    const bool open = ImGui::BeginPopupContextItem(id);
    ImGui::PopStyleVar(2);
    if (open)
        menu_keys();
    return open;
}
bool menu_row(App::State& s, const char* glyph, const char* label, const std::string& chord = {}, bool enabled = true) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float w = std::max(ImGui::GetContentRegionAvail().x, px(190));
    ImGui::BeginDisabled(!enabled);
    bool clicked = ImGui::InvisibleButton(label, ImVec2(w, px(31)));
    ImGui::EndDisabled();
    const bool hov = enabled && ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // herdr menu/main.js 152-153: the highlighted row is "selected", a hovered one "hover".
    bool run = false;
    const bool on = enabled && g_menu_nav.row(false, &run);
    clicked = clicked || run;
    if (hov || on)
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + px(31)), hov ? g_pal->hover : g_pal->selected, px(7));
    const float cy = a.y + px(15.5f);
    const ImU32 col = enabled ? g_pal->text : g_pal->faint;
    if (glyph != nullptr)
        draw_icon(s, dl, glyph, fpx(14), ImVec2(a.x + px(15), cy), enabled ? g_pal->dim : g_pal->faint);
    dl->AddText(s.fonts.ui, fpx(12.8f), ImVec2(a.x + px(31), cy - fpx(12.8f) * 0.6f), col, label);
    if (!chord.empty()) {
        const float cw = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, chord.c_str()).x;
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(a.x + w - px(8) - cw, cy - fpx(11) * 0.6f), g_pal->faint, chord.c_str());
    }
    if (clicked)
        ImGui::CloseCurrentPopup();
    return clicked;
}
// The shells this machine has (host profiles that run a shell), the default one first.
std::vector<std::pair<std::string, std::string>> shell_profiles(App::State& s, const std::string& machine) {
    ui::HostClient* h = s.host_for(machine);
    const auto profiles = h->profiles();
    const std::string def = pick_shell(s.settings.shell_profile, profiles);
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [id, name] : profiles)
        if (is_shell_profile(id) || program_of_profile(h->profile_executable(id), id).shell)
            out.emplace_back(id, name);
    std::stable_partition(out.begin(), out.end(), [&](const auto& p) { return p.first == def; });
    return out;
}

// A menu of the shells: the chosen one starts a new space (or tab) in the focused folder.
void draw_shell_picker(App::State& s, const char* id, const char* target) {
    ImGui::SetNextWindowSizeConstraints(ImVec2(px(220), 0), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(5), px(5)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    const bool open = ImGui::BeginPopup(id);
    ImGui::PopStyleVar(2);
    if (!open)
        return;
    menu_keys();
    const std::string machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
    const auto shells = shell_profiles(s, machine);
    if (shells.empty()) {
        ImGui::TextDisabled("No shell found");
    }
    for (std::size_t i = 0; i < shells.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const std::string chord = i == 0 ? s.keymap.chord_for(std::string(target) + ".new") : std::string();
        if (menu_row(s, ui::icon::kTerminal, shells[i].second.c_str(), chord))
            run_command(s, std::string("launch:") + target + ":" + shells[i].first);
        ImGui::PopID();
    }
    ImGui::EndPopup();
}

void menu_separator() {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, px(9)));
    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, a.y + px(4)), ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y + px(4)),
                                        g_pal->border);
}

// Agent status marks (agents.js 208-217): spinner, "Needs you" pill or "!", done dot, idle ring.
// Draws with its left edge at `x`, vertically centred on `cy`; returns the width used.
float status_mark(App::State& s, ImDrawList* dl, float x, float cy, Attention a, bool compact, bool measure = false) {
    switch (a) {
        case Attention::kWorking: {
            const float r = px(5);
            if (!measure) {
                const float t = static_cast<float>(ImGui::GetTime());
                const float a0 = t * (2.0f * 3.14159265f / 0.9f);  // one turn per 900 ms
                dl->PathArcTo(ImVec2(x + px(1) + r + px(1), cy), r, a0, a0 + 4.4f, 18);
                dl->PathStroke(g_pal->working, 0, px(1.6f));
            }
            return px(14);
        }
        case Attention::kBlocked: {
            if (compact) {
                if (!measure) {
                    dl->AddRectFilled(ImVec2(x, cy - px(7)), ImVec2(x + px(14), cy + px(7)),
                                      alpha(g_pal->needs_you, 0x26), px(4));
                    const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(10.5f), FLT_MAX, 0, "!");
                    dl->AddText(s.fonts.ui_bold, fpx(10.5f), ImVec2(x + (px(14) - t.x) * 0.5f, cy - t.y * 0.5f),
                                g_pal->needs_you, "!");
                }
                return px(14);
            }
            const char* label = "Needs you";
            const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(10.5f), FLT_MAX, 0, label);
            const float w = t.x + px(12);
            if (!measure) {
                dl->AddRectFilled(ImVec2(x, cy - px(9)), ImVec2(x + w, cy + px(9)), alpha(g_pal->needs_you, 0x26),
                                  px(5));
                dl->AddText(s.fonts.ui_bold, fpx(10.5f), ImVec2(x + px(6), cy - t.y * 0.5f), g_pal->needs_you, label);
            }
            return w;
        }
        case Attention::kDone:
            if (!measure)
                dl->AddCircleFilled(ImVec2(x + px(3) + px(3.5f), cy), px(3.5f), g_pal->done);
            return px(13);
        case Attention::kIdle:
            if (!measure)
                dl->AddCircle(ImVec2(x + px(3) + px(3.5f), cy), px(3.0f), g_pal->idle, 0, px(1.5f));
            return px(13);
        case Attention::kNone:
            break;
    }
    return 0;
}

// Dragging an empty part of the brand row or the tab bar moves the window (herdr window_drag:
// posts SC_MOVE so snapping works); double-click toggles maximize.
void window_drag_area(App::State& s, ImVec2 a, ImVec2 b) {
    if (s.config.hwnd == nullptr || ImGui::IsAnyItemHovered() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        return;
    if (!ImGui::IsMouseHoveringRect(a, b, false) || !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        return;
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        ::ShowWindow(s.config.hwnd, ::IsZoomed(s.config.hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ::ReleaseCapture();
        ::SendMessageW(s.config.hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);  // the move loop ate the release
    }
}

// The space's git tail (spaces.js 425-442): "repo · branch" without the parts the name already
// says, main/master dropped, then ahead/behind.
struct GitTail {
    std::string text;
    int ahead = 0;
    int behind = 0;
};

GitTail git_tail(const GitInfo& g, const std::string& space_name, bool worktree_row) {
    GitTail t;
    if (!g.repo)
        return t;
    auto same = [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) == 0; };
    const std::string repo = folder_name(g.common_top.empty() ? g.top : g.common_top);
    std::string branch = g.branch == "HEAD" ? std::string("(detached)") : g.branch;
    if (branch == "main" || branch == "master" || same(branch, space_name))
        branch.clear();
    std::string r = worktree_row || same(repo, space_name) ? std::string() : repo;
    t.text = r;
    if (!branch.empty())
        t.text += (t.text.empty() ? "" : " \xC2\xB7 ") + branch;
    t.ahead = g.ahead;
    t.behind = g.behind;
    return t;
}

void open_confirm(App::State& s, std::string title, std::string detail, std::string ok, std::string command) {
    s.confirm_title = std::move(title);
    s.confirm_detail = std::move(detail);
    s.confirm_ok = std::move(ok);
    s.confirm_command = std::move(command);
    s.show_confirm = true;
}

// "Close space?" (window.rs 3067-3080): "<name> — N tabs, M terminals", always plural as
// window.rs 3075 writes it.
void confirm_close_space(App::State& s, int index) {
    if (index < 0 || index >= static_cast<int>(s.ws.spaces.size()))
        return;
    const auto& sp = s.ws.spaces[static_cast<std::size_t>(index)];
    std::size_t terms = 0;
    for (const auto& t : sp.tabs)
        terms += s.ws.sessions_in(t).size();
    char detail[256];
    std::snprintf(detail, sizeof detail, "%s \xE2\x80\x94 %zu tabs, %zu terminals",
                  s.ws.space_title(index, s.session_cwds()).c_str(), sp.tabs.size(), terms);
    open_confirm(s, "Close space?", detail, "Close", "space.close_now:" + sp.id);
}

// ---------------------------------------------------------------- sidebar: brand row
// workspace-chrome/main.js 48-53: the mark and name, then search and the sidebar toggle (no
// tooltips on either).
void draw_brand(App::State& s, ImVec2 pos, float w, bool rail) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = px(46);
    const float cy = pos.y + h * 0.5f;
    float x = pos.x + (rail ? px(6) : px(14));
    if (!rail) {
        const float m = px(20);
        dl->AddRectFilled(ImVec2(x, cy - m * 0.5f), ImVec2(x + m, cy + m * 0.5f), g_pal->focus, px(5));
        dl->AddRectFilled(ImVec2(x + px(5), cy - px(4)), ImVec2(x + px(14), cy - px(1.5f)), g_pal->card, px(1));
        dl->AddRectFilled(ImVec2(x + px(5), cy + px(1.5f)), ImVec2(x + px(11), cy + px(4)), g_pal->card, px(1));
        const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(13.5f), FLT_MAX, 0, "AgentDesk");
        dl->AddText(s.fonts.ui_bold, fpx(13.5f), ImVec2(x + m + px(8), cy - t.y * 0.5f), g_pal->text, "AgentDesk");
    }
    const float b = px(26);
    float bx = rail ? pos.x + (w - b) * 0.5f : pos.x + w - px(10) - b;
    if (icon_button(s, "##sidebar-toggle", ui::icon::kSidebar, ImVec2(bx, cy - b * 0.5f), b, fpx(15), nullptr,
                    g_pal->dim))
        run_command(s, "sidebar.rail");
    if (!rail) {
        bx -= b + px(4);
        if (icon_button(s, "##search", ui::icon::kSearch, ImVec2(bx, cy - b * 0.5f), b, fpx(15), nullptr, g_pal->dim))
            run_command(s, "app.picker");
    }
    window_drag_area(s, pos, ImVec2(pos.x + w, pos.y + h));
}

// ---------------------------------------------------------------- sidebar: spaces
struct SpaceRow {
    int index = -1;       // space index, or -1 for a machine header
    std::string machine;  // header: machine name ("" = this computer)
    bool tree = false;    // a worktree drawn under its main checkout
};

// Git facts of a space's first terminal (spaces.js 22-27 place(): where the space is now).
GitInfo space_git(App::State& s, const ui::Space& sp) {
    for (const auto& t : sp.tabs) {
        const auto ids = s.ws.sessions_in(t);
        if (ids.empty())
            continue;
        const auto it = s.views_by_id.find(ids.front());
        return it == s.views_by_id.end() ? GitInfo{} : s.git.get(it->second.cwd, sp.machine);
    }
    return {};
}

std::vector<SpaceRow> space_rows(App::State& s) {
    // spaces.js 298-335: each linked worktree goes right under the first main checkout of its
    // repo, and the spaces are moved (space.move) so the model order is the drawn one: rail
    // numbers and the space.select keys then follow what you see.
    const std::size_t n = s.ws.spaces.size();
    std::vector<ui::SpaceRepo> info(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& sp = s.ws.spaces[i];
        const GitInfo g = space_git(s, sp);
        std::string key = g.common_top;
        for (auto& c : key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        info[i] = {g.repo, g.repo && g.worktree, sp.machine + "|" + key};
    }
    std::vector<bool> tree;
    std::vector<int> order = ui::nest_worktrees(info, &tree);
    if (s.drag_space < 0 && s.ws.reorder_spaces(order)) {
        for (int i = 0; i < static_cast<int>(order.size()); ++i)
            order[static_cast<std::size_t>(i)] = i;
    }
    std::vector<std::string> machines = {""};
    for (const auto& r : s.remotes)
        machines.push_back(r.cfg.name);
    for (const auto& sp : s.ws.spaces)
        if (std::find(machines.begin(), machines.end(), sp.machine) == machines.end())
            machines.push_back(sp.machine);
    std::vector<SpaceRow> rows;
    for (const auto& m : machines) {
        if (!s.remotes.empty())
            rows.push_back({-1, m, false});
        if (s.folded_machines.contains(m))
            continue;
        for (std::size_t k = 0; k < order.size(); ++k)
            if (s.ws.spaces[static_cast<std::size_t>(order[k])].machine == m)
                rows.push_back({order[k], m, tree[k]});
    }
    return rows;
}

void space_menu(App::State& s, int i) {
    const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
    GitInfo g;
    for (const auto& t : sp.tabs) {
        const auto ids = s.ws.sessions_in(t);
        if (!ids.empty() && s.views_by_id.count(ids.front())) {
            g = s.git.get(s.views_by_id[ids.front()].cwd, sp.machine);
            break;
        }
    }
    // herdr menu/main.js 84 menuIcon(): ".rename" -> square-pen, "worktree" -> git-branch (all
    // three), ".close" -> x.
    if (menu_row(s, ui::icon::kEdit, "Rename", s.keymap.chord_for("space.rename"))) {
        s.ws.select_space(i);
        open_rename(s, "space", sp.id, sp.name);
    }
    if (g.repo) {
        if (menu_row(s, ui::icon::kBranch, "New worktree", s.keymap.chord_for("worktree.new"))) {
            s.ws.select_space(i);
            run_command(s, "worktree.new");
        }
        if (menu_row(s, ui::icon::kBranch, "Open worktree")) {
            s.ws.select_space(i);
            run_command(s, "worktree.open");
        }
        if (g.worktree && menu_row(s, ui::icon::kBranch, "Remove worktree")) {
            s.ws.select_space(i);
            run_command(s, "worktree.remove");
        }
    }
    menu_separator();
    if (menu_row(s, ui::icon::kClose, "Close", s.keymap.chord_for("space.close")))
        confirm_close_space(s, i);
}

float draw_spaces(App::State& s, ImVec2 pos, float w, float h, bool rail) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {
        // A space remembers where it was made (herdr Space.folder): its name when nothing else says.
        std::map<std::string, std::string> starts;
        for (const auto& [id, v] : s.views_by_id)
            starts[id] = v.start_cwd.empty() ? v.cwd : v.start_cwd;
        s.ws.remember_folders(starts);
    }
    const auto rows = space_rows(s);
    const auto cwds = s.session_cwds();
    s.ws.assign_colors(&cwds);
    // Header (31 px): "Spaces" + "+" (spaces.js 541-545).
    if (!rail) {
        const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, "Spaces");
        dl->AddText(s.fonts.ui_medium, fpx(11.5f), ImVec2(pos.x + px(16), pos.y + (px(31) - t.y) * 0.5f), g_pal->faint,
                    "Spaces");
    }
    {
        const float b = px(26);
        const ImVec2 bp = rail ? ImVec2(pos.x + (w - b) * 0.5f, pos.y + px(2)) : ImVec2(pos.x + w - px(10) - b, pos.y + px(2.5f));
        // AgentDesk's own (herdr opens its one shell): pick the shell the new space starts in,
        // from the shells found on this machine (PowerShell 7, Windows PowerShell, cmd, WSL).
        if (icon_button(s, "##space-new", ui::icon::kPlus, bp, b, fpx(14), nullptr, g_pal->dim))
            ImGui::OpenPopup("##space-shells");
        draw_shell_picker(s, "##space-shells", "space");
    }
    const float head = px(31);
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + head));
    ImGui::BeginChild("##spaces", ImVec2(w, std::max(px(20), h - head)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float pad = rail ? 0.0f : px(8);
    const float rw = ImGui::GetContentRegionAvail().x - pad * 2;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int drop_target = -1;
    float drop_y = 0;
    // Where the active space's row is (spaces.js 342-345, 533-539).
    int active_at = -1;
    float active_top = 0, active_bottom = 0;
    for (std::size_t ri = 0; ri < rows.size(); ++ri) {
        const SpaceRow& row = rows[ri];
        const ImVec2 a0 = ImGui::GetCursorScreenPos();
        const ImVec2 a(a0.x + pad, a0.y);
        if (row.index < 0) {
            // Machine header (26 px): icon, name, state, "+", dot, chevron; click folds.
            ImGui::PushID(("m:" + row.machine).c_str());
            const float hh = px(26);
            const bool on = s.machine_connected(row.machine);
            // herdr: a small "+" link (space.new {machine}) when connected, left of the dot.
            const float plus_x = a.x + rw - px(8) - px(11) - px(7) - px(6) - px(7) - px(16);
            const bool over_plus = on && !rail && ImGui::IsMouseHoveringRect(ImVec2(plus_x, a.y), ImVec2(plus_x + px(16), a.y + hh));
            ImGui::SetCursorScreenPos(a);
            const bool header_clicked = ImGui::InvisibleButton("##mh", ImVec2(rw, hh));
            if (header_clicked && over_plus) {
                run_command(s, "space.new_on:" + row.machine);
            } else if (header_clicked) {
                if (s.folded_machines.contains(row.machine))
                    s.folded_machines.erase(row.machine);
                else
                    s.folded_machines.insert(row.machine);
                s.settings.folded_machines.assign(s.folded_machines.begin(), s.folded_machines.end());
                save_settings(s.settings);
            }
            const bool hov = ImGui::IsItemHovered();
            if (hov)
                d->AddRectFilled(a, ImVec2(a.x + rw, a.y + hh), g_pal->hover, px(6));
            const float cy = a.y + hh * 0.5f;
            if (rail) {
                draw_icon(s, d, row.machine.empty() ? ui::icon::kMonitor : ui::icon::kServer, fpx(13),
                          ImVec2(a.x + rw * 0.5f, cy), g_pal->dim);
                d->AddCircleFilled(ImVec2(a.x + rw * 0.5f + px(7), cy + px(5)), px(3), on ? g_pal->done : g_pal->faint);
                // spaces/main.js 370: "<m> · connected", "<m> · <state>", or "This computer".
                if (hov)
                    ImGui::SetTooltip("%s", row.machine.empty()
                                                ? "This computer"
                                                : (row.machine + " \xC2\xB7 " + s.machine_state(row.machine)).c_str());
            } else {
                float x = a.x + px(8);
                draw_icon(s, d, row.machine.empty() ? ui::icon::kMonitor : ui::icon::kServer, fpx(13),
                          ImVec2(x + px(6.5f), cy), g_pal->faint);
                x += px(13) + px(7);
                const std::string label = row.machine.empty() ? std::string("This computer") : row.machine;
                const float right = a.x + rw - px(8) - px(11) - px(7) - px(6) - px(7) - (on ? px(18) : 0.0f);
                float nw = text_clipped(d, s.fonts.ui, fpx(11), ImVec2(x, cy - fpx(11) * 0.6f), g_pal->faint, label,
                                        right - x);
                if (!on && !row.machine.empty())
                    text_clipped(d, s.fonts.ui, fpx(11), ImVec2(x + nw + px(7), cy - fpx(11) * 0.6f), g_pal->faint,
                                 "unreachable", right - x - nw - px(7));
                if (on)
                    draw_icon(s, d, ui::icon::kPlus, fpx(12), ImVec2(plus_x + px(8), cy), over_plus ? g_pal->text : g_pal->faint);
                d->AddCircleFilled(ImVec2(a.x + rw - px(8) - px(11) - px(7) - px(3), cy), px(3),
                                   on ? g_pal->done : g_pal->faint);
                draw_icon(s, d,
                          s.folded_machines.contains(row.machine) ? ui::icon::kChevronRight : ui::icon::kChevronDown,
                          fpx(11), ImVec2(a.x + rw - px(8) - px(5.5f), cy), g_pal->faint);
                if (hov && !on) {
                    for (const auto& r : s.remotes)
                        if (r.cfg.name == row.machine && !r.host->last_error().empty())
                            ImGui::SetTooltip("%s", r.host->last_error().c_str());
                }
            }
            ImGui::PopID();
            ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + hh));
            // herdr spaces/main.js 352, 396-402, 413: an empty, unfolded machine shows "Loading…"
            // only while its layout loads; otherwise "New space there" with a 12 px plus (in the
            // rail a cell with an 18 px plus box), which runs space.new on that machine.
            const bool empty = ri + 1 >= rows.size() || rows[ri + 1].index < 0;
            if (empty && !s.folded_machines.contains(row.machine)) {
                const bool loading = s.machine_loading(row.machine);
                const float eh_h = px(28);
                const float ew = rail ? px(32) : rw;  // RAIL_CELL
                const ImVec2 e(rail ? a0.x + (ImGui::GetContentRegionAvail().x - ew) * 0.5f : a0.x + pad, a.y + hh);
                ImGui::SetCursorScreenPos(e);
                ImGui::PushID(("e:" + row.machine).c_str());
                const bool clicked = ImGui::InvisibleButton("##empty", ImVec2(ew, eh_h));
                const bool eh = !loading && ImGui::IsItemHovered();
                ImGui::PopID();
                const float ecy = e.y + eh_h * 0.5f;
                if (loading) {
                    // 28 high, px 8 (0 in the rail, centred), 12 px dim: "Loading…" or "…".
                    const char* text = rail ? "\xE2\x80\xA6" : "Loading\xE2\x80\xA6";
                    const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, text);
                    d->AddText(s.fonts.ui, fpx(12),
                               ImVec2(rail ? e.x + (ew - t.x) * 0.5f : e.x + px(8), ecy - t.y * 0.5f), g_pal->faint, text);
                } else {
                    const ImU32 c = eh ? g_pal->text : g_pal->faint;
                    if (eh)
                        d->AddRectFilled(e, ImVec2(e.x + ew, e.y + eh_h), g_pal->hover, rail ? px(6) : px(7));
                    if (rail) {
                        const float bs = px(18);
                        const ImVec2 bp(e.x + (ew - bs) * 0.5f, ecy - bs * 0.5f);
                        d->AddRect(bp, ImVec2(bp.x + bs, bp.y + bs), c, px(5));
                        draw_icon(s, d, ui::icon::kPlus, fpx(10), ImVec2(bp.x + bs * 0.5f, ecy), c);
                        if (eh)
                            ImGui::SetTooltip("New space on %s",
                                              row.machine.empty() ? "this computer" : row.machine.c_str());
                    } else {
                        // px 8, gap 7: the 12 px plus, then the words.
                        draw_icon(s, d, ui::icon::kPlus, fpx(12), ImVec2(e.x + px(8) + px(6), ecy), c);
                        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, "New space there");
                        d->AddText(s.fonts.ui, fpx(12), ImVec2(e.x + px(8) + px(12) + px(7), ecy - t.y * 0.5f), c,
                                   "New space there");
                    }
                    if (clicked)
                        run_command(s, "space.new_on:" + row.machine);
                }
                ImGui::SetCursorScreenPos(ImVec2(a0.x, e.y + eh_h));
            }
            continue;
        }
        const int i = row.index;
        const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
        const bool active = i == s.ws.active_space;
        const std::string title = s.ws.space_title(i, cwds);
        ImGui::PushID(sp.id.c_str());
        if (rail) {
            // 32x26 cell, the cells touching: position number (13 px), status glyph at the right
            // (spaces.js 501-515).
            const float cw = px(32), ch = px(26);
            const ImVec2 c(a0.x + (ImGui::GetContentRegionAvail().x - cw) * 0.5f, a0.y);
            ImGui::SetCursorScreenPos(c);
            if (ImGui::InvisibleButton("##sr", ImVec2(cw, ch))) {
                s.ws.select_space(i);
                s.focus_view(s.ws.focused_session());
            }
            if (active)
                d->AddRectFilled(c, ImVec2(c.x + cw, c.y + ch), g_pal->selected, px(6));
            else if (ImGui::IsItemHovered())
                d->AddRectFilled(c, ImVec2(c.x + cw, c.y + ch), g_pal->hover, px(6));
            const std::string num = std::to_string(i + 1);
            const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, num.c_str());
            d->AddText(s.fonts.ui, fpx(13), ImVec2(c.x + (cw - t.x) * 0.5f, c.y + (ch - t.y) * 0.5f),
                       active ? g_pal->text : g_pal->dim, num.c_str());
            std::vector<Attention> atts;
            for (const auto& t2 : sp.tabs)
                for (const auto& id : s.ws.sessions_in(t2))
                    atts.push_back(s.attention_of(id));
            const Attention att = ui::most_urgent(atts);
            const bool strong = att == Attention::kBlocked || att == Attention::kDone;
            d->AddCircleFilled(ImVec2(c.x + cw - px(5), c.y + ch * 0.5f), strong ? px(2.5f) : px(1.2f),
                               att == Attention::kNone ? g_pal->faint : attention_color(att));
            if (active) {
                active_at = static_cast<int>(ri);
                active_top = c.y;
                active_bottom = c.y + ch;
            }
            ImGui::SetCursorScreenPos(ImVec2(a0.x, c.y + ch));
            ImGui::PopID();
            continue;
        }
        const float rh = row.tree ? px(28) : px(30);
        ImGui::SetCursorScreenPos(a);
        ImGui::InvisibleButton("##space", ImVec2(rw, rh), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hov = ImGui::IsItemHovered();
        const ImVec2 b(a.x + rw, a.y + rh);
        if (active) {
            active_at = static_cast<int>(ri);
            active_top = a.y;
            active_bottom = b.y;
        }
        // Select on mouse-up on the same row; a press dragged onto another row of the same
        // machine moves the space there (spaces.js 466-489).
        if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            s.drag_space = i;
        if (s.drag_space >= 0 && s.drag_space != i && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            mouse.y >= a.y && mouse.y < b.y &&
            s.ws.spaces[static_cast<std::size_t>(s.drag_space)].machine == sp.machine) {
            drop_target = i;
            drop_y = s.drag_space < i ? b.y : a.y;
        }
        if (hov && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && s.drag_space == i) {
            s.ws.select_space(i);
            s.focus_view(s.ws.focused_session());
        }
        if (begin_menu("##spacectx")) {
            space_menu(s, i);
            ImGui::EndPopup();
        }
        const bool dragged = s.drag_space == i && ImGui::IsMouseDragging(ImGuiMouseButton_Left, px(4));
        if (active)
            d->AddRectFilled(a, b, g_pal->selected, px(8));
        else if (hov)
            d->AddRectFilled(a, b, g_pal->hover, px(8));
        const ImU32 fade = dragged ? 128u : 255u;
        auto faded = [&](ImU32 c) { return alpha(c, static_cast<int>(((c >> 24) & 0xFF) * fade / 255)); };
        const float cy = a.y + rh * 0.5f;
        float x = a.x + px(8);
        if (row.tree) {
            // A worktree: indented 22 px, the branch icon in place of the avatar (spaces.js 494).
            x += px(22);
            draw_icon(s, d, ui::icon::kBranch, fpx(13), ImVec2(x + px(6.5f), cy), faded(g_pal->faint));
            x += px(13) + px(9);
        } else {
            const float side = px(18);
            const ImVec2 am(x, cy - side * 0.5f);
            d->AddRectFilled(am, ImVec2(am.x + side, am.y + side), faded(avatar_color(sp.color)), px(5));
            std::string letter = title.empty() ? std::string("?") : title.substr(0, 1);
            if (static_cast<unsigned char>(letter[0]) >= 0x80) {
                std::size_t len = 1;
                while (len < title.size() && (static_cast<unsigned char>(title[len]) & 0xC0) == 0x80)
                    ++len;
                letter = title.substr(0, len);
            } else {
                letter[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(letter[0])));
            }
            const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(10.5f), FLT_MAX, 0, letter.c_str());
            d->AddText(s.fonts.ui_bold, fpx(10.5f), ImVec2(am.x + (side - t.x) * 0.5f, am.y + (side - t.y) * 0.5f),
                       faded(IM_COL32(255, 255, 255, 255)), letter.c_str());
            x += side + px(9);
        }
        // Git tail, right-aligned: "repo · branch ↑N ↓N".
        const GitInfo g = space_git(s, sp);
        const GitTail tail = git_tail(g, title, row.tree);
        std::string ahead = tail.ahead > 0 ? "\xE2\x86\x91" + std::to_string(tail.ahead) : std::string();
        std::string behind = tail.behind > 0 ? "\xE2\x86\x93" + std::to_string(tail.behind) : std::string();
        const float ts = fpx(11.5f);
        auto tw = [&](const std::string& t) { return t.empty() ? 0.0f : s.fonts.ui->CalcTextSizeA(ts, FLT_MAX, 0, t.c_str()).x; };
        const float extra = (ahead.empty() ? 0 : tw(ahead) + px(5)) + (behind.empty() ? 0 : tw(behind) + px(5));
        const float avail = b.x - px(8) - x;
        const float name_w = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, title.c_str()).x;
        float tail_w = tw(tail.text);
        float name_room = name_w;
        if (name_w + px(9) + tail_w + extra > avail) {  // shrink both in proportion, 48 px minimum
            const float total = name_w + tail_w;
            const float room = std::max(0.0f, avail - px(9) - extra);
            name_room = std::max(px(48), room * name_w / std::max(1.0f, total));
            tail_w = std::max(tail.text.empty() ? 0.0f : px(48), room - name_room);
        }
        text_clipped(d, s.fonts.ui, fpx(13), ImVec2(x, cy - fpx(13) * 0.6f), faded(g_pal->text), title, name_room);
        float rx = b.x - px(8);
        if (!behind.empty()) {
            rx -= tw(behind);
            d->AddText(s.fonts.ui, ts, ImVec2(rx, cy - ts * 0.6f), faded(g_pal->needs_you), behind.c_str());
            rx -= px(5);
        }
        if (!ahead.empty()) {
            rx -= tw(ahead);
            d->AddText(s.fonts.ui, ts, ImVec2(rx, cy - ts * 0.6f), faded(g_pal->done), ahead.c_str());
            rx -= px(5);
        }
        if (!tail.text.empty()) {
            const float used = std::min(tail_w, tw(tail.text));
            text_clipped(d, s.fonts.ui, ts, ImVec2(rx - used, cy - ts * 0.6f), faded(g_pal->faint), tail.text, used);
        }
        ImGui::PopID();
        ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + rh));
    }
    if (drop_target >= 0) {
        d->AddRectFilled(ImVec2(pos.x + pad, drop_y - px(1)), ImVec2(pos.x + pad + rw, drop_y + px(1)), g_pal->focus);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (s.drag_space >= 0 && drop_target >= 0)
            s.ws.move_space(s.drag_space, drop_target);
        s.drag_space = -1;
    }
    // Bring the active space into view when it, or its place in the list, changes, and when the
    // list is resized; a row already in view stays put (spaces.js 533-539, 547-548).
    {
        const std::string active_id =
            s.ws.spaces.empty() ? std::string()
                                : s.ws.spaces[static_cast<std::size_t>(std::clamp(s.ws.active_space, 0,
                                                                                  static_cast<int>(s.ws.spaces.size()) - 1))].id;
        const float list_h = ImGui::GetWindowHeight();
        const bool moved = active_id != s.spaces_revealed || active_at != s.spaces_revealed_at;
        const bool resized = std::abs(list_h - s.spaces_list_h) > 0.5f;
        if ((moved || resized) && active_at >= 0) {
            const float top = ImGui::GetWindowPos().y;
            const float scroll = ImGui::GetScrollY();
            if (active_top < top)
                ImGui::SetScrollY(scroll - (top - active_top));
            else if (active_bottom > top + list_h)
                ImGui::SetScrollY(scroll + (active_bottom - (top + list_h)));
        }
        s.spaces_revealed = active_id;
        s.spaces_revealed_at = active_at;
        s.spaces_list_h = list_h;
    }
    // Mod panels for the sidebar, below the spaces.
    if (!rail)
        draw_mod_panels(s, "sidebar");
    ImGui::Dummy(ImVec2(0, px(14)));
    ImGui::EndChild();
    return h;
}

// ---------------------------------------------------------------- sidebar: agents
// Terminals whose program reports a status (herdr agents()): the host has a structured state
// for them now or had one before.
bool is_agent(const ui::SessionView& v) {
    return !v.agent_source.empty() || v.agent_state != "Unknown" || v.last_agent_state != "Unknown";
}

std::string lead_pane(App::State& s, const std::vector<std::string>& ids) {
    for (const auto& id : ids)
        if (const auto it = s.views_by_id.find(id); it != s.views_by_id.end() && is_agent(it->second))
            return id;
    return ids.empty() ? std::string() : ids.front();
}

int urgency(Attention a) {
    switch (a) {
        case Attention::kBlocked:
            return 4;
        case Attention::kDone:
            return 3;
        case Attention::kWorking:
            return 2;
        case Attention::kIdle:
            return 1;
        case Attention::kNone:
            break;
    }
    return 0;
}

// The agent's live terminal title, cleaned like agents.js 28-51: no leading status glyph or
// spinner, no Codex "[ ! ] " mark; ignored when it is a path, has '@', or is the program name.
std::string agent_detail(App::State& s, const std::string& id, const ui::SessionView& v) {
    const auto loc = s.ws.find(id);
    if (loc.space >= 0) {
        const auto& t = s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
        if (!t.name.empty())
            return t.name;
    }
    if (!v.name.empty())
        return v.name;  // the name it was started with (agentctl run --name, sub-agent name)
    std::string title;
    if (const auto it = s.by_id.find(id); it != s.by_id.end())
        title = it->second->snapshot()->title;
    // Strip leading non-ASCII glyphs (spinners, status dots) and spaces.
    std::size_t i = 0;
    while (i < title.size()) {
        const unsigned char c = static_cast<unsigned char>(title[i]);
        if (c == ' ') {
            ++i;
        } else if (c >= 0x80) {
            const std::size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
            // Keep letters of other scripts (CJK starts at U+2E80: 3-byte lead >= 0xE2 0xBA).
            if (c >= 0xE3)
                break;
            i += len;
        } else {
            break;
        }
    }
    title = title.substr(i);
    for (const char* mark : {"[ ! ] ", "[ . ] "})
        if (title.rfind(mark, 0) == 0)
            title = title.substr(6);
    const auto pi = program_of(v);
    if (title.empty() || title.find('@') != std::string::npos || title.find(":\\") != std::string::npos ||
        title.front() == '/' || title.front() == '~' || _stricmp(title.c_str(), pi.name.c_str()) == 0 ||
        _stricmp(title.c_str(), v.program.c_str()) == 0)
        return {};
    return title;
}

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
}

// "<1m", "5m", "2h", "3d" from ms (agents.js 67-73).
std::string ago_ms(long long ms) {
    const long long m = std::max(0LL, ms) / 60000;
    if (m < 1)
        return "<1m";
    if (m < 60)
        return std::to_string(m) + "m";
    const long long h = m / 60;
    return h < 24 ? std::to_string(h) + "h" : std::to_string(h / 24) + "d";
}

// agents.js 95-110: stamps agents whose state changed. Done and idle count as one: looking at a
// finished agent turns done into idle, and looking isn't the agent doing anything. Kept in
// ui.json across restarts; ones gone for 30 days are dropped.
void track_agents(App::State& s, const std::vector<std::string>& list) {
    if (!s.changed_loaded) {
        s.changed_loaded = true;
        for (const auto& [id, c] : s.settings.agents_changed.items())
            if (c.is_object())
                s.changed[id] = {c.value("s", ""), c.value("at", 0LL)};
    }
    const long long now = now_ms();
    bool dirty = false;
    for (const auto& id : list) {
        const Attention a = s.attention_of(id);
        if (a == Attention::kNone)
            continue;  // no state known yet (just started, or not running)
        const std::string st = a == Attention::kBlocked ? "blocked" : a == Attention::kWorking ? "working" : "idle";
        auto& c = s.changed[id];
        if (c.s == st)
            continue;
        c = {st, now};
        dirty = true;
    }
    if (!dirty)
        return;
    const std::set<std::string> listed(list.begin(), list.end());
    Json out = Json::object();
    for (auto it = s.changed.begin(); it != s.changed.end();) {
        if (!listed.contains(it->first) && now - it->second.at > 30LL * 86400000LL) {
            it = s.changed.erase(it);
            continue;
        }
        out[it->first] = {{"s", it->second.s}, {"at", it->second.at}};
        ++it;
    }
    s.settings.agents_changed = std::move(out);
    save_settings(s.settings);
}

// herdr spaces.js 128-133 and 266-286: a command or an agent's turn that ends may have committed
// or checked out, so every space in that repo looks again; the 60 s fallback runs only while the
// window is in front.
void refresh_git_facts(App::State& s) {
    s.git.set_active(s.window_focused);
    for (const auto& [id, v] : s.views_by_id) {
        const bool busy = v.agent_state == "Working" || !(is_shell(v.program) || is_agent(v));
        const auto it = s.busy_terminal.find(id);
        if (it != s.busy_terminal.end() && it->second && !busy) {
            const auto mi = s.machine_of.find(id);
            s.git.invalidate_repo(v.cwd, mi == s.machine_of.end() ? std::string() : mi->second);
        }
        s.busy_terminal[id] = busy;
    }
}

// An agent as herdr's agents() lists it (window.rs 1855-1883, model.rs 597-605): a running
// terminal in a pane of a space whose program reports a status now.
bool live_agent(App::State& s, const std::string& id, const ui::SessionView& v) {
    return !v.lost && v.process_state == "Running" && v.agent_state != "Unknown" && !s.hidden.contains(id) &&
           s.ws.find(id).space >= 0;
}

void draw_agents(App::State& s, ImVec2 pos, float w, float h, bool rail) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(pos.x, pos.y), ImVec2(pos.x + w, pos.y), g_pal->border);
    const bool by_priority = s.settings.agents_sort != "space";
    // Live terminals by the agent that started them (agents.js 171-172), and the agents: a
    // sub-agent without a pane isn't one of them; it shows in its parent's tree (53-54).
    std::map<std::string, std::vector<std::string>> kids;
    std::vector<std::string> list;
    std::map<std::string, std::size_t> seq;  // creation order (agents.js 240 seq)
    {
        std::vector<std::pair<std::string, std::size_t>> born;
        for (std::size_t i = 0; i < s.sessions.size(); ++i)
            born.emplace_back(s.views_by_id[s.sessions[i]->id()].created_at, i);
        std::stable_sort(born.begin(), born.end());
        for (std::size_t k = 0; k < born.size(); ++k)
            seq[s.sessions[born[k].second]->id()] = k;
    }
    for (const auto& sess : s.sessions) {
        const std::string& id = sess->id();
        const auto& v = s.views_by_id[id];
        if (v.lost)
            continue;
        const bool sub = !v.parent.empty() && s.views_by_id.count(v.parent) != 0;
        if (sub && v.process_state != "Exited")
            kids[v.parent].push_back(id);
        // herdr window.rs 1855-1883, model.rs 597-605: the list is built from the panes in the
        // spaces, and a terminal is in it only while it has a status now. An agent that exited
        // (its report dropped) or whose space or pane was closed is gone from the list.
        if (!live_agent(s, id, v))
            continue;
        list.push_back(id);
    }
    const std::set<std::string> listed(list.begin(), list.end());
    track_agents(s, list);
    auto since = [&](const std::string& id) {
        const auto it = s.changed.find(id);
        return it == s.changed.end() ? 0LL : it->second.at;
    };
    // A kept sub-agent (one with a pane) is listed too: where a tree shows it, it gets no row of
    // its own, unless it's pinned (agents.js 231-234). The rail shows no trees.
    if (!rail) {
        std::set<std::string> nested;
        for (const auto& a : list)
            for (const auto& r : ui::agent_tree(kids, a))
                nested.insert(r.id);
        std::erase_if(list, [&](const std::string& a) { return !s.ws.pinned.contains(a) && nested.contains(a); });
    }
    // Most urgent of the agent and its sub-agents (agents.js 238).
    std::map<std::string, int> urgency_of;
    for (const auto& a : list) {
        int u = ui::attention_rank(s.attention_of(a));
        for (const auto& r : ui::agent_tree(kids, a))
            u = std::max(u, ui::attention_rank(ui::kid_status(s.attention_of(r.id))));
        urgency_of[a] = u;
    }
    if (by_priority) {
        std::stable_sort(list.begin(), list.end(), [&](const std::string& a, const std::string& b) {
            return ui::priority_before({urgency_of[a], since(a), static_cast<long long>(seq[a])},
                                       {urgency_of[b], since(b), static_cast<long long>(seq[b])});
        });
    } else {
        // Model order: space, tab, pane.
        const auto all = s.ws.all_sessions();
        std::stable_sort(list.begin(), list.end(), [&](const std::string& a, const std::string& b) {
            return std::find(all.begin(), all.end(), a) < std::find(all.begin(), all.end(), b);
        });
    }
    std::stable_partition(list.begin(), list.end(), [&](const std::string& id) { return s.ws.pinned.contains(id); });
    s.agent_order = list;  // agents.js 244 setAgentOrder

    // A row reads "space · detail": the detail is a tab someone named or the agent's own title,
    // never an automatic tab number unless two rows would otherwise read the same (279-296).
    const auto cwds = s.session_cwds();
    std::map<std::string, int> tab_counts;
    auto tab_key = [&](const std::string& id) {
        const auto loc = s.ws.find(id);
        return loc.space < 0 ? std::string() : s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)].id;
    };
    for (const auto& a : list)
        if (const auto k = tab_key(a); !k.empty())
            ++tab_counts[k];
    auto live_title = [&](const std::string& id) {
        const auto it = s.by_id.find(id);
        return it == s.by_id.end() ? std::string() : it->second->snapshot()->title;
    };
    std::map<std::string, std::string> label, space_name;
    std::vector<std::pair<std::string, std::string>> detail_in;
    for (const auto& a : list) {
        const auto& v = s.views_by_id[a];
        const auto loc = s.ws.find(a);
        if (loc.space >= 0) {
            const auto& tab = s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
            const bool many = tab_counts[tab.id] > 1;
            const std::string lead = many ? a : lead_pane(s, s.ws.sessions_in(tab));
            const auto& lv = s.views_by_id[lead];
            label[a] = ui::agent_tab_label({ui::Workspace::tab_title(tab, loc.tab), !tab.name.empty() && !many,
                                            live_title(lead), program_of(lv).name,
                                            lv.program.empty() ? lv.profile_id : lv.program, lead});
            space_name[a] = s.ws.space_title(loc.space, cwds);
        } else {
            const std::string t = ui::clean_agent_title(live_title(a));
            label[a] = !t.empty() ? t : program_of(v).name;
        }
        detail_in.emplace_back(space_name[a], label[a]);
    }
    std::map<std::string, std::string> detail;
    {
        const auto d = ui::agent_details(detail_in);
        for (std::size_t i = 0; i < list.size(); ++i)
            detail[list[i]] = d[i];
    }

    float y = pos.y;
    if (!rail) {
        // Header (41 px: pt 14, pb 5): "Agents" + the sort button showing the current sort's icon
        // (agents.js 424-445).
        const float hy = pos.y + px(14);
        const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, "Agents");
        dl->AddText(s.fonts.ui_medium, fpx(11.5f), ImVec2(pos.x + px(16), hy + (px(22) - t.y) * 0.5f), g_pal->faint,
                    "Agents");
        const std::string tip = std::string("Sort agents: ") + (by_priority ? "Priority" : "Grouped by space");
        if (icon_button(s, "##agents-sort", by_priority ? ui::icon::kClock : ui::icon::kLayers,
                        ImVec2(pos.x + w - px(16) - px(22), hy), px(22), fpx(14), tip.c_str(), g_pal->faint))
            ImGui::OpenPopup("##sortagents");
        // The sort menu (agents.js 140-164): "Sort agents", then 31 px rows with each sort's icon
        // and a check on the current one.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(5), px(5)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(px(200), 0));
        const bool open = ImGui::BeginPopup("##sortagents");
        ImGui::PopStyleVar(2);
        if (open) {
            const ImVec2 m = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddText(s.fonts.ui, fpx(11), ImVec2(m.x + px(8), m.y + px(4)), g_pal->faint,
                                                "Sort agents");
            ImGui::Dummy(ImVec2(px(190), px(4) + fpx(11) * 1.15f + px(6)));
            struct Sort {
                const char* id;
                const char* label;
                const char* icon;
            };
            for (const Sort& o : {Sort{"priority", "Priority", ui::icon::kClock},
                                  Sort{"space", "Grouped by space", ui::icon::kLayers}}) {
                const bool on = (o.id == std::string("space")) != by_priority;
                if (menu_row(s, o.icon, o.label)) {
                    s.settings.agents_sort = o.id;
                    save_settings(s.settings);
                }
                if (on) {
                    const ImVec2 r1 = ImGui::GetItemRectMax();
                    const ImVec2 r0 = ImGui::GetItemRectMin();
                    draw_icon(s, ImGui::GetWindowDrawList(), ui::icon::kCheck, fpx(14),
                              ImVec2(r1.x - px(8) - px(7), (r0.y + r1.y) * 0.5f), g_pal->text);
                }
            }
            ImGui::EndPopup();
        }
        y += px(41);
    } else {
        y += px(6);
    }
    ImGui::SetCursorScreenPos(ImVec2(pos.x, y));
    const float rail_more = rail ? px(28) : 0.0f;
    ImGui::BeginChild("##agents", ImVec2(w, std::max(px(20), pos.y + h - y - rail_more)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float pad = px(8);
    const float rw = ImGui::GetContentRegionAvail().x - (rail ? px(12) : pad * 2);
    const std::string shown = s.ws.focused_session();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    auto inside = [&](ImVec2 a, ImVec2 b) { return mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y; };

    if (list.empty() && !rail) {
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + px(12), ImGui::GetCursorScreenPos().y));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - px(36));
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->faint));
        ImGui::PushFont(s.fonts.ui, 12);
        ImGui::TextWrapped(
                "No agent status yet. Agents show up here when they report their status. Older versions and agents "
                "inside tmux may not.");
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }

    // A sub-agent's row (agents.js 182-206): its tree lines, icon, name and status.
    auto sub_row = [&](const ui::TreeRow& r, bool compact) {
        const std::string& k = r.id;
        const auto& v = s.views_by_id[k];
        ImGui::PushID(("sub:" + k).c_str());
        const ImVec2 a0 = ImGui::GetCursorScreenPos();
        const ImVec2 a(a0.x + pad, a0.y);
        const float rh = px(26);
        const ImVec2 b(a.x + rw, a.y + rh);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton("##sub", ImVec2(rw, rh));
        const bool hov = ImGui::IsItemHovered();
        const bool on = k == shown;
        if (hov)
            d->AddRectFilled(a, b, g_pal->hover, px(8));
        else if (on)
            d->AddRectFilled(a, b, raised_color(), px(8));
        const float cy = a.y + rh * 0.5f;
        float x = a.x + px(39);
        // Each line: a 9x17 box from 4 px above the row, 1.5 px left border; ├ and └ add the bottom
        // border with a 6 px rounded corner; 4 px after it.
        for (const auto line : r.lines) {
            const float top = a.y - px(4), bottom = a.y + px(13), lx = x + px(0.75f);
            if (line == ui::TreeLine::kBar) {
                d->AddLine(ImVec2(lx, top), ImVec2(lx, bottom), g_pal->border, px(1.5f));
            } else if (line != ui::TreeLine::kNone) {
                const float rr = px(6);
                const float by = bottom - px(0.75f);
                d->PathLineTo(ImVec2(lx, top));
                d->PathLineTo(ImVec2(lx, by - rr));
                d->PathArcTo(ImVec2(lx + rr, by - rr), rr, 3.14159265f, 3.14159265f * 0.5f, 8);
                d->PathLineTo(ImVec2(x + px(9), by));
                d->PathStroke(g_pal->border, 0, px(1.5f));
            }
            x += px(9) + px(4);
        }
        program_icon(d, program_of(v), ImVec2(x, cy - px(7)), px(14), g_pal->text);  // agents/main.js 185
        x += px(14) + px(8);
        const Attention st = ui::kid_status(s.attention_of(k));
        const float mw = std::max(status_mark(s, d, 0, cy, st, compact, true), px(13));
        status_mark(s, d, b.x - px(8) - mw, cy, st, compact);
        const std::string name = !v.name.empty() ? v.name : program_of(v).name;
        text_clipped(d, s.fonts.ui, fpx(12.5f), ImVec2(x, cy - fpx(12.5f) * 0.6f), on ? g_pal->text : g_pal->dim, name,
                     b.x - px(8) - mw - x);
        if (clicked) {
            if (listed.contains(k) || s.ws.find(k).space >= 0)
                s.focus(k);
            else
                run_command(s, "agent.peek:" + k);  // agents.js 199: one without a pane is peeked
        }
        ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + rh));
        ImGui::PopID();
    };

    // An agent's row (agents.js 298-374).
    auto agent_row = [&](const std::string& id, int index, const std::vector<ui::TreeRow>& subs) {
        const auto& v = s.views_by_id[id];
        const Attention att = s.attention_of(id);
        const bool focused = id == shown;
        ImGui::PushID(id.c_str());
        const ImVec2 a0 = ImGui::GetCursorScreenPos();
        if (rail) {
            // 26 px, the number right-aligned (13 px, foreground when focused), the status in a
            // 14 px cell (agents.js 303-317).
            const ImVec2 a(a0.x + px(6), a0.y);
            const float rh = px(26);
            ImGui::SetCursorScreenPos(a);
            if (ImGui::InvisibleButton("##ar", ImVec2(rw, rh)))
                s.focus(id);
            const bool hov = ImGui::IsItemHovered();
            if (focused)
                d->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), g_pal->selected, px(6));
            else if (hov)
                d->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), g_pal->hover, px(6));
            const float cell = a.x + rw - px(2) - px(14);
            const float mw = status_mark(s, d, 0, a.y + rh * 0.5f, att, true, true);
            status_mark(s, d, cell + (px(14) - mw) * 0.5f, a.y + rh * 0.5f, att, true);
            const std::string num = std::to_string(index + 1);
            const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, num.c_str());
            d->AddText(s.fonts.ui, fpx(13), ImVec2(cell - px(4) - t.x, a.y + (rh - t.y) * 0.5f),
                       focused ? g_pal->text : g_pal->dim, num.c_str());
            ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + rh));
            ImGui::PopID();
            return;
        }
        const float rh = px(30);
        const ImVec2 a(a0.x + pad, a0.y);
        const ImVec2 b(a.x + rw, a.y + rh);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton("##agent", ImVec2(rw, rh));
        const bool hov = ImGui::IsItemHovered();
        if (focused)
            d->AddRectFilled(a, b, g_pal->selected, px(8));
        else if (hov)
            d->AddRectFilled(a, b, g_pal->hover, px(8));
        const float cy = a.y + rh * 0.5f;
        const bool pinned = s.ws.pinned.contains(id);
        // Left: the program icon (16 px); with sub-agents, clicking it folds them (360-365).
        const float x0 = a.x + px(8);
        program_icon(d, program_of(v), ImVec2(x0, cy - px(8)), px(16), g_pal->text);  // agents/main.js 299
        const bool on_icon = !subs.empty() && inside(ImVec2(x0, cy - px(8)), ImVec2(x0 + px(16), cy + px(8)));
        float x = x0 + px(16) + px(9);
        // Right, from the edge leftwards: status, pin (hovered) or time, fold, machine (369-373).
        const bool compact = by_priority && !pinned;
        float rx = b.x - px(8);
        const float mw = std::max(status_mark(s, d, 0, cy, att, compact, true), px(13));
        status_mark(s, d, rx - mw, cy, att, compact);
        rx -= mw + px(9);
        bool on_pin = false, on_fold = false;
        if (hov) {
            const float pb = px(17);
            const ImVec2 p0(rx - pb, cy - pb * 0.5f), p1(rx, cy + pb * 0.5f);
            on_pin = inside(p0, p1);
            if (on_pin)
                d->AddRectFilled(p0, p1, g_pal->hover, px(4));
            draw_icon(s, d, pinned ? ui::icon::kPinned : ui::icon::kPin, fpx(13), ImVec2(rx - pb * 0.5f, cy),
                      on_pin ? g_pal->text : g_pal->faint);
            if (on_pin)
                ImGui::SetTooltip("%s", pinned ? "Unpin" : "Pin to the top");
            rx -= pb + px(9);
        } else if (const long long when = since(id); when != 0) {
            const std::string t = ago_ms(now_ms() - when);
            const float tw = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, t.c_str()).x;
            d->AddText(s.fonts.ui, fpx(11), ImVec2(rx - tw, cy - fpx(11) * 0.6f), g_pal->faint, t.c_str());
            rx -= tw + px(9);
        }
        if (!subs.empty()) {
            // The fold chevron; folded, the count too, coloured by the most urgent sub-agent (253-277).
            const bool open = !s.folded_agents.contains(id);
            Attention top = Attention::kNone;
            for (const auto& r : subs) {
                const Attention ks = ui::kid_status(s.attention_of(r.id));
                if (ui::attention_rank(ks) > ui::attention_rank(top))
                    top = ks;
            }
            const std::string count = std::to_string(subs.size());
            const float cw = open ? 0.0f : s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, count.c_str()).x + px(3);
            const float fw = px(3) + px(11) + cw + px(3);
            const ImVec2 f0(rx - fw, cy - px(9)), f1(rx, cy + px(9));
            on_fold = inside(f0, f1);
            if (on_fold)
                d->AddRectFilled(f0, f1, g_pal->hover, px(4));
            const ImU32 col = on_fold ? g_pal->text : open || top == Attention::kNone ? g_pal->faint : attention_color(top);
            draw_icon(s, d, open ? ui::icon::kChevronDown : ui::icon::kChevronRight, fpx(11),
                      ImVec2(f0.x + px(3) + px(5.5f), cy), col);
            if (!open)
                d->AddText(s.fonts.ui, fpx(11), ImVec2(f0.x + px(3) + px(11) + px(3), cy - fpx(11) * 0.6f), col,
                           count.c_str());
            rx -= fw + px(9);
        }
        if (const auto mi = s.machine_of.find(id); mi != s.machine_of.end() && !mi->second.empty()) {
            const float mw2 = s.fonts.ui->CalcTextSizeA(fpx(10), FLT_MAX, 0, mi->second.c_str()).x;
            d->AddText(s.fonts.ui, fpx(10), ImVec2(rx - mw2, cy - fpx(10) * 0.6f), g_pal->faint, mi->second.c_str());
            rx -= mw2 + px(9);
        }
        // Name: under a space header the space goes without saying; without one (priority,
        // pinned) the row names its space, then "· detail" (339-342, 366-368).
        const bool full = by_priority || pinned;
        std::string more = detail[id];
        if (const auto deco = s.mods->decorate("agent", {{"id", id},
                                                         {"title", s.pane_title(id)},
                                                         {"state", v.agent_state},
                                                         {"program", v.program},
                                                         {"cwd", v.cwd}});
            !deco.empty())
            more = deco + (more.empty() ? "" : "  " + more);
        const std::string& lab = label[id];
        const bool number = !lab.empty() && std::all_of(lab.begin(), lab.end(), [](char c) { return c >= '0' && c <= '9'; });
        std::string name;
        if (full)
            name = space_name[id].empty() ? lab : space_name[id];
        else if (!more.empty())
            name = more;
        else
            name = number ? program_of(v).name : lab;
        const ImU32 name_col = att == Attention::kIdle && !focused ? g_pal->dim : g_pal->text;
        const float fs = fpx(13);
        const float nw = text_clipped(d, s.fonts.ui, fs, ImVec2(x, cy - fs * 0.6f), name_col, name,
                                      std::min(full && !more.empty() ? px(120) : px(200), rx - x));
        if (full && !more.empty())
            text_clipped(d, s.fonts.ui, fs, ImVec2(x + nw + px(5), cy - fs * 0.6f), g_pal->faint,
                         "\xC2\xB7 " + more, rx - x - nw - px(5));
        if (clicked) {
            if (on_icon || on_fold) {
                if (s.folded_agents.contains(id))
                    s.folded_agents.erase(id);
                else
                    s.folded_agents.insert(id);
            } else if (on_pin) {
                if (pinned)
                    s.ws.pinned.erase(id);
                else
                    s.ws.pinned.insert(id);
            } else {
                s.focus(id);
            }
        }
        // agents/main.js: the row has no tooltip (only the pin, 330, and the sort, 442, do).
        ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + rh));
        ImGui::PopID();
    };

    // A group's header; it folds the group. Folded, it says how many and the most urgent state
    // (agents.js 386-412).
    auto group_header = [&](const std::string& key, const std::string& text, ImU32 col,
                            const std::vector<std::string>& members) {
        const ImVec2 a0 = ImGui::GetCursorScreenPos();
        const ImVec2 a(a0.x + pad, a0.y + px(8));
        const float hh = px(22);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(("g:" + key).c_str());
        if (ImGui::InvisibleButton("##gh", ImVec2(rw, hh))) {
            if (s.folded_groups.contains(key))
                s.folded_groups.erase(key);
            else
                s.folded_groups.insert(key);
        }
        ImGui::PopID();
        const bool folded = s.folded_groups.contains(key);
        const float cy = a.y + hh * 0.5f;
        float x = a.x + px(8);
        const float right = a.x + rw - px(8) - (folded ? px(11) + px(6) + px(20) : 0.0f);
        x += text_clipped(d, s.fonts.ui_medium, fpx(11), ImVec2(x, cy - fpx(11) * 0.6f), col, text, right - x);
        if (by_priority || folded) {
            const std::string n = std::to_string(members.size());
            d->AddText(s.fonts.ui_medium, fpx(11), ImVec2(x + px(6), cy - fpx(11) * 0.6f), g_pal->faint, n.c_str());
        }
        if (folded) {
            const float cx = a.x + rw - px(8) - px(11);
            draw_icon(s, d, ui::icon::kChevronRight, fpx(11), ImVec2(cx + px(5.5f), cy), g_pal->faint);
            Attention top = Attention::kNone;
            for (const auto& id : members)
                if (ui::attention_rank(s.attention_of(id)) > ui::attention_rank(top))
                    top = s.attention_of(id);
            if (!by_priority && top != Attention::kNone && top != Attention::kIdle) {
                const float mw = status_mark(s, d, 0, cy, top, true, true);
                status_mark(s, d, cx - px(6) - mw, cy, top, true);
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + hh));
        return !folded;
    };

    auto with_kids = [&](const std::string& id, int index) {
        const std::vector<ui::TreeRow> subs = rail ? std::vector<ui::TreeRow>{} : ui::agent_tree(kids, id);
        agent_row(id, index, subs);
        if (s.folded_agents.contains(id))
            return;
        for (const auto& r : subs)
            sub_row(r, by_priority);  // sub-rows are compact only in Priority (agents.js 250)
    };

    if (rail) {
        for (std::size_t i = 0; i < list.size(); ++i)
            with_kids(list[i], static_cast<int>(i));
    } else {
        // Groups in list order: Pinned, then Needs you / Done / Working / Idle, or one per space
        // (agents.js 376-384).
        struct Group {
            std::string key, label;
            ImU32 color;
            std::vector<std::string> ids;
        };
        std::vector<Group> groups;
        for (const auto& id : list) {
            std::string key, text;
            ImU32 col = g_pal->faint;
            if (s.ws.pinned.contains(id)) {
                key = "pinned";
                text = "Pinned";
            } else if (!by_priority) {
                const auto loc = s.ws.find(id);
                key = loc.space >= 0 ? "space:" + s.ws.spaces[static_cast<std::size_t>(loc.space)].id : "space:-";
                text = space_name[id];
            } else {
                const int u = std::max(urgency_of[id], 1);
                key = u == 4 ? "blocked" : u == 3 ? "done" : u == 2 ? "working" : "idle";
                text = u == 4 ? "Needs you" : u == 3 ? "Done" : u == 2 ? "Working" : "Idle";
                col = u == 4 ? g_pal->needs_you : u == 3 ? g_pal->done : u == 2 ? g_pal->working : g_pal->faint;
            }
            if (groups.empty() || groups.back().key != key)
                groups.push_back({key, text, col, {}});
            groups.back().ids.push_back(id);
        }
        int index = 0;
        for (const auto& g : groups) {
            if (!group_header(g.key, g.label, g.color, g.ids)) {
                index += static_cast<int>(g.ids.size());
                continue;
            }
            for (const auto& id : g.ids)
                with_kids(id, index++);
        }
    }
    ImGui::Dummy(ImVec2(0, px(8)));
    ImGui::EndChild();
    if (rail) {
        // "»" expands the sidebar again.
        const ImVec2 a(pos.x, pos.y + h - px(28));
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton("##expand", ImVec2(w, px(28))))
            run_command(s, "sidebar.rail");
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, "\xC2\xBB");
        dl->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + (w - t.x) * 0.5f, a.y + (px(28) - t.y) * 0.5f),
                    ImGui::IsItemHovered() ? g_pal->text : g_pal->faint, "\xC2\xBB");
    }
}

// ---------------------------------------------------------------- sidebar: footer
// workspace-chrome/main.js 35-46: Settings in muted, Add machine all dim; hover brings both to
// the foreground.
void draw_sidebar_footer(App::State& s, ImVec2 pos, float w, bool rail) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(pos, ImVec2(pos.x + w, pos.y), g_pal->border);
    float y = pos.y + px(4);
    auto item = [&](const char* id, const char* glyph, const char* label, ImU32 col, const char* cmd) {
        const float rh = px(28);
        const float x0 = rail ? pos.x + (w - px(32)) * 0.5f : pos.x + px(8);
        const float iw = rail ? px(32) : w - px(16);
        ImGui::SetCursorScreenPos(ImVec2(x0, y));
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(iw, rh));
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + iw, y + rh), g_pal->hover, px(8));
        const float cy = y + rh * 0.5f;
        const ImU32 c = hov ? g_pal->text : col;
        if (rail) {
            draw_icon(s, dl, glyph, fpx(14), ImVec2(x0 + iw * 0.5f, cy), c);
            if (hov)
                ImGui::SetTooltip("%s", label);
        } else {
            draw_icon(s, dl, glyph, fpx(14), ImVec2(x0 + px(8) + px(7), cy), c);
            dl->AddText(s.fonts.ui, fpx(13), ImVec2(x0 + px(8) + px(14) + px(9), cy - fpx(13) * 0.6f), c, label);
        }
        if (clicked)
            run_command(s, cmd);
        y += rh;
    };
    item("##f-settings", ui::icon::kSettings, "Settings", g_pal->dim, "app.settings");
    item("##f-machine", ui::icon::kPlus, "Add machine", g_pal->faint, "machine.add");
}

// ---------------------------------------------------------------- tab bar (tabs.js)
// herdr new-menu (defaults/mods/new-menu/main.js): Terminal, "Start an agent" (installed ones
// numbered 1-9, then up to two not installed), New space, New worktree…, Open worktree…. Rows
// 31 px, hover "selected"; Up/Down/Enter/Esc like the context menus (menu/main.js 131-137).
void draw_new_menu(App::State& s) {
    ImGui::SetNextWindowSize(ImVec2(px(250), 0));
    if (!ImGui::BeginPopup("##newmenu"))
        return;
    menu_keys();
    auto row = [&](const char* glyph, const std::string& label, const std::string& hint, const std::string& cmd,
                   bool enabled = true, const ui::Program* prog = nullptr) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = px(31) - ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginDisabled(!enabled);
        bool clicked = ImGui::InvisibleButton(("##" + label).c_str(), ImVec2(w, h));
        ImGui::EndDisabled();
        const bool hov = enabled && ImGui::IsItemHovered();
        bool run = false;
        const bool on = enabled && g_menu_nav.row(false, &run);
        clicked = enabled && (clicked || run);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hov || on)
            dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), g_pal->selected, px(7));
        const float cy = a.y + h * 0.5f;
        const ImU32 col = enabled ? g_pal->text : g_pal->faint;
        if (prog != nullptr)
            program_icon(dl, *prog, ImVec2(a.x + px(9), cy - px(7)), px(14), col);  // new-menu/main.js 65
        else if (glyph != nullptr)
            draw_icon(s, dl, glyph, fpx(14), ImVec2(a.x + px(9) + px(7), cy), col);
        dl->AddText(s.fonts.ui, fpx(12.8f), ImVec2(a.x + px(9) + px(14) + px(10), cy - fpx(12.8f) * 0.6f), col,
                    label.c_str());
        if (!hint.empty()) {
            const float hw = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, hint.c_str()).x;
            dl->AddText(s.fonts.ui, fpx(11), ImVec2(a.x + w - hw - px(9), cy - fpx(11) * 0.6f), g_pal->faint,
                        hint.c_str());
        }
        if (clicked) {
            ImGui::CloseCurrentPopup();
            run_command(s, cmd);
        }
    };
    row(ui::icon::kTerminal, "Terminal", s.keymap.chord_for("tab.new"), "tab.new");
    // The agents of the active space's machine (herdr knownAgents({on: machine})).
    const std::string machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
    ui::HostClient* mh = s.host_for(machine);
    const auto profiles = mh->profiles();
    // The other shells this machine has (PowerShell 7, Windows PowerShell, cmd, WSL): a tab in
    // the focused folder, like Terminal (AgentDesk's own; herdr has the one shell).
    for (const auto& [id, name] : shell_profiles(s, machine))
        if (id != pick_shell(s.settings.shell_profile, profiles))
            row(ui::icon::kTerminal, name, "", "launch:tab:" + id);
    ImGui::Dummy(ImVec2(0, px(2)));
    ImGui::PushFont(s.fonts.ui_medium, 11);
    ImGui::TextDisabled("Start an agent");
    ImGui::PopFont();
    // Installed agents (profiles that are not shells), numbered 1-9; then up to two known
    // agents that are not installed, disabled. A digit with Ctrl, Alt or Win held is not ours
    // (new-menu/main.js 56).
    const ImGuiIO& io = ImGui::GetIO();
    const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
    int n = 0;
    std::string have;
    for (const auto& [id, name] : profiles) {
        if (is_shell_profile(id))
            continue;
        for (const char ch : id + " " + name + " ")
            have += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        ++n;
        const std::string key = n <= 9 ? std::to_string(n) : std::string();
        const ui::Program prog = program_of_profile(mh->profile_executable(id), id);
        row(nullptr, name, key, "launch:tab:" + id, true, &prog);
        if (n <= 9 && plain && ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + n - 1), false)) {
            ImGui::CloseCurrentPopup();
            run_command(s, "launch:tab:" + id);
        }
    }
    if (profiles.empty()) {
        ImGui::PushFont(s.fonts.ui, 12);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(9));
        ImGui::TextDisabled("%s", mh->connected() ? "Checking installed agents\xE2\x80\xA6"
                                                  : "Could not check installed agents");
        ImGui::PopFont();
    } else {
        static constexpr std::array<std::pair<const char*, const char*>, 4> kKnown = {
            {{"claude", "Claude Code"}, {"codex", "Codex"}, {"gemini", "Gemini CLI"}, {"opencode", "OpenCode"}}};
        int missing = 0;
        for (const auto& [key, label] : kKnown) {
            if (missing >= 2 || have.find(key) != std::string::npos)
                continue;
            ++missing;
            const ui::Program prog = program_of_profile({}, key);
            row(nullptr, label, "not installed", "", false, &prog);
        }
    }
    ImGui::Separator();
    // new-menu/main.js 69-71: worktrees need an open space (no hint on these rows). New space has
    // a "+"; both worktree rows have branch.svg.
    const bool space = s.ws.active() != nullptr;
    row(ui::icon::kPlus, "New space", s.keymap.chord_for("space.new"), "space.new");
    row(ui::icon::kBranch, "New worktree\xE2\x80\xA6", "", "worktree.new", space);
    row(ui::icon::kBranch, "Open worktree\xE2\x80\xA6", "", "worktree.open", space);
    ImGui::EndPopup();
}

void draw_window_buttons(App::State& s, float right, float top, float h) {
    if (s.config.hwnd == nullptr)
        return;
    const float b = px(26);
    float x = right - b * 3 - px(4);
    const float y = top + (h - b) * 0.5f;
    const bool zoomed = ::IsZoomed(s.config.hwnd) != FALSE;
    if (icon_button(s, "##win-min", ui::icon::kMinimize, ImVec2(x, y), b, fpx(16), nullptr, g_pal->dim))
        ::ShowWindow(s.config.hwnd, SW_MINIMIZE);
    x += b + px(2);
    if (icon_button(s, "##win-max", zoomed ? ui::icon::kRestore : ui::icon::kMaximize, ImVec2(x, y), b, fpx(16),
                    nullptr, g_pal->dim))
        ::ShowWindow(s.config.hwnd, zoomed ? SW_RESTORE : SW_MAXIMIZE);
    x += b + px(2);
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    const bool close = ImGui::InvisibleButton("##win-close", ImVec2(b, b));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hov)
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + b, y + b), g_pal->hover, px(6));
    draw_icon(s, dl, ui::icon::kWinClose, fpx(16), ImVec2(x + b * 0.5f, y + b * 0.5f), hov ? g_pal->text : g_pal->dim);
    if (close)
        ::PostMessageW(s.config.hwnd, WM_CLOSE, 0, 0);
}

// Width of the tab-bar items right of the tabs (+, Files, Changes, window buttons).
void draw_tab_bar(App::State& s, ImVec2 pos, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), g_pal->sidebar);
    dl->AddLine(ImVec2(pos.x, pos.y + height - 1), ImVec2(pos.x + width, pos.y + height - 1), g_pal->border);
    const float th = px(29);
    const float ty = pos.y + (height - th) * 0.5f;
    // Right part first, so the tabs know how much room is left.
    float right = pos.x + width - px(10);
    if (s.config.hwnd != nullptr) {
        draw_window_buttons(s, right + px(10) - px(6), pos.y, height);
        right -= px(26) * 3 + px(4) + px(12);
    }
    auto* sp = s.ws.active();
    auto panel_button = [&](const char* id, const char* glyph, const char* label, const char* which) {
        const float tw = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, label).x;
        const float bw = px(8) + px(14) + px(5) + tw + px(8);
        right -= bw;
        const ImVec2 a(right, pos.y + (height - px(26)) * 0.5f);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(bw, px(26)));
        const bool hov = ImGui::IsItemHovered();
        const bool on = sp != nullptr && sp->panel == which;
        if (on || hov)
            dl->AddRectFilled(a, ImVec2(a.x + bw, a.y + px(26)), on ? g_pal->selected : g_pal->hover, px(6));
        const ImU32 col = on || hov ? g_pal->text : g_pal->dim;
        draw_icon(s, dl, glyph, fpx(14), ImVec2(a.x + px(8) + px(7), a.y + px(13)), col);
        dl->AddText(s.fonts.ui, fpx(12), ImVec2(a.x + px(8) + px(14) + px(5), a.y + px(13) - fpx(12) * 0.6f), col, label);
        if (clicked && sp != nullptr) {
            sp->panel = on ? std::string() : std::string(which);
            s.doc_dirty = true;
            s.doc_dirty_at = Clock::now();
        }
        right -= px(3);
    };
    auto wants = [&](const char* p) {
        return std::find(s.settings.panels.begin(), s.settings.panels.end(), p) != s.settings.panels.end();
    };
    if (wants("changes"))
        panel_button("##p-changes", ui::icon::kChanges, "Changes", "changes");
    if (wants("files"))
        panel_button("##p-files", ui::icon::kFile, "Files", "files");

    // tabs/main.js 147-150: the tab strip is flex_1, so "+" sits at its right, next to Files.
    const float plus_x = right - px(26);
    float x = pos.x + px(10);
    const float tabs_right = plus_x - px(3);
    std::string close_tab;  // id of the tab to close after drawing
    if (sp != nullptr) {
        // Keep the active tab in view: start from an offset that shows it.
        struct T {
            int i;
            float w;
            std::string label;
        };
        std::vector<T> items;
        for (int i = 0; i < static_cast<int>(sp->tabs.size()); ++i) {
            // tabs/main.js 93: every tab of the space, settled ones too (herdr's bar does not
            // mark them; tab.settle only takes them out of tab.next/prev and the lists).
            auto& tab = sp->tabs[static_cast<std::size_t>(i)];
            const auto ids = s.ws.sessions_in(tab);
            // herdr: an unnamed tab follows its agent's live title; a shell shows its program.
            std::string label = tab.name;
            if (label.empty() && !ids.empty()) {
                const std::string shown_id = lead_pane(s, ids);
                if (const auto vi = s.views_by_id.find(shown_id); vi != s.views_by_id.end() && is_agent(vi->second))
                    label = agent_detail(s, shown_id, vi->second);
                if (label.empty())
                    label = s.pane_title(shown_id);
            }
            if (label.empty())
                label = std::to_string(i + 1);
            if (!tab.zoomed.empty())
                label += " Z";
            if (const auto deco = s.mods->decorate("tab", {{"title", label}, {"sessions", ids}}); !deco.empty())
                label += "  \xC2\xB7 " + deco;
            const float lw = std::min(s.fonts.ui->CalcTextSizeA(fpx(12.8f), FLT_MAX, 0, label.c_str()).x, px(170));
            float w = px(10) + px(14) + px(7) + lw + px(10);
            std::vector<Attention> atts;
            for (const auto& id : ids)
                atts.push_back(s.attention_of(id));
            const Attention att = ui::most_urgent(atts);
            if (att == Attention::kWorking || att == Attention::kBlocked || att == Attention::kDone)
                w += px(7) + px(12);
            if (i == sp->active_tab)
                w += px(7) + px(12);
            items.push_back({i, w, label});
        }
        float total = 0;
        for (const auto& it : items)
            total += it.w + px(3);
        float visible = tabs_right - x;
        float active_end = 0, active_start = 0, acc = 0;
        for (const auto& it : items) {
            if (it.i == sp->active_tab) {
                active_start = acc;
                active_end = acc + it.w;
            }
            acc += it.w + px(3);
        }
        s.tab_scroll = std::clamp(s.tab_scroll, 0.0f, std::max(0.0f, total - visible));
        if (active_end - s.tab_scroll > visible)
            s.tab_scroll = active_end - visible;
        if (active_start < s.tab_scroll)
            s.tab_scroll = active_start;
        // ImGui::PushClipRect clips hit-testing too: a tab running under "+" must not take its clicks.
        ImGui::PushClipRect(ImVec2(x, pos.y), ImVec2(tabs_right, pos.y + height), true);
        float tx = x - s.tab_scroll;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        int drop = -1;
        for (const auto& it : items) {
            auto& tab = sp->tabs[static_cast<std::size_t>(it.i)];
            const bool active = it.i == sp->active_tab;
            const ImVec2 a(tx, ty);
            const ImVec2 b(tx + it.w, ty + th);
            tx += it.w + px(3);
            if (b.x < x || a.x > tabs_right)
                continue;
            ImGui::PushID(tab.id.c_str());
            ImGui::SetCursorScreenPos(a);
            // The close button is drawn on top of the tab (herdr tabs/main.js): let it take the
            // mouse, or the tab's own button keeps every press and ✕ never fires.
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##tab", ImVec2(it.w, th));
            const bool hov = ImGui::IsItemHovered();
            if (ImGui::IsItemActivated())
                s.drag_tab = it.i;
            if (s.drag_tab >= 0 && s.drag_tab != it.i && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y)
                drop = it.i;
            if (hov && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && s.drag_tab == it.i) {
                s.ws.select_tab(it.i);
                s.focus_view(s.ws.focused_session());
            }
            if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                run_command(s, "tab.rename:" + tab.id);  // tabs/main.js 113
            // menu/main.js 58-63: Rename, New tab, Close, all by the tab's id.
            if (begin_menu("##tabctx")) {
                if (menu_row(s, ui::icon::kEdit, "Rename", s.keymap.chord_for("tab.rename")))
                    run_command(s, "tab.rename:" + tab.id);
                if (menu_row(s, ui::icon::kPlus, "New tab", s.keymap.chord_for("tab.new")))
                    run_command(s, "tab.new");
                menu_separator();
                if (menu_row(s, ui::icon::kClose, "Close", s.keymap.chord_for("tab.close")))
                    close_tab = tab.id;
                ImGui::EndPopup();
            }
            if (active) {
                dl->AddRectFilled(a, b, raised_color(), px(7));
                dl->AddRect(a, b, g_pal->border, px(7));
            } else if (hov) {
                dl->AddRectFilled(a, b, g_pal->hover, px(7));
            }
            const float cy = a.y + th * 0.5f;
            float cx = a.x + px(10);
            const auto ids = s.ws.sessions_in(tab);
            const std::string shown = ids.empty() ? std::string() : lead_pane(s, ids);
            if (s.views_by_id.count(shown)) {
                const auto& v = s.views_by_id[shown];
                program_icon(dl, program_of(v), ImVec2(cx, cy - px(7)), px(14), g_pal->dim);  // tabs/main.js 120
            }
            cx += px(14) + px(7);
            const ImU32 col = active ? g_pal->text : g_pal->dim;
            const float lw = text_clipped(dl, s.fonts.ui, fpx(12.8f), ImVec2(cx, cy - fpx(12.8f) * 0.6f), col, it.label, px(170));
            cx += lw + px(7);
            std::vector<Attention> atts;
            for (const auto& id : ids)
                atts.push_back(s.attention_of(id));
            const Attention att = ui::most_urgent(atts);
            if (att == Attention::kWorking) {
                status_mark(s, dl, cx - px(1), cy, att, true);
                cx += px(12) + px(7);
            } else if (att == Attention::kBlocked || att == Attention::kDone) {
                dl->AddCircleFilled(ImVec2(cx + px(3), cy), px(3), att == Attention::kBlocked ? g_pal->needs_you : g_pal->done);
                cx += px(12) + px(7);
            }
            if (active) {
                ImGui::SetCursorScreenPos(ImVec2(cx - px(3), cy - px(9)));
                if (ImGui::InvisibleButton("##x", ImVec2(px(18), px(18))))
                    close_tab = tab.id;
                const bool xh = ImGui::IsItemHovered();
                if (xh)
                    dl->AddRectFilled(ImVec2(cx - px(3), cy - px(9)), ImVec2(cx + px(15), cy + px(9)), g_pal->hover, px(4));
                draw_icon(s, dl, ui::icon::kClose, fpx(12), ImVec2(cx + px(6), cy), xh ? g_pal->text : g_pal->dim);
            }
            ImGui::PopID();
        }
        ImGui::PopClipRect();
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (s.drag_tab >= 0 && drop >= 0)
                s.ws.move_tab(s.ws.active_space, s.drag_tab, drop);
            s.drag_tab = -1;
        }
        // The wheel over the bar switches tabs (herdr tabs.js wheel handling).
        const float wheel = ImGui::GetIO().MouseWheel;
        if (ImGui::IsMouseHoveringRect(ImVec2(x, pos.y), ImVec2(tabs_right, pos.y + height)) && wheel != 0 &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
            const auto now = Clock::now();
            auto& w = s.tab_wheel;
            int step = 0;
            if (std::abs(wheel) >= 1.0f) {  // a mouse notch: one tab
                step = wheel < 0 ? 1 : -1;
                w = {};
            } else {
                const float px_delta = -wheel * 120.0f;
                if (now - w.last > std::chrono::milliseconds(250) || (w.acc != 0 && (w.acc > 0) != (px_delta > 0)))
                    w = {};
                w.acc += px_delta;
                w.last = now;
                const float need = w.steps == 0 ? 20.0f : 60.0f;
                if (std::abs(w.acc) >= need && now - w.switched >= std::chrono::milliseconds(100)) {
                    step = w.acc > 0 ? 1 : -1;
                    w.acc -= static_cast<float>(step) * need;
                    ++w.steps;
                    w.switched = now;
                }
            }
            if (step != 0) {
                s.ws.cycle_tab(step);
                s.focus_view(s.ws.focused_session());
            }
        }
    }
    // "+" -> new-menu (tabs/main.js 150), right of the flex_1 tab strip; no tooltip there.
    if (icon_button(s, "##tab-new", ui::icon::kPlus, ImVec2(plus_x, pos.y + (height - px(26)) * 0.5f), px(26), fpx(14),
                    nullptr, g_pal->dim))
        ImGui::OpenPopup("##newmenu");
    draw_new_menu(s);
    if (!close_tab.empty())
        run_command(s, "tab.close:" + close_tab);  // by id: the shown tab stays (menu/main.js 62)
    window_drag_area(s, pos, ImVec2(pos.x + width, pos.y + height));
}

// ---------------------------------------------------------------- Terminals layout
// herdr layouts/terminals.json + mods/terminals-bar + mods/terminals-pane. herdr gives it its own
// graphite colours; here they are taken from our theme by the same steps from the background
// toward the text (graphite: background #131619, text #e8e9eb).
struct TerminalsLook {
    ImU32 tray, tab, tab_edge, hover, edge, chip, text, dim, tab_text;  // terminals-bar/main.js 7-17
    ImU32 pane_border, pane_focus, button, button_hover;                 // terminals.json, terminals-pane
};
TerminalsLook terminals_look() {
    const ImU32 bg = g_pal->bg, fg = g_pal->text;
    TerminalsLook l{};
    l.tray = mix(bg, fg, 0.047f);      // #1d1f22
    l.tab = mix(bg, fg, 0.155f);       // #34373c
    l.tab_edge = mix(bg, fg, 0.258f);  // #4a4d53
    l.hover = alpha(fg, 7);            // #ffffff07
    l.edge = mix(bg, fg, 0.174f);      // #383b40
    l.chip = mix(bg, fg, 0.085f);      // #252628
    l.text = fg;                       // #e8e9eb
    l.dim = g_pal->dim;                // #8b8f95
    l.tab_text = mix(bg, fg, 0.83f);   // #c4c6ca
    l.pane_border = mix(bg, fg, 0.08f);   // #24272b
    l.pane_focus = mix(bg, fg, 0.15f);    // #33363b
    l.button = mix(bg, fg, 0.33f);        // #5d6168
    l.button_hover = mix(bg, fg, 0.92f);  // #d6d8db
    return l;
}

// The pane a tab speaks for (terminals-bar/main.js 25-29): the focused one, else its agent, else
// the first.
std::string terminals_lead(App::State& s, const ui::Tab& tab) {
    const auto ids = s.ws.sessions_in(tab);
    if (std::find(ids.begin(), ids.end(), tab.focused) != ids.end())
        return tab.focused;
    for (const auto& id : ids)
        if (const auto it = s.views_by_id.find(id); it != s.views_by_id.end() && is_agent(it->second))
            return id;
    return ids.empty() ? std::string() : ids.front();
}

// The program's tile (terminals-bar/main.js 31-53): 26 px, radius 7, a faint white edge, the
// program's glyph; a status other than idle puts an 11 px dot at the top right.
void terminals_tile(App::State& s, ImDrawList* dl, ImVec2 a, const std::string& id, const TerminalsLook& l) {
    const float side = px(26);
    // herdr: bg(g.tile ?? chip), border #ffffff14, the icon 15 px (tinted: g.color ?? text).
    std::optional<ui::Program> g;
    if (const auto it = s.views_by_id.find(id); it != s.views_by_id.end())
        g = program_of(it->second);
    dl->AddRectFilled(a, ImVec2(a.x + side, a.y + side), g ? hex_color(g->tile, l.chip) : l.chip, px(7));
    dl->AddRect(a, ImVec2(a.x + side, a.y + side), IM_COL32(255, 255, 255, 0x14), px(7));
    if (g)
        program_icon(dl, *g, ImVec2(a.x + (side - px(15)) * 0.5f, a.y + (side - px(15)) * 0.5f), px(15),
                     hex_color(g->color, l.text));
    const Attention att = id.empty() ? Attention::kNone : s.attention_of(id);
    if (att != Attention::kNone && att != Attention::kIdle) {
        const ImVec2 c(a.x + side + px(4) - px(5.5f), a.y - px(4) + px(5.5f));
        dl->AddCircleFilled(c, px(5.5f), l.tray);  // the 2 px border in the tray's colour
        dl->AddCircleFilled(c, px(3.5f), attention_color(att));
    }
}

// The draggable top bar (terminals-bar/main.js 94-187): the space chip (opens the picker), the
// tabs in a tray, then the palette, "+" (a new tab at once) and the window buttons.
void draw_terminals_bar(App::State& s, ImVec2 pos, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const TerminalsLook l = terminals_look();
    auto* sp = s.ws.active();
    const float cy = pos.y + height * 0.5f;
    float right = pos.x + width;
    // Window buttons: pl 6, pr 4, gap 8; 24 px round chips, 14 px icons; hover edge + text.
    if (s.config.hwnd != nullptr) {
        const bool zoomed = ::IsZoomed(s.config.hwnd) != FALSE;
        struct B {
            const char* id;
            const char* glyph;
            const char* cmd;
        };
        const B buttons[] = {{"##tm-close", ui::icon::kWinClose, "window.close"},
                             {"##tm-max", zoomed ? ui::icon::kRestore : ui::icon::kMaximize, "window.maximize"},
                             {"##tm-min", ui::icon::kMinimize, "window.minimize"}};
        right -= px(4);
        for (const auto& b : buttons) {
            right -= px(24);
            const ImVec2 a(right, cy - px(12));
            ImGui::SetCursorScreenPos(a);
            const bool hit = ImGui::InvisibleButton(b.id, ImVec2(px(24), px(24)));
            const bool hov = ImGui::IsItemHovered();
            dl->AddCircleFilled(ImVec2(a.x + px(12), cy), px(12), hov ? l.edge : l.chip);
            draw_icon(s, dl, b.glyph, fpx(14), ImVec2(a.x + px(12), cy), hov ? l.text : l.dim);
            if (hit)
                run_command(s, b.cmd);
            right -= px(8);
        }
        right += px(8) - px(6) - px(8);  // the group's pl 6, then the bar's gap 8
    }
    if (sp == nullptr) {
        window_drag_area(s, pos, ImVec2(pos.x + width, pos.y + height));
        return;
    }
    // Icon buttons: p 7, radius 8, 17 px icons, dim; hover chip + text.
    auto icon_btn = [&](const char* id, const char* glyph, const char* cmd) {
        const float side = px(17) + px(14);
        right -= side;
        const ImVec2 a(right, cy - side * 0.5f);
        ImGui::SetCursorScreenPos(a);
        const bool hit = ImGui::InvisibleButton(id, ImVec2(side, side));
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(a, ImVec2(a.x + side, a.y + side), l.chip, px(8));
        draw_icon(s, dl, glyph, fpx(17), ImVec2(a.x + side * 0.5f, cy), hov ? l.text : l.dim);
        if (hit)
            run_command(s, cmd);
        right -= px(8);
    };
    icon_btn("##tm-new", ui::icon::kPlus, "tab.new");  // "+" runs tab.new directly (:174)
    icon_btn("##tm-keys", "\xF3\xB0\x98\xB3", "app.palette");  // nf-md-apple_keyboard_command (herdr command.svg)
    // Space chip: h 30, pl 10, pr 12, gap 8, round; 15 px icon, the name (max 160) in text.
    float x = pos.x;
    const auto cwds = s.session_cwds();
    const std::string sname = s.ws.space_title(s.ws.active_space, cwds);
    const float nw = std::min(s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, sname.c_str()).x, px(160));
    const float chip_w = px(10) + px(15) + px(8) + nw + px(12);
    {
        const ImVec2 a(x, cy - px(15));
        ImGui::SetCursorScreenPos(a);
        const bool hit = ImGui::InvisibleButton("##tm-space", ImVec2(chip_w, px(30)));
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(a, ImVec2(a.x + chip_w, a.y + px(30)), l.chip, px(15));
        draw_icon(s, dl, ui::icon::kLayers, fpx(15), ImVec2(a.x + px(10) + px(7.5f), cy), hov ? l.text : l.dim);
        text_clipped(dl, s.fonts.ui, fpx(13), ImVec2(a.x + px(10) + px(15) + px(8), cy - fpx(13) * 0.6f), l.text, sname,
                     px(160));
        if (hit)
            run_command(s, "app.picker");  // the picker drops from the top (popup at top, offset 40)
        x += chip_w + px(8);
    }
    // The tray: h 36, p 3, gap 3, radius 16; tabs 260 wide that shrink to fit.
    const int n = static_cast<int>(sp->tabs.size());
    const float avail = std::max(0.0f, right - x);
    const float want = static_cast<float>(n) * px(260) + static_cast<float>(std::max(0, n - 1)) * px(3) + px(6);
    const float tray_w = std::min(want, avail);
    const float tab_w = n > 0 ? std::max(0.0f, (tray_w - px(6) - static_cast<float>(n - 1) * px(3)) / static_cast<float>(n)) : 0.0f;
    const ImVec2 ta(x, cy - px(18));
    if (n > 0 && tray_w > px(6))
        dl->AddRectFilled(ta, ImVec2(ta.x + tray_w, ta.y + px(36)), l.tray, px(16));
    dl->PushClipRect(ta, ImVec2(ta.x + tray_w, ta.y + px(36)), true);
    std::string close_tab;
    float tx = ta.x + px(3);
    for (int i = 0; i < n; ++i) {
        auto& tab = sp->tabs[static_cast<std::size_t>(i)];
        const bool active = i == sp->active_tab;
        const ImVec2 a(tx, ta.y + px(3));
        const ImVec2 b(tx + tab_w, ta.y + px(33));
        tx += tab_w + px(3);
        ImGui::PushID(tab.id.c_str());
        ImGui::SetCursorScreenPos(a);
        const bool hit = ImGui::InvisibleButton("##tm-tab", ImVec2(std::max(1.0f, tab_w), px(30)));
        const bool hov = ImGui::IsItemHovered();
        if (hit) {
            s.ws.select_tab(i);
            s.focus_view(s.ws.focused_session());
        }
        if (begin_menu("##tabctx")) {  // the tab menu (menu/main.js 58-63), by id
            if (menu_row(s, ui::icon::kEdit, "Rename", s.keymap.chord_for("tab.rename")))
                run_command(s, "tab.rename:" + tab.id);
            if (menu_row(s, ui::icon::kPlus, "New tab", s.keymap.chord_for("tab.new")))
                run_command(s, "tab.new");
            menu_separator();
            if (menu_row(s, ui::icon::kClose, "Close", s.keymap.chord_for("tab.close")))
                close_tab = tab.id;
            ImGui::EndPopup();
        }
        if (active) {
            dl->AddRectFilled(a, b, l.tab, px(13));
            dl->AddRect(a, b, l.tab_edge, px(13));
        } else if (hov) {
            dl->AddRectFilled(a, b, l.hover, px(13));
        }
        // pl 5, pr 12, gap 9: the tile, the name (flex_none), the folder (dim, truncated).
        dl->PushClipRect(a, ImVec2(b.x - px(12), b.y), true);
        const std::string lead = terminals_lead(s, tab);
        terminals_tile(s, dl, ImVec2(a.x + px(5), a.y + px(2)), lead, l);
        float nx = a.x + px(5) + px(26) + px(9);
        const std::string name = ui::Workspace::tab_title(tab, i);
        const float name_w = text_clipped(dl, s.fonts.ui, fpx(13), ImVec2(nx, a.y + px(15) - fpx(13) * 0.6f),
                                          active ? l.text : l.tab_text, name, std::max(0.0f, b.x - px(12) - nx));
        nx += name_w + px(6);
        if (const auto vi = s.views_by_id.find(lead); vi != s.views_by_id.end() && !vi->second.cwd.empty())
            text_clipped(dl, s.fonts.ui, fpx(13), ImVec2(nx, a.y + px(15) - fpx(13) * 0.6f), l.dim,
                         display_path(vi->second.cwd), b.x - px(12) - nx);
        dl->PopClipRect();
        ImGui::PopID();
    }
    dl->PopClipRect();
    if (!close_tab.empty())
        run_command(s, "tab.close:" + close_tab);
    window_drag_area(s, pos, ImVec2(pos.x + width, pos.y + height));
}

// The pane header of the Terminals layout (terminals-pane/main.js 34-61): 38 px; pl 12, pr 8,
// gap 8, 14 px text; the program icon, the terminal's title (semibold, max 320), its folder when
// the title does not already say it, then split right, split down, zoom and close, always shown.
void draw_terminals_pane_header(App::State& s, const std::string& id, const ui::SessionView& v, ImVec2 pos,
                                float w, float h, bool focused) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const TerminalsLook l = terminals_look();
    const float cy = pos.y + h * 0.5f;
    float x = pos.x + px(12);
    program_icon(dl, program_of(v), ImVec2(x, cy - px(8)), px(16), IM_COL32(0xd6, 0xd8, 0xdb, 255));  // terminals-pane/main.js 53
    x += px(16) + px(8);
    // const title = p.title || "shell"; dir unless the title holds it or starts with ~ or /.
    std::string title = s.pane_title(id);
    if (title.empty())
        title = "shell";
    const std::string where = display_path(v.cwd);
    const bool says = !where.empty() && (title.find(where) != std::string::npos || title[0] == '~' ||
                                         title[0] == '/' || (title.size() > 2 && title[1] == ':' && title[2] == '\\'));
    const std::string dir = says ? std::string() : where;
    // Buttons from the right: p 4, radius 5, 15 px icons; #5d6168, hover #ffffff0d + #d6d8db.
    float right = pos.x + w - px(8);
    const auto* tab = s.ws.active_tab();
    const bool zoomed = tab != nullptr && tab->zoomed == id;
    struct B {
        const char* bid;
        const char* glyph;
        const char* cmd;
    };
    // terminals-pane/main.js 19-32: no tooltips.
    const B buttons[] = {{"##th-x", ui::icon::kClose, "pane.close"},
                         {"##th-z", zoomed ? ui::icon::kUnzoom : ui::icon::kZoom, "pane.zoom"},
                         {"##th-d", ui::icon::kSplitDown, "pane.split:down"},
                         {"##th-r", ui::icon::kSplitRight, "pane.split:right"}};
    std::string run;
    for (const auto& b : buttons) {
        const float side = px(15) + px(8);
        right -= side;
        const ImVec2 a(right, cy - side * 0.5f);
        ImGui::SetCursorScreenPos(a);
        const bool hit = ImGui::InvisibleButton(b.bid, ImVec2(side, side));
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(a, ImVec2(a.x + side, a.y + side), alpha(g_pal->text, 0x0d), px(5));
        draw_icon(s, dl, b.glyph, fpx(15), ImVec2(a.x + side * 0.5f, cy), hov ? l.button_hover : l.button);
        if (hit)
            run = b.cmd;
        right -= px(8);
    }
    const float tw = text_clipped(dl, s.fonts.ui_bold, fpx(14), ImVec2(x, cy - fpx(14) * 0.6f),
                                  focused ? l.text : mix(g_pal->dim, g_pal->text, 0.4f), title,
                                  std::min(px(320), std::max(0.0f, right - x)));
    x += tw + px(8);
    text_clipped(dl, s.fonts.ui, fpx(14), ImVec2(x, cy - fpx(14) * 0.6f), l.dim, dir, right - x);
    // A press on the header focuses the pane (pane.focus).
    if (ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + w, pos.y + h)) && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        s.ws.focus(id);
        s.focus_view(id);
    }
    if (!run.empty()) {
        s.ws.focus(id);
        run_command(s, run);
    }
}

// ---------------------------------------------------------------- status bar (workspace-status)
void draw_status_bar(App::State& s, ImVec2 pos, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), g_pal->sidebar);
    dl->AddLine(pos, ImVec2(pos.x + width, pos.y), g_pal->border);
    const float fs = fpx(11.8f);
    const float cy = pos.y + height * 0.5f;
    float x = pos.x + px(14);
    auto text = [&](const std::string& t, ImU32 col) {
        dl->AddText(s.fonts.ui, fs, ImVec2(x, cy - fs * 0.6f), col, t.c_str());
        x += s.fonts.ui->CalcTextSizeA(fs, FLT_MAX, 0, t.c_str()).x;
    };
    const auto* sp = s.ws.active();
    const std::string machine = sp != nullptr ? sp->machine : std::string();
    const bool connected = s.machine_connected(machine);
    draw_icon(s, dl, machine.empty() ? ui::icon::kMonitor : ui::icon::kServer, fpx(13), ImVec2(x + px(6.5f), cy),
              connected ? g_pal->dim : g_pal->needs_you);
    x += px(13) + px(6);
    if (!s.host->connected()) {
        text("agent-host disconnected: " + s.host->last_error(), g_pal->needs_you);
        ImGui::SetCursorScreenPos(ImVec2(x + px(8), pos.y + px(3)));
        if (ImGui::SmallButton("Start host"))
            s.host->request_start_host();
        x += px(90);
    } else {
        text(machine.empty() ? std::string("This computer") : machine, g_pal->dim);
    }
    x += px(16);
    const std::string fid = s.ws.focused_session();
    if (s.views_by_id.count(fid)) {
        const auto g = s.git.get(s.views_by_id[fid].cwd, machine);
        if (g.repo && !g.branch.empty()) {
            draw_icon(s, dl, ui::icon::kBranch, fpx(13), ImVec2(x + px(6.5f), cy), g_pal->dim);
            x += px(13) + px(6);
            text(g.branch == "HEAD" ? "(detached)" : g.branch, g_pal->dim);
            x += px(16);
        }
    }
    int agents = 0, need = 0;
    for (const auto& [id, v] : s.views_by_id) {
        if (!live_agent(s, id, v))  // the same agents the sidebar lists
            continue;
        ++agents;
        need += s.attention_of(id) == Attention::kBlocked ? 1 : 0;
    }
    dl->AddCircleFilled(ImVec2(x + px(3), cy), px(3), g_pal->faint);
    x += px(12);
    text(std::to_string(agents) + (agents == 1 ? " agent" : " agents"), g_pal->dim);
    if (need > 0)
        text(" \xC2\xB7 " + std::to_string(need) + (need == 1 ? " needs you" : " need you"), g_pal->needs_you);
    if (s.host->host_dies_with_launcher()) {
        x += px(16);
        text("host ends when the launching terminal closes", g_pal->working);
    }
    // Right: the palette hint (click opens the palette).
    // workspace-status/main.js 8-15: without a binding the hint is just "commands".
    const std::string chord = s.keymap.chord_for("app.palette");
    const std::string hint = chord.empty() ? std::string("commands") : chord + "  commands";
    const float hw = s.fonts.ui->CalcTextSizeA(fs, FLT_MAX, 0, hint.c_str()).x;
    const ImVec2 ha(pos.x + width - px(14) - hw, pos.y);
    if (ha.x > x + px(16)) {
        ImGui::SetCursorScreenPos(ha);
        if (ImGui::InvisibleButton("##palette-hint", ImVec2(hw, height)))
            run_command(s, "app.palette");
        dl->AddText(s.fonts.ui, fs, ImVec2(ha.x, cy - fs * 0.6f), ImGui::IsItemHovered() ? g_pal->text : g_pal->faint,
                    hint.c_str());
    }
}


namespace w98 {
float pane_border(App::State& s);
float pane_header_h(App::State& s);
void pane_header_at(App::State& s, const std::string& id, ImVec2 a, ImVec2 b, bool focused);
}  // namespace w98

void draw_pane(App::State& s, const std::string& id, ui::Rect r, bool connected) {
    const auto vit = s.views_by_id.find(id);
    const auto sit = s.by_id.find(id);
    if (vit == s.views_by_id.end() || sit == s.by_id.end())
        return;
    const auto& v = vit->second;
    auto& vs = s.views[id];
    const bool focused = s.ws.focused_session() == id;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos(r.x, r.y);
    const ImVec2 max(r.x + r.w, r.y + r.h);
    const Attention att = s.attention_of(id);
    // herdr pane frame (main.json 107-131, pane.js 59-91): radius 10, 1 px border, 1 px accent
    // when focused; 30 px header: icon, name, folder (~), status; buttons only while hovered.
    // The Terminals layout (terminals.json 37-45): radius 12, a quiet border and focus edge, and
    // its own 38 px header (terminals-pane).
    // Herdr 98 (win98.json 26-58, kit.js 464-473): radius 0, a 3 px #c0c0c0 border (the same
    // focused), background #000000, its own 18 px title bar at the top (window.js).
    const bool tm = s.layout().style == "terminals";
    const bool w98p = s.layout().style == "win98";
    const float edge = w98p ? w98::pane_border(s) : 0.0f;
    const TerminalsLook tl = terminals_look();
    const float radius = w98p ? 0.0f : px(tm ? 12.0f : 10.0f);
    if (w98p) {
        dl->AddRectFilled(pos, max, IM_COL32(0, 0, 0, 255));
        const ImU32 face = IM_COL32(0xc0, 0xc0, 0xc0, 255);
        dl->AddRectFilled(pos, ImVec2(max.x, pos.y + edge), face);
        dl->AddRectFilled(ImVec2(pos.x, max.y - edge), max, face);
        dl->AddRectFilled(pos, ImVec2(pos.x + edge, max.y), face);
        dl->AddRectFilled(ImVec2(max.x - edge, pos.y), max, face);
    } else {
        dl->AddRectFilled(pos, max, g_pal->term_bg, radius);
        dl->AddRect(pos, max, tm ? (focused ? tl.pane_focus : tl.pane_border) : (focused ? g_pal->focus : g_pal->border),
                    radius, 0, 1.0f);
    }

    const float fs = ImGui::GetFontSize();
    const float header_h = w98p ? w98::pane_header_h(s) : px(tm ? 38.0f : 30.0f);
    ImGui::PushID(id.c_str());
    if (w98p) {
        w98::pane_header_at(s, id, ImVec2(pos.x + edge, pos.y + edge), ImVec2(max.x - edge, pos.y + edge + header_h), focused);
    } else if (tm) {
        draw_terminals_pane_header(s, id, v, pos, r.w, header_h, focused);
    } else {
        const float cy = pos.y + header_h * 0.5f;
        // herdr pane/main.js 86-90: the buttons show while the header is hovered. Pressing one
        // makes it the active item, which must not hide it before the release lands.
        const bool pane_hovered =
            ImGui::IsMouseHoveringRect(pos, ImVec2(max.x, pos.y + header_h), false) &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        float hx = pos.x + px(10);
        const ui::Program prog = program_of(v);
        program_icon(dl, prog, ImVec2(hx, cy - px(7)), px(14), g_pal->dim);  // pane/main.js 76
        hx += px(14) + px(8);
        // pane/main.js 61: name || ((agent || shell) && program.name) || title || program.name || "shell".
        std::string title = s.pane_title(id);
        if (v.name.empty() && !prog.agent && !prog.shell)
            if (const auto si = s.by_id.find(id); si != s.by_id.end()) {
                std::string t = si->second->snapshot()->title;
                while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back())))
                    t.pop_back();
                if (!t.empty() && (title.empty() || title == prog.name))
                    title = t;
            }
        if (title.empty())
            title = "shell";
        // pane/main.js 69, 78-79: the name 12 px medium, the folder 12 px dim.
        const float tw = text_clipped(dl, s.fonts.ui_medium, fpx(12), ImVec2(hx, cy - fpx(12) * 0.6f), g_pal->text, title,
                                      std::min(px(260), r.w * 0.4f));
        hx += tw + px(8);

        float rx = max.x - px(8);
        if (pane_hovered) {
            // pane/main.js 44-56: no tooltips on these buttons.
            auto hbtn = [&](const char* bid, const char* glyph, const char* cmd) {
                const float b = px(20);  // 14 px icon, 3 px padding
                rx -= b;
                if (icon_button(s, bid, glyph, ImVec2(rx, cy - b * 0.5f), b, fpx(14), nullptr, g_pal->dim)) {
                    s.ws.focus(id);
                    run_command(s, cmd);
                }
                rx -= px(2);
            };
            const auto* tab = s.ws.active_tab();
            const bool zoomed = tab != nullptr && tab->zoomed == id;
            hbtn("##close", ui::icon::kClose, "pane.close");
            hbtn("##zoom", zoomed ? ui::icon::kUnzoom : ui::icon::kZoom, "pane.zoom");
            hbtn("##sd", ui::icon::kSplitDown, "pane.split:down");
            hbtn("##sr", ui::icon::kSplitRight, "pane.split:right");
            rx -= px(4);
        }
        // Status: spinner / "Needs you" pill / done dot.
        if (v.process_state == "Running" && att != Attention::kNone && att != Attention::kIdle) {
            const float mw = status_mark(s, dl, 0, cy, att, false, true);
            if (rx - mw > hx) {
                status_mark(s, dl, rx - mw, cy, att, false);
                rx -= mw + px(8);
            }
        }
        // Folder (~ for home), dim, the rest of the width.
        text_clipped(dl, s.fonts.ui, fpx(12), ImVec2(hx, cy - fpx(12) * 0.6f), g_pal->faint, display_path(v.cwd), rx - hx);
        // Grip (herdr pane_drag.rs): 28x18 in the middle of the header, two rows of three dots;
        // only with two or more panes and not zoomed. Dragging it moves the pane.
        {
            const auto* tab = s.ws.active_tab();
            const bool can_drag = tab != nullptr && tab->zoomed.empty() && s.ws.sessions_in(*tab).size() >= 2;
            const bool header_hov = ImGui::IsMouseHoveringRect(pos, ImVec2(max.x, pos.y + header_h), false);
            if (can_drag && (header_hov || s.drag_pane == id)) {
                const ImVec2 g(pos.x + r.w * 0.5f - px(14), cy - px(9));
                ImGui::SetCursorScreenPos(g);
                ImGui::InvisibleButton("##grip", ImVec2(px(28), px(18)));
                const bool gh = ImGui::IsItemHovered() || s.drag_pane == id;
                if (ImGui::IsItemActivated()) {
                    s.drag_pane = id;
                    s.drag_pane_from = ImGui::GetIO().MousePos;
                }
                if (gh)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                dl->AddRectFilled(g, ImVec2(g.x + px(28), g.y + px(18)), alpha(g_pal->term_bg, 217), px(4));
                for (int row = 0; row < 2; ++row)
                    for (int col = 0; col < 3; ++col)
                        dl->AddCircleFilled(ImVec2(g.x + px(9) + static_cast<float>(col) * px(5), g.y + px(6.5f) + static_cast<float>(row) * px(5)),
                                            px(1), gh ? g_pal->text : g_pal->faint);
            }
        }
        if (ImGui::IsMouseHoveringRect(pos, ImVec2(max.x, pos.y + header_h)) && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
            s.ws.focus(id);
            vs.focus_next = true;
        }
    }

    float top = pos.y + edge + header_h;
    std::string banner;
    ImU32 bc = g_pal->working;
    if (!connected) {
        banner = "Disconnected from agent-host";
    } else if (v.lost) {
        banner = "Lost: owned by a previous agent-host; nothing was re-run";
    } else if (v.process_state == "Exited") {
        char b[96];
        std::snprintf(b, sizeof b, "Exited%s%s", v.exit_code ? " with code " : "",
                      v.exit_code ? std::to_string(*v.exit_code).c_str() : "");
        banner = b;
        bc = v.exit_code && *v.exit_code != 0 ? g_pal->needs_you : g_pal->dim;
    } else if (!v.is_controller && v.process_state == "Running") {
        banner = "Read-only: another window controls this session";
        bc = g_pal->info;
    }
    if (!banner.empty()) {
        dl->AddRectFilled(ImVec2(pos.x + px(10), top), ImVec2(max.x - px(10), top + px(30)), alpha(bc, 34),
                          px(7));
        dl->AddText(s.fonts.ui, fs, ImVec2(pos.x + px(20), top + (px(30) - fs) * 0.5f), bc, banner.c_str());
        float bx = max.x - px(20);
        auto bbtn = [&](const char* label, auto fn) {
            const float bw = ImGui::CalcTextSize(label, nullptr, true).x + px(16);
            bx -= bw;
            ImGui::SetCursorScreenPos(ImVec2(bx, top + px(3)));
            if (ImGui::Button(label, ImVec2(bw, px(24))))
                fn();
            bx -= px(6);
        };
        if (connected && (v.lost || v.process_state == "Exited")) {
            bbtn("Restart here", [&] {
                s.ws.focus(id);
                run_command(s, "session.restart");
            });
        } else if (connected && !v.lost && !v.is_controller) {
            bbtn("Take control", [&] { sit->second->take_control(); });
        }
        top += px(36);
    }

    // herdr main.json terminal_padding [14, 12]; the Terminals layout keeps the default [10, 6]
    // (settings.rs 564); Herdr 98 [4, 2] inside its 3 px border (win98.json 93-96).
    const ImVec2 body_min = w98p ? ImVec2(pos.x + edge + px(4), top + px(2))
                                 : ImVec2(pos.x + px(tm ? 10.0f : 14.0f), top + px(tm ? 6.0f : 2.0f));
    const ImVec2 body_size = w98p ? ImVec2(r.w - edge * 2 - px(8), max.y - edge - top - px(4))
                                  : ImVec2(r.w - px(tm ? 20.0f : 28.0f), max.y - top - (tm ? px(12) : px(2) + px(12)));
    if (body_size.x > px(20) && body_size.y > px(10)) {
        ImGui::SetCursorScreenPos(body_min);
        ImGui::BeginChild(
            "##body", body_size, ImGuiChildFlags_None,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
        vs.view.keymap = &s.keymap;
        vs.view.copy_on_select = s.settings.copy_on_select;
        vs.view.scroll_speed = s.settings.scroll_speed;
        vs.view.line_height = s.settings.line_height;
        vs.view.right_click_to_program = s.ws.right_click_program.contains(id);
        // herdr pane menu (menu/main.js 24-81): Split right, Split down, Zoom, Rename, where
        // right-clicks go, Send back to <parent>, Close pane.
        vs.view.menu_rows = [&s, id, parent = v.parent] {
            menu_keys();  // Up/Down/Enter/Esc (menu/main.js 131-137)
            auto pick = [&](const char* glyph, const char* label, const char* cmd) {
                if (!menu_row(s, glyph, label, cmd != nullptr ? s.keymap.chord_for(cmd) : std::string()))
                    return false;
                s.ws.focus(id);
                return true;
            };
            if (pick(ui::icon::kSplitRight, "Split right", "pane.split:right")) {
                run_command(s, "pane.split:right");
                return true;
            }
            if (pick(ui::icon::kSplitDown, "Split down", "pane.split:down")) {
                run_command(s, "pane.split:down");
                return true;
            }
            if (pick(ui::icon::kSquare, "Zoom", "pane.zoom")) {  // menu/main.js 84: square
                run_command(s, "pane.zoom");
                return true;
            }
            if (pick(ui::icon::kEdit, "Rename", "pane.rename")) {  // menu/main.js 84: square-pen
                run_command(s, "pane.rename");
                return true;
            }
            const bool to_program = s.ws.right_click_program.contains(id);
            if (pick(ui::icon::kTerminal, to_program ? "Use AgentDesk right-click menu" : "Send right-clicks to pane",
                     nullptr)) {
                run_command(s, to_program ? "terminal.right_click:menu" : "terminal.right_click:program");
                return true;
            }
            if (!parent.empty() && s.views_by_id.count(parent)) {
                const std::string label = "Send back to " + s.pane_title(parent);
                if (pick(ui::icon::kTerminal, label.c_str(), nullptr)) {  // menu/main.js 84: terminal
                    run_command(s, "pane.park");
                    return true;
                }
            }
            menu_separator();
            if (pick(ui::icon::kClose, "Close pane", "pane.close")) {
                run_command(s, "pane.close");
                return true;
            }
            return false;
        };
        vs.view.parent_name = !v.parent.empty() && s.views_by_id.count(v.parent) ? s.pane_title(v.parent) : std::string();
        vs.view.cell_width = s.settings.cell_width;
        vs.view.theme.default_bg = g_pal->term_bg;
        vs.view.theme.default_fg = g_pal->term_fg;
        vs.view.theme.cursor = g_pal->cursor;
        vs.view.theme.selection = g_pal->selection;
        vs.view.theme.ansi = g_pal->ansi;
        vs.view.theme.scrollbar = alpha(g_pal->dim, 120);
        const bool active = vs.view.draw("grid", *sit->second, s.fonts, vs.focus_next);
        vs.focus_next = false;
        if (active && s.ws.focused_session() != id)
            s.ws.focus(id);
        for (const auto& cmd : vs.view.take_commands()) {
            s.ws.focus(id);
            run_command(s, cmd);
        }
        ImGui::EndChild();
    }
    ImGui::PopID();
}

void draw_panes(App::State& s, ImVec2 pos, ImVec2 size, bool connected) {
    s.pane_area = {pos.x, pos.y, size.x, size.y};
    ui::Tab* tab = s.ws.active_tab();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (tab == nullptr) {
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->card, px(12));
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->border, px(12));
        const float fs = ImGui::GetFontSize();
        const char* title = connected ? "Open a terminal" : "Connecting to agent-host...";
        const ImVec2 tsz = s.fonts.ui_bold->CalcTextSizeA(fs * 1.4f, FLT_MAX, 0, title);
        const float cx = pos.x + size.x * 0.5f;
        const float cy = pos.y + size.y * 0.4f;
        dl->AddText(s.fonts.ui_bold, fs * 1.4f, ImVec2(cx - tsz.x * 0.5f, cy - tsz.y), g_pal->text, title);
        const char* hint = "cd to your project, then run claude or codex. It keeps running after you close this window.";
        const ImVec2 hsz = ImGui::CalcTextSize(hint);
        dl->AddText(s.fonts.ui, fs, ImVec2(cx - hsz.x * 0.5f, cy + px(10)), g_pal->dim, hint);
        if (connected) {
            const std::string label = "New terminal   " + s.keymap.chord_for("tab.new");
            const std::string other = "Start a program...   " + s.keymap.chord_for("agent.new");
            const float bw = ImGui::CalcTextSize(label.c_str()).x + px(36);
            const float ow = ImGui::CalcTextSize(other.c_str()).x + px(36);
            const float total = bw + px(10) + ow;
            ImGui::SetCursorScreenPos(ImVec2(cx - total * 0.5f, cy + px(44)));
            ImGui::PushStyleColor(ImGuiCol_Button, v4(g_pal->focus));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(alpha(g_pal->focus, 220)));
            ImGui::PushStyleColor(ImGuiCol_Text, v4(IM_COL32(255, 255, 255, 255)));
            if (ImGui::Button(label.c_str(), ImVec2(bw, px(36))))
                run_command(s, "tab.new");
            ImGui::PopStyleColor(3);
            ImGui::SameLine(0, px(10));
            if (ImGui::Button(other.c_str(), ImVec2(ow, px(36))))
                run_command(s, "agent.new");
        }
        return;
    }
    const ui::Rect area{pos.x, pos.y, size.x, size.y};
    const float gap = px(8);
    if (!tab->zoomed.empty()) {
        draw_pane(s, tab->zoomed, area, connected);
        return;
    }
    s.pane_rects = ui::layout_tree(tab->root, area, gap);
    for (const auto& [id, r] : s.pane_rects)
        draw_pane(s, id, r, connected);
    // Pane drag: after 4 px, the pane under the pointer is the target; its outer 25 % picks the
    // nearest edge, the middle swaps. Accent preview; release drops, Esc cancels.
    if (!s.drag_pane.empty()) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float dx = m.x - s.drag_pane_from.x, dy = m.y - s.drag_pane_from.y;
        const bool moved = dx * dx + dy * dy >= px(4) * px(4);
        std::string target;
        std::optional<Dir> edge;
        ui::Rect pr{};
        for (const auto& [pid, r] : s.pane_rects) {
            if (pid == s.drag_pane || m.x < r.x || m.x >= r.x + r.w || m.y < r.y || m.y >= r.y + r.h)
                continue;
            target = pid;
            const float fx = (m.x - r.x) / r.w, fy = (m.y - r.y) / r.h;
            const float d = std::min({fx, 1 - fx, fy, 1 - fy});
            pr = r;
            if (d < 0.25f) {
                if (d == fx) {
                    edge = Dir::kLeft;
                    pr.w *= 0.5f;
                } else if (d == 1 - fx) {
                    edge = Dir::kRight;
                    pr.x += r.w * 0.5f;
                    pr.w *= 0.5f;
                } else if (d == fy) {
                    edge = Dir::kUp;
                    pr.h *= 0.5f;
                } else {
                    edge = Dir::kDown;
                    pr.y += r.h * 0.5f;
                    pr.h *= 0.5f;
                }
            }
        }
        if (moved && !target.empty()) {
            ImDrawList* fg = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
            fg->AddRectFilled(ImVec2(pr.x, pr.y), ImVec2(pr.x + pr.w, pr.y + pr.h), alpha(g_pal->focus, 46), px(10));
            fg->AddRect(ImVec2(pr.x, pr.y), ImVec2(pr.x + pr.w, pr.y + pr.h), g_pal->focus, px(10), 0, 2.0f);
        }
        if (moved)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            s.drag_pane.clear();
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (moved && !target.empty() && s.ws.move_pane(s.drag_pane, target, edge))
                s.focus_view(s.drag_pane);
            s.drag_pane.clear();
        }
    }
    // Draggable borders between panes.
    int n = 0;
    for (const auto& d : ui::dividers(tab->root, area, gap)) {
        ImGui::PushID(n++);
        ImGui::SetCursorScreenPos(ImVec2(d.bar.x, d.bar.y));
        ImGui::InvisibleButton("##div", ImVec2(std::max(d.bar.w, 2.0f), std::max(d.bar.h, 2.0f)));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(d.vertical ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            ui::drag_divider(tab->root, d, d.vertical ? m.y - gap * 0.5f : m.x - gap * 0.5f, gap);
            dl->AddRectFilled(ImVec2(d.bar.x, d.bar.y), ImVec2(d.bar.x + d.bar.w, d.bar.y + d.bar.h),
                              alpha(g_pal->focus, 120), px(3));
        }
        ImGui::PopID();
    }
}

// Folder the panels follow: the focused pane's live folder (herdr files.js 6-12), on the machine
// that pane runs on ("" = this computer).
std::string panel_folder(App::State& s, std::string* machine = nullptr) {
    const auto id = s.ws.focused_session();
    if (const auto it = s.views_by_id.find(id); it != s.views_by_id.end()) {
        if (machine != nullptr)
            *machine = s.machine_of[id];
        return it->second.cwd;
    }
    return {};
}

ImU32 mark_color(char m) {
    return m == 'A' ? g_pal->done : (m == 'D' || m == 'U') ? g_pal->needs_you : g_pal->working;
}

void panel_header(App::State& s, ImDrawList* dl, ImVec2 a, float w, const char* title, const std::string& right,
                  bool back, bool* back_clicked) {
    const float h = px(42);
    const float cy = a.y + h * 0.5f;
    float x = a.x + px(14);
    if (back) {
        ImGui::SetCursorScreenPos(ImVec2(x - px(4), cy - px(11)));
        if (ImGui::InvisibleButton("##back", ImVec2(px(20), px(22))) && back_clicked != nullptr)
            *back_clicked = true;
        draw_icon(s, dl, ui::icon::kArrowLeft, fpx(14), ImVec2(x + px(6), cy), ImGui::IsItemHovered() ? g_pal->text : g_pal->dim);
        x += px(20) + px(4);
    }
    const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(13), FLT_MAX, 0, title);
    dl->AddText(s.fonts.ui_bold, fpx(13), ImVec2(x, cy - t.y * 0.5f), g_pal->text, title);
    x += t.x + px(8);
    if (!right.empty()) {
        const float rw = std::min(s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, right.c_str()).x, a.x + w - px(14) - x);
        text_clipped(dl, s.fonts.ui, fpx(12), ImVec2(a.x + w - px(14) - rw, cy - fpx(12) * 0.6f), g_pal->faint, right, rw);
    }
    dl->AddLine(ImVec2(a.x, a.y + h), ImVec2(a.x + w, a.y + h), g_pal->border);
}

// ---- read-only code preview (herdr files/main.js:141-144 shows files in a read-only editor with
// syntax colours: keyword ansi5, string ansi2, comment dim, function ansi4, type ansi3, number ansi1,
// constant ansi6). A small lexer stands in for the editor's tree-sitter grammars.
enum class CodeTok : std::uint8_t { kText, kKeyword, kString, kComment, kFunction, kType, kNumber, kConstant };

struct CodeLang {
    std::string_view line_comment;   // "" = none
    std::string_view line_comment2;  // a second form ("#" next to "//" in PHP, ";" in INI)
    std::string_view block_open;     // "" = none
    std::string_view block_close;
    bool code = true;  // false: plain text, no colours
};

CodeLang code_lang(const std::string& path) {
    std::string ext;
    const auto dot = path.find_last_of('.');
    const auto slash = path.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        ext = path.substr(dot + 1);
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    for (auto& c : name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const std::set<std::string> kSlash = {"c",  "h",    "cpp", "hpp",   "cc",   "cxx",  "hh",   "inl",  "cs",
                                                 "java", "kt", "kts", "swift", "go",   "rs",   "js",   "jsx",  "ts",
                                                 "tsx", "mjs", "cjs", "mts",   "cts",  "css",  "scss", "less", "zig",
                                                 "dart", "scala", "glsl", "hlsl", "proto", "jsonc", "json5", "m", "mm"};
    static const std::set<std::string> kHash = {"py", "pyw", "sh",  "bash", "zsh", "fish", "ps1",  "psm1", "psd1", "rb",
                                                "yaml", "yml", "toml", "cmake", "r", "pl", "pm",   "conf", "nix",  "mk",
                                                "tf", "hcl", "gitignore", "gitattributes", "editorconfig", "env", "dockerignore"};
    if (kSlash.contains(ext))
        return {"//", "", "/*", "*/"};
    if (ext == "php")
        return {"//", "#", "/*", "*/"};
    if (kHash.contains(ext) || name == "cmakelists.txt" || name == "dockerfile" || name == "makefile")
        return {"#", "", "", ""};
    if (ext == "ini" || ext == "cfg" || ext == "reg")
        return {";", "#", "", ""};
    if (ext == "lua" || ext == "sql" || ext == "hs")
        return {"--", "", ext == "lua" ? "--[[" : "/*", ext == "lua" ? "]]" : "*/"};
    if (ext == "html" || ext == "htm" || ext == "xml" || ext == "svg" || ext == "vue" || ext == "svelte" ||
        ext == "xaml" || ext == "csproj" || ext == "props" || ext == "targets" || ext == "vcxproj")
        return {"", "", "<!--", "-->"};
    if (ext == "json")
        return {"", "", "", ""};
    if (ext == "bat" || ext == "cmd")
        return {"rem ", "::", "", ""};
    return {"", "", "", "", false};
}

bool code_keyword(std::string_view w) {
    static const std::set<std::string_view> kWords = {
        "if", "else", "elif", "for", "while", "do", "switch", "case", "default", "break", "continue", "return",
        "goto", "try", "catch", "finally", "throw", "throws", "new", "delete", "class", "struct", "enum", "union",
        "interface", "namespace", "using", "typedef", "template", "typename", "public", "private", "protected",
        "static", "const", "constexpr", "consteval", "constinit", "volatile", "extern", "inline", "virtual",
        "override", "final", "explicit", "friend", "operator", "sizeof", "alignof", "decltype", "auto", "mutable",
        "noexcept", "co_await", "co_return", "co_yield", "import", "export", "from", "as", "package", "module",
        "def", "lambda", "pass", "with", "yield", "async", "await", "in", "is", "not", "and", "or", "del",
        "global", "nonlocal", "assert", "raise", "except", "fn", "let", "mut", "impl", "trait", "pub", "crate",
        "mod", "use", "match", "loop", "where", "move", "ref", "dyn", "unsafe", "extends", "implements",
        "instanceof", "typeof", "var", "function", "this", "super", "self", "void", "func", "go", "defer",
        "chan", "select", "type", "range", "map", "fallthrough", "end", "then", "local", "begin", "until",
        "elseif", "fi", "esac", "done", "echo", "export", "readonly", "when", "val", "object", "companion",
        "override", "sealed", "open", "internal", "suspend", "foreach", "of", "satisfies", "declare", "abstract",
        "int", "char", "bool", "float", "double", "long", "short", "unsigned", "signed", "string"};
    return kWords.contains(w);
}

bool code_constant(std::string_view w) {
    static const std::set<std::string_view> kConst = {"true", "false", "null", "nullptr", "nil", "None", "True",
                                                      "False", "undefined", "NULL", "NaN", "Infinity"};
    if (kConst.contains(w))
        return true;
    // ALL_CAPS names (two letters or more).
    bool upper = false;
    for (const char c : w) {
        if (std::islower(static_cast<unsigned char>(c)))
            return false;
        upper = upper || std::isupper(static_cast<unsigned char>(c));
    }
    return upper && w.size() > 1;
}

struct CodeSpan {
    std::size_t a = 0;
    std::size_t b = 0;
    CodeTok tok = CodeTok::kText;
};

// One line's coloured spans; `in_block` carries an open block comment to the next line.
void highlight_code_line(std::string_view line, const CodeLang& lang, bool& in_block, std::vector<CodeSpan>& out) {
    out.clear();
    if (!lang.code) {
        out.push_back({0, line.size(), CodeTok::kText});
        return;
    }
    auto ident = [](unsigned char c) { return std::isalnum(c) != 0 || c == '_' || c == '$' || c >= 0x80; };
    std::size_t i = 0;
    std::size_t text_from = 0;
    auto flush_text = [&](std::size_t upto) {
        if (upto > text_from)
            out.push_back({text_from, upto, CodeTok::kText});
    };
    auto emit = [&](std::size_t a, std::size_t b, CodeTok t) {
        flush_text(a);
        out.push_back({a, b, t});
        text_from = b;
    };
    auto starts = [&](std::size_t at, std::string_view tok) {
        return !tok.empty() && line.substr(at, tok.size()) == tok;
    };
    while (i < line.size()) {
        if (in_block) {
            const auto end = line.find(lang.block_close, i);
            const std::size_t b = end == std::string_view::npos ? line.size() : end + lang.block_close.size();
            emit(i, b, CodeTok::kComment);
            i = b;
            in_block = end == std::string_view::npos;
            continue;
        }
        const auto c = static_cast<unsigned char>(line[i]);
        if (starts(i, lang.block_open)) {
            in_block = true;
            const std::size_t from = i;
            i += lang.block_open.size();
            const auto end = line.find(lang.block_close, i);
            const std::size_t b = end == std::string_view::npos ? line.size() : end + lang.block_close.size();
            emit(from, b, CodeTok::kComment);
            i = b;
            in_block = end == std::string_view::npos;
            continue;
        }
        if (starts(i, lang.line_comment) || starts(i, lang.line_comment2)) {
            emit(i, line.size(), CodeTok::kComment);
            i = line.size();
            break;
        }
        if (c == '"' || c == '\'' || c == '`') {
            std::size_t j = i + 1;
            while (j < line.size() && line[j] != static_cast<char>(c))
                j += line[j] == '\\' ? 2 : 1;
            j = std::min(j + 1, line.size());
            emit(i, j, CodeTok::kString);
            i = j;
            continue;
        }
        if (std::isdigit(c) != 0 && (i == 0 || !ident(static_cast<unsigned char>(line[i - 1])))) {
            std::size_t j = i + 1;
            while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) != 0 || line[j] == '.' ||
                                       line[j] == '_' || line[j] == '\''))
                ++j;
            emit(i, j, CodeTok::kNumber);
            i = j;
            continue;
        }
        if (ident(c) && std::isdigit(c) == 0) {
            std::size_t j = i + 1;
            while (j < line.size() && ident(static_cast<unsigned char>(line[j])))
                ++j;
            const std::string_view w = line.substr(i, j - i);
            std::size_t k = j;
            while (k < line.size() && line[k] == ' ')
                ++k;
            CodeTok t = CodeTok::kText;
            if (code_keyword(w))
                t = CodeTok::kKeyword;
            else if (code_constant(w))
                t = CodeTok::kConstant;
            else if (k < line.size() && line[k] == '(')
                t = CodeTok::kFunction;
            else if (std::isupper(c) != 0)
                t = CodeTok::kType;
            if (t != CodeTok::kText)
                emit(i, j, t);
            i = j;
            continue;
        }
        ++i;
    }
    flush_text(line.size());
}

ImU32 code_color(CodeTok t) {
    switch (t) {
        case CodeTok::kKeyword:
            return mix(g_pal->needs_you, g_pal->info, 0.5f);  // magenta (ansi5)
        case CodeTok::kString:
            return g_pal->done;  // green (ansi2)
        case CodeTok::kComment:
            return g_pal->faint;  // dim
        case CodeTok::kFunction:
            return g_pal->info;  // blue (ansi4)
        case CodeTok::kType:
            return g_pal->working;  // yellow (ansi3)
        case CodeTok::kNumber:
            return g_pal->needs_you;  // red (ansi1)
        case CodeTok::kConstant:
            return mix(g_pal->info, g_pal->done, 0.5f);  // cyan (ansi6)
        default:
            return g_pal->text;
    }
}

// Line starts and the block-comment state at each line start, recomputed when the text changes.
struct CodeLayout {
    const std::string* text = nullptr;
    std::size_t size = 0;
    std::string path;
    std::vector<std::size_t> starts;
    std::vector<std::uint8_t> in_block;
};

void layout_code(CodeLayout& L, const std::string& text, const std::string& path) {
    if (L.text == &text && L.size == text.size() && L.path == path)
        return;
    L.text = &text;
    L.size = text.size();
    L.path = path;
    L.starts = {0};
    for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\n')
            L.starts.push_back(i + 1);
    const CodeLang lang = code_lang(path);
    L.in_block.assign(L.starts.size(), 0);
    bool block = false;
    std::vector<CodeSpan> spans;
    for (std::size_t n = 0; n < L.starts.size(); ++n) {
        L.in_block[n] = block ? 1 : 0;
        if (lang.block_open.empty())
            continue;
        const std::size_t a = L.starts[n];
        const std::size_t b = n + 1 < L.starts.size() ? L.starts[n + 1] - 1 : text.size();
        highlight_code_line(std::string_view(text).substr(a, b - a), lang, block, spans);
    }
}

// Selectable read-only text with line numbers: drag selects, double-click a word, Ctrl+A all,
// Ctrl+C (or the right-click Copy) copies. Caller holds the panel mutex.
void draw_code_preview(App::State& s, SidePanels& p, ImVec2 size) {
    static CodeLayout layout;
    const std::string& text = p.preview_text;
    layout_code(layout, text, p.preview_path);
    const CodeLang lang = code_lang(p.preview_path);
    ImGui::BeginChild("##preview", size, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);
    ImGui::PushFont(s.fonts.mono, 12);
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const float lh = std::round(ImGui::GetTextLineHeightWithSpacing());
    const float text_x = px(48);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const int n = static_cast<int>(layout.starts.size());
    auto line_end = [&](int i) {
        std::size_t b = i + 1 < n ? layout.starts[static_cast<std::size_t>(i) + 1] - 1 : text.size();
        const std::size_t a = layout.starts[static_cast<std::size_t>(i)];
        if (b > a && text[b - 1] == '\r')
            --b;
        return b;
    };
    auto width_of = [&](std::size_t a, std::size_t b) {
        return b > a ? font->CalcTextSizeA(fs, FLT_MAX, 0, text.data() + a, text.data() + b).x : 0.0f;
    };
    std::size_t longest = 0;
    for (int i = 0; i < n; ++i)
        longest = std::max(longest, line_end(i) - layout.starts[static_cast<std::size_t>(i)]);
    const float content_w = text_x + static_cast<float>(longest) * fs * 0.62f + px(20);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float top = origin.y + px(4);
    // Mouse -> (line, byte): the byte boundary nearest the pointer.
    auto hit = [&](ImVec2 m) -> std::pair<int, std::size_t> {
        const int line = std::clamp(static_cast<int>(std::floor((m.y - top) / lh)), 0, std::max(0, n - 1));
        const std::size_t a = layout.starts[static_cast<std::size_t>(line)];
        const std::size_t b = line_end(line);
        float x = origin.x + text_x;
        std::size_t i = a;
        while (i < b) {
            std::size_t j = i + 1;
            while (j < b && (static_cast<unsigned char>(text[j]) & 0xC0) == 0x80)
                ++j;
            const float w = width_of(i, j);
            if (m.x < x + w * 0.5f)
                break;
            x += w;
            i = j;
        }
        return {line, i};
    };
    const bool hovered = ImGui::IsWindowHovered();
    const ImGuiIO& io = ImGui::GetIO();
    if (hovered && io.MousePos.x >= origin.x + text_x - px(4))
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && n > 0) {
        const auto [line, at] = hit(io.MousePos);
        if (io.MouseClickedCount[ImGuiMouseButton_Left] == 2) {
            std::size_t a = at, b = at;
            const std::size_t la = layout.starts[static_cast<std::size_t>(line)], lb = line_end(line);
            auto word = [&](std::size_t k) {
                const auto c = static_cast<unsigned char>(text[k]);
                return std::isalnum(c) != 0 || c == '_' || c >= 0x80;
            };
            while (a > la && word(a - 1))
                --a;
            while (b < lb && word(b))
                ++b;
            p.sel_line_a = p.sel_line_b = line;
            p.sel_byte_a = a;
            p.sel_byte_b = b;
        } else {
            p.sel_line_a = p.sel_line_b = line;
            p.sel_byte_a = p.sel_byte_b = at;
            p.sel_dragging = true;
        }
    }
    if (p.sel_dragging) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const auto [line, at] = hit(io.MousePos);
            p.sel_line_b = line;
            p.sel_byte_b = at;
            const float wy0 = ImGui::GetWindowPos().y;
            if (io.MousePos.y < wy0)  // drag past an edge scrolls
                ImGui::SetScrollY(ImGui::GetScrollY() - lh);
            else if (io.MousePos.y > wy0 + ImGui::GetWindowHeight())
                ImGui::SetScrollY(ImGui::GetScrollY() + lh);
        } else {
            p.sel_dragging = false;
        }
    }
    auto selected = [&]() -> std::string {
        if (p.sel_line_a < 0 || p.sel_line_b < 0)
            return {};
        std::size_t a = p.sel_byte_a, b = p.sel_byte_b;
        if (a > b)
            std::swap(a, b);
        return text.substr(a, b - a);
    };
    const bool focused = ImGui::IsWindowFocused();
    if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && n > 0) {
        p.sel_line_a = 0;
        p.sel_byte_a = 0;
        p.sel_line_b = n - 1;
        p.sel_byte_b = line_end(n - 1);
    }
    if (focused && io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_C, false) || ImGui::IsKeyPressed(ImGuiKey_Insert, false))) {
        if (const std::string t = selected(); !t.empty())
            ImGui::SetClipboardText(t.c_str());
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##preview-menu");
    if (ImGui::BeginPopup("##preview-menu")) {
        const std::string t = selected();
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, !t.empty()))
            ImGui::SetClipboardText(t.c_str());
        if (ImGui::MenuItem("Select all", "Ctrl+A") && n > 0) {
            p.sel_line_a = 0;
            p.sel_byte_a = 0;
            p.sel_line_b = n - 1;
            p.sel_byte_b = line_end(n - 1);
        }
        ImGui::EndPopup();
    }
    std::size_t s0 = 0, s1 = 0;
    if (p.sel_line_a >= 0 && p.sel_line_b >= 0) {
        s0 = std::min(p.sel_byte_a, p.sel_byte_b);
        s1 = std::max(p.sel_byte_a, p.sel_byte_b);
    }
    ImGuiListClipper clip;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, top));
    clip.Begin(n, lh);
    std::vector<CodeSpan> spans;
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(content_w, lh));
            const std::size_t la = layout.starts[static_cast<std::size_t>(i)];
            const std::size_t lb = line_end(i);
            const float ty = a.y + (lh - fs) * 0.5f;
            const std::string num = std::to_string(i + 1);
            d->AddText(font, fs, ImVec2(a.x + px(10), ty), g_pal->faint, num.c_str());
            const float x0 = a.x + text_x;
            // Selection behind the text; a selected line break shows as a little extra width.
            if (s1 > s0 && s0 <= lb && s1 >= la) {
                const std::size_t ca = std::clamp(s0, la, lb);
                const std::size_t cb = std::clamp(s1, la, lb);
                const float xa = x0 + width_of(la, ca);
                float xb = x0 + width_of(la, cb);
                if (s1 > lb)
                    xb += fs * 0.5f;
                if (xb > xa)
                    d->AddRectFilled(ImVec2(xa, a.y), ImVec2(xb, a.y + lh), alpha(g_pal->focus, 70));
            }
            bool block = layout.in_block[static_cast<std::size_t>(i)] != 0;
            highlight_code_line(std::string_view(text).substr(la, lb - la), lang, block, spans);
            float x = x0;
            for (const auto& sp : spans) {
                const char* b0 = text.data() + la + sp.a;
                const char* b1 = text.data() + la + sp.b;
                d->AddText(font, fs, ImVec2(x, ty), code_color(sp.tok), b0, b1);
                x += font->CalcTextSizeA(fs, FLT_MAX, 0, b0, b1).x;
            }
        }
    }
    ImGui::PopFont();
    ImGui::EndChild();
}

void draw_files_panel(App::State& s, ImVec2 pos, ImVec2 size) {
    auto& p = *s.panels;
    std::string machine;
    const std::string root = panel_folder(s, &machine);
    p.track_files(root, machine);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    std::unique_lock lock(p.mutex);
    bool back = false;
    const bool previewing = !p.preview_path.empty();
    panel_header(s, dl, pos, size.x, "Files", previewing ? p.preview_path : folder_name(root), previewing, &back);
    const float top = pos.y + px(42) + 1;
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 1, top));
    if (previewing) {
        // Read-only editor in the terminal font at 12 (herdr files/main.js:141-144).
        const ImVec2 area(size.x - 2, pos.y + size.y - top - px(4));
        if (!p.preview_message.empty() && p.preview_text.empty()) {
            // files/main.js:177: a padded (12) div in the dim UI text, wrapping.
            ImGui::BeginChild("##preview", area, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
            ImGui::PushFont(s.fonts.ui, 12);
            ImGui::Dummy(ImVec2(0, px(8)));
            ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
            ImGui::SetCursorPosX(px(12));
            ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - px(12));
            ImGui::TextUnformatted(p.preview_message.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::EndChild();
        } else {
            draw_code_preview(s, p, area);
        }
        lock.unlock();
        if (back)
            p.close_preview();
        return;
    }
    ImGui::BeginChild("##tree", ImVec2(size.x - 2, pos.y + size.y - top - px(4)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    std::string toggle, open;
    const float rw = ImGui::GetContentRegionAvail().x - px(12);
    std::function<void(const std::string&, int)> rows = [&](const std::string& rel, int depth) {
        const auto it = p.listing.find(rel);
        if (it == p.listing.end() || depth > 40)
            return;
        for (const auto& e : it->second) {
            const std::string child = rel.empty() ? e.name : rel + "/" + e.name;
            const ImVec2 a0 = ImGui::GetCursorScreenPos();
            const ImVec2 a(a0.x + px(6), a0.y);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(child.c_str());
            if (ImGui::InvisibleButton("##f", ImVec2(rw, px(28)))) {
                if (e.dir)
                    toggle = child;
                else
                    open = child;
            }
            const bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            if (hov)
                d->AddRectFilled(a, ImVec2(a.x + rw, a.y + px(28)), g_pal->hover, px(7));
            const float cy = a.y + px(14);
            float x = a.x + px(8) + static_cast<float>(depth) * px(18);
            const bool open_dir = e.dir && p.expanded.contains(child);
            if (e.dir)
                draw_icon(s, d, open_dir ? ui::icon::kChevronDown : ui::icon::kChevronRight, fpx(12), ImVec2(x + px(6), cy),
                          g_pal->faint);
            x += px(12) + px(7);
            draw_icon(s, d, e.dir ? ui::icon::kFolder : ui::icon::kFile, fpx(14), ImVec2(x + px(7), cy), g_pal->dim);
            x += px(14) + px(7);
            char mark = 0;
            if (!e.dir) {
                std::string key = child;
                for (auto& c : key)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (const auto m = p.marks.find(key); m != p.marks.end())
                    mark = m->second;
            }
            const float right = a.x + rw - px(8) - (mark != 0 ? px(14) : 0.0f);
            text_clipped(d, s.fonts.ui, fpx(13), ImVec2(x, cy - fpx(13) * 0.6f), g_pal->text, e.name, right - x);
            if (mark != 0) {
                const char m[2] = {mark, 0};
                d->AddText(s.fonts.ui_medium, fpx(11), ImVec2(a.x + rw - px(8) - px(8), cy - fpx(11) * 0.6f),
                           mark_color(mark), m);
            }
            ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + px(28)));
            if (open_dir)
                rows(child, depth + 1);
        }
    };
    ImGui::Dummy(ImVec2(0, px(6)));
    rows("", 0);
    auto note = [&](const std::string& t) {
        ImGui::SetCursorPosX(px(14));
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->faint));
        ImGui::PushFont(s.fonts.ui, 12);
        ImGui::TextWrapped("%s", t.c_str());
        ImGui::PopFont();
        ImGui::PopStyleColor();
    };
    if (root.empty())
        note("No folder");
    else if (!p.files_error.empty())
        note(p.files_error);
    else if (!p.listing.count(""))
        note("Loading files\xE2\x80\xA6");
    else if (p.listing[""].empty())
        note("No files");
    if (p.files_limited)
        note("File list limited to 500 entries per folder and 5,000 total");
    ImGui::EndChild();
    lock.unlock();
    if (!toggle.empty())
        p.toggle_folder(toggle);
    if (!open.empty())
        p.open_file(open);
}

void draw_changes_panel(App::State& s, ImVec2 pos, ImVec2 size) {
    auto& p = *s.panels;
    std::string machine;
    const std::string cwd = panel_folder(s, &machine);
    p.track_changes(cwd, machine);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    std::unique_lock lock(p.mutex);
    // Header: "Changes", "<n> files", +add −del.
    const float hh = px(42);
    const float cy = pos.y + hh * 0.5f;
    float x = pos.x + px(14);
    const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(13), FLT_MAX, 0, "Changes");
    dl->AddText(s.fonts.ui_bold, fpx(13), ImVec2(x, cy - t.y * 0.5f), g_pal->text, "Changes");
    x += t.x + px(8);
    int adds = 0, dels = 0;
    for (const auto& c : p.changes) {
        adds += c.add;
        dels += c.del;
    }
    {
        // herdr changes/main.js render(): always "<n> files" and +add −del, even at 0.
        const std::string badge = std::to_string(p.changes.size()) + " files";
        const ImVec2 bt = s.fonts.ui->CalcTextSizeA(fpx(10), FLT_MAX, 0, badge.c_str());
        dl->AddRect(ImVec2(x, cy - bt.y * 0.5f - px(1)), ImVec2(x + bt.x + px(10), cy + bt.y * 0.5f + px(1)), g_pal->border,
                    px(4));
        dl->AddText(s.fonts.ui, fpx(10), ImVec2(x + px(5), cy - bt.y * 0.5f), g_pal->dim, badge.c_str());
        float rx = pos.x + size.x - px(14);
        const std::string del = "\xE2\x88\x92" + std::to_string(dels);
        const std::string add = "+" + std::to_string(adds);
        rx -= s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, del.c_str()).x;
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(rx, cy - fpx(11) * 0.6f), g_pal->needs_you, del.c_str());
        rx -= px(6) + s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, add.c_str()).x;
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(rx, cy - fpx(11) * 0.6f), g_pal->done, add.c_str());
    }
    dl->AddLine(ImVec2(pos.x, pos.y + hh), ImVec2(pos.x + size.x, pos.y + hh), g_pal->border);

    const float footer_h = px(32) + px(16) + (p.commit_note.empty() ? 0.0f : px(18));
    const float body_top = pos.y + hh + 1;
    const float limited_h = p.changes_more ? px(22) : 0.0f;
    const float body_h = pos.y + size.y - footer_h - body_top - limited_h;
    // herdr: the list and the diff are both flex_1 (equal halves); the list is at least 44 px and
    // at most its rows (28 each + 12), and the diff takes what the list leaves.
    const float list_h = std::clamp(body_h * 0.5f, px(44),
                                    std::max(px(44), static_cast<float>(p.changes.size()) * px(28) + px(12)));
    std::string select;
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 1, body_top));
    ImGui::BeginChild("##changes", ImVec2(size.x - 2, list_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const float rw = ImGui::GetContentRegionAvail().x - px(12);
        ImGui::Dummy(ImVec2(0, px(6)));
        for (const auto& c : p.changes) {
            const ImVec2 a0 = ImGui::GetCursorScreenPos();
            const ImVec2 a(a0.x + px(6), a0.y);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(c.path.c_str());
            if (ImGui::InvisibleButton("##c", ImVec2(rw, px(28))))
                select = c.path;
            const bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            if (c.path == p.open_diff)
                d->AddRectFilled(a, ImVec2(a.x + rw, a.y + px(28)), g_pal->selected, px(7));
            else if (hov)
                d->AddRectFilled(a, ImVec2(a.x + rw, a.y + px(28)), g_pal->hover, px(7));
            const float ry = a.y + px(14);
            const char letter[2] = {c.letter, 0};
            d->AddText(s.fonts.ui_medium, fpx(11), ImVec2(a.x + px(8) + px(3), ry - fpx(11) * 0.6f), mark_color(c.letter),
                       letter);
            float cx = a.x + px(8) + px(14) + px(8);
            std::string add = c.add > 0 ? "+" + std::to_string(c.add) : std::string();
            std::string del = c.del > 0 ? "\xE2\x88\x92" + std::to_string(c.del) : std::string();
            float rx = a.x + rw - px(8);
            if (!del.empty()) {
                rx -= s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, del.c_str()).x;
                d->AddText(s.fonts.ui, fpx(11), ImVec2(rx, ry - fpx(11) * 0.6f), g_pal->needs_you, del.c_str());
                rx -= px(6);
            }
            if (!add.empty()) {
                rx -= s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, add.c_str()).x;
                d->AddText(s.fonts.ui, fpx(11), ImVec2(rx, ry - fpx(11) * 0.6f), g_pal->done, add.c_str());
                rx -= px(6);
            }
            const auto slash = c.path.find_last_of('/');
            const std::string dir = slash == std::string::npos ? std::string() : c.path.substr(0, slash + 1);
            const std::string name = slash == std::string::npos ? c.path : c.path.substr(slash + 1);
            const float name_w = std::min(px(260), s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, name.c_str()).x);
            const float dir_room = std::max(0.0f, rx - cx - name_w - px(4));
            const float dw = dir.empty() ? 0.0f : text_clipped(d, s.fonts.ui, fpx(13), ImVec2(cx, ry - fpx(13) * 0.6f),
                                                               g_pal->faint, dir, dir_room);
            text_clipped(d, s.fonts.ui, fpx(13), ImVec2(cx + dw, ry - fpx(13) * 0.6f), g_pal->text, name, rx - cx - dw);
            ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + px(28)));
        }
        auto note = [&](const std::string& t2) {
            ImGui::SetCursorPosX(px(14));
            ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->faint));
            ImGui::PushFont(s.fonts.ui, 12);
            ImGui::TextWrapped("%s", t2.c_str());
            ImGui::PopFont();
            ImGui::PopStyleColor();
        };
        if (cwd.empty())
            note("No folder");
        else if (p.not_repo)
            note("Not a git repository");
        else if (!p.changes_error.empty())
            note(p.changes_error);
        else if (p.changes.empty())
            note(p.repo_top.empty() ? "Loading changes\xE2\x80\xA6" : "No uncommitted changes");
    }
    ImGui::EndChild();
    dl->AddLine(ImVec2(pos.x, body_top + list_h), ImVec2(pos.x + size.x, body_top + list_h), g_pal->border);
    if (p.changes_more) {  // its own row under the list (herdr: px 12, py 4, 11 px, dim)
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(pos.x + px(12), body_top + list_h + 1 + px(4)), g_pal->faint,
                    "Showing the first 1,000 changed files");
    }

    // Diff: unified, one number column, +/- prefix removed, 9 % green/red rows, terminal font 11.
    const float diff_top = body_top + list_h + 1 + limited_h;
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 1, diff_top));
    ImGui::BeginChild("##diff", ImVec2(size.x - 2, std::max(px(20), pos.y + size.y - footer_h - diff_top)),
                      ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);
    {
        ImDrawList* d = ImGui::GetWindowDrawList();
        if (!p.diff_message.empty() && p.diff.empty()) {
            ImGui::SetCursorPos(ImVec2(px(14), px(10)));
            ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->faint));
            ImGui::TextUnformatted(p.diff_message.c_str());
            ImGui::PopStyleColor();
        } else if (p.diff.empty() && !p.changes.empty()) {
            ImGui::SetCursorPos(ImVec2(px(14), px(10)));
            ImGui::TextDisabled("Select a file to view its changes");
        } else {
            ImGui::PushFont(s.fonts.mono, 11);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            const float rh = px(20);
            std::size_t longest = 0;
            for (const auto& r : p.diff)
                longest = std::max(longest, r.text.size());
            const float width = std::max(px(300), static_cast<float>(longest + 1) * px(7) + px(50));
            ImGuiListClipper clip;
            clip.Begin(static_cast<int>(p.diff.size()), rh);
            while (clip.Step()) {
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                    const auto& r = p.diff[static_cast<std::size_t>(i)];
                    const ImVec2 a = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(width, rh));
                    const ImVec2 b(a.x + std::max(width, ImGui::GetWindowWidth()), a.y + rh);
                    const float ty = a.y + (rh - fpx(11)) * 0.5f;
                    if (r.kind == SidePanels::DiffRow::kHunk) {
                        d->AddRectFilled(a, b, raised_color());
                        d->AddText(s.fonts.mono, fpx(11), ImVec2(a.x + px(12), ty), g_pal->faint, r.text.c_str());
                        continue;
                    }
                    if (r.kind == SidePanels::DiffRow::kAdd)
                        d->AddRectFilled(a, b, alpha(g_pal->done, 0x18));
                    else if (r.kind == SidePanels::DiffRow::kDel)
                        d->AddRectFilled(a, b, alpha(g_pal->needs_you, 0x18));
                    if (r.kind != SidePanels::DiffRow::kOther) {
                        const std::string n = std::to_string(r.number);
                        const float nw = s.fonts.mono->CalcTextSizeA(fpx(11), FLT_MAX, 0, n.c_str()).x;
                        d->AddText(s.fonts.mono, fpx(11), ImVec2(a.x + px(8) + px(30) - nw, ty), g_pal->faint, n.c_str());
                    }
                    d->AddText(s.fonts.mono, fpx(11), ImVec2(a.x + px(8) + px(30) + px(10), ty), g_pal->text, r.text.c_str());
                }
            }
            ImGui::PopStyleVar();
            ImGui::PopFont();
        }
    }
    ImGui::EndChild();

    // Footer: commit message + Commit (git add -A, git commit -m).
    const float fy = pos.y + size.y - footer_h + px(8);
    dl->AddLine(ImVec2(pos.x, fy - px(8)), ImVec2(pos.x + size.x, fy - px(8)), g_pal->border);
    const float bw = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, p.committing ? "Committing\xE2\x80\xA6" : "Commit").x + px(28);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + px(10), fy));
    ImGui::SetNextItemWidth(size.x - px(20) - bw - px(8));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), (px(32) - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(8));
    // Esc leaves the box and keeps the message (herdr changes/main.js on_key_down); ImGui would
    // revert the edit, so the text from before the key is put back.
    static std::string typed;
    const bool esc = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    bool commit = ImGui::InputTextWithHint("##commitmsg", "Commit message", p.commit_msg, sizeof p.commit_msg,
                                           ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemActive())
        typed = p.commit_msg;
    else if (ImGui::IsItemDeactivated() && esc)
        std::snprintf(p.commit_msg, sizeof p.commit_msg, "%s", typed.c_str());
    std::string msg = p.commit_msg;
    const bool can = !msg.empty() && msg.find_first_not_of(" \n") != std::string::npos && !p.repo_top.empty() &&
                     !p.committing && !p.changes_loading;
    ImGui::SameLine(0, px(8));
    ImGui::BeginDisabled(!can);
    ImGui::PushStyleColor(ImGuiCol_Button, v4(can ? g_pal->text : raised_color()));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(can ? g_pal->card : g_pal->faint));
    commit |= ImGui::Button(p.committing ? "Committing\xE2\x80\xA6##commit" : "Commit##commit", ImVec2(bw, px(32)));
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();
    ImGui::PopStyleVar(2);
    if (!p.commit_note.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(pos.x + px(12), fy + px(36)));
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->needs_you));
        ImGui::TextUnformatted(p.commit_note.c_str());
        ImGui::PopStyleColor();
    }
    lock.unlock();
    if (!select.empty())
        p.select_change(select);
    if (commit && can)
        p.commit();
}

// The right panel: one card, Files or Changes (only one shows at a time).
void draw_side_panel(App::State& s, ImVec2 pos, ImVec2 size) {
    auto* sp = s.ws.active();
    if (sp == nullptr || sp->panel.empty())
        return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->card, px(10));
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->border, px(10));
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushID(sp->panel.c_str());
    ImGui::BeginChild("##sidepanel", size, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    if (sp->panel == "files")
        draw_files_panel(s, pos, size);
    else
        draw_changes_panel(s, pos, size);
    ImGui::EndChild();
    ImGui::PopID();
}

void draw_details(App::State& s, ImVec2 pos, ImVec2 size, bool connected) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->card, px(12));
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->border, px(12));
    ImGui::SetCursorScreenPos(ImVec2(pos.x + px(16), pos.y + px(14)));
    ImGui::BeginChild("##details", ImVec2(size.x - px(32), size.y - px(28)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    const std::string id = s.ws.focused_session();
    const auto it = s.views_by_id.find(id);
    if (it == s.views_by_id.end()) {
        ImGui::TextDisabled("No session focused");
        ImGui::EndChild();
        return;
    }
    const auto& v = it->second;
    const auto pi = program_of(v);
    ImGui::PushFont(s.fonts.ui_bold, 16.0f);
    ImGui::TextUnformatted(s.pane_title(id).c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s", pi.name.c_str());
    ImGui::Dummy(ImVec2(0, px(4)));
    auto kv = [&](const char* k, const std::string& value, ImU32 color) {
        ImGui::TextDisabled("%s", k);
        ImGui::SameLine(px(92));
        ImGui::PushStyleColor(ImGuiCol_Text, v4(color));
        ImGui::TextWrapped("%s", value.c_str());
        ImGui::PopStyleColor();
    };
    const Attention att = s.attention_of(id);
    kv("Agent", v.agent_state + (att == Attention::kDone ? "  (finished, not seen)" : ""),
       attention_color(att));
    if (v.agent_state == "Unknown" && v.last_agent_state != "Unknown")
        kv("Last known", v.last_agent_state, g_pal->dim);
    kv("Source", v.agent_source.empty() ? std::string("none") : v.agent_source, g_pal->dim);
    if (!v.agent_message.empty())
        kv("Message", v.agent_message, g_pal->text);
    ImGui::Separator();
    std::string proc = v.lost ? std::string("Lost") : v.process_state;
    if (v.pid != 0)
        proc += "  \xC2\xB7  pid " + std::to_string(v.pid);
    kv("Process", proc, g_pal->text);
    if (!v.program.empty())
        kv("Running", v.program, g_pal->dim);
    if (v.exit_code) {
        char buf[40];
        std::snprintf(buf, sizeof buf, "%u (0x%08X)", *v.exit_code, *v.exit_code);
        kv("Exit code", buf, g_pal->text);
    }
    if (v.remaining_processes > 0)
        kv("Cleaning", std::to_string(v.remaining_processes) + " child processes", g_pal->working);
    if (s.machine_of.count(id) && !s.machine_of[id].empty())
        kv("Machine", s.machine_of[id], g_pal->info);
    kv("Folder", display_path(v.cwd), g_pal->dim);
    if (const auto b = ui::git_branch(v.cwd); !b.empty())
        kv("Branch", b, g_pal->dim);
    if (const auto age = elapsed_since(v.created_at); !age.empty())
        kv("Started", age + " ago", g_pal->dim);
    kv("Input", v.is_controller ? std::string("this window") : std::string("read-only"), g_pal->dim);
    if (!v.parent.empty())
        kv("Started by", s.views_by_id.count(v.parent) ? s.pane_title(v.parent) : v.parent.substr(0, 8),
           g_pal->dim);
    std::string labels;
    for (const auto& [k, val] : v.labels)
        labels += (labels.empty() ? "" : "  ") + k + "=" + val;
    if (!labels.empty())
        kv("Labels", labels, g_pal->dim);
    std::vector<std::string> subs;
    for (const auto& [cid, cv] : s.views_by_id)
        if (cv.parent == id)
            subs.push_back(cid);
    if (!subs.empty()) {
        ImGui::TextDisabled("Sub-agents");
        for (const auto& c : subs) {
            ImGui::PushID(c.c_str());
            if (ImGui::SmallButton(s.pane_title(c).c_str()))
                run_command(s, s.ws.find(c).space < 0 ? "agent.peek:" + c : "agent.focus:" + c);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", s.views_by_id[c].agent_state.c_str());
            ImGui::PopID();
        }
    }
    draw_mod_panels(s, "details");
    ImGui::Separator();
    const bool running = v.process_state == "Running";
    ImGui::BeginDisabled(!connected);
    if (running && ImGui::Button("Ctrl+C"))
        run_command(s, "session.interrupt");
    if (running) {
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
            run_command(s, "session.stop");
    }
    if (v.process_state == "Stopping") {
        ImGui::TextDisabled(v.still_running_after_interrupt ? "Still running after interrupt"
                                                            : "Stopping...");
        ImGui::PushStyleColor(ImGuiCol_Button, v4(alpha(g_pal->needs_you, 200)));
        if (ImGui::Button("Force stop"))
            run_command(s, "session.force_stop");
        ImGui::PopStyleColor();
    }
    if (!running && v.process_state != "Stopping" && ImGui::Button("Restart here"))
        run_command(s, "session.restart");
    if (running && !v.is_controller && ImGui::Button("Take control"))
        if (auto r = s.by_id.find(id); r != s.by_id.end())
            r->second->take_control();
    ImGui::EndDisabled();
    ImGui::EndChild();
}

// ------------------------------------------------------------------ overlays (herdr mods)
ImU32 popover_color() {
    return g_pal->light ? mix(g_pal->card, IM_COL32(0, 0, 0, 255), 0.03f)
                        : mix(g_pal->card, IM_COL32(255, 255, 255, 255), 0.06f);
}

// herdr overlay frame: popover background, 1 px border, radius 8.
void push_overlay_style() {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, v4(popover_color()));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, v4(popover_color()));
    ImGui::PushStyleColor(ImGuiCol_Border, v4(g_pal->border));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(18), px(18)));
}
void pop_overlay_style() {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
}

// How a dialog's title row shows Esc.
enum class EscChip {
    kButton,  // help/main.js 61, picker 250, machine-add 305: px 6, py 2, rounded 5, 11 px dim,
              // no border; hover: background + foreground; a click closes
    kPlain,   // prompt/main.js 42, confirm/main.js 42: plain 11 px dim text
};

// Title row (15 px semibold) with "Esc" at the right; returns true when Esc is clicked.
bool overlay_title(App::State& s, const char* title, EscChip esc = EscChip::kButton, float size = 15.0f) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(size), FLT_MAX, 0, title);
    dl->AddText(s.fonts.ui_bold, fpx(size), p, g_pal->text, title);
    const ImVec2 e = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, "Esc");
    bool clicked = false;
    if (esc == EscChip::kPlain) {
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(p.x + w - e.x, p.y + (t.y - e.y) * 0.5f), g_pal->faint, "Esc");
    } else {
        const ImVec2 cs(e.x + px(12), e.y + px(4));
        const ImVec2 ea(p.x + w - cs.x, p.y + (t.y - cs.y) * 0.5f);
        ImGui::SetCursorScreenPos(ea);
        clicked = ImGui::InvisibleButton("##esc", cs);
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(ea, ImVec2(ea.x + cs.x, ea.y + cs.y), g_pal->hover, px(5));
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(ea.x + px(6), ea.y + px(2)), hov ? g_pal->text : g_pal->faint, "Esc");
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + t.y + px(10)));
    return clicked;
}

// worktree/main.js 301, 349: the title at the dialog's 13 px, semibold, no Esc; `right` (Open's
// "N checkouts") at the line's end, 11 px dim. Then the dialog's gap 10.
void worktree_title(App::State& s, const std::string& title, const std::string& right = {}) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = std::round(fpx(13) * 1.3f);
    float rw = 0;
    if (!right.empty()) {
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, right.c_str());
        rw = t.x;
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(p.x + w - t.x, p.y + (h - t.y) * 0.5f), g_pal->faint, right.c_str());
    }
    const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(13), FLT_MAX, 0, title.c_str());
    text_clipped(dl, s.fonts.ui_bold, fpx(13), ImVec2(p.x, p.y + (h - t.y) * 0.5f), g_pal->text, title,
                 w - rw - (rw > 0 ? px(8) : 0.0f));
    ImGui::Dummy(ImVec2(w, h));
    ImGui::Dummy(ImVec2(0, std::max(0.0f, px(10) - 2 * ImGui::GetStyle().ItemSpacing.y)));
}

// worktree/main.js 305, 353-361: the input, h 32, px 10, rounded 6, a 1 px border (accent while
// focused, else the input colour), the background.
bool worktree_input(App::State& s, const char* id, const char* hint, char* buf, std::size_t size,
                    ImGuiInputTextFlags flags = 0) {
    ImGui::SetNextItemWidth(-1);
    ImGui::PushFont(s.fonts.ui, 13);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), (px(32) - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(6));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(g_pal->bg));
    const bool r = ImGui::InputTextWithHint(id, hint, buf, size, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                        ImGui::IsItemActive() ? g_pal->focus : g_pal->border, px(6));
    return r;
}

// Key chips: each part of "Ctrl+Shift+P" in its own bordered chip.
float key_chips(App::State& s, ImDrawList* dl, float right, float cy, const std::string& chord) {
    if (chord.empty())
        return 0;
    std::vector<std::string> parts;
    std::string cur;
    for (const char c : chord) {
        if (c == '+' && !cur.empty()) {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty())
        parts.push_back(cur);
    float x = right;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        std::string k = *it;
        for (auto& c : k)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(10.5f), FLT_MAX, 0, k.c_str());
        const float w = t.x + px(10);
        x -= w;
        dl->AddRectFilled(ImVec2(x, cy - px(9)), ImVec2(x + w, cy + px(9)), raised_color(), px(4));
        dl->AddRect(ImVec2(x, cy - px(9)), ImVec2(x + w, cy + px(9)), g_pal->border, px(4));
        dl->AddText(s.fonts.ui, fpx(10.5f), ImVec2(x + px(5), cy - t.y * 0.5f), g_pal->dim, k.c_str());
        x -= px(4);
    }
    return right - x;
}

// herdr palette fuzzy score (palette/main.js 112-129): letters in order, +1 per hit, +6 at a word
// start, +3 per consecutive run, -0.3 per skipped char (max 6), -0.02 per title char.
std::optional<float> palette_score(const std::string& title, const std::string& query, std::vector<int>* hits) {
    if (query.empty())
        return 0.0f;
    float score = 0;
    std::size_t qi = 0;
    int run = 0;
    int last = -1;
    for (std::size_t i = 0; i < title.size() && qi < query.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(title[i])) != std::tolower(static_cast<unsigned char>(query[qi])))
            continue;
        score += 1;
        const char prev = i == 0 ? ' ' : title[i - 1];
        if (i == 0 || std::strchr(" :.-_/", prev) != nullptr)
            score += 6;
        if (last >= 0 && static_cast<int>(i) == last + 1) {
            ++run;
            score += 3.0f * static_cast<float>(run);
        } else {
            if (last >= 0)
                score -= 0.3f * static_cast<float>(std::min<std::size_t>(6, i - static_cast<std::size_t>(last) - 1));
            run = 0;
        }
        last = static_cast<int>(i);
        if (hits != nullptr)
            hits->push_back(static_cast<int>(i));
        ++qi;
    }
    if (qi < query.size())
        return std::nullopt;
    return score - 0.02f * static_cast<float>(title.size());
}

const char* command_group(const std::string& id) {
    auto starts = [&](const char* p) { return id.rfind(p, 0) == 0; };
    if (starts("tab.") || id == "app.picker")
        return "Tab";
    if (starts("pane."))
        return "Pane";
    if (starts("space.") || starts("worktree."))
        return "Space";
    if (starts("layout") || starts("sidebar.") || starts("details.") || starts("panel"))
        return "Layout";
    if (starts("terminal.") || starts("session."))
        return "Terminal";
    if (starts("agent."))
        return "Agent";
    if (starts("window."))
        return "Window";
    return "App";
}

// Command palette (herdr defaults/frame/palette/main.js): 18 px padding, 10 px gap; a 36 px
// search box with an accent border and 9 px corners; rows 32 px (group titles 26 px), 10 shown.
// Empty: Recent (5), then every group without them; typing: fuzzy on the title, else on
// "<group> <title> <command> <keywords>" (4 lower).
void draw_palette(App::State& s) {
    if (!s.show_palette)
        return;
    struct Entry {
        std::string title, cmd, group, chord, words;
        float score = 0;
        std::vector<int> hits;
    };
    std::vector<Entry> all;
    auto add = [&](std::string title, std::string cmd, std::string group, std::string words) {
        all.push_back({std::move(title), cmd, std::move(group), s.keymap.chord_for(cmd), std::move(words)});
    };
    // palette/main.js 22-63: the named entries (command, title, group, keywords), in order.
    struct Named {
        const char* cmd;
        const char* title;
        const char* group;
        const char* words;
    };
    static constexpr Named kNamed[] = {
        {"tab.new", "New tab", "Tab", "tab agent new task"},
        {"app.picker", "Go to space or tab\xE2\x80\xA6", "Tab", "switch jump find"},
        {"tab.rename", "Rename tab", "Tab", "tab"},
        {"tab.close", "Close tab", "Tab", "tab kill"},
        {"tab.next", "Next tab", "Tab", "tab"},
        {"tab.prev", "Previous tab", "Tab", "tab"},
        {"pane.split:right", "Split right", "Pane", "vertical"},
        {"pane.split:down", "Split down", "Pane", "horizontal"},
        {"pane.close", "Close pane", "Pane", "kill"},
        {"pane.zoom", "Zoom pane", "Pane", "maximize fullscreen"},
        {"pane.go:left", "Focus pane left", "Pane", "move go"},
        {"pane.go:right", "Focus pane right", "Pane", "move go"},
        {"pane.go:up", "Focus pane up", "Pane", "move go"},
        {"pane.go:down", "Focus pane down", "Pane", "move go"},
        {"pane.swap:left", "Swap pane left", "Pane", "move"},
        {"pane.swap:right", "Swap pane right", "Pane", "move"},
        {"pane.swap:up", "Swap pane up", "Pane", "move"},
        {"pane.swap:down", "Swap pane down", "Pane", "move"},
        {"space.new", "New space", "Space", "workspace"},
        {"space.rename", "Rename space", "Space", "workspace"},
        {"space.close", "Close space", "Space", "workspace kill"},
        {"space.next", "Next space", "Space", "workspace"},
        {"space.prev", "Previous space", "Space", "workspace"},
        {"layout.switch", "Next layout", "Layout", "look switch"},
        {"layout.reset", "Reset layout", "Layout", "new version default restore original"},
        {"layout.clone", "Duplicate layout", "Layout", "clone copy customize"},
        {"mod.clone", "Duplicate mod", "Layout", "clone copy customize"},
        {"terminal.open", "New terminal", "Terminal", "shell"},
        {"terminal.copy", "Copy", "Terminal", "clipboard"},
        {"terminal.paste", "Paste", "Terminal", "clipboard"},
        {"agent.next", "Next agent", "Agent", ""},
        {"agent.prev", "Previous agent", "Agent", ""},
        {"window.minimize", "Minimize window", "Window", ""},
        {"window.maximize", "Maximize window", "Window", "restore"},
        {"window.close", "Close window", "Window", "quit"},
        {"app.settings", "Settings", "App", "preferences options config look font theme sound"},
        {"app.help", "Keys", "App", "keybindings shortcuts help"},
        {"app.welcome", "Welcome", "App", "intro start onboarding"},
        {"app.whats_new", "What's new", "App", "release changes news update"},
        {"app.reset", "Reset to the default screen", "App", "escape hatch broken restore"},
        {"app.zoom_in", "Zoom in", "App", "bigger font size"},
        {"app.zoom_out", "Zoom out", "App", "smaller font size"},
        {"app.zoom_reset", "Reset zoom", "App", "font size"},
        {"app.quit", "Quit AgentDesk", "App", "exit"},
        {"app.palette", "", "", ""},  // not listed (the palette itself)
    };
    std::set<std::string> named;
    for (const auto& n : kNamed) {
        named.insert(n.cmd);
        if (*n.title != '\0')
            add(n.title, n.cmd, n.group, n.words);
        if (std::string_view(n.cmd) == "app.quit")  // palette/main.js 62
            for (const Palette* p : all_palettes())
                if (!p->layout_only)
                    add(std::string("Theme: ") + p->label, std::string("theme:") + p->id, "App", "colors look");
    }
    // palette/main.js 86-89: "Layout: <Name>", capitalised; main is "Herdr" (ours: the
    // "workspace" layout, "AgentDesk"), terminals "Terminals", win98 "Herdr 98".
    for (const auto& l : s.layouts) {
        std::string t = l.name == "workspace"   ? std::string("AgentDesk")
                         : l.name == "terminals" ? std::string("Terminals")
                         : l.name == "win98"     ? std::string("AgentDesk 98")
                                                 : l.name;
        if (!t.empty())
            t[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])));
        add("Layout: " + t, "layout:" + l.name, "Layout", "look " + l.name);
    }
    // Then every other command, in its group (herdr adds the app's other commands the same way).
    for (const auto& c : kCommands)
        if (!named.contains(c.id))
            add(c.label, c.id, command_group(c.id), "");
    for (const auto& c : s.mods->commands())
        add(c.mod + ": " + c.label, "mod:" + c.mod + ":" + c.id, "Mods", "");
    std::string q;
    for (const char* c = s.palette_buf; *c != 0; ++c)
        if (*c != ' ')
            q += *c;
    std::vector<Entry> shown;
    for (auto& e : all) {
        if (auto sc = palette_score(e.title, q, &e.hits)) {
            e.score = *sc;
            shown.push_back(e);
        } else if (auto sc2 = palette_score(e.group + " " + e.title + " " + e.cmd + " " + e.words, q, nullptr)) {
            e.score = *sc2 - 4;  // palette/main.js 179-180
            e.hits.clear();
            shown.push_back(e);
        }
    }
    auto recency = [&](const std::string& cmd) {
        const auto it = std::find(s.settings.palette_recent.begin(), s.settings.palette_recent.end(), cmd);
        return it == s.settings.palette_recent.end() ? 1000 : static_cast<int>(it - s.settings.palette_recent.begin());
    };
    struct Row {
        const Entry* e = nullptr;
        std::string header;
    };
    std::vector<Row> rows;
    std::vector<const Entry*> order;
    if (q.empty()) {
        // Recent (5 of the last 20), then the groups in herdr's order without them (:170-173).
        std::vector<const Entry*> recent;
        for (const auto& cmd : s.settings.palette_recent) {
            if (recent.size() >= 5)
                break;
            for (const auto& e : shown)
                if (e.cmd == cmd) {
                    recent.push_back(&e);
                    break;
                }
        }
        auto is_recent = [&](const Entry* e) { return std::find(recent.begin(), recent.end(), e) != recent.end(); };
        for (const auto* e : recent) {
            if (order.empty())
                rows.push_back({nullptr, "Recent"});
            rows.push_back({e, {}});
            order.push_back(e);
        }
        for (const char* g : {"Tab", "Pane", "Space", "Layout", "Terminal", "Agent", "Window", "App", "Mods"}) {
            bool head = false;
            for (const auto& e : shown) {
                if (e.group != g || is_recent(&e))
                    continue;
                if (!head) {
                    rows.push_back({nullptr, g});
                    head = true;
                }
                rows.push_back({&e, {}});
                order.push_back(&e);
            }
        }
    } else {
        std::vector<const Entry*> sorted;
        for (const auto& e : shown)
            sorted.push_back(&e);
        std::stable_sort(sorted.begin(), sorted.end(), [&](const Entry* a, const Entry* b) {
            if (a->score != b->score)
                return a->score > b->score;
            return recency(a->cmd) < recency(b->cmd);
        });
        for (const auto* e : sorted) {
            rows.push_back({e, {}});
            order.push_back(e);
        }
    }
    const int n = static_cast<int>(order.size());
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = std::min(px(620), vp->WorkSize.x - px(16));
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + px(60)));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    push_overlay_style();  // padding 18 (palette/main.js 271)
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(10), px(10)));  // gap 10 (:272)
    ImGui::Begin("##palette", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    // palette/main.js 286-300: h 36, px 10, gap 10, radius 9, accent border, background fill,
    // 16 px search icon, input 12.8, "Esc" 11 dim at the right.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 sp = ImGui::GetCursorScreenPos();
    const float fw = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(sp, ImVec2(sp.x + fw, sp.y + px(36)), g_pal->bg, px(9));
    dl->AddRect(sp, ImVec2(sp.x + fw, sp.y + px(36)), g_pal->focus, px(9));
    draw_icon(s, dl, ui::icon::kSearch, fpx(16), ImVec2(sp.x + px(10) + px(8), sp.y + px(18)), g_pal->dim);
    const float esc_w = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, "Esc").x;
    dl->AddText(s.fonts.ui, fpx(11), ImVec2(sp.x + fw - px(10) - esc_w, sp.y + px(18) - fpx(11) * 0.6f), g_pal->dim, "Esc");
    const float input_x = px(10) + px(16) + px(10);
    ImGui::SetCursorScreenPos(ImVec2(sp.x + input_x, sp.y));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, (px(36) - fpx(12.8f)) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(alpha(g_pal->text, 0)));
    ImGui::PushFont(s.fonts.ui, 12.8f);
    ImGui::SetNextItemWidth(fw - input_x - px(10) - esc_w - px(10));
    if (ImGui::InputTextWithHint("##q", "Type a command\xE2\x80\xA6", s.palette_buf, sizeof s.palette_buf))
        s.palette_sel = 0;
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(ImVec2(sp.x, sp.y + px(36) + px(10)));
    // Up/Down stop at the ends; PageUp/PageDown move 10.
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        ++s.palette_sel;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        --s.palette_sel;
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
        s.palette_sel += 10;
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
        s.palette_sel -= 10;
    s.palette_sel = std::clamp(s.palette_sel, 0, std::max(0, n - 1));
    std::string run;
    float list_h = 0;
    for (const auto& row : rows)
        list_h += row.e == nullptr ? px(26) : px(32);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    if (n > 0) {
        // At most 10 rows (LIST_H 320), and never past the window's bottom (the popup is at
        // most 85 % of the window in herdr).
        const float room = vp->WorkPos.y + vp->WorkSize.y * 0.85f - ImGui::GetCursorScreenPos().y - px(24);
        ImGui::BeginChild("##list", ImVec2(fw, std::max(px(64), std::min({px(320), list_h, room}))), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground);
        ImDrawList* d = ImGui::GetWindowDrawList();
        const float lw = ImGui::GetContentRegionAvail().x;
        const ImU32 muted = mix(g_pal->dim, g_pal->text, 0.4f);
        int idx = -1;
        for (const auto& row : rows) {
            if (row.e == nullptr) {  // group title: px 14, pb 4, bottom-aligned, 11 semibold dim
                const ImVec2 a = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(lw, px(26)));
                d->AddText(s.fonts.ui_bold, fpx(11), ImVec2(a.x + px(14), a.y + px(26) - px(4) - fpx(11)), g_pal->dim,
                           row.header.c_str());
                continue;
            }
            ++idx;
            const auto* e = row.e;
            const ImVec2 a0 = ImGui::GetCursorScreenPos();
            const ImVec2 a(a0.x + px(6), a0.y);  // mx 6
            const float rw = lw - px(12);
            ImGui::PushID(idx);
            ImGui::SetCursorScreenPos(a);
            if (ImGui::InvisibleButton("##e", ImVec2(rw, px(32))))
                run = e->cmd;
            const bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            const bool sel = idx == s.palette_sel;
            if (hov || sel)
                d->AddRectFilled(a, ImVec2(a.x + rw, a.y + px(32)), hov ? g_pal->hover : g_pal->selected, px(7));
            if (sel && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
                        ImGui::IsKeyPressed(ImGuiKey_PageDown) || ImGui::IsKeyPressed(ImGuiKey_PageUp)))
                ImGui::SetScrollHereY();
            const float cy = a.y + px(16);
            // Title: matched letters accent semibold; the rest foreground when selected, else
            // muted (palette/main.js 187-203).
            float x = a.x + px(10);
            for (std::size_t i = 0; i < e->title.size();) {
                std::size_t len = 1;
                const unsigned char c = static_cast<unsigned char>(e->title[i]);
                if (c >= 0xF0)
                    len = 4;
                else if (c >= 0xE0)
                    len = 3;
                else if (c >= 0xC0)
                    len = 2;
                const bool hit = std::find(e->hits.begin(), e->hits.end(), static_cast<int>(i)) != e->hits.end();
                ImFont* f = hit ? s.fonts.ui_bold : s.fonts.ui;
                const char* b = e->title.c_str() + i;
                d->AddText(f, fpx(12.8f), ImVec2(x, cy - fpx(12.8f) * 0.6f), hit ? g_pal->focus : sel ? g_pal->text : muted,
                           b, b + len);
                x += f->CalcTextSizeA(fpx(12.8f), FLT_MAX, 0, b, b + len).x;
                i += len;
            }
            float right = a.x + rw - px(10);
            right -= key_chips(s, d, right, cy, e->chord);
            if (!q.empty()) {
                const float gw = s.fonts.ui->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, e->group.c_str()).x;
                d->AddText(s.fonts.ui, fpx(11.5f), ImVec2(right - gw - (e->chord.empty() ? 0.0f : px(10)), cy - fpx(11.5f) * 0.6f),
                           g_pal->dim, e->group.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::Dummy(ImVec2(0, px(6)));  // mb 6
    } else {  // palette/main.js 318: px 16, py 14, 13 dim
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const std::string msg = std::string("Nothing matches \"") + s.palette_buf + "\". Places are in the picker.";
        ImGui::Dummy(ImVec2(fw, px(14) * 2 + fpx(13)));
        ImGui::GetWindowDrawList()->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + px(16), a.y + px(14)), g_pal->dim, msg.c_str());
    }
    ImGui::PopStyleVar();
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && n > 0)
        run = order[static_cast<std::size_t>(s.palette_sel)]->cmd;
    const bool close = ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                       (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)));
    ImGui::End();
    pop_overlay_style();
    if (!run.empty() || close) {
        s.show_palette = false;
        s.focus_view(s.ws.focused_session());
    }
    if (!run.empty()) {
        auto& r = s.settings.palette_recent;
        r.erase(std::remove(r.begin(), r.end(), run), r.end());
        r.insert(r.begin(), run);
        if (r.size() > 20)
            r.resize(20);
        save_settings(s.settings);
        run_command(s, run);
    }
}

// "Go to…" picker (mods/picker): spaces with their tabs, busy ones first, quiet ones folded under
// "earlier"; substring filter on "<space> <tab> <programs>"; Up/Down wrap; Ctrl+D closes the tab.
void draw_picker(App::State& s) {
    if (!s.show_picker)
        return;
    const auto cwds = s.session_cwds();
    struct Row {
        enum Kind { kSpace, kTab, kEarlier, kNew } kind;
        int space = -1;
        int tab = -1;
        std::string label, detail, cmd;
        Attention att = Attention::kNone;
    };
    const std::string q = [&] {
        std::string x = s.picker_buf;
        for (auto& c : x)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return x;
    }();
    struct SpaceRows {
        int index;
        std::vector<Row> tabs;
    };
    std::vector<SpaceRows> groups;
    std::vector<ui::PickerSpace> facts;
    for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i) {
        const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
        const std::string sname = s.ws.space_title(i, cwds);
        SpaceRows sr{i, {}};
        // picker/main.js 86: a space is busy when it has any status (idle too), from all its tabs.
        std::vector<Attention> space_atts;
        for (const auto& tab : sp.tabs)
            for (const auto& id : s.ws.sessions_in(tab))
                space_atts.push_back(s.attention_of(id));
        const Attention space_att = ui::most_urgent(space_atts);
        for (int t = 0; t < static_cast<int>(sp.tabs.size()); ++t) {
            const auto& tab = sp.tabs[static_cast<std::size_t>(t)];
            const auto ids = s.ws.sessions_in(tab);
            std::vector<Attention> atts;
            // picker/main.js 79-80, 206, 224: the tab's agents only, each by its program's name
            // ("agent" without one), joined by " · "; then "  +N", the live terminals they
            // started (and those started, teamOf 71-76). The filter reads agents' program names.
            std::string programs;
            std::string names;
            std::vector<std::string> mine;
            for (const auto& id : ids) {
                atts.push_back(s.attention_of(id));
                if (const auto it = s.views_by_id.find(id); it != s.views_by_id.end() && is_agent(it->second)) {
                    std::string pn = program_of(it->second).name;
                    if (pn.empty())
                        pn = "agent";
                    programs += (programs.empty() ? "" : " \xC2\xB7 ") + pn;
                    names += " " + program_of(it->second).name;
                    mine.push_back(id);
                }
            }
            int team = 0;
            {
                std::set<std::string> seen(mine.begin(), mine.end());
                std::vector<std::string> todo = mine;
                while (!todo.empty()) {
                    const std::string p = todo.back();
                    todo.pop_back();
                    for (const auto& [cid, cv] : s.views_by_id)
                        if (cv.parent == p && cv.process_state == "Running" && seen.insert(cid).second) {
                            ++team;
                            todo.push_back(cid);
                        }
                }
            }
            if (team > 0)
                programs += "  +" + std::to_string(team);
            // An unnamed tab is listed by its number (herdr model.rs 282-288 tab_name).
            const std::string tname = ui::Workspace::tab_title(tab, t);
            std::string hay = sname + " " + tname + names;
            for (auto& c : hay)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!q.empty() && hay.find(q) == std::string::npos)
                continue;
            sr.tabs.push_back(Row{Row::kTab, i, t, tname, programs, "", ui::most_urgent(atts)});
        }
        facts.push_back({space_att != Attention::kNone, space_att == Attention::kBlocked,
                         static_cast<int>(sr.tabs.size())});
        groups.push_back(std::move(sr));
    }
    // picker/main.js 86-98: busy spaces (blocked first), then "earlier: N quiet spaces", always
    // shown; its spaces listed when expanded or while filtering.
    const auto plan = ui::plan_picker(facts, !q.empty(), s.picker_earlier);
    std::vector<Row> rows;
    auto put = [&](int g) {
        const auto& sr = groups[static_cast<std::size_t>(g)];
        rows.push_back({Row::kSpace, sr.index, -1, s.ws.space_title(sr.index, cwds), "", "", Attention::kNone});
        for (const auto& t : sr.tabs)
            rows.push_back(t);
    };
    for (const int g : plan.busy)
        put(g);
    if (plan.earlier_row) {
        char b[64];
        std::snprintf(b, sizeof b, "earlier: %zu quiet space%s %s", plan.quiet.size(), plan.quiet.size() > 1 ? "s" : "",
                      s.picker_earlier ? "\xE2\x96\xB4" : "\xE2\x96\xBE");
        rows.push_back({Row::kEarlier, -1, -1, b, "", "", Attention::kNone});
    }
    if (plan.quiet_listed)
        for (const int g : plan.quiet)
            put(g);
    rows.push_back({Row::kNew, -1, -1, "+ new tab", "", "tab.new", Attention::kNone});
    rows.push_back({Row::kNew, -1, -1, "+ new space", "", "space.new", Attention::kNone});
    for (const auto& r : s.remotes)
        if (r.host->connected())
            rows.push_back({Row::kNew, -1, -1, "+ new space on " + r.cfg.name, "", "space.new_on:" + r.cfg.name,
                            Attention::kNone});
    std::vector<int> selectable;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (rows[static_cast<std::size_t>(i)].kind != Row::kSpace)
            selectable.push_back(i);
    // Opens on the current tab; typing selects the first matching tab.
    if (s.picker_sel < 0 || s.picker_reset) {
        s.picker_sel = 0;
        for (int k = 0; k < static_cast<int>(selectable.size()); ++k) {
            const auto& r = rows[static_cast<std::size_t>(selectable[static_cast<std::size_t>(k)])];
            if (r.kind == Row::kTab &&
                (!q.empty() || (r.space == s.ws.active_space && s.ws.active() && r.tab == s.ws.active()->active_tab))) {
                s.picker_sel = k;
                break;
            }
        }
        s.picker_reset = false;
    }
    const int n = static_cast<int>(selectable.size());
    if (n > 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            s.picker_sel = (s.picker_sel + 1) % n;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            s.picker_sel = (s.picker_sel + n - 1) % n;
        s.picker_sel = std::clamp(s.picker_sel, 0, n - 1);
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = std::min(px(640), vp->WorkSize.x - px(16));
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + px(40)));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    push_overlay_style();
    ImGui::Begin("##picker", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_AlwaysAutoResize);
    bool close = overlay_title(s, "Go to\xE2\x80\xA6");
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    const ImVec2 sp = ImGui::GetCursorScreenPos();
    const float fw = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(34), (px(36) - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(7));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, v4(g_pal->focus));
    ImGui::SetNextItemWidth(fw);
    if (ImGui::InputTextWithHint("##pq", "Go to a tab, space or agent\xE2\x80\xA6", s.picker_buf, sizeof s.picker_buf))
        s.picker_reset = true;
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    draw_icon(s, dl, ui::icon::kSearch, fpx(14), ImVec2(sp.x + px(17), sp.y + px(18)), g_pal->faint);
    ImGui::Dummy(ImVec2(0, px(6)));
    std::string run;
    bool toggle_earlier = false;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    float list_h = 0;
    for (const auto& r : rows)
        list_h += r.kind == Row::kSpace ? px(31) : r.kind == Row::kEarlier ? px(35) : r.kind == Row::kNew ? px(36) : px(32);
    ImGui::BeginChild("##plist", ImVec2(fw, std::min(px(320), list_h + px(2))), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    int k = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& r = rows[static_cast<std::size_t>(i)];
        const ImVec2 a = ImGui::GetCursorScreenPos();
        if (r.kind == Row::kSpace) {
            // picker/main.js 157-168: px 10, pt 10, pb 3, gap 8; an 18 px avatar (rounded 5,
            // 10.5 px bold white letter), the name 11 px semibold dim.
            ImGui::Dummy(ImVec2(fw, px(31)));
            const auto& spc = s.ws.spaces[static_cast<std::size_t>(r.space)];
            const float side = px(18), cy = a.y + px(10) + side * 0.5f;
            d->AddRectFilled(ImVec2(a.x + px(10), cy - side * 0.5f), ImVec2(a.x + px(10) + side, cy + side * 0.5f),
                             r.label == "~" ? g_pal->faint : avatar_color(spc.color), px(5));
            const std::string letter = r.label.empty() ? std::string("?") : std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(r.label[0]))));
            const ImVec2 lt = s.fonts.ui_bold->CalcTextSizeA(fpx(10.5f), FLT_MAX, 0, letter.c_str());
            d->AddText(s.fonts.ui_bold, fpx(10.5f), ImVec2(a.x + px(10) + (side - lt.x) * 0.5f, cy - lt.y * 0.5f),
                       IM_COL32(255, 255, 255, 255), letter.c_str());
            const ImVec2 nt = s.fonts.ui_bold->CalcTextSizeA(fpx(11), FLT_MAX, 0, r.label.c_str());
            text_clipped(d, s.fonts.ui_bold, fpx(11), ImVec2(a.x + px(10) + side + px(8), cy - nt.y * 0.5f), g_pal->faint,
                         r.label, fw - px(20) - side - px(8));
            continue;
        }
        ++k;
        const float rh = r.kind == Row::kEarlier ? px(35) : r.kind == Row::kNew ? px(36) : px(32);
        ImGui::PushID(i);
        const bool clicked = ImGui::InvisibleButton("##r", ImVec2(fw - px(4), rh));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hov && (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0))
            s.picker_sel = k;
        const bool sel = k == s.picker_sel;
        if (sel) {
            d->AddRectFilled(a, ImVec2(a.x + fw - px(4), a.y + rh), g_pal->selected, px(7));
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                ImGui::SetScrollHereY();
        }
        const float cy = a.y + rh * 0.5f;
        if (r.kind == Row::kTab) {
            // Status glyph: spinner working, ○ idle, ● status colour, · none.
            const float gx = a.x + px(33);
            if (r.att == Attention::kWorking)
                status_mark(s, d, gx - px(2), cy, r.att, true);
            else if (r.att == Attention::kIdle)
                d->AddCircle(ImVec2(gx + px(4), cy), px(3.5f), g_pal->idle, 0, px(1.3f));
            else if (r.att != Attention::kNone)
                d->AddCircleFilled(ImVec2(gx + px(4), cy), px(3.5f), attention_color(r.att));
            else
                d->AddCircleFilled(ImVec2(gx + px(4), cy), px(1.2f), g_pal->faint);
            const bool here = s.ws.active_space == r.space && s.ws.active() != nullptr && s.ws.active()->active_tab == r.tab;
            // picker/main.js 216-224: the name takes the room (flex_1, truncated); then, gap 10,
            // "needs you" / "finished" and the agents, both 11 px, at the row's end (px 10).
            const float x = gx + px(16);
            float right = a.x + fw - px(4) - px(10);
            const ImVec2 wt = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, r.detail.c_str());
            if (!r.detail.empty()) {
                right -= wt.x;
                d->AddText(s.fonts.ui, fpx(11), ImVec2(right, cy - wt.y * 0.5f), g_pal->faint, r.detail.c_str());
                right -= px(10);
            }
            if (r.att == Attention::kBlocked || r.att == Attention::kDone) {
                const char* word = r.att == Attention::kBlocked ? "needs you" : "finished";
                const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, word);
                right -= t.x;
                d->AddText(s.fonts.ui, fpx(11), ImVec2(right, cy - t.y * 0.5f), attention_color(r.att), word);
                right -= px(10);
            }
            ImFont* nf = here ? s.fonts.ui_bold : s.fonts.ui;
            const float nh = nf->CalcTextSizeA(fpx(13), FLT_MAX, 0, "Ag").y;
            text_clipped(d, nf, fpx(13), ImVec2(x, cy - nh * 0.5f), g_pal->text, r.label, std::max(0.0f, right - x));
        } else {
            d->AddText(s.fonts.ui, fpx(r.kind == Row::kEarlier ? 12.0f : 13.0f), ImVec2(a.x + px(12), cy - fpx(13) * 0.6f),
                       r.kind == Row::kEarlier ? g_pal->faint : g_pal->dim, r.label.c_str());
        }
        const bool go = clicked || (sel && ImGui::IsKeyPressed(ImGuiKey_Enter, false));
        if (go) {
            if (r.kind == Row::kEarlier)
                toggle_earlier = true;
            else if (r.kind == Row::kNew)
                run = r.cmd;
            else
                run = "tab.goto:" + std::to_string(r.space) + ":" + std::to_string(r.tab);
        }
        if (sel && r.kind == Row::kTab && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
            run = "tab.close_at:" + std::to_string(r.space) + ":" + std::to_string(r.tab);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    close = close || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
             (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)));
    ImGui::End();
    pop_overlay_style();
    if (toggle_earlier)
        s.picker_earlier = !s.picker_earlier;  // stays open, selection stays on the toggle
    if (!run.empty() || close) {
        s.show_picker = false;
        s.focus_view(s.ws.focused_session());
    }
    if (!run.empty())
        run_command(s, run);
}

// Toasts (mods/toast): 340 wide, bottom-right 16 px in, 8 px apart; a dot coloured by level;
// title 13 semibold and body 12 muted, one line each; click runs its action and dismisses.
// ---- the update pill (herdr window.rs 4820-4890) and agent-host's update error (4800-4815)
// What's waiting, in words (window.rs 4824-4833).
std::optional<std::string> update_label() {
    const auto core = ui::updates::core();
    const auto c = core.ready && !core.updating && !core.managed ? core.ready : std::nullopt;
    const auto a = ui::updates::ready();
    if (a && c)
        return "AgentDesk " + *a + " and agent-host " + *c + " are ready";
    if (a)
        return "AgentDesk " + *a + " is ready";
    if (c)
        return "agent-host " + *c + " is ready";
    return std::nullopt;
}

// Height taken at the bottom right (34 + 8), 0 when hidden: toasts go above it (4727-4732).
float draw_update_pill(App::State& s) {
    // 4835-4841. herdr also shows "Updated to <version>" when no release notes opened by
    // themselves; AgentDesk's What's new always opens after an update (draw_notes), so it never
    // shows here.
    const auto app_version = ui::updates::ready();
    const bool app_ready = app_version.has_value();
    const auto label = update_label();
    if (!label)
        return 0.0f;
    const std::string dismissal = app_version ? "app:" + *app_version : *label;
    if (s.update_dismissed == dismissal)
        return 0.0f;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = px(34);
    const char* notes = app_ready ? "What changed" : "Open Updates";
    const float label_w = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, label->c_str()).x;
    const float restart_w = app_ready ? s.fonts.ui_bold->CalcTextSizeA(fpx(13), FLT_MAX, 0, "Restart").x + px(20) : 0.0f;
    const float notes_w = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, notes).x + px(12);
    const float gap = px(10);
    const float w = px(14) + label_w + gap + (app_ready ? restart_w + gap : 0.0f) + notes_w + gap + px(24) + px(6);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - px(16) - w, vp->WorkPos.y + vp->WorkSize.y - px(16) - h));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, h * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyle().Colors[ImGuiCol_PopupBg]);
    ImGui::Begin("##update-pill", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float cy = wp.y + h * 0.5f;
    float x = wp.x + px(14);
    const float th = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, "Ag").y;
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(x, cy - th * 0.5f), g_pal->text, label->c_str());
    x += label_w + gap;
    if (app_ready) {
        // 4859-4875: "Restart": 24 high, px 10, round, accent, semibold.
        ImGui::SetCursorScreenPos(ImVec2(x, cy - px(12)));
        if (ImGui::InvisibleButton("##update-restart", ImVec2(restart_w, px(24))))
            run_command(s, "app.update");
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(ImVec2(x, cy - px(12)), ImVec2(x + restart_w, cy + px(12)),
                          hov ? alpha(g_pal->focus, 230) : g_pal->focus, px(12));
        dl->AddText(s.fonts.ui_bold, fpx(13), ImVec2(x + px(10), cy - th * 0.5f), accent_foreground(), "Restart");
        x += restart_w + gap;
    }
    // 4876-4881: "What changed" / "Open Updates" in the accent colour: the Updates page.
    ImGui::SetCursorScreenPos(ImVec2(x, cy - px(12)));
    if (ImGui::InvisibleButton("##update-notes", ImVec2(notes_w, px(24))))
        run_command(s, "app.settings:updates");
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(x + px(6), cy - th * 0.5f), g_pal->focus, notes);
    x += notes_w + gap;
    // 4882-4897: x dismisses this one.
    ImGui::SetCursorScreenPos(ImVec2(x, cy - px(12)));
    if (ImGui::InvisibleButton("##update-dismiss", ImVec2(px(24), px(24))))
        s.update_dismissed = dismissal;
    const bool hov = ImGui::IsItemHovered();
    if (hov)
        dl->AddCircleFilled(ImVec2(x + px(12), cy), px(12), raised_color());
    const ImVec2 xt = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, "\xC3\x97");
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(x + px(12) - xt.x * 0.5f, cy - xt.y * 0.5f), hov ? g_pal->text : g_pal->faint,
                "\xC3\x97");
    ImGui::End();
    return h + px(8);
}

// 4800-4815: agent-host's update failed (until dismissed): open Updates, or x. `bottom`: how
// far up from the bottom the toasts reach.
void draw_core_update_error(App::State& s, float bottom) {
    const auto core = ui::updates::core();
    if (!core.error || core.managed || s.core_update_error_dismissed == core.error)
        return;
    const std::string text = "agent-host update failed: " + *core.error + " \xC2\xB7 Open Updates";
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float max_w = px(540);
    const float tw = std::min(s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, text.c_str()).x, max_w - px(24) - px(32));
    const float w = px(12) + tw + px(8) + px(24) + px(12);
    const float h = px(12) * 2 + px(24);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - px(16) - w, vp->WorkPos.y + vp->WorkSize.y - bottom - h));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(8));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyle().Colors[ImGuiCol_PopupBg]);
    ImGui::Begin("##core-update-error", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float cy = wp.y + h * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(wp.x + px(12), wp.y + px(12)));
    if (ImGui::InvisibleButton("##core-update-error-open", ImVec2(tw, px(24))))
        run_command(s, "app.settings:updates");
    text_clipped(dl, s.fonts.ui, fpx(13), ImVec2(wp.x + px(12), cy - fpx(13) * 0.6f), g_pal->text, text, tw);
    const float xx = wp.x + px(12) + tw + px(8);
    ImGui::SetCursorScreenPos(ImVec2(xx, cy - px(12)));
    if (ImGui::InvisibleButton("##core-update-error-dismiss", ImVec2(px(24), px(24))))
        s.core_update_error_dismissed = core.error;  // window.rs 3973-3976
    const bool hov = ImGui::IsItemHovered();
    if (hov)
        dl->AddCircleFilled(ImVec2(xx + px(12), cy), px(12), raised_color());
    const ImVec2 xt = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, "\xC3\x97");
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(xx + px(12) - xt.x * 0.5f, cy - xt.y * 0.5f), hov ? g_pal->text : g_pal->faint,
                "\xC3\x97");
    ImGui::End();
}

void draw_toasts(App::State& s) {
    const auto now = Clock::now();
    std::erase_if(s.toasts, [&](const Toast& t) { return t.until < now; });
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // window.rs 4727-4732: the update pill sits at the bottom; toasts go above it.
    const float pill = draw_update_pill(s);
    float y = vp->WorkPos.y + vp->WorkSize.y - px(16) - pill;
    const float w = px(340);
    int n = 0;
    // window.rs 4794-4796: toast_y grows with each toast in the order opened, so the oldest sits
    // at the bottom and newer ones stack above it.
    for (auto it = s.toasts.begin(); it != s.toasts.end(); ++it, ++n) {
        const auto& t = *it;
        const float h = px(72);
        y -= h;
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - w - px(16), y));
        ImGui::SetNextWindowSize(ImVec2(w, h));
        char name[32];
        std::snprintf(name, sizeof name, "##toast%d", n);
        push_overlay_style();
        ImGui::Begin(name, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        // toast/main.js 17-37: px 18, gap 12, items centred; the hover background; an 8 px dot,
        // then the title (13 semibold) over the body (12 muted), both truncated, no tooltip.
        if (ImGui::IsWindowHovered())
            dl->AddRectFilled(ImVec2(wp.x + 1, wp.y + 1), ImVec2(wp.x + w - 1, wp.y + h - 1), g_pal->hover, px(7));
        const float ty = wp.y + (t.body.empty() ? h * 0.5f - fpx(13) * 0.6f : h * 0.5f - px(17));
        dl->AddCircleFilled(ImVec2(wp.x + px(18) + px(4), wp.y + h * 0.5f), px(4), t.color);
        const float tx = wp.x + px(18) + px(8) + px(12);
        text_clipped(dl, s.fonts.ui_bold, fpx(13), ImVec2(tx, ty), g_pal->text, t.title, wp.x + w - px(18) - tx);
        if (!t.body.empty()) {
            std::string body = t.body;
            std::replace(body.begin(), body.end(), '\n', ' ');
            text_clipped(dl, s.fonts.ui, fpx(12), ImVec2(tx, ty + px(20)), g_pal->dim, body, wp.x + w - px(18) - tx);
        }
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (!t.session.empty())
                s.focus(t.session);
            it->until = now;  // dismiss
        }
        ImGui::End();
        pop_overlay_style();
        y -= px(8);
    }
    // window.rs 4800-4801: after the toasts, at the next toast_y.
    draw_core_update_error(s, vp->WorkPos.y + vp->WorkSize.y - y);
}

// herdr window.rs 4764-4777: a dialog dims the window behind it and a press outside it closes it
// (ImGui modals dim; the press is ours to catch). Call inside the dialog's popup.
bool dialog_outside_click() {
    // Behind a modal nothing is hoverable, so "no window hovered" is outside the dialog and the
    // popups it opened (a combo list).
    // A press on one of the dialog's own widgets makes it the active item in that same frame;
    // without AllowWhenBlockedByActiveItem the dialog would then not count as hovered and close.
    return !ImGui::IsWindowAppearing() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
           !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
}

// Peek (window.rs 3949-3960, mods/peek): a dialog 80 % x 80 %, a 34 px bar over the live
// terminal. Esc stays with the terminal (window.rs 4757: an overlay with a layout takes no
// Overlay keys), so it closes by ✕, a press outside, or ↗.
void draw_peek(App::State& s) {
    const bool want = !s.peek.empty() && s.views_by_id.count(s.peek) != 0 && s.by_id.count(s.peek) != 0;
    if (!want)
        s.peek.clear();
    if (want && !ImGui::IsPopupOpen("##peek"))
        ImGui::OpenPopup("##peek");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = vp->WorkSize.x * 0.8f;
    const float h = vp->WorkSize.y * 0.8f;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + (vp->WorkSize.y - h) * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    push_overlay_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::BeginPopupModal("##peek", nullptr,
                                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    pop_overlay_style();
    if (!open)
        return;
    if (!want) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const auto& v = s.views_by_id.find(s.peek)->second;
    const auto session = s.by_id.find(s.peek)->second;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float bw = ImGui::GetContentRegionAvail().x;
    // peek/main.js 52-74: surface, bottom border; px 12, gap 8, 12 px text.
    dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + px(34)), g_pal->card, px(8), ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(p.x, p.y + px(34) - 1), ImVec2(p.x + bw, p.y + px(34) - 1), g_pal->border);
    const float cy = p.y + px(17);
    const Attention att = s.attention_of(s.peek);
    float x = p.x + px(12);
    // Glyph: spinner working, ○ idle, ● a status, · none (peek/main.js 51).
    if (att == Attention::kWorking)
        status_mark(s, dl, x, cy, att, true);
    else if (att == Attention::kIdle)
        dl->AddCircle(ImVec2(x + px(6), cy), px(3.5f), g_pal->idle, 0, px(1.3f));
    else if (att != Attention::kNone)
        dl->AddCircleFilled(ImVec2(x + px(6), cy), px(3.5f), attention_color(att));
    else
        dl->AddCircleFilled(ImVec2(x + px(6), cy), px(1.2f), g_pal->dim);
    x += px(12) + px(8);
    program_icon(dl, program_of(v), ImVec2(x, cy - px(7)), px(14), g_pal->text);  // peek/main.js 49
    x += px(14) + px(8);
    x += text_clipped(dl, s.fonts.ui_bold, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f), g_pal->text, s.pane_title(s.peek),
                      px(260)) + px(8);
    // The status word only when there is a status (peek/main.js 63).
    const char* word = att == Attention::kWorking   ? "working"
                       : att == Attention::kBlocked ? "needs you"
                       : att == Attention::kDone    ? "finished"
                       : att == Attention::kIdle    ? "idle"
                                                    : nullptr;
    if (word != nullptr) {
        dl->AddText(s.fonts.ui, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f), attention_color(att), word);
        x += s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, word).x + px(8);
    }
    if (const auto pit = s.views_by_id.find(v.parent); pit != s.views_by_id.end())
        text_clipped(dl, s.fonts.ui, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f), g_pal->dim, "from " + s.pane_title(v.parent),
                     px(200));
    // Buttons: px 8, py 2, radius 5, muted; hover background + foreground.
    const ImU32 muted = mix(g_pal->dim, g_pal->text, 0.4f);
    float bx = p.x + bw - px(12);
    auto button = [&](const char* id, const char* label) {
        const float tw = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, label).x + px(16);
        bx -= tw;
        const ImVec2 a(bx, cy - px(10));
        ImGui::SetCursorScreenPos(a);
        const bool hit = ImGui::InvisibleButton(id, ImVec2(tw, px(20)));
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(a, ImVec2(a.x + tw, a.y + px(20)), g_pal->hover, px(5));
        dl->AddText(s.fonts.ui, fpx(12), ImVec2(a.x + px(8), cy - fpx(12) * 0.6f), hov ? g_pal->text : muted, label);
        bx -= px(8);
        return hit;
    };
    const bool close = button("##peekx", "\xE2\x9C\x95") || dialog_outside_click();
    const bool has_pane = s.ws.find(s.peek).space >= 0;
    std::string cmd;
    if (button("##go", has_pane ? "\xE2\x86\x97 go to" : "\xE2\x86\x97 keep"))
        cmd = has_pane ? "agent.focus:" + s.peek : "agent.keep:" + s.peek;
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + px(34)));
    ImGui::BeginChild("##peekbody", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    auto& vs = s.views[s.peek];
    vs.view.keymap = &s.keymap;
    vs.view.theme.default_bg = g_pal->term_bg;
    vs.view.theme.default_fg = g_pal->term_fg;
    vs.view.theme.cursor = g_pal->cursor;
    vs.view.theme.selection = g_pal->selection;
    vs.view.theme.ansi = g_pal->ansi;
    vs.view.draw("peek", *session, s.fonts, vs.focus_next || ImGui::IsWindowAppearing());
    vs.focus_next = false;
    std::vector<std::string> commands = vs.view.take_commands();
    ImGui::EndChild();
    if (close || !cmd.empty()) {
        ImGui::CloseCurrentPopup();
        s.peek.clear();
    }
    ImGui::EndPopup();
    for (const auto& c : commands)
        run_command(s, c);
    if (!cmd.empty())
        run_command(s, cmd);
}

// ------------------------------------------------------------------ overlays
void center_next(float w, float y_frac = 0.18f) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * y_frac),
        ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
}

void heading(App::State& s, const char* text) {
    ImGui::PushFont(s.fonts.ui_bold, 15);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void primary_button_style(bool push) {
    // herdr "solid" buttons: foreground colour fill, background-coloured text.
    if (push) {
        ImGui::PushStyleColor(ImGuiCol_Button, v4(g_pal->text));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(alpha(g_pal->text, 220)));
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->card));
    } else {
        ImGui::PopStyleColor(3);
    }
}

// Keybinds (defaults/mods/help/main.js, window.rs 3614 dialog 640 x 520): every keys.json entry
// as written (the terminal section too, "(terminal)" after its keys), grouped spaces / tabs /
// panes / agents / terminal / window, filtered by typing.
void draw_help(App::State& s) {
    if (s.show_help && !ImGui::IsPopupOpen("##help"))
        ImGui::OpenPopup("##help");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = std::min(px(640), vp->WorkSize.x - px(16));
    const float h = std::min(px(520), vp->WorkSize.y - px(16));
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + (vp->WorkSize.y - h) * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    push_overlay_style();
    const bool open = ImGui::BeginPopupModal("##help", nullptr,
                                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                                 ImGuiWindowFlags_NoMove);
    pop_overlay_style();
    if (!open)
        return;
    if (!s.show_help) {  // app.help pressed again
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    bool close = overlay_title(s, "Keybinds");
    if (ImGui::IsWindowAppearing()) {
        s.help_buf[0] = '\0';
        ImGui::SetKeyboardFocusHere();
    }
    // help/main.js 63: h 36, px 10, radius 9, accent border, background fill, 12.8 text.
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), (px(36) - fpx(12.8f)) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(9));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, v4(g_pal->focus));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(g_pal->bg));
    ImGui::PushFont(s.fonts.ui, 12.8f);
    ImGui::InputTextWithHint("##hf", "Type to filter\xE2\x80\xA6", s.help_buf, sizeof s.help_buf);
    ImGui::PopFont();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    // keys.rs listing(): read from keys.json as it is now (the defaults when it does not read).
    static std::vector<ui::Keymap::Binding> listed;
    static std::filesystem::file_time_type listed_at{};
    if (ImGui::IsWindowAppearing() || listed_at != s.keys_mtime) {
        listed_at = s.keys_mtime;
        listed = ui::Keymap::listing(Json::parse(read_text_file(data_dir() / L"keys.json"), nullptr, false));
    }
    // const all = keys().filter(k => !f || k.keys.includes(f) || label(k.action).includes(f))
    std::string f = s.help_buf;
    for (auto& c : f)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::BeginChild("##keys", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, px(2)));  // rows 2 apart (help/main.js 74)
    ImDrawList* d = ImGui::GetWindowDrawList();
    bool first = true;
    for (const char* g : {"spaces", "tabs", "panes", "agents", "terminal", "window"}) {
        bool head = false;
        for (const auto& b : listed) {
            if (std::string_view(ui::Keymap::help_group(b.action)) != g)
                continue;
            const std::string label = ui::Keymap::help_label(b.action);
            if (!f.empty() && b.keys.find(f) == std::string::npos && label.find(f) == std::string::npos)
                continue;
            if (!head) {  // group title: 11.5 medium dim; groups 10 apart, rows 2 apart
                if (!first)
                    ImGui::Dummy(ImVec2(0, px(10)));
                first = false;
                const ImVec2 a = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(0, fpx(11.5f) + px(4)));
                d->AddText(s.fonts.ui_medium, fpx(11.5f), a, g_pal->dim, g);
                head = true;
            }
            // h 32, px 8: the keys (accent, 190 wide), then what they do.
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, px(32)));
            const std::string keys = b.keys + (b.section != "global" ? "  (" + b.section + ")" : "");
            text_clipped(d, s.fonts.ui, fpx(12), ImVec2(a.x + px(8), a.y + px(16) - fpx(12) * 0.6f), g_pal->focus, keys,
                         px(186));
            d->AddText(s.fonts.ui, fpx(12), ImVec2(a.x + px(8) + px(190), a.y + px(16) - fpx(12) * 0.6f), g_pal->text,
                       label.c_str());
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    close = close || ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click();
    if (close) {
        s.show_help = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_launcher(App::State& s) {
    if (s.show_launcher) {
        ImGui::OpenPopup("##launcher");
        s.show_launcher = false;
    }
    center_next(px(520), 0.14f);
    if (!ImGui::BeginPopupModal("##launcher", nullptr,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
        return;
    const char* what = s.launch_target == LaunchTarget::kSpace        ? "New space"
                       : s.launch_target == LaunchTarget::kSplitRight ? "Split right"
                       : s.launch_target == LaunchTarget::kSplitDown  ? "Split down"
                                                                      : "New tab";
    heading(s, what);
    ImGui::TextDisabled("Runs in agent-host; it keeps running when this window closes.");
    ImGui::Dummy(ImVec2(0, px(2)));
    ImGui::TextDisabled("Folder");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##cwd", s.launch_cwd, sizeof s.launch_cwd);
    ImGui::TextDisabled("Name (optional)");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##name", s.launch_name, sizeof s.launch_name);
    ImGui::Dummy(ImVec2(0, px(2)));
    if (!s.remotes.empty()) {
        ImGui::TextDisabled("Machine");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##machine", s.machine_label(s.launch_machine).c_str())) {
            if (ImGui::Selectable("This PC", s.launch_machine.empty()))
                s.launch_machine.clear();
            for (const auto& r : s.remotes) {
                const std::string label = r.cfg.name + (r.host->connected() ? "" : "  (not connected)");
                if (ImGui::Selectable(label.c_str(), s.launch_machine == r.cfg.name))
                    s.launch_machine = r.cfg.name;
            }
            ImGui::EndCombo();
        }
    }
    ui::HostClient* lhost = s.host_for(s.launch_machine);
    const auto profiles = lhost->profiles();
    s.launcher_sel = std::clamp(s.launcher_sel, 0, std::max(0, static_cast<int>(profiles.size()) - 1));
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        ++s.launcher_sel;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        --s.launcher_sel;
    s.launcher_sel = std::clamp(s.launcher_sel, 0, std::max(0, static_cast<int>(profiles.size()) - 1));
    int start = -1;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fs = ImGui::GetFontSize();
    for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
        ImGui::PushID(i);
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::Selectable("##p", i == s.launcher_sel, 0, ImVec2(0, px(34))))
            start = i;
        if (ImGui::IsItemHovered())
            s.launcher_sel = i;
        const auto& pid = profiles[static_cast<std::size_t>(i)].first;
        program_icon(dl, program_of_profile(s.host->profile_executable(pid), pid), ImVec2(a.x + px(11), a.y + px(10)),
                     px(14), g_pal->text);  // new-menu/main.js 65
        dl->AddText(s.fonts.ui_bold, fs, ImVec2(a.x + px(38), a.y + (px(34) - fs) * 0.5f), g_pal->text,
                    profiles[static_cast<std::size_t>(i)].second.c_str());
        if (i < 9) {
            char k[8];
            std::snprintf(k, sizeof k, "%d", i + 1);
            dl->AddText(s.fonts.ui, fs, ImVec2(a.x + w - px(18), a.y + (px(34) - fs) * 0.5f), g_pal->faint,
                        k);
            if (!ImGui::IsAnyItemActive() &&
                ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false))
                start = i;
        }
        ImGui::PopID();
    }
    if (profiles.empty())
        ImGui::TextDisabled("No profiles (agent-host not connected?)");
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !profiles.empty())
        start = s.launcher_sel;
    if (!s.launch_error.empty())
        ImGui::TextColored(v4(g_pal->needs_you), "%s", s.launch_error.c_str());
    if (s.launch_busy)
        ImGui::TextDisabled("Starting...");
    ImGui::Separator();
    if (ImGui::Button("New git worktree...")) {
        ImGui::CloseCurrentPopup();
        run_command(s, "worktree.new");
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click())
        ImGui::CloseCurrentPopup();
    if (start >= 0 && lhost->connected() && !s.launch_busy) {
        Placement p;
        p.machine = s.launch_machine;
        p.target = s.launch_target;
        p.split_of = s.launch_split_of;
        p.space = s.ws.active_space;
        s.launch_error.clear();
        s.settings.last_cwd = s.launch_cwd;
        save_settings(s.settings);
        create_session(s, profiles[static_cast<std::size_t>(start)].first, s.launch_cwd, s.launch_name, p);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Prompt (mods/prompt): 380 wide, title, "Esc", field "Name" prefilled and fully selected, a hint,
// Cancel / Save. An empty name goes back to the automatic name.
void draw_rename(App::State& s) {
    if (s.show_rename) {
        ImGui::OpenPopup("##rename");
        s.show_rename = false;
    }
    center_next(px(380), 0.22f);
    push_overlay_style();
    const bool open = ImGui::BeginPopupModal("##rename", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    pop_overlay_style();
    s.rename_open = open;
    if (!open) {
        s.rename_close = false;
        return;
    }
    const bool mod_clone = s.rename_kind == "mod.clone";
    // A second rename press closes the prompt (window.rs 3047-3051). Keys do not reach the
    // command while the field has them, so the prompt watches its own command's keys.
    if (!mod_clone && !ImGui::IsWindowAppearing())
        for (const auto& [chord, cmd] : s.keymap.global_map())
            if (cmd.rfind(s.rename_kind + ".rename", 0) == 0 && ImGui::IsKeyChordPressed(chord))
                s.rename_close = true;
    const char* title = mod_clone                  ? "Duplicate mod"
                        : s.rename_kind == "space" ? "Rename space"
                        : s.rename_kind == "tab"   ? "Rename tab"
                                                   : "Rename pane";
    bool cancel = overlay_title(s, title, EscChip::kPlain);  // prompt/main.js 42
    ImGui::PushFont(s.fonts.ui, 12);
    ImGui::TextDisabled(mod_clone ? "Which mod" : "Name");
    ImGui::PopFont();
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), (px(36) - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, v4(g_pal->focus));
    const bool enter = ImGui::InputText("##n", s.rename_buf, sizeof s.rename_buf,
                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PushFont(s.fonts.ui, 11.5f);
    ImGui::TextDisabled("%s", mod_clone                  ? "its folder name in the mods folder"
                              : s.rename_kind == "space" ? "empty: named after its folder"
                              : s.rename_kind == "tab" ? "empty: its number"
                                                       : "empty: unnamed");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(6)));
    const float bw = px(80);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw * 2 - px(26));
    ImGui::PushStyleColor(ImGuiCol_Button, v4(raised_color()));
    cancel = ImGui::Button("Cancel", ImVec2(bw, px(30))) || cancel || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
             dialog_outside_click() || std::exchange(s.rename_close, false);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    primary_button_style(true);
    const bool pressed = ImGui::Button(mod_clone ? "Duplicate" : "Save", ImVec2(bw, px(30))) || enter;
    const bool ok = !cancel && pressed;
    primary_button_style(false);
    if (ok) {
        // window.rs 3138: the name is trimmed.
        std::string name = s.rename_buf;
        const auto b = name.find_first_not_of(" \t\r\n");
        const auto e = name.find_last_not_of(" \t\r\n");
        name = b == std::string::npos ? std::string() : name.substr(b, e - b + 1);
        std::snprintf(s.rename_buf, sizeof s.rename_buf, "%s", name.c_str());
    }
    if (cancel)
        ImGui::CloseCurrentPopup();
    if (ok && mod_clone) {
        ImGui::CloseCurrentPopup();
        if (s.rename_buf[0] != '\0')
            run_command(s, std::string("mod.clone:") + s.rename_buf);
    } else if (ok) {
        for (auto& sp : s.ws.spaces) {
            if (s.rename_kind == "space" && sp.id == s.rename_target)
                sp.name = s.rename_buf;
            for (auto& t : sp.tabs)
                if (s.rename_kind == "tab" && t.id == s.rename_target)
                    t.name = s.rename_buf;
        }
        if (s.rename_kind == "pane") {
            const auto loc = s.ws.find(s.rename_target);
            if (loc.space >= 0) {
                auto& t = s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
                if (s.rename_buf[0] == '\0')
                    t.pane_names.erase(s.rename_target);
                else
                    t.pane_names[s.rename_target] = s.rename_buf;
            }
        }
        ImGui::CloseCurrentPopup();
        s.focus_view(s.ws.focused_session());
    }
    ImGui::EndPopup();
}

// ------------------------------------------------------------------ worktrees (herdr mods/worktree)
// herdr slug: runs of characters other than letters, digits and "._-" become "-", trimmed.
std::string branch_slug(const std::string& b) {
    return host::worktree_slug(b);  // herdr worktree/main.js slug(), shared with the host's default
}

void draw_worktree(App::State& s) {
    if (s.show_worktree) {
        ImGui::OpenPopup("##worktree");
        s.show_worktree = false;
    }
    center_next(px(460), 0.2f);
    push_overlay_style();
    const bool open = ImGui::BeginPopupModal("##worktree", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    pop_overlay_style();
    if (!open)
        return;
    const std::string wt_machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
    const GitInfo g = s.git.get(s.wt_repo, wt_machine);
    const std::string repo = folder_name(g.common_top.empty() ? g.top : g.common_top);
    // Another machine's home is its own: its agent-host picks %USERPROFILE%\.agentdesk\worktrees
    // there unless a folder is set.
    std::string root = s.settings.worktree_dir;
    if (root.empty() && wt_machine.empty()) {
        wchar_t home[MAX_PATH]{};
        ::GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        root = win::to_utf8(home) + "\\.agentdesk\\worktrees";
    }
    const std::string shown_root = root.empty() ? std::string("~\\.agentdesk\\worktrees") : root;
    bool cancel = false;
    worktree_title(s, g.repo ? "New worktree of " + repo : std::string("New worktree"));  // worktree/main.js 349
    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
    if (!g.repo)
        ImGui::TextWrapped("%s is not in a git repository.", display_path(s.wt_repo).c_str());
    else
        ImGui::TextWrapped("A new space on a checkout in %s\\%s\\. A branch that exists is checked out; a new one "
                           "starts from here.",
                           display_path(shown_root).c_str(), repo.c_str());
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, px(6)));
    ui::HostClient* wthost = s.host_for(wt_machine);
    bool create = false;
    if (g.repo) {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        create = worktree_input(s, "##branch", "branch name", s.wt_branch, sizeof s.wt_branch,
                                ImGuiInputTextFlags_EnterReturnsTrue);
    }
    {
        std::lock_guard lock(s.placements_mutex);
        if (!s.wt_error.empty() && s.wt_error != "done") {
            ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->needs_you));
            ImGui::TextWrapped("%s", s.wt_error.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::TextDisabled("%s", s.wt_busy ? "working\xE2\x80\xA6" : "\xE2\x86\xB5 create \xC2\xB7 esc cancel");
    cancel = cancel || ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click();
    if (create && !s.wt_busy) {
        const std::string branch = trimmed(s.wt_branch);  // herdr create(): this.text.trim()
        if (branch.empty()) {
            std::lock_guard lock(s.placements_mutex);
            s.wt_error = "Type a branch name.";
        } else if (wthost->connected()) {
            s.wt_busy = true;
            s.wt_error.clear();
            Json params{{"repo", g.top}, {"branch", branch}};
            if (!root.empty())
                params["path"] = root + "\\" + repo + "\\" + branch_slug(branch);
            const std::string shell = pick_shell(s.settings.shell_profile, wthost->profiles());
            wthost->call(
                "worktree.add", params,
                [st = &s, shell, wthost, wt_machine](Json result, std::string error) {
                    if (!error.empty()) {
                        std::lock_guard lock(st->placements_mutex);
                        st->wt_error = error;
                        st->wt_busy = false;
                        return;
                    }
                    Json p{{"profile_id", shell}, {"cwd", result.value("path", "")}};
                    wthost->call(
                        "session.create", p,
                        [st, wt_machine](Json r, std::string e) {
                            std::lock_guard lock(st->placements_mutex);
                            st->wt_busy = false;
                            if (!e.empty()) {
                                st->wt_error = e;
                                return;
                            }
                            Placement pl;  // the space stays unnamed: it follows the checkout
                            pl.session = r.value("session_id", "");
                            pl.target = LaunchTarget::kSpace;
                            pl.machine = wt_machine;
                            st->placements.push_back(pl);
                            st->wt_error = "done";
                        },
                        std::chrono::seconds(15));
                },
                std::chrono::seconds(600));
        }
    }
    {
        std::lock_guard lock(s.placements_mutex);
        if (s.wt_error == "done") {
            s.wt_error.clear();
            cancel = true;
        }
    }
    if (cancel)
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// "Open worktree of <repo>": main checkout first, then by last commit; filter; Up/Down wrap.
void draw_worktree_open(App::State& s) {
    if (s.show_worktree_open) {
        ImGui::OpenPopup("##wtopen");
        s.show_worktree_open = false;
        s.wt_filter[0] = 0;
        s.wt_sel = 0;
    }
    center_next(px(560), 0.12f);
    push_overlay_style();
    const bool open = ImGui::BeginPopupModal("##wtopen", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    pop_overlay_style();
    if (!open)
        return;
    const GitInfo g = s.git.get(s.focused_cwd(), s.worktree_machine);
    const std::string repo = folder_name(g.common_top.empty() ? g.top : g.common_top);
    const bool no_repo = !s.worktree_not_repo.empty();
    std::string f = s.wt_filter;
    for (auto& c : f)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::vector<const WorktreeEntry*> shown;
    for (const auto& e : s.worktree_list) {
        std::string hay = e.branch + " " + e.path;
        for (auto& c : hay)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (f.empty() || hay.find(f) != std::string::npos)
            shown.push_back(&e);
    }
    char count[64];
    if (shown.size() == s.worktree_list.size())
        std::snprintf(count, sizeof count, "%zu checkouts", s.worktree_list.size());
    else
        std::snprintf(count, sizeof count, "%zu/%zu checkouts", shown.size(), s.worktree_list.size());
    // worktree/main.js 301: the title, and "N checkouts" at its right (11 px dim).
    bool close = false;
    worktree_title(s, no_repo ? std::string("Open worktree") : "Open worktree of " + repo,
                   no_repo ? std::string() : std::string(count));
    if (no_repo) {
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
        ImGui::TextWrapped("%s is not in a git repository.", s.worktree_not_repo.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("\xE2\x86\x91\xE2\x86\x93 choose \xC2\xB7 \xE2\x86\xB5 open \xC2\xB7 esc close");
        if (close || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    if (worktree_input(s, "##wf", "filter worktrees", s.wt_filter, sizeof s.wt_filter))
        s.wt_sel = 0;
    const int n = static_cast<int>(shown.size());
    if (n > 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            s.wt_sel = (s.wt_sel + 1) % n;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            s.wt_sel = (s.wt_sel + n - 1) % n;
        s.wt_sel = std::clamp(s.wt_sel, 0, n - 1);
    }
    std::string go;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginChild("##wtl", ImVec2(0, std::min(px(330), static_cast<float>(std::max(n, 1)) * px(44) + px(4))),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    if (n == 0) {  // herdr: p 10, dim
        ImGui::SetCursorPos(ImVec2(px(10), px(10)));
        ImGui::TextDisabled("No matching worktrees.");
    }
    const auto cwds = s.session_cwds();
    for (int i = 0; i < n; ++i) {
        const auto& e = *shown[static_cast<std::size_t>(i)];
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##w", ImVec2(w, px(44))))
            go = e.path;
        if (ImGui::IsItemHovered() && (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0))
            s.wt_sel = i;
        ImGui::PopID();
        if (i == s.wt_sel)
            d->AddRectFilled(a, ImVec2(a.x + w, a.y + px(44)), g_pal->selected, px(7));
        float x = a.x + px(12);
        const std::string branch = e.branch.empty() ? std::string("detached") : e.branch;
        d->AddText(s.fonts.ui_medium, fpx(13), ImVec2(x, a.y + px(6)), g_pal->text, branch.c_str());
        x += s.fonts.ui_medium->CalcTextSizeA(fpx(13), FLT_MAX, 0, branch.c_str()).x + px(8);
        if (e.main) {
            d->AddText(s.fonts.ui, fpx(11), ImVec2(x, a.y + px(8)), g_pal->faint, "main checkout");
        }
        // "open" if a space shows it, else the age of its last commit.
        // herdr spaceOf(): a terminal in the checkout's folder, the deepest checkout winning.
        auto inside = [](const std::string& p, const std::string& dir) {
            return _strnicmp(p.c_str(), dir.c_str(), dir.size()) == 0 &&
                   (p.size() == dir.size() || p[dir.size()] == '\\' || p[dir.size()] == '/');
        };
        bool shown_in_space = false;
        for (const auto& [id, cwd] : cwds) {
            if (!inside(cwd, e.path))
                continue;
            const bool deeper = std::any_of(s.worktree_list.begin(), s.worktree_list.end(), [&](const WorktreeEntry& o) {
                return o.path.size() > e.path.size() && inside(cwd, o.path);
            });
            shown_in_space = shown_in_space || !deeper;
        }
        const std::string right = shown_in_space ? std::string("open") : e.age;
        const float rw = s.fonts.ui->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, right.c_str()).x;
        d->AddText(s.fonts.ui, fpx(11.5f), ImVec2(a.x + w - px(12) - rw, a.y + px(8)),
                   shown_in_space ? g_pal->focus : g_pal->faint, right.c_str());
        text_clipped(d, s.fonts.ui, fpx(11.5f), ImVec2(a.x + px(12), a.y + px(25)), g_pal->faint, display_path(e.path),
                     w - px(24));
        if (i == s.wt_sel && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            go = e.path;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::TextDisabled("\xE2\x86\x91\xE2\x86\x93 choose \xC2\xB7 \xE2\x86\xB5 open \xC2\xB7 esc close");
    close = close || ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click();
    if (!go.empty()) {
        close = true;
        run_command(s, "worktree.open_at:" + s.worktree_machine + "|" + go);
    }
    if (close)
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ------------------------------------------------------------------ settings (herdr settings.js)
// Dialog 880 wide (window.rs 3594), 620 high, left navigation 190 px (settings/main.js 897-918);
// every change applies and saves at once (no Save). The body has no item spacing: each gap is
// herdr's own.
float line_h(float size) {
    return std::round(fpx(size) * 1.3f);
}

// Moves the cursor down by `h` (the body has no item spacing).
void settings_gap(float h) {
    ImGui::Dummy(ImVec2(0, std::max(0.0f, h)));
}

// Dim text wrapped to the body's width (herdr: 12.5 px with 18 px lines, or `size` and `lh`).
void settings_wrapped(App::State& s, const char* text, ImU32 color, float size = 12.5f, float lh = 18.0f,
                      ImFont* font = nullptr) {
    ImFont* f = font != nullptr ? font : s.fonts.ui;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* t = text;
    const char* end = text + std::strlen(text);
    float y = p.y;
    while (t < end) {
        const char* brk = f->CalcWordWrapPosition(fpx(size), t, end, w);
        if (brk == t)
            brk = t + 1;
        const float th = f->CalcTextSizeA(fpx(size), FLT_MAX, 0, t, brk).y;
        dl->AddText(f, fpx(size), ImVec2(p.x, y + (px(lh) - th) * 0.5f), color, t, brk);
        y += px(lh);
        t = brk;
        while (t < end && (*t == ' ' || *t == '\n'))
            ++t;
    }
    ImGui::Dummy(ImVec2(w, y - p.y));
}

// settings/main.js 157: pt 6, pb 8, 12 px semibold, dim, upper case.
void settings_section(App::State& s, const char* title) {
    std::string up;
    for (const char* c = title; *c != 0; ++c)
        up += static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(s.fonts.ui_bold, fpx(12), ImVec2(p.x, p.y + px(6)), g_pal->faint, up.c_str());
    settings_gap(px(6) + line_h(12) + px(8));
}

// settings/main.js 161-176 row(): gap 16, py 12, items centred, a 1 px bottom border; the
// title 14 px foreground, then (gap 3) the note 12.5 px dim with 18 px lines; the control on
// the right. Call the control after this returns: the cursor is placed for it.
void settings_row(App::State& s, const char* label, const char* note, float control_w, float control_h = -1.0f) {
    if (control_h < 0)
        control_h = std::round(fpx(13) * 1.3f) + px(10) + 2.0f;  // a settings button
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float wrap = std::max(px(80), w - control_w - px(16));
    std::vector<std::pair<const char*, const char*>> lines;
    if (note != nullptr && *note != 0) {
        const char* t = note;
        const char* end = note + std::strlen(note);
        while (t < end) {
            const char* brk = s.fonts.ui->CalcWordWrapPosition(fpx(12.5f), t, end, wrap);
            if (brk == t)
                brk = t + 1;
            lines.emplace_back(t, brk);
            t = brk;
            while (t < end && (*t == ' ' || *t == '\n'))
                ++t;
        }
    }
    const float text_h = line_h(14) + (lines.empty() ? 0.0f : px(3) + px(18) * static_cast<float>(lines.size()));
    const float inner = std::max(text_h, control_h);
    float y = p.y + px(12) + (inner - text_h) * 0.5f;
    dl->AddText(s.fonts.ui, fpx(14), ImVec2(p.x, y), g_pal->text, label);
    y += line_h(14) + px(3);
    for (const auto& [a, b] : lines) {
        const float th = s.fonts.ui->CalcTextSizeA(fpx(12.5f), FLT_MAX, 0, a, b).y;
        dl->AddText(s.fonts.ui, fpx(12.5f), ImVec2(p.x, y + (px(18) - th) * 0.5f), g_pal->faint, a, b);
        y += px(18);
    }
    const float h = px(12) + inner + px(12);
    dl->AddLine(ImVec2(p.x, p.y + h - 0.5f), ImVec2(p.x + w, p.y + h - 0.5f), g_pal->border);
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - control_w, p.y + px(12) + (inner - control_h) * 0.5f));
    ImGui::SetNextItemWidth(control_w);
    s.settings_row_end = p.y + h;
    s.settings_row_x = p.x;
}
void settings_row_done(App::State& s) {
    ImGui::SetCursorScreenPos(ImVec2(s.settings_row_x, s.settings_row_end));
    ImGui::Dummy(ImVec2(0, 0));
}

// settings/main.js 196-222 stepper(): one bordered group (rounded 7, raised): 28 x 28 cells
// with "−" and "+" (15 px muted; hover: background + foreground) around a 54 px value (13 px).
// 112 x 30 with the border. Returns true when the value changed.
constexpr float kStepperW = 28 + 54 + 28 + 2;
constexpr float kStepperH = 28 + 2;
bool stepper(App::State& s, const char* id, float& v, float step, float lo, float hi, const char* fmt, float scale = 1.0f) {
    bool changed = false;
    ImGui::PushID(id);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + px(kStepperW), a.y + px(kStepperH));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, raised_color(), px(7));
    auto cell = [&](const char* key, const char* label, float x, float dir) {
        const ImVec2 c(x, a.y + px(1));
        ImGui::SetCursorScreenPos(c);
        if (ImGui::InvisibleButton(key, ImVec2(px(28), px(28)))) {
            v = std::clamp(std::round((v + dir * step) / step) * step, lo, hi);
            changed = true;
        }
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(c, ImVec2(c.x + px(28), c.y + px(28)), g_pal->hover,
                              px(6), dir < 0 ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersRight);
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(15), FLT_MAX, 0, label);
        dl->AddText(s.fonts.ui, fpx(15), ImVec2(c.x + (px(28) - t.x) * 0.5f, c.y + (px(28) - t.y) * 0.5f),
                    hov ? g_pal->text : g_pal->dim, label);
    };
    cell("##minus", "\xE2\x88\x92", a.x + px(1), -1.0f);
    char buf[32];
    std::snprintf(buf, sizeof buf, fmt, v * scale);
    const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, buf);
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + px(29) + (px(54) - t.x) * 0.5f, a.y + (px(kStepperH) - t.y) * 0.5f),
                g_pal->text, buf);
    cell("##plus", "+", a.x + px(29) + px(54), 1.0f);
    dl->AddRect(a, b, g_pal->border, px(7));
    ImGui::SetCursorScreenPos(a);
    ImGui::Dummy(ImVec2(px(kStepperW), px(kStepperH)));
    ImGui::PopID();
    return changed;
}

// Terminal fonts offered in Settings: the bundled default, then monospace faces Windows ships.
// ---- fonts (herdr: every installed family, filterable; weight; interface font)
const std::vector<ui::FontFamily>& font_families() {
    static const std::vector<ui::FontFamily> families = ui::installed_font_families();
    return families;
}

const ui::FontFamily* find_family(const std::string& name) {
    for (const auto& f : font_families())
        if (_stricmp(f.name.c_str(), name.c_str()) == 0)
            return &f;
    return nullptr;
}

namespace w98 {
void ensure_fonts(App::State& s);
ImFont* layout_font(App::State& s, int which);  // 0 terminals, 1 interface, 2 interface bold
float terminal_font_size(App::State& s);
void preview(App::State& s, ImDrawList* dl, ImVec2 a, ImVec2 size, float rounding, ImU32 border);
}  // namespace w98

// Terminal font: family (empty: bundled JetBrains Mono) at the chosen weight, bold from the
// family's 700 face. A layout with fonts of its own (Herdr 98: win98.json "font") uses those.
void apply_terminal_font(App::State& s) {
    if (s.layout().style == "win98") {
        w98::ensure_fonts(s);
        s.fonts.mono = s.fonts.mono_bold = w98::layout_font(s, 0);
        return;
    }
    const auto* f = find_family(s.settings.terminal_font.empty() ? std::string("JetBrains Mono") : s.settings.terminal_font);
    ImFont* mono = f != nullptr ? ui::load_font_file(f->file_for(s.settings.terminal_weight), s.settings.font_size, true) : nullptr;
    s.fonts.mono = mono != nullptr ? mono : ui::load_terminal_font(L"", s.settings.font_size);
    ImFont* bold = f != nullptr ? ui::load_font_file(f->file_for(700), s.settings.font_size, true) : nullptr;
    s.fonts.mono_bold = bold != nullptr ? bold : s.fonts.mono;
}

// Interface font: family (empty: bundled Geist, kept from startup) at 400 / 500 / 600.
void apply_ui_font(App::State& s) {
    if (s.base_ui == nullptr) {  // the bundled set, loaded once at startup
        s.base_ui = s.fonts.ui;
        s.base_ui_medium = s.fonts.ui_medium;
        s.base_ui_bold = s.fonts.ui_bold;
    }
    if (s.layout().style == "win98") {  // win98.json "ui_font": "Pixelated MS Sans Serif"
        w98::ensure_fonts(s);
        s.fonts.ui = s.fonts.ui_medium = w98::layout_font(s, 1);
        s.fonts.ui_bold = w98::layout_font(s, 2);
        ImGui::GetIO().FontDefault = s.fonts.ui;
        return;
    }
    const auto* f = s.settings.ui_font.empty() ? nullptr : find_family(s.settings.ui_font);
    ImFont* r = f != nullptr ? ui::load_font_file(f->file_for(400), 13.0f, false) : nullptr;
    if (r == nullptr) {
        s.fonts.ui = s.base_ui;
        s.fonts.ui_medium = s.base_ui_medium;
        s.fonts.ui_bold = s.base_ui_bold;
    } else {
        ImFont* m = ui::load_font_file(f->file_for(500), 13.0f, false);
        ImFont* b = ui::load_font_file(f->file_for(600), 13.0f, false);
        s.fonts.ui = r;
        s.fonts.ui_medium = m != nullptr ? m : r;
        s.fonts.ui_bold = b != nullptr ? b : s.fonts.ui_medium;
    }
    ImGui::GetIO().FontDefault = s.fonts.ui;
}

// herdr font list: a filter box and up to 200 matching families.
bool font_picker(App::State& /*s*/, const char* id, std::string& family, const char* default_label, float width) {
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    const std::string current = family.empty() ? std::string(default_label) : family;
    if (ImGui::BeginCombo(id, current.c_str(), ImGuiComboFlags_HeightLarge)) {
        static char filter[64] = {};
        if (ImGui::IsWindowAppearing()) {
            filter[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "Search fonts", filter, sizeof filter);
        if (ImGui::Selectable(default_label, family.empty())) {
            family.clear();
            changed = true;
        }
        int shown = 0;
        for (const auto& f : font_families()) {
            if (filter[0] != '\0') {
                std::string a = f.name, q = filter;
                for (auto& c : a)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                for (auto& c : q)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (a.find(q) == std::string::npos)
                    continue;
            }
            if (++shown > 200) {
                ImGui::TextDisabled("Showing 200: type to narrow the list");
                break;
            }
            if (ImGui::Selectable(f.name.c_str(), _stricmp(f.name.c_str(), family.c_str()) == 0)) {
                family = f.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void play_alert(App::State& s, bool blocked);

// A row of choices, one selected (herdr's segment control).
bool segmented(App::State& s, const char* id, std::string& value,
               std::initializer_list<std::pair<const char*, const char*>> options) {
    bool changed = false;
    ImGui::PushID(id);
    const float bw = px(78), bh = px(28);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + bw * static_cast<float>(options.size()) + px(4), a.y + bh + px(4)), raised_color(), px(7));
    float x = a.x + px(2);
    for (const auto& [key, label] : options) {
        ImGui::SetCursorScreenPos(ImVec2(x, a.y + px(2)));
        if (ImGui::InvisibleButton(key, ImVec2(bw, bh)) && value != key) {
            value = key;
            changed = true;
        }
        const bool on = value == key;
        const bool hov = ImGui::IsItemHovered();
        if (on || hov)
            dl->AddRectFilled(ImVec2(x, a.y + px(2)), ImVec2(x + bw, a.y + px(2) + bh), on ? g_pal->selected : g_pal->hover, px(6));
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, label);
        dl->AddText(s.fonts.ui, fpx(12), ImVec2(x + (bw - t.x) * 0.5f, a.y + px(2) + (bh - t.y) * 0.5f),
                    on ? g_pal->text : g_pal->dim, label);
        x += bw;
    }
    ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + bh + px(6)));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopID();
    return changed;
}

// herdr frame/settings/main.js 242-270: a small drawing of each layout in the theme's colors.
// main: a 42 px sidebar of lines, a tab strip (accent + two raised), two terminals side by side.
// terminals: its own dark look, a bar with one tab and two cards. Anything else: a plain frame.
void layout_preview(ImDrawList* d, const std::string& name, ImVec2 a, ImVec2 size) {
    const ImVec2 b(a.x + size.x, a.y + size.y);
    auto line = [&](float x, float y, float w, ImU32 c) {
        d->AddRectFilled(ImVec2(x, y), ImVec2(x + px(w), y + px(3)), c, px(2));
    };
    auto lines = [&](float x, float y, int n, ImU32 c) {
        for (int i = 0; i < n; ++i)
            line(x + px(5), y + px(5) + static_cast<float>(i) * px(7), i % 2 ? 22.0f : 32.0f, c);
    };
    auto term = [&](ImVec2 p, ImVec2 q) {
        d->AddRectFilled(p, q, g_pal->bg, px(2));
        d->AddRect(p, q, g_pal->border, px(2));
    };
    if (name == "terminals") {
        const ImU32 bg = IM_COL32(0x13, 0x16, 0x19, 255);
        d->AddRectFilled(a, b, bg, px(6));
        d->AddRect(a, b, g_pal->border, px(6));
        const ImVec2 in(a.x + px(4), a.y + px(4));
        const float iw = size.x - px(8);
        d->AddRectFilled(in, ImVec2(in.x + iw, in.y + px(10)), IM_COL32(0x1d, 0x1f, 0x22, 255), px(4));
        d->AddRectFilled(ImVec2(in.x + px(1), in.y + px(1)), ImVec2(in.x + px(31), in.y + px(9)),
                         IM_COL32(0x34, 0x37, 0x3c, 255), px(3));
        d->AddRect(ImVec2(in.x + px(1), in.y + px(1)), ImVec2(in.x + px(31), in.y + px(9)),
                   IM_COL32(0x4a, 0x4d, 0x53, 255), px(3));
        const float top = in.y + px(14);
        const float cw = (iw - px(4)) * 0.5f;
        for (int i = 0; i < 2; ++i) {
            const ImVec2 p(in.x + static_cast<float>(i) * (cw + px(4)), top);
            const ImVec2 q(p.x + cw, b.y - px(4));
            d->AddRectFilled(p, q, IM_COL32(0x17, 0x19, 0x1c, 255), px(4));
            d->AddRect(p, q, IM_COL32(0x2c, 0x2f, 0x34, 255), px(4));
            d->AddLine(ImVec2(p.x, p.y + px(7)), ImVec2(q.x, p.y + px(7)), IM_COL32(0x25, 0x28, 0x2c, 255));
            line(p.x + px(3), p.y + px(2), 14, IM_COL32(0x5d, 0x61, 0x68, 255));
        }
        return;
    }
    d->AddRectFilled(a, b, g_pal->sidebar, px(6));
    if (name == "workspace") {
        const float sx = a.x + px(42);
        d->AddLine(ImVec2(sx, a.y), ImVec2(sx, b.y), g_pal->border);
        lines(a.x, a.y, 4, g_pal->dim);
        const float ty = a.y + px(11);
        d->AddLine(ImVec2(sx, ty), ImVec2(b.x, ty), g_pal->border);
        float x = sx + px(2);
        d->AddRectFilled(ImVec2(x, a.y + px(2)), ImVec2(x + px(20), ty - px(2)), g_pal->focus, px(2));
        x += px(22);
        for (int i = 0; i < 2; ++i, x += px(18))
            d->AddRectFilled(ImVec2(x, a.y + px(2)), ImVec2(x + px(16), ty - px(2)), raised_color(), px(2));
        const float tw = (b.x - sx - px(8) - px(3)) * 0.5f;
        term(ImVec2(sx + px(4), ty + px(4)), ImVec2(sx + px(4) + tw, b.y - px(4)));
        term(ImVec2(sx + px(4) + tw + px(3), ty + px(4)), ImVec2(b.x - px(4), b.y - px(4)));
    } else {
        const float ty = a.y + px(11);
        d->AddLine(ImVec2(a.x, ty), ImVec2(b.x, ty), g_pal->border);
        term(ImVec2(a.x + px(4), ty + px(4)), ImVec2(b.x - px(4), b.y - px(4)));
    }
    d->AddRect(a, b, g_pal->border, px(6));
}

// ---- herdr settings main.js helpers used by Updates and Privacy
// main.js 180-192: px 12, py 5, rounded 7, a 1 px border, raised, 13 px text, hover background.
ImVec2 settings_button_size(App::State& s, const char* label, ImFont* font = nullptr) {
    const ImVec2 t = (font != nullptr ? font : s.fonts.ui)->CalcTextSizeA(fpx(13), FLT_MAX, 0, label);
    return ImVec2(t.x + px(24) + 2.0f, std::round(fpx(13) * 1.3f) + px(10) + 2.0f);
}

bool settings_button(App::State& s, const char* id, const char* label) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 size = settings_button_size(s, label);
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(a.x + size.x, a.y + size.y);
    dl->AddRectFilled(a, b, hov ? g_pal->hover : raised_color(), px(7));
    dl->AddRect(a, b, g_pal->border, px(7));
    const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, label);
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + (size.x - t.x) * 0.5f, a.y + (size.y - t.y) * 0.5f), g_pal->text, label);
    return clicked;
}

// main.js 787-788 primary(): px 12, py 5, rounded 7, accent fill, accent_foreground semibold 13,
// hover opacity 0.9.
bool settings_primary(App::State& s, const char* id, const char* label) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 size = settings_button_size(s, label, s.fonts.ui_bold);
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + size.x, a.y + size.y), hov ? alpha(g_pal->focus, 230) : g_pal->focus, px(7));
    const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(13), FLT_MAX, 0, label);
    dl->AddText(s.fonts.ui_bold, fpx(13), ImVec2(a.x + (size.x - t.x) * 0.5f, a.y + (size.y - t.y) * 0.5f),
                hov ? alpha(accent_foreground(), 230) : accent_foreground(), label);
    return clicked;
}

// main.js 779, 781: a button's place taken by text (px 12, py 5, 13 px).
void settings_chip(App::State& s, const char* label, ImU32 color) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 size = settings_button_size(s, label);
    ImGui::Dummy(size);
    const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, label);
    ImGui::GetWindowDrawList()->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + px(12) + 1.0f, a.y + (size.y - t.y) * 0.5f),
                                        color, label);
}

// main.js 225-238: 36 x 20, rounded 10, padding 2; accent when on (knob at the end), hover
// colour when off; a 16 px knob, accent_foreground on, muted off.
bool settings_toggle(const char* id, bool on) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 size(px(36), px(20));
    const bool clicked = ImGui::InvisibleButton(id, size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + size.x, a.y + size.y), on ? g_pal->focus : g_pal->hover, px(10));
    const float r = px(8);
    const float cx = on ? a.x + size.x - px(2) - r : a.x + px(2) + r;
    dl->AddCircleFilled(ImVec2(cx, a.y + size.y * 0.5f), r, on ? accent_foreground() : g_pal->dim);
    return clicked;
}

// main.js 757, 815: a dim note under the rows (pt 12, 12.5 px, 18 px lines).
void settings_footnote(App::State& s, const char* text) {
    ImGui::Dummy(ImVec2(0, px(12)));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* t = text;
    const char* end = text + std::strlen(text);
    float y = p.y;
    while (t < end) {
        const char* brk = s.fonts.ui->CalcWordWrapPosition(fpx(12.5f), t, end, w);
        if (brk == t)
            brk = t + 1;
        dl->AddText(s.fonts.ui, fpx(12.5f), ImVec2(p.x, y), g_pal->faint, t, brk);
        y += px(18);
        t = brk;
        while (t < end && (*t == ' ' || *t == '\n'))
            ++t;
    }
    ImGui::Dummy(ImVec2(w, y - p.y));
}

// main.js 764-767: local HH:MM.
std::string clock_at(std::uint64_t secs) {
    const std::time_t t = static_cast<std::time_t>(secs);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return buf;
}

// main.js 847: new Date(secs * 1000).toLocaleString(): the date and time as Windows writes them.
std::string local_date_time(std::uint64_t secs) {
    ULARGE_INTEGER u{};
    u.QuadPart = (secs + 11644473600ull) * 10000000ull;
    FILETIME ft{u.LowPart, u.HighPart};
    FILETIME local{};
    SYSTEMTIME st{};
    ::FileTimeToLocalFileTime(&ft, &local);
    ::FileTimeToSystemTime(&local, &st);
    wchar_t d[64]{};
    wchar_t t[64]{};
    ::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, d, 64, nullptr);
    ::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, nullptr, t, 64);
    return win::to_utf8(std::wstring(d) + L", " + t);
}

// ---- Updates (herdr settings main.js 760-816). True: the settings dialog should close.
bool settings_updates(App::State& s) {
    bool close = false;
    const auto u = ui::updates::status();
    const std::string checked = u.checked ? " Checked at " + clock_at(*u.checked) + "." : std::string();
    const std::string result = u.checking ? std::string("Checking\xE2\x80\xA6")
                               : u.checked ? "Up to date." + checked
                                           : std::string("No successful check. Check your connection and try again.");
    // Pressed: "Checking..." while it looks, then "Up to date" for a moment (a find changes the
    // row instead). One per row.
    const bool asked =
        s.updates_asked && Clock::now() - *s.updates_asked < std::chrono::milliseconds(3000);
    enum class Control { kNone, kCheck, kRestart, kRetry, kUpdateCore };
    // The check control's look (main.js 775-786).
    const auto check_w = [&]() -> float {
        if (u.checking && s.updates_asked)
            return settings_button_size(s, "Checking\xE2\x80\xA6").x;
        if (asked && u.checked && !u.checking)
            return settings_button_size(s, "\xE2\x9C\x93 Up to date").x;
        return settings_button_size(s, "Check now").x;
    };
    const auto draw_check = [&](const char* id) {
        if (u.checking && s.updates_asked) {
            settings_chip(s, "Checking\xE2\x80\xA6", g_pal->faint);
        } else if (asked && u.checked && !u.checking) {
            settings_chip(s, "\xE2\x9C\x93 Up to date", g_pal->done);  // t.ansi2
        } else if (settings_button(s, id, "Check now")) {
            s.updates_asked = Clock::now();
            run_command(s, "app.check_updates");
        }
    };
    // main.js 789-793
    std::string app_text;
    Control app_control = Control::kNone;
    if (!u.app_updates) {
        app_text = "A build from source: it updates with git, not here.";
    } else if (u.app_ready) {
        app_text = *u.app_ready + " is ready. Restart to switch now; it also switches when you quit.";
        app_control = Control::kRestart;
    } else {
        app_text = result;
        app_control = u.checks ? Control::kCheck : Control::kNone;
    }
    // main.js 794-805
    const auto& c = u.core;
    std::string core_text;
    Control core_control = Control::kNone;
    if (c.managed) {
        core_text = "Managed by " + *c.managed + ": update it there." + (c.ready ? " " + *c.ready + " is available." : "");
        core_control = u.checks ? Control::kCheck : Control::kNone;
    } else if (c.updating) {
        core_text = "Updating\xE2\x80\xA6 your terminals keep running.";
    } else if (c.error) {
        core_text = "The update failed: " + *c.error;
        core_control = Control::kRetry;
    } else if (c.ready) {
        core_text = *c.ready + " is ready. Your terminals keep running through it.";
        core_control = Control::kUpdateCore;
    } else if (u.checks) {
        core_text = result;
        core_control = Control::kCheck;
    } else {
        core_text = "Not checked: a build from source looks for no updates.";
    }
    const auto control_w = [&](Control k) -> float {
        switch (k) {
            case Control::kCheck:
                return check_w();
            case Control::kRestart:
                return settings_button_size(s, "Restart to update", s.fonts.ui_bold).x;
            case Control::kRetry:
                return settings_button_size(s, "Try again", s.fonts.ui_bold).x;
            case Control::kUpdateCore:
                return settings_button_size(s, "Update agent-host", s.fonts.ui_bold).x;
            case Control::kNone:
                break;
        }
        return 0.0f;
    };
    const auto draw_control = [&](Control k, const char* check_id) {
        switch (k) {
            case Control::kCheck:
                draw_check(check_id);
                break;
            case Control::kRestart:
                if (settings_primary(s, "##st-app-update", "Restart to update"))
                    run_command(s, "app.update");
                break;
            case Control::kRetry:
                if (settings_primary(s, "##st-core-retry", "Try again"))
                    run_command(s, "app.update_core");
                break;
            case Control::kUpdateCore:
                if (settings_primary(s, "##st-core-update", "Update agent-host"))
                    run_command(s, "app.update_core");
                break;
            case Control::kNone:
                break;
        }
    };
    // main.js 806-811: heading "Herdr" (the product), "Herdr <version>", What's new + the control.
    settings_section(s, "AgentDesk");
    {
        const float wn = settings_button_size(s, "What's new").x;
        const float extra = control_w(app_control);
        const std::string title = "AgentDesk " + u.app_version;
        settings_row(s, title.c_str(), app_text.c_str(), wn + (extra > 0 ? px(8) + extra : 0.0f));
        if (settings_button(s, "##st-whats-new", "What's new")) {
            close = true;  // main.js 810: closeOverlay, then app.whats_new
            run_command(s, "app.whats_new");
        }
        if (extra > 0) {
            ImGui::SameLine(0, px(8));
            draw_control(app_control, "##st-check-app");
        }
        settings_row_done(s);
    }
    // main.js 812-814: 18 px, heading "herdr-core", "herdr-core <version>".
    ImGui::Dummy(ImVec2(0, px(18)));
    settings_section(s, "agent-host");
    {
        const std::string title = "agent-host " + c.version.value_or("");
        settings_row(s, title.c_str(), core_text.c_str(), control_w(core_control));
        draw_control(core_control, "##st-check-core");
        settings_row_done(s);
    }
    // main.js 815
    settings_footnote(s, "AgentDesk looks for both every hour and tells you in the corner. agent-host runs your "
                         "terminals, so they keep running through its updates.");
    return close;
}

// main.js 821-842: a report as it goes out, one line per line of JSON.
void settings_report(App::State& s, const std::string& title, const std::string& about,
                     const ui::telemetry::Json& report) {
    if (s.report_font == nullptr)
        s.report_font = ui::load_terminal_font(L"", 12.0f);  // the bundled JetBrains Mono
    ImFont* mono = s.report_font != nullptr ? s.report_font : s.fonts.mono;
    const std::string text = report.dump(2, ' ', false, ui::telemetry::Json::error_handler_t::replace);
    ImGui::Dummy(ImVec2(0, px(14) - ImGui::GetStyle().ItemSpacing.y));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddText(s.fonts.ui, fpx(13.5f), p, g_pal->text, title.c_str());
    p.y += std::round(fpx(13.5f) * 1.3f) + px(4);
    if (!about.empty()) {
        dl->AddText(s.fonts.ui, fpx(12.5f), p, g_pal->faint, about.c_str());
        p.y += std::round(fpx(12.5f) * 1.3f) + px(4);
    }
    p.y += px(4);  // mt 4
    int lines = 1;
    for (const char ch : text)
        lines += ch == '\n' ? 1 : 0;
    const float h = px(10) * 2 + px(17) * static_cast<float>(lines);
    const ImVec2 b(p.x + w, p.y + h);
    dl->AddRectFilled(p, b, g_pal->bg, px(8));
    dl->AddRect(p, b, g_pal->border, px(8));
    dl->PushClipRect(ImVec2(p.x + 1, p.y + 1), ImVec2(b.x - 1, b.y - 1), true);
    float y = p.y + px(10);
    for (std::size_t at = 0; at <= text.size();) {
        const auto nl = text.find('\n', at);
        const std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        dl->AddText(mono, fpx(12), ImVec2(p.x + px(10), y + (px(17) - fpx(12)) * 0.5f), g_pal->dim,
                    line.empty() ? " " : line.c_str());
        y += px(17);
        if (nl == std::string::npos)
            break;
        at = nl + 1;
    }
    dl->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(p.x, b.y));
    ImGui::Dummy(ImVec2(w, 0));
}

// ---- Privacy (herdr settings main.js 844-882). True: ui.json changed.
bool settings_privacy(App::State& s) {
    bool changed = false;
    const ui::telemetry::Json st = ui::telemetry::status();
    const bool have = st.is_object();
    const bool on = have ? st.value("setting", false) : s.settings.telemetry;
    std::string why;
    if (have && st.contains("off_because") && st["off_because"].is_string())
        why = "Off for now: " + st["off_because"].get<std::string>() + " is set where AgentDesk started.";
    settings_section(s, "Usage data");
    settings_row(s, "Share anonymous usage data",
                 !why.empty() ? why.c_str()
                              : "Once a day: which agents and features you use, how many spaces and tabs, your system "
                                "and AgentDesk's version. Never what's in your terminals, never names of files, "
                                "folders or projects.",
                 px(36));
    if (settings_toggle("##st-telemetry", on)) {
        // main.js 858-861: on -> "telemetry": false; off -> the key goes.
        s.settings.telemetry = !on;
        changed = true;
        ui::telemetry::set_setting(s.settings.telemetry);  // window.rs 955: read again on change
    }
    settings_row_done(s);
    const bool sends = have && st.value("sends", false);
    const char* label = s.show_data ? "Hide" : "Show";
    settings_row(s, "What we send",
                 have && !sends ? "This build never sends: it's built from source. You can still see what a release "
                                  "would send."
                                : "Exactly what leaves your computer, word for word.",
                 settings_button_size(s, label).x);
    if (settings_button(s, "##st-telemetry-show", label))
        s.show_data = !s.show_data;
    settings_row_done(s);
    if (!s.show_data)
        return changed;
    if (!have || !st.value("enabled", false)) {
        ImGui::Dummy(ImVec2(0, px(14) - ImGui::GetStyle().ItemSpacing.y));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText(s.fonts.ui, fpx(13), p, g_pal->faint,
                                            "Nothing is collected or sent while it's off.");
        ImGui::Dummy(ImVec2(0, std::round(fpx(13) * 1.3f)));
        return changed;
    }
    // main.js 879-881
    if (const auto q = st.find("queued"); q != st.end() && q->is_array())
        for (const auto& r : *q)
            settings_report(s, "Waiting to send: " + r.value("day", std::string()), "Sent as is, soon.", r);
    if (const auto d = st.find("draft"); d != st.end() && d->is_object())
        settings_report(s, "Today so far", "A draft: it changes until the day ends (UTC), then goes out once.", *d);
    if (const auto l = st.find("last_sent"); l != st.end() && l->is_object() && l->contains("report")) {
        const auto& r = (*l)["report"];
        const std::uint64_t at = l->contains("at") && (*l)["at"].is_number_unsigned() ? (*l)["at"].get<std::uint64_t>() : 0;
        settings_report(s, "Last sent: " + (r.is_object() ? r.value("day", std::string()) : std::string()),
                        "Sent " + local_date_time(at) + ".", r);
    }
    return changed;
}

void draw_settings(App::State& s) {
    if (s.show_settings) {
        ImGui::OpenPopup("##settings");
        s.show_settings = false;
        if (!s.settings_tab.empty())
            s.settings_section = s.settings_tab;
        s.settings_tab.clear();
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = std::min(px(880), vp->WorkSize.x - px(16));  // window.rs 3594: width 880
    const float h = std::min(px(620), vp->WorkSize.y - px(16));
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + (vp->WorkSize.y - h) * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    push_overlay_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::BeginPopupModal("##settings", nullptr,
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();
    pop_overlay_style();
    if (!open)
        return;
    bool changed = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    // Left navigation (190 px): heading, sections.
    const float nav_w = px(190);
    dl->AddRectFilled(wp, ImVec2(wp.x + nav_w, wp.y + h), g_pal->sidebar, px(8), ImDrawFlags_RoundCornersLeft);
    dl->AddLine(ImVec2(wp.x + nav_w, wp.y), ImVec2(wp.x + nav_w, wp.y + h), g_pal->border);
    // settings/main.js 897-899: nav p 10; "Settings" px 10, pt 6, pb 12, 16 px semibold.
    dl->AddText(s.fonts.ui_bold, fpx(16), ImVec2(wp.x + px(10) + px(10), wp.y + px(10) + px(6)), g_pal->text, "Settings");
    struct Section {
        const char* name;
        const char* glyph;
    };
    // main.js 9-16: palette, server, bell, refresh, shield (Mods is ours: C++ DLL mods).
    static constexpr Section kSections[] = {{"Look", ui::icon::kPalette},  {"Machines", ui::icon::kServer},
                                            {"Alerts", ui::icon::kBell},    {"Mods", ui::icon::kExtensions},
                                            {"Updates", ui::icon::kRefresh}, {"Privacy", ui::icon::kShield}};
    // main.js 888-889: something waits to be installed.
    const auto upd = ui::updates::status();
    const bool waiting = upd.app_ready.has_value() || (upd.core.ready.has_value() && !upd.core.managed);
    if (s.settings_section.empty())
        s.settings_section = "Look";
    bool close = false;
    // main.js 900-918: items px 10, py 7, gap 10, rounded 7, gap 2 between; a 15 px icon (accent
    // when chosen, else dim), 13.5 px text (foreground when chosen, else muted); raised when
    // chosen or hovered.
    const float item_w = nav_w - px(20);
    const float item_h = px(7) + line_h(13.5f) + px(7);
    float ny = wp.y + px(10) + px(6) + line_h(16) + px(12);
    for (const auto& sec : kSections) {
        const ImVec2 a(wp.x + px(10), ny);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(sec.name);
        if (ImGui::InvisibleButton("##nav", ImVec2(item_w, item_h)))
            s.settings_section = sec.name;
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool on = s.settings_section == sec.name;
        if (on || hov)
            dl->AddRectFilled(a, ImVec2(a.x + item_w, a.y + item_h), raised_color(), px(7));
        const float cy = a.y + item_h * 0.5f;
        draw_icon(s, dl, sec.glyph, fpx(15), ImVec2(a.x + px(10) + px(7.5f), cy), on ? g_pal->focus : g_pal->faint);
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13.5f), FLT_MAX, 0, sec.name);
        dl->AddText(s.fonts.ui, fpx(13.5f), ImVec2(a.x + px(10) + px(15) + px(10), cy - t.y * 0.5f),
                    on ? g_pal->text : g_pal->dim, sec.name);
        // main.js 917: a 7 px accent dot at the row's end.
        if (std::string_view(sec.name) == "Updates" && waiting)
            dl->AddCircleFilled(ImVec2(a.x + item_w - px(10) - px(3.5f), cy), px(3.5f), g_pal->focus);
        ny += item_h + px(2);
    }
    // main.js 967: "Send feedback", a settings button at the bottom of the nav; it closes
    // Settings first, then opens the feedback window.
    {
        const ImVec2 bs = settings_button_size(s, "Send feedback");
        ImGui::SetCursorScreenPos(ImVec2(wp.x + px(10), wp.y + h - px(10) - bs.y));
        if (settings_button(s, "##st-feedback", "Send feedback")) {
            close = true;
            run_command(s, "app.feedback");
        }
    }
    // Content.
    ImGui::SetCursorScreenPos(ImVec2(wp.x + nav_w + 1, wp.y));
    // main.js 949-990: no title and no Esc in the body (Esc still closes); padding px 28, py 22
    // inside the scroll area; the page starts with its first heading.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(28), px(22)));
    ImGui::BeginChild("##settings-body", ImVec2(w - nav_w - 1, h), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), 0));
    const std::string& sec = s.settings_section;
    // Cards that wrap (flex_wrap, gap 8): the next card's place.
    struct CardFlow {
        float x0, right, x, y, row_h = 0;
        ImVec2 next(float cw, float ch) {
            if (x > x0 && x + cw > right) {
                x = x0;
                y += row_h + px(8);
                row_h = 0;
            }
            const ImVec2 a(x, y);
            x += cw + px(8);
            row_h = std::max(row_h, ch);
            return a;
        }
        void end() const {
            ImGui::SetCursorScreenPos(ImVec2(x0, y + row_h));
            ImGui::Dummy(ImVec2(0, 0));
        }
    };
    auto flow = [] {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        return CardFlow{p.x, p.x + ImGui::GetContentRegionAvail().x, p.x, p.y};
    };
    if (sec == "Look") {
        settings_section(s, "Layout");
        // herdr frame/settings/main.js 272-320: cards 124 wide, p 6, rounded 10, a 2 px accent
        // border and raised fill when chosen (raised on hover), a 62 px preview, then the name
        // (12.5, muted; foreground and a check when chosen). Shipped ones first.
        CardFlow lf = flow();
        for (const auto& l : s.layouts) {
            const bool on = l.name == s.settings.layout;
            const float cw = px(124), pad = px(6), ph = px(62), lh = line_h(12.5f);
            const float ch = pad + ph + px(6) + lh + pad;
            const ImVec2 a = lf.next(cw, ch);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(l.name.c_str());
            if (ImGui::InvisibleButton("##lay", ImVec2(cw, ch)))
                run_command(s, "layout:" + l.name);
            const bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            ImDrawList* d = ImGui::GetWindowDrawList();
            if (on || hov)
                d->AddRectFilled(a, ImVec2(a.x + cw, a.y + ch), raised_color(), px(10));
            if (on)
                d->AddRect(a, ImVec2(a.x + cw, a.y + ch), g_pal->focus, px(10), 0, px(2));
            // herdr settings/main.js 86-99, 302-309: a layout with a "preview" picture shows it
            // (win98.json "preview": layouts/win98.svg), rounded 6 with a 1 px border.
            if (l.style == "win98")
                w98::preview(s, d, ImVec2(a.x + pad, a.y + pad), ImVec2(cw - pad * 2, ph), px(6), g_pal->border);
            else
                layout_preview(d, l.name, ImVec2(a.x + pad, a.y + pad), ImVec2(cw - pad * 2, ph));
            // herdr LOOKS: main -> "Herdr" (ours is AgentDesk), terminals -> "Terminals",
            // win98 -> "Herdr 98" (settings/main.js 18).
            std::string label = l.name == "workspace" ? std::string("AgentDesk")
                                : l.name == "terminals" ? std::string("Terminals")
                                : l.name == "win98"     ? std::string("AgentDesk 98")
                                                        : l.name;
            const ImVec2 lp(a.x + pad, a.y + pad + ph + px(6));
            const float tw = text_clipped(d, s.fonts.ui, fpx(12.5f), lp, on ? g_pal->text : g_pal->dim, label,
                                          cw - pad * 2 - (on ? px(17) : 0.0f));
            if (on)
                draw_icon(s, d, ui::icon::kCheck, fpx(12), ImVec2(lp.x + tw + px(5) + px(6), lp.y + fpx(12.5f) * 0.55f),
                          g_pal->focus);
        }
        lf.end();
        settings_gap(px(18));  // main.js 520
        settings_section(s, "Theme");
        settings_row(s, "Appearance", "Light or dark, or as your system is now.", px(240), px(32));
        if (segmented(s, "##appearance", s.settings.appearance, {{"system", "System"}, {"light", "Light"}, {"dark", "Dark"}}))
            changed = true;
        settings_row_done(s);
        // main.js 523-527: the note (pt 12, pb 8, 12.5 dim), "Dark" (pb 6, 12.5 medium muted),
        // the dark cards, "Light" (pt 10, pb 6), the light cards.
        settings_gap(px(12));
        settings_wrapped(s, "For the layout you're in: each layout keeps its own light and dark theme.", g_pal->faint);
        settings_gap(px(8));
        for (const bool light_cards : {false, true}) {
            if (light_cards)
                settings_gap(px(10));
            settings_wrapped(s, light_cards ? "Light" : "Dark", g_pal->dim, 12.5f, 12.5f * 1.3f, s.fonts.ui_medium);
            settings_gap(px(6));
            // main.js 353-405 themePicker(): cards 124 wide, gap 6, p 6, rounded 10, a 2 px
            // border (accent when chosen), raised on hover; a 46 px preview (p 7, gap 6, rounded
            // 6, 1 px border, the theme's background) with a 44 x 4 bar in its terminal
            // foreground and 8 px dots (gap 4) in accent, ansi1, ansi2, ansi3, ansi5; under it
            // the name (12.5 px, foreground when chosen, else muted) and a 12 px accent check.
            CardFlow tf = flow();
            for (const Palette* pal : all_palettes()) {
                const Palette& p = *pal;
                if (p.light != light_cards || p.layout_only)
                    continue;
                const auto shown = layout_theme_pair(s.settings);
                const bool on = (light_cards ? shown.second : shown.first) == p.id;
                const float cw = px(124), pad = px(6), ph = px(46);
                const float ch = pad + ph + px(6) + line_h(12.5f) + pad;
                const ImVec2 a = tf.next(cw, ch);
                ImGui::SetCursorScreenPos(a);
                ImGui::PushID(p.id);
                if (ImGui::InvisibleButton("##theme", ImVec2(cw, ch))) {
                    run_command(s, std::string("theme:") + p.id);
                    changed = true;
                }
                const bool hov = ImGui::IsItemHovered();
                ImGui::PopID();
                ImDrawList* d = ImGui::GetWindowDrawList();
                if (hov)
                    d->AddRectFilled(a, ImVec2(a.x + cw, a.y + ch), raised_color(), px(10));
                if (on)
                    d->AddRect(a, ImVec2(a.x + cw, a.y + ch), g_pal->focus, px(10), 0, px(2));
                const ImVec2 pa(a.x + pad, a.y + pad);
                const ImVec2 pb(a.x + cw - pad, pa.y + ph);
                d->AddRectFilled(pa, pb, p.bg, px(6));
                d->AddRect(pa, pb, g_pal->border, px(6));
                d->AddRectFilled(ImVec2(pa.x + px(7), pa.y + px(7)), ImVec2(pa.x + px(7) + px(44), pa.y + px(11)), p.term_fg,
                                 px(2));
                // The theme's ansi table, else libvterm's defaults (what its terminals show).
                const auto ansi = [&](int i) {
                    static constexpr ImU32 kVterm[] = {IM_COL32(0, 0, 0, 255),     IM_COL32(224, 0, 0, 255),
                                                       IM_COL32(0, 224, 0, 255),   IM_COL32(224, 224, 0, 255),
                                                       IM_COL32(0, 0, 224, 255),   IM_COL32(224, 0, 224, 255)};
                    return p.ansi != nullptr ? (*p.ansi)[static_cast<std::size_t>(i)] : kVterm[i];
                };
                const ImU32 dots[] = {p.focus, ansi(1), ansi(2), ansi(3), ansi(5)};
                float dx = pa.x + px(7);
                const float dy = pa.y + px(7) + px(4) + px(6);
                for (const ImU32 c : dots) {
                    d->AddCircleFilled(ImVec2(dx + px(4), dy + px(4)), px(4), c);
                    dx += px(8) + px(4);
                }
                const ImVec2 lp(a.x + pad, pb.y + px(6));
                const float tw = text_clipped(d, s.fonts.ui, fpx(12.5f), lp, on ? g_pal->text : g_pal->dim, p.label,
                                              cw - pad * 2 - (on ? px(17) : 0.0f));
                if (on)
                    draw_icon(s, d, ui::icon::kCheck, fpx(12), ImVec2(lp.x + tw + px(5) + px(6), lp.y + line_h(12.5f) * 0.5f),
                              g_pal->focus);
            }
            tf.end();
        }
        settings_gap(px(18));  // main.js 530
        settings_section(s, "Size and fonts");
        // main.js 531-543: the Zoom stepper and a Reset button.
        settings_row(s, "Zoom", "Everything: terminals, panels, text.",
                     px(kStepperW) + px(8) + settings_button_size(s, "Reset").x, px(kStepperH));
        if (stepper(s, "zoom", s.settings.zoom, 0.1f, 0.5f, 3.0f, "%.0f%%", 100.0f)) {
            g_scale = s.dpi * ui_scale(s.settings);
            apply_style(s.dpi, ui_scale(s.settings));
            changed = true;
        }
        ImGui::SameLine(0, px(8));
        if (settings_button(s, "##st-zoom-reset", "Reset"))
            run_command(s, "app.zoom_reset");
        settings_row_done(s);
        settings_row(s, "Interface text", "Sidebar, tabs, menus and panels, at 100% zoom.", px(kStepperW), px(kStepperH));
        if (stepper(s, "uitext", s.settings.ui_text, 1.0f, 11.0f, 24.0f, "%.0f px")) {
            g_scale = s.dpi * ui_scale(s.settings);
            apply_style(s.dpi, ui_scale(s.settings));
            changed = true;
        }
        settings_row_done(s);
        {
            // main.js 500-512 fontRow(): the font button (220 wide), gap 8, the size stepper
            // with the bare number (`${Math.round(sizeNow)}`, 437 / 511).
            settings_row(s, "Terminal font", nullptr, px(220) + px(8) + px(kStepperW), px(kStepperH));
            if (font_picker(s, "##tfont", s.settings.terminal_font, "Default (JetBrains Mono)", px(220))) {
                apply_terminal_font(s);
                changed = true;
            }
            ImGui::SameLine(0, px(8));
            if (stepper(s, "tsize", s.settings.font_size, 1.0f, 8.0f, 32.0f, "%.0f"))
                changed = true;
            settings_row_done(s);
            // herdr settings main.js:546-548: a 100-900 stepper (step 100); the family's nearest
            // face is used (FontFamily::file_for).
            settings_row(s, "Terminal font weight", "Bold text stays at 700.", px(kStepperW), px(kStepperH));
            float weight = static_cast<float>(s.settings.terminal_weight);
            if (stepper(s, "tweight", weight, 100.0f, 100.0f, 900.0f, "%.0f")) {
                s.settings.terminal_weight = static_cast<int>(weight);
                apply_terminal_font(s);
                changed = true;
            }
            settings_row_done(s);
        }
        settings_row(s, "Terminal line height", nullptr, px(kStepperW), px(kStepperH));
        changed |= stepper(s, "lh", s.settings.line_height, 0.1f, 0.8f, 2.0f, "%.0f%%", 100.0f);
        settings_row_done(s);
        settings_row(s, "Terminal cell width", nullptr, px(kStepperW), px(kStepperH));
        changed |= stepper(s, "cw", s.settings.cell_width, 0.05f, 0.8f, 2.0f, "%.0f%%", 100.0f);
        settings_row_done(s);
        settings_row(s, "Terminal scroll speed", nullptr, px(kStepperW), px(kStepperH));
        changed |= stepper(s, "ss", s.settings.scroll_speed, 0.1f, 0.1f, 5.0f, "%.0f%%", 100.0f);
        settings_row_done(s);
        settings_row(s, "Interface font", nullptr, px(220), ImGui::GetFrameHeight());
        if (font_picker(s, "##uifont", s.settings.ui_font, "Default (Geist)", px(220))) {
            apply_ui_font(s);
            changed = true;
        }
        settings_row_done(s);
        settings_gap(px(18));
        settings_section(s, "Terminals");
        {
            const auto profiles = s.host->profiles();
            const std::string cur = pick_shell(s.settings.shell_profile, profiles);
            std::string label = cur.empty() ? std::string("(none found)") : cur;
            for (const auto& [id, pname] : profiles)
                if (id == cur)
                    label = pname;
            settings_row(s, "Shell", "New spaces, tabs and splits open it in the focused folder.", px(220),
                         ImGui::GetFrameHeight());
            if (ImGui::BeginCombo("##shell", label.c_str())) {
                for (const auto& [id, pname] : profiles)
                    if (ImGui::Selectable(pname.c_str(), id == cur)) {
                        s.settings.shell_profile = id;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            settings_row_done(s);
        }
        // On and off as herdr's settings switch it (main.js 225-238 toggle()).
        settings_row(s, "Copy on select", "Copy the selection when the mouse is released.", px(36), px(20));
        if (settings_toggle("##cos", s.settings.copy_on_select)) {
            s.settings.copy_on_select = !s.settings.copy_on_select;
            changed = true;
        }
        settings_row_done(s);
        settings_row(s, "Keys", "keys.json: change any shortcut.", settings_button_size(s, "Edit keys.json").x);
        if (settings_button(s, "##keys", "Edit keys.json"))
            run_command(s, "app.keys");
        settings_row_done(s);
        for (const auto& e : s.keymap_errors) {
            const std::string msg = "keys.json: cannot use \"" + e + "\"";
            settings_wrapped(s, msg.c_str(), g_pal->needs_you);
        }
        {
            static char buf[512];
            if (ImGui::IsWindowAppearing() || buf[0] == '\0')
                std::snprintf(buf, sizeof buf, "%s", s.settings.worktree_dir.c_str());
            settings_row(s, "Worktrees folder", "Empty: %USERPROFILE%\\.agentdesk\\worktrees", px(260),
                         ImGui::GetFrameHeight());
            if (ImGui::InputText("##wt", buf, sizeof buf)) {
                s.settings.worktree_dir = buf;
                changed = true;
            }
            settings_row_done(s);
        }
    } else if (sec == "Machines") {
        // main.js 725-736 machines(): the heading, the note (pb 8), a "This computer" row with a
        // green 7 px dot, a row per machine, then (pt 16) an "Add a machine" button.
        settings_section(s, "Machines");
        settings_wrapped(s, "Your computers, reached over ssh. Their terminals keep running when you disconnect.",
                         g_pal->faint);
        settings_gap(px(8));
        {
            // main.js 730: "Connected · hc <version>" (ours: agent-host).
            const std::string v = s.host->host_version();
            const std::string note = "Connected \xC2\xB7 " + (v.empty() ? std::string("agent-host version unknown")
                                                                         : "agent-host " + v);
            settings_row(s, "This computer", note.c_str(), px(7), px(7));
            const ImVec2 dp = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(dp.x + px(3.5f), dp.y + px(3.5f)), px(3.5f), g_pal->done);
            settings_row_done(s);
        }
        // herdr settings main.js machineRow(): Rename and Remove; both ask inline first.
        static std::string edit_machine;   // the row being edited
        static std::string edit_action;    // "rename" or "rm"
        static char rename_buf[128] = {};
        static bool focus_rename = false;
        static std::string machine_notice;
        if (ImGui::IsWindowAppearing()) {
            edit_machine.clear();
            machine_notice.clear();
        }
        int remove = -1;
        int rename = -1;
        std::string new_name;
        for (int i = 0; i < static_cast<int>(s.settings.machines.size()); ++i) {
            const auto& m = s.settings.machines[static_cast<std::size_t>(i)];
            ImGui::PushID(i);
            // main.js 637-662: py 12, gap 8, a bottom border; the head row (gap 10): a 7 px dot
            // (green connected, working while it connects, else blocked), the name 14 px over
            // "target · state · version" 12.5 px dim, then Rename and Remove (gap 6). The version
            // is left out while it is not known.
            const std::string state = s.machine_state(m.name);
            const ImU32 dot = state == "connected"    ? g_pal->done
                              : state == "connecting" ? g_pal->working
                                                      : g_pal->needs_you;
            const std::string version = state == "connected" ? s.host_for(m.name)->host_version() : std::string();
            const std::string sub =
                m.target + " \xC2\xB7 " + state + (version.empty() ? std::string() : " \xC2\xB7 agent-host " + version);
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const float rw = ImGui::GetContentRegionAvail().x;
            ImDrawList* d = ImGui::GetWindowDrawList();
            const ImVec2 bsz_rename = settings_button_size(s, "Rename");
            const ImVec2 bsz_remove = settings_button_size(s, "Remove");
            const float text_h = line_h(14) + px(3) + line_h(12.5f);
            const float head_h = std::max(text_h, bsz_rename.y);
            const float cy = a.y + px(12) + head_h * 0.5f;
            d->AddCircleFilled(ImVec2(a.x + px(3.5f), cy), px(3.5f), dot);
            const float tx = a.x + px(7) + px(10);
            const float bx = a.x + rw - bsz_remove.x - px(6) - bsz_rename.x;
            const float ty = cy - text_h * 0.5f;
            text_clipped(d, s.fonts.ui, fpx(14), ImVec2(tx, ty), g_pal->text, m.name, bx - px(10) - tx);
            text_clipped(d, s.fonts.ui, fpx(12.5f), ImVec2(tx, ty + line_h(14) + px(3)), g_pal->faint, sub,
                         bx - px(10) - tx);
            ImGui::SetCursorScreenPos(ImVec2(bx, cy - bsz_rename.y * 0.5f));
            if (settings_button(s, "##rename-btn", "Rename")) {
                edit_machine = m.name;
                edit_action = "rename";
                std::snprintf(rename_buf, sizeof rename_buf, "%s", m.name.c_str());
                machine_notice.clear();
                focus_rename = true;
            }
            ImGui::SameLine(0, px(6));
            if (settings_button(s, "##remove-btn", "Remove")) {
                edit_machine = m.name;
                edit_action = "rm";
                machine_notice.clear();
            }
            float y = a.y + px(12) + head_h;
            auto line = [&](const std::string& text, ImU32 color, float size) {
                y += px(8);
                d->AddText(s.fonts.ui, fpx(size), ImVec2(a.x, y), color, text.c_str());
                y += line_h(size);
            };
            if (edit_machine == m.name && edit_action == "rm") {
                // main.js 676-678: the question (12.5 muted), then Cancel and "Remove machine".
                line("Remove " + m.name + "? Its terminals keep running there.", g_pal->dim, 12.5f);
                y += px(8);
                ImGui::SetCursorScreenPos(ImVec2(a.x, y));
                if (settings_button(s, "##rm-cancel", "Cancel"))
                    edit_machine.clear();
                ImGui::SameLine(0, px(6));
                if (settings_button(s, "##rm-confirm", "Remove machine"))
                    remove = i;
                y += bsz_rename.y;
            } else if (edit_machine == m.name && edit_action == "rename") {
                // main.js 679-683: "New name (...)" 12 px dim, then (gap 6) the input (h 32, px 10,
                // rounded 7, accent border while focused, background, 13 px), Cancel and Save.
                line("New name (addresses using the old name stop working)", g_pal->faint, 12);
                y += px(6);  // gap 6 under the label
                ImGui::SetCursorScreenPos(ImVec2(a.x, y));
                const float buttons = settings_button_size(s, "Cancel").x + px(6) + settings_button_size(s, "Save").x;
                ImGui::SetNextItemWidth(std::max(px(120), rw - buttons - px(6)));
                if (std::exchange(focus_rename, false))
                    ImGui::SetKeyboardFocusHere();
                ImGui::PushFont(s.fonts.ui, 13);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), (px(32) - ImGui::GetFontSize()) * 0.5f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(7));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(g_pal->bg));
                const bool enter = ImGui::InputText("##rename", rename_buf, sizeof rename_buf,
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(2);
                ImGui::PopFont();
                d->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                           ImGui::IsItemActive() ? g_pal->focus : g_pal->border, px(7));
                const float iy = ImGui::GetItemRectMin().y;
                ImGui::SameLine(0, px(6));
                ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, iy + (px(32) - bsz_rename.y) * 0.5f));
                if (settings_button(s, "##rn-cancel", "Cancel"))
                    edit_machine.clear();
                ImGui::SameLine(0, px(6));
                if (settings_button(s, "##rn-save", "Save") || enter) {
                    const std::string name = trimmed(rename_buf);
                    const bool taken = std::any_of(s.settings.machines.begin(), s.settings.machines.end(),
                                                   [&](const MachineConfig& o) { return o.name == name; });
                    if (!name.empty() && name != m.name && !taken) {
                        rename = i;
                        new_name = name;
                    } else if (name == m.name || name.empty()) {
                        edit_machine.clear();
                    }
                }
                y = iy + px(32);
            }
            // main.js 685: what went wrong, 12.5 px in the blocked colour.
            if (state != "connected")
                for (const auto& r : s.remotes)
                    if (r.cfg.name == m.name && !r.host->last_error().empty())
                        line(r.host->last_error(), g_pal->needs_you, 12.5f);
            y += px(12);
            d->AddLine(ImVec2(a.x, y - 0.5f), ImVec2(a.x + rw, y - 0.5f), g_pal->border);
            ImGui::SetCursorScreenPos(ImVec2(a.x, y));
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::PopID();
        }
        // main.js 732-734: "No other machines yet." (py 14) and the notice (py 10), 12.5 px.
        if (s.settings.machines.empty()) {
            settings_gap(px(14));
            settings_wrapped(s, "No other machines yet.", g_pal->faint, 12.5f, 12.5f * 1.3f);
            settings_gap(px(14));
        }
        if (!machine_notice.empty()) {
            settings_gap(px(10));
            settings_wrapped(s, machine_notice.c_str(), g_pal->done, 12.5f, 12.5f * 1.3f);
            settings_gap(px(10));
        }
        if (remove >= 0) {
            machine_notice = "Removed " + s.settings.machines[static_cast<std::size_t>(remove)].name +
                             "; its terminals keep running there.";
            s.settings.machines.erase(s.settings.machines.begin() + remove);
            edit_machine.clear();
            changed = true;
            start_remotes(s);
        }
        if (rename >= 0) {
            auto& m = s.settings.machines[static_cast<std::size_t>(rename)];
            const std::string old = m.name;
            m.name = new_name;
            for (auto& sp : s.ws.spaces)
                if (sp.machine == old)
                    sp.machine = new_name;
            for (auto& f : s.settings.folded_machines)
                if (f == old)
                    f = new_name;
            machine_notice = "Renamed " + old + " to " + new_name + ".";
            edit_machine.clear();
            s.doc_dirty = true;
            s.doc_dirty_at = Clock::now();
            changed = true;
            start_remotes(s);
        }
        // main.js 736: pt 16, a settings button.
        settings_gap(px(16));
        if (settings_button(s, "##st-m-add", "Add a machine"))
            s.show_machine_add = true;
    } else if (sec == "Alerts") {
        // main.js 743-757 alerts(): Sounds; Play sounds (a toggle); the two sounds with Play; the
        // footnote. No other section: a note shows in the corner either way.
        settings_section(s, "Sounds");
        settings_row(s, "Play sounds",
                     "When an agent needs you, and when one finishes while you're not looking. A note shows in the "
                     "corner either way.",
                     px(36), px(20));
        if (settings_toggle("##st-sounds", s.settings.sounds)) {
            s.settings.sounds = !s.settings.sounds;
            changed = true;
        }
        settings_row_done(s);
        auto sound_row = [&](const char* label, const std::string& file, bool blocked) {
            // herdr settings main.js:743: the file's name, not its path.
            const auto slash = file.find_last_of("\\/");
            const std::string name = slash == std::string::npos ? file : file.substr(slash + 1);
            settings_row(s, label, file.empty() ? "AgentDesk's sound" : name.c_str(), settings_button_size(s, "Play").x);
            if (settings_button(s, blocked ? "##st-play-blocked" : "##st-play-done", "Play"))
                play_alert(s, blocked);
            settings_row_done(s);
        };
        sound_row("An agent needs you", s.settings.sound_blocked, true);
        sound_row("An agent finished", s.settings.sound_done, false);
        // main.js 757, with our file and keys (ui.json sound_done / sound_blocked).
        settings_footnote(s, "Want your own sounds? Ask your agent to set sound_done and sound_blocked in ui.json to "
                             "an MP3.");
    } else if (sec == "Mods") {
        // Ours (C++ DLL mods), laid out like herdr's pages: heading, note, rows with toggles.
        settings_section(s, "Installed");
        settings_wrapped(s,
                         "Mods are C++ DLLs in the mods folder (one folder per mod, mod.dll). They can react to "
                         "agents, add commands, panels and tab text, and start programs. A mod runs with your "
                         "rights like any program you install, so only turn on mods you trust; one that crashes "
                         "is turned off. The sdk folder of the zip has the headers and the examples' sources.",
                         g_pal->faint);
        settings_gap(px(8));
        for (const auto& m : s.mods->mods()) {
            ImGui::PushID(m.name.c_str());
            const bool on = s.settings.mods_enabled.contains(m.name);
            settings_row(s, m.name.c_str(), m.description.c_str(), px(36), px(20));
            if (settings_toggle("##on", on)) {
                if (!on)
                    s.settings.mods_enabled.insert(m.name);
                else
                    s.settings.mods_enabled.erase(m.name);
                changed = true;
                s.mods->load(s.settings.mods_enabled);
            }
            settings_row_done(s);
            if (!m.error.empty()) {
                settings_gap(px(6));
                settings_wrapped(s, m.error.c_str(), g_pal->needs_you);
            }
            ImGui::PopID();
        }
        if (s.mods->mods().empty()) {
            settings_gap(px(14));
            settings_wrapped(s, "No mods yet.", g_pal->faint, 12.5f, 12.5f * 1.3f);
        }
        settings_gap(px(16));
        if (settings_button(s, "##mods-reload", "Reload"))
            run_command(s, "mods.reload");
        ImGui::SameLine(0, px(8));
        if (settings_button(s, "##mods-folder", "Open mods folder"))
            run_command(s, "mods.folder");
    } else if (sec == "Updates") {
        close = settings_updates(s) || close;
    } else if (sec == "Privacy") {
        changed |= settings_privacy(s);
    }
    ImGui::PopStyleVar();  // ItemSpacing
    ImGui::EndChild();
    if (changed)
        save_settings(s.settings);
    if (close || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ------------------------------------------------------------------ sounds (herdr sound.rs)
// Bundled done.mp3 / request.mp3 (or the person's own MP3), played through MCI on a thread with a
// unique alias so sounds can overlap.
void play_alert(App::State& s, bool blocked) {
    std::filesystem::path file;
    const std::string custom = blocked ? s.settings.sound_blocked : s.settings.sound_done;
    std::error_code ec;
    if (!custom.empty() && std::filesystem::is_regular_file(win::to_wide(custom), ec))
        file = win::to_wide(custom);
    if (file.empty()) {
        wchar_t buf[MAX_PATH]{};
        const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
        file = std::filesystem::path(std::wstring(buf, n)).parent_path() / L"sounds" /
               (blocked ? L"request.mp3" : L"done.mp3");
    }
    if (!std::filesystem::is_regular_file(file, ec)) {
        ::PlaySoundW(blocked ? L"SystemExclamation" : L"SystemAsterisk", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
        return;
    }
    static std::atomic<int> counter{0};
    std::thread([file, id = ++counter] {
        const std::wstring alias = L"agentdesk" + std::to_wstring(id);
        const std::wstring open = L"open \"" + file.native() + L"\" type mpegvideo alias " + alias;
        if (::mciSendStringW(open.c_str(), nullptr, 0, nullptr) == 0) {
            ::mciSendStringW((L"play " + alias + L" wait").c_str(), nullptr, 0, nullptr);
            ::mciSendStringW((L"close " + alias).c_str(), nullptr, 0, nullptr);
        }
    }).detach();
}

// ------------------------------------------------------------------ welcome (herdr welcome.js)
// First run: 4 pages, dots, Back / Next; Esc skips, Enter/Right next, Left back.
void draw_welcome(App::State& s) {
    if (s.show_welcome) {
        ImGui::OpenPopup("##welcome");
        s.show_welcome = false;
        s.welcome_page = 0;
        s.welcome_pos = ImVec2(-1.0f, -1.0f);
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = std::min(px(660), vp->WorkSize.x - px(16));
    const float h = std::min(px(600), vp->WorkSize.y - px(16));
    if (s.welcome_pos.x < 0.0f)
        s.welcome_pos = ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + (vp->WorkSize.y - h) * 0.5f);
    // Keep the header reachable when the window shrinks.
    s.welcome_pos.x = std::clamp(s.welcome_pos.x, vp->WorkPos.x - w + px(80), vp->WorkPos.x + vp->WorkSize.x - px(80));
    s.welcome_pos.y = std::clamp(s.welcome_pos.y, vp->WorkPos.y, vp->WorkPos.y + vp->WorkSize.y - px(52));
    ImGui::SetNextWindowPos(s.welcome_pos);
    ImGui::SetNextWindowSize(ImVec2(w, h));
    push_overlay_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(32), px(28)));
    const bool open = ImGui::BeginPopupModal("##welcome", nullptr,
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                 ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    pop_overlay_style();
    if (!open)
        return;
    int& page = s.welcome_page;
    // Header (herdr welcome main.js:491-520): logo, "Welcome to AgentDesk", "Drag to move" and a
    // close button. Only the header drags the window, and only it shows the grab hand.
    bool skip = false;
    {
        ImDrawList* hd = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        const float hh = px(52);
        const float cy = wp.y + hh * 0.5f;
        float x = wp.x + px(20);
        const float m = px(24);
        hd->AddRectFilled(ImVec2(x, cy - m * 0.5f), ImVec2(x + m, cy + m * 0.5f), g_pal->focus, px(6));
        hd->AddRectFilled(ImVec2(x + px(6), cy - px(5)), ImVec2(x + px(17), cy - px(2)), g_pal->card, px(1));
        hd->AddRectFilled(ImVec2(x + px(6), cy + px(2)), ImVec2(x + px(13), cy + px(5)), g_pal->card, px(1));
        x += m + px(10);
        const char* kTitle = "Welcome to AgentDesk";
        const ImVec2 tt = s.fonts.ui_bold->CalcTextSizeA(fpx(14), FLT_MAX, 0, kTitle);
        hd->AddText(s.fonts.ui_bold, fpx(14), ImVec2(x, cy - tt.y * 0.5f), g_pal->text, kTitle);
        const float close_x = wp.x + w - px(12) - px(26);
        const ImVec2 dt = s.fonts.ui->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, "Drag to move");
        hd->AddText(s.fonts.ui, fpx(11.5f), ImVec2(close_x - px(10) - dt.x, cy - dt.y * 0.5f), g_pal->faint, "Drag to move");
        ImGui::SetCursorScreenPos(ImVec2(close_x, cy - px(13)));
        if (ImGui::InvisibleButton("##w-close", ImVec2(px(26), px(26))))
            skip = true;
        const bool close_hov = ImGui::IsItemHovered();
        if (close_hov)
            hd->AddRectFilled(ImVec2(close_x, cy - px(13)), ImVec2(close_x + px(26), cy + px(13)), g_pal->hover, px(7));
        draw_icon(s, hd, ui::icon::kClose, fpx(14), ImVec2(close_x + px(13), cy), close_hov ? g_pal->text : g_pal->dim);
        ImGui::SetCursorScreenPos(wp);
        ImGui::InvisibleButton("##w-drag", ImVec2(std::max(1.0f, close_x - px(4) - wp.x), hh));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            s.welcome_pos.x += ImGui::GetIO().MouseDelta.x;
            s.welcome_pos.y += ImGui::GetIO().MouseDelta.y;
        }
        hd->AddLine(ImVec2(wp.x, wp.y + hh), ImVec2(wp.x + w, wp.y + hh), g_pal->border);
        hd->AddLine(ImVec2(wp.x, wp.y + h - px(56)), ImVec2(wp.x + w, wp.y + h - px(56)), g_pal->border);
        // The page scrolls between the header and the footer (herdr w-body, padding 22).
        ImGui::SetCursorScreenPos(ImVec2(wp.x, wp.y + hh + 1));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(22), px(22)));
        ImGui::BeginChild("##w-body", ImVec2(w, h - hh - 1 - px(57)), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar();
    }
    // welcome/main.js 135-140 title(): 18 px semibold, then the paragraph (gap 6).
    auto title = [&](const char* t, const char* sub) {
        ImGui::PushFont(s.fonts.ui_bold, 18);
        ImGui::TextWrapped("%s", t);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, px(4)));
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
        ImGui::TextWrapped("%s", sub);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, px(14)));
    };
    // welcome/main.js 217-223 feature(): a 32 px tile (rounded 9, raised, a 1 px border) with the
    // 16 px accent icon, gap 12, then the title (13.5 semibold) over the paragraph.
    auto feature = [&](const char* glyph, const char* head, const char* text) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImDrawList* fd = ImGui::GetWindowDrawList();
        fd->AddRectFilled(a, ImVec2(a.x + px(32), a.y + px(32)), raised_color(), px(9));
        fd->AddRect(a, ImVec2(a.x + px(32), a.y + px(32)), g_pal->border, px(9));
        draw_icon(s, fd, glyph, fpx(16), ImVec2(a.x + px(16), a.y + px(16)), g_pal->focus);
        ImGui::SetCursorScreenPos(ImVec2(a.x + px(32) + px(12), a.y));
        ImGui::BeginGroup();
        ImGui::PushFont(s.fonts.ui_medium, 13.5f);
        ImGui::TextUnformatted(head);
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - px(120));
        ImGui::TextWrapped("%s", text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0, px(10)));
    };
    // herdr welcome/main.js 139-141 para(): 13 px, 20 px lines, muted, the full width. Draws at
    // `p` within `width` (draw false: only measures); returns the height.
    auto para_at = [&](ImVec2 p, float width, const char* text, bool draw = true, ImU32 color = 0) -> float {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const char* t = text;
        const char* end = text + std::strlen(text);
        float y = p.y;
        while (t < end) {
            const char* brk = s.fonts.ui->CalcWordWrapPosition(fpx(13), t, end, width);
            if (brk == t)
                brk = t + 1;
            if (draw) {
                const float th = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, t, brk).y;
                d->AddText(s.fonts.ui, fpx(13), ImVec2(p.x, y + (px(20) - th) * 0.5f), color != 0 ? color : g_pal->dim, t, brk);
            }
            y += px(20);
            t = brk;
            while (t < end && (*t == ' ' || *t == '\n'))
                ++t;
        }
        return y - p.y;
    };
    auto para = [&](const char* text) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = para_at(p, ImGui::GetContentRegionAvail().x, text);
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, h));
    };
    // A gap of `g` between blocks (the body's own item spacing taken off).
    auto gap = [&](float g) { ImGui::Dummy(ImVec2(0, std::max(0.0f, g - 2 * ImGui::GetStyle().ItemSpacing.y))); };
    bool changed = false;
    if (page == 0) {
        // welcome/main.js 284-293 pageOne (Herdr -> AgentDesk, herdr-core -> agent-host). Its
        // "Coming from herdr?" box (261-280) is for herdr's earlier app and is left out.
        title("AgentDesk runs your terminals in the background",
              "The terminals and agents you open here don't live in this window. They run in agent-host, a small "
              "background program on your computer. Close AgentDesk, update it, or let it crash, and your agents "
              "keep working. Open it again and everything is where you left it, as long as the computer is on.");
        feature(ui::icon::kBot, "Agents tell you when they need you",
                "A ring while one works, \xE2\x80\x9CNeeds you\xE2\x80\x9D when it asks something, a sound when it "
                "finishes.");
        feature(ui::icon::kMessages, "Agents can start helpers",
                "They show up under the agent that started them. Click one to watch it.");
        feature(ui::icon::kLayers, "Your other machines, in the same window",
                "A server or a Mac mini runs its own agent-host, so its agents keep going even while this laptop "
                "sleeps.");
        {  // welcome/main.js 290: a 1 px border line before Appearance and Size.
            const ImVec2 lp = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(lp, ImVec2(lp.x + ImGui::GetContentRegionAvail().x, lp.y), g_pal->border);
            ImGui::Dummy(ImVec2(0, px(10)));
        }
        // herdr welcome page 1: Appearance System / Light / Dark.
        ImGui::TextDisabled("Appearance");
        ImGui::SameLine(px(120));
        if (segmented(s, "##wappearance", s.settings.appearance,
                      {{"system", "System"}, {"light", "Light"}, {"dark", "Dark"}}))
            changed = true;
        ImGui::TextDisabled("Size");
        ImGui::SameLine(px(120));
        if (stepper(s, "wzoom", s.settings.zoom, 0.1f, 0.5f, 3.0f, "%.0f%%", 100.0f)) {
            g_scale = s.dpi * ui_scale(s.settings);
            apply_style(s.dpi, ui_scale(s.settings));
            changed = true;
        }
    } else if (page == 1) {
        title("Add your other machines",
              "A desktop at home, a server: AgentDesk reaches them over ssh, and agents there keep running while "
              "this PC sleeps.");
        // welcome/main.js 297-345: one rounded box (12, a 1 px border, the background): "This
        // computer · here", a row per machine, then an accent "Add a machine" row; under it, the
        // note. Rows (machineRow 297-308): h 42, px 12, gap 10, a bottom border; a 15 px muted
        // icon, the name (13 px), the note (12 px dim), an 8 px dot (green when connected).
        {
            ImDrawList* d = ImGui::GetWindowDrawList();
            const ImVec2 b0 = ImGui::GetCursorScreenPos();
            const float bw = ImGui::GetContentRegionAvail().x;
            const float rh = px(42);
            const int n_rows = 1 + static_cast<int>(s.settings.machines.size());
            const float bh = rh * static_cast<float>(n_rows + 1);
            d->AddRectFilled(b0, ImVec2(b0.x + bw, b0.y + bh), g_pal->bg, px(12));
            auto mrow = [&](int i, const char* glyph, const std::string& name, const std::string& note, bool ok) {
                const float y = b0.y + rh * static_cast<float>(i);
                const float cy = y + rh * 0.5f;
                draw_icon(s, d, glyph, fpx(15), ImVec2(b0.x + px(12) + px(7.5f), cy), g_pal->dim);
                const float nx = b0.x + px(12) + px(15) + px(10);
                const ImVec2 nt = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, note.c_str());
                const float dot_x = b0.x + bw - px(12) - px(8);
                const float note_x = dot_x - px(10) - nt.x;
                text_clipped(d, s.fonts.ui, fpx(13), ImVec2(nx, cy - line_h(13) * 0.5f), g_pal->text, name,
                             std::max(0.0f, note_x - px(10) - nx));
                d->AddText(s.fonts.ui, fpx(12), ImVec2(note_x, cy - nt.y * 0.5f), g_pal->faint, note.c_str());
                d->AddCircleFilled(ImVec2(dot_x + px(4), cy), px(4), ok ? g_pal->done : g_pal->faint);
                d->AddLine(ImVec2(b0.x, y + rh - 0.5f), ImVec2(b0.x + bw, y + rh - 0.5f), g_pal->border);
            };
            mrow(0, ui::icon::kMonitor, "This computer", "here", true);
            for (std::size_t i = 0; i < s.settings.machines.size(); ++i) {
                const auto& m = s.settings.machines[i];
                const std::string st = s.machine_state(m.name);
                mrow(static_cast<int>(i) + 1, ui::icon::kServer, m.name, st, st == "connected");
            }
            // The add row (330-343): accent, 13 px, a 15 px plus; hover background.
            const ImVec2 ar(b0.x, b0.y + rh * static_cast<float>(n_rows));
            ImGui::SetCursorScreenPos(ar);
            if (ImGui::InvisibleButton("##w-add-machine", ImVec2(bw, rh)))
                s.show_machine_add = true;
            if (ImGui::IsItemHovered())
                d->AddRectFilled(ar, ImVec2(ar.x + bw, ar.y + rh), g_pal->hover, px(12), ImDrawFlags_RoundCornersBottom);
            draw_icon(s, d, ui::icon::kPlus, fpx(15), ImVec2(ar.x + px(12) + px(7.5f), ar.y + rh * 0.5f), g_pal->focus);
            d->AddText(s.fonts.ui, fpx(13), ImVec2(ar.x + px(12) + px(15) + px(10), ar.y + (rh - line_h(13)) * 0.5f),
                       g_pal->focus, "Add a machine");
            d->AddRect(b0, ImVec2(b0.x + bw, b0.y + bh), g_pal->border, px(12));
            ImGui::SetCursorScreenPos(b0);
            ImGui::Dummy(ImVec2(bw, bh));
        }
        gap(px(18));
        para("Not now? Add machine is at the bottom of the sidebar too.");  // welcome/main.js 345
    } else if (page == 2) {
        // welcome/main.js 396-400.
        title("Choose your panels",
              "AgentDesk comes with a couple of panels. Turn one on and it appears beside your terminals. Drag "
              "this window aside to see it. Later, the buttons in the tab bar open and close them.");
        // herdr welcome.js 396-413: the first visit to this page starts with both off; skipping
        // the welcome leaves both on.
        if (!s.welcome_panels_seen) {
            s.welcome_panels_seen = true;
            s.settings.panels.clear();
            changed = true;
        }
        // welcome/main.js 348-394 panelCard(): p 12, gap 12, rounded 12, a 1 px border (accent
        // when on, else the input colour; the ring colour on hover), an accent tint when on; a
        // 32 px tile (rounded 9, raised, bordered) with the 16 px icon; the title (13.5
        // semibold) and a `mods/<id>` chip (px 6, rounded 5, hover background, 11 px dim), then
        // the description; a 34 x 20 switch (mt 4).
        auto toggle = [&](const char* id, const char* glyph, const char* label, const char* text) {
            bool on = std::find(s.settings.panels.begin(), s.settings.panels.end(), id) != s.settings.panels.end();
            ImDrawList* d = ImGui::GetWindowDrawList();
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const float cw = ImGui::GetContentRegionAvail().x;
            const float tx = a.x + px(12) + px(32) + px(12);
            const float text_w = cw - px(12) - px(32) - px(12) - px(12) - px(34) - px(12);
            const float body_h = para_at(ImVec2(tx, 0), text_w, text, false);
            const float inner = std::max(px(32), line_h(13.5f) + px(4) + body_h);
            const float ch = px(12) + inner + px(12);
            ImGui::PushID(id);
            const bool clicked = ImGui::InvisibleButton("##panel", ImVec2(cw, ch));
            const bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            const ImVec2 b(a.x + cw, a.y + ch);
            if (on)
                d->AddRectFilled(a, b, alpha(g_pal->focus, 0x10), px(12));
            d->AddRect(a, b, on ? g_pal->focus : hov ? g_pal->dim : g_pal->border, px(12));
            const ImVec2 t0(a.x + px(12), a.y + px(12));
            d->AddRectFilled(t0, ImVec2(t0.x + px(32), t0.y + px(32)), raised_color(), px(9));
            d->AddRect(t0, ImVec2(t0.x + px(32), t0.y + px(32)), g_pal->border, px(9));
            draw_icon(s, d, glyph, fpx(16), ImVec2(t0.x + px(16), t0.y + px(16)), g_pal->text);
            const ImVec2 lt = s.fonts.ui_bold->CalcTextSizeA(fpx(13.5f), FLT_MAX, 0, label);
            const float ly = a.y + px(12);
            d->AddText(s.fonts.ui_bold, fpx(13.5f), ImVec2(tx, ly + (line_h(13.5f) - lt.y) * 0.5f), g_pal->text, label);
            const std::string chip = std::string("mods/") + id;
            const ImVec2 ct = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, chip.c_str());
            const ImVec2 c0(tx + lt.x + px(8), ly + (line_h(13.5f) - ct.y - px(1)) * 0.5f);
            d->AddRectFilled(c0, ImVec2(c0.x + ct.x + px(12), c0.y + ct.y + px(1)), g_pal->hover, px(5));
            d->AddText(s.fonts.ui, fpx(11), ImVec2(c0.x + px(6), c0.y + px(0.5f)), g_pal->faint, chip.c_str());
            para_at(ImVec2(tx, ly + line_h(13.5f) + px(4)), text_w, text);
            const ImVec2 sw(b.x - px(12) - px(34), a.y + px(12) + px(4));
            d->AddRectFilled(sw, ImVec2(sw.x + px(34), sw.y + px(20)), on ? g_pal->focus : g_pal->border, px(10));
            d->AddCircleFilled(ImVec2(on ? sw.x + px(34) - px(10) : sw.x + px(10), sw.y + px(10)), px(8),
                               on ? accent_foreground() : g_pal->dim);
            gap(px(14));
            if (clicked) {
                on = !on;
                if (on)
                    s.settings.panels.push_back(id);
                else
                    s.settings.panels.erase(std::remove(s.settings.panels.begin(), s.settings.panels.end(), id),
                                            s.settings.panels.end());
                changed = true;
                // herdr togglePanel(): turning one on shows it at once; turning off the one shown
                // (or the last one) hides the panel.
                if (auto* sp = s.ws.active()) {
                    if (on)
                        sp->panel = id;
                    else if (sp->panel == id || s.settings.panels.empty())
                        sp->panel.clear();
                    s.doc_dirty = true;
                    s.doc_dirty_at = Clock::now();
                }
            }
        };
        // welcome/main.js 18-21 PANELS.
        toggle("files", ui::icon::kFile, "Files", "The folder you're in as a tree. Open a file to read it, highlighted.");
        toggle("changes", ui::icon::kChanges, "Changes",
               "What your agents changed: every file with its diff, and a commit box.");
        // welcome/main.js 403-411: p 12, rounded 10, raised.
        {
            const char* kBoard = "Ask your agent for a board of your agents, your pull requests, a cost meter, or the "
                                 "sidebar on the right. Not sure what's possible? Ask it that too.";
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const float cw = ImGui::GetContentRegionAvail().x;
            const float th = para_at(ImVec2(a.x + px(12), 0), cw - px(24), kBoard, false);
            ImDrawList* d = ImGui::GetWindowDrawList();
            d->AddRectFilled(a, ImVec2(a.x + cw, a.y + th + px(24)), raised_color(), px(10));
            para_at(ImVec2(a.x + px(12), a.y + px(12)), cw - px(24), kBoard);
            ImGui::Dummy(ImVec2(cw, th + px(24)));
        }
    } else {
        // welcome/main.js 416-436: first, while a key opens the palette, "One key opens
        // everything" (p 12, gap 12, rounded 10, an accent border and tint): the key in a chip
        // (px 8, py 3, rounded 6, bordered, raised, 13 px semibold), the title (13.5 semibold)
        // and its line. Then (gap 16) the rest of the page.
        if (const std::string pal = s.keymap.chord_for("app.palette"); !pal.empty()) {
            const char* kPal = "It opens the command palette, with every action by name and its key. It works the same "
                               "in every layout.";
            ImDrawList* d = ImGui::GetWindowDrawList();
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const float cw = ImGui::GetContentRegionAvail().x;
            const ImVec2 kt = s.fonts.ui_bold->CalcTextSizeA(fpx(13), FLT_MAX, 0, pal.c_str());
            const ImVec2 ks(kt.x + px(16) + 2.0f, line_h(13) + px(6) + 2.0f);
            const float tx = a.x + px(12) + ks.x + px(12);
            const float tw = a.x + cw - px(12) - tx;
            const float body = para_at(ImVec2(tx, 0), tw, kPal, false);
            const float inner = std::max(ks.y, line_h(13.5f) + px(2) + body);
            const ImVec2 b(a.x + cw, a.y + px(12) + inner + px(12));
            d->AddRectFilled(a, b, alpha(g_pal->focus, 0x10), px(10));
            d->AddRect(a, b, g_pal->focus, px(10));
            const ImVec2 k0(a.x + px(12), a.y + px(12) + (inner - ks.y) * 0.5f);
            d->AddRectFilled(k0, ImVec2(k0.x + ks.x, k0.y + ks.y), raised_color(), px(6));
            d->AddRect(k0, ImVec2(k0.x + ks.x, k0.y + ks.y), g_pal->border, px(6));
            d->AddText(s.fonts.ui_bold, fpx(13), ImVec2(k0.x + (ks.x - kt.x) * 0.5f, k0.y + (ks.y - kt.y) * 0.5f),
                       g_pal->text, pal.c_str());
            const float ty = a.y + px(12) + (inner - (line_h(13.5f) + px(2) + body)) * 0.5f;
            d->AddText(s.fonts.ui_bold, fpx(13.5f), ImVec2(tx, ty), g_pal->text, "One key opens everything");
            para_at(ImVec2(tx, ty + line_h(13.5f) + px(2)), tw, kPal);
            ImGui::Dummy(ImVec2(cw, b.y - a.y));
            gap(px(16));
        }
        title("Make it yours", "Everything AgentDesk does is a command.");
        ImGui::Text("Press %s for the command palette, %s to jump to a space or agent.",
                    s.keymap.chord_for("app.palette").c_str(), s.keymap.chord_for("app.picker").c_str());
        ImGui::Dummy(ImVec2(0, px(6)));
        ImGui::TextWrapped("Change any shortcut in keys.json, the look in Settings (%s), and add C++ mods in the mods "
                           "folder. agentctl drives AgentDesk from a terminal or from your agents.",
                           s.keymap.chord_for("app.settings").c_str());
        // herdr page 4: a line to give your agent so it learns to drive the app.
        ImGui::Dummy(ImVec2(0, px(8)));
        ImGui::TextDisabled("Tell your agent:");
        static constexpr const char* kAsk =
            "Run `agentctl --skill` and read it, then use agentctl to start, watch and report to other agents here.";
        ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(raised_color()));
        ImGui::BeginChild("##ask", ImVec2(-1, px(52)), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPos(ImVec2(px(10), px(8)));
        ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - px(80));
        ImGui::TextUnformatted(kAsk);
        ImGui::PopTextWrapPos();
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - px(72), px(12)));
        if (ImGui::Button(s.welcome_copied ? "Copied" : "Copy", ImVec2(px(62), px(26)))) {
            ImGui::SetClipboardText(kAsk);
            s.welcome_copied = true;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        // herdr welcome/main.js 451-459: "More examples in Settings" (p 12, gap 10, rounded 10,
        // raised), naming Herdr 98.
        ImGui::Dummy(ImVec2(0, px(8)));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(raised_color()));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(10));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(12)));
        ImGui::BeginChild("##examples", ImVec2(-1, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::PushFont(s.fonts.ui_bold, 13.5f);
        ImGui::TextUnformatted("More examples in Settings");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, px(2)));
        ImGui::TextWrapped("Settings \xE2\x80\xBA Look has more layouts to try or start from, AgentDesk 98 among them. Your agent can "
                           "copy any of them and change anything.");
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        gap(px(16));
        para("Something off or missing? Settings \xE2\x80\xBA Send feedback. We read every one.");  // welcome/main.js 461
    }
    ImGui::Dummy(ImVec2(0, px(20)));
    ImGui::EndChild();
    // Footer: dots, Back / Next.
    const float fy = ImGui::GetWindowPos().y + h - px(56) + px(12);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < 4; ++i)
        dl->AddCircleFilled(ImVec2(ImGui::GetWindowPos().x + px(36) + static_cast<float>(i) * px(14), fy + px(15)), px(3.5f),
                            i == page ? g_pal->focus : g_pal->border);
    const char* next = page < 3 ? (page == 1 && s.settings.machines.empty() ? "Not now" : "Next")
                                : (s.ws.spaces.empty() ? "Open a space" : "Done");
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + w - px(32) - px(220), fy));
    bool back = false, fwd = false;
    if (page > 0) {
        ImGui::PushStyleColor(ImGuiCol_Button, v4(raised_color()));
        back = ImGui::Button("Back", ImVec2(px(100), px(30)));
        ImGui::PopStyleColor();
        ImGui::SameLine(0, px(12));
    } else {
        ImGui::Dummy(ImVec2(px(100), px(30)));
        ImGui::SameLine(0, px(12));
    }
    primary_button_style(true);
    fwd = ImGui::Button(next, ImVec2(px(108), px(30)));
    primary_button_style(false);
    const bool typing = ImGui::GetIO().WantTextInput;
    // herdr: Enter or Right goes on; on the last page only Enter finishes.
    if (!typing && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && page < 3)))
        fwd = true;
    if (!typing && ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && page > 0)
        back = true;
    if (back)
        --page;
    if (fwd) {
        if (page < 3) {
            ++page;
        } else {
            s.settings.welcomed = "finished";
            changed = true;
            ImGui::CloseCurrentPopup();
            if (s.ws.spaces.empty())
                run_command(s, "space.new");
        }
    }
    if (skip || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        s.settings.welcomed = "skipped";
        changed = true;
        ImGui::CloseCurrentPopup();
    }
    if (changed)
        save_settings(s.settings);
    ImGui::EndPopup();
}

// ------------------------------------------------------------------ add a machine (herdr machine-add)
// Port of herdr defaults/mods/machine-add/main.js parseConfig/includeFiles: named Host entries (no
// patterns), "Key value" or "Key=value", a Match block belongs to no host, Include followed 16
// deep (~/ is home, relative is under ~/.ssh, * ? [ match files).
std::vector<std::pair<std::string, std::string>> ssh_config_hosts() {
    struct Host {
        std::string alias;
        std::vector<std::string> aliases;
        std::string hostname, user;
    };
    std::vector<Host> hosts;
    wchar_t home_buf[MAX_PATH]{};
    if (::GetEnvironmentVariableW(L"USERPROFILE", home_buf, MAX_PATH) == 0)
        return {};
    const std::filesystem::path home(home_buf);
    auto include_files = [&](const std::string& value) {
        std::vector<std::filesystem::path> files;
        std::vector<std::string> words;
        for (std::size_t i = 0; i < value.size();) {
            while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i])))
                ++i;
            if (i >= value.size())
                break;
            std::size_t j = i;
            if (value[i] == '"') {
                j = value.find('"', i + 1);
                words.push_back(value.substr(i + 1, (j == std::string::npos ? value.size() : j) - i - 1));
                i = j == std::string::npos ? value.size() : j + 1;
                continue;
            }
            while (j < value.size() && !std::isspace(static_cast<unsigned char>(value[j])))
                ++j;
            words.push_back(value.substr(i, j - i));
            i = j;
        }
        for (const auto& w : words) {
            std::filesystem::path p;
            if (w.rfind("~/", 0) == 0 || w.rfind("~\\", 0) == 0)
                p = home / win::to_wide(w.substr(2));
            else if (std::filesystem::path(win::to_wide(w)).is_absolute() || w.rfind("/", 0) == 0)
                p = win::to_wide(w);
            else
                p = home / L".ssh" / win::to_wide(w);
            p.make_preferred();
            if (w.find_first_of("*?[") == std::string::npos) {
                files.push_back(p);
                continue;
            }
            // A pattern in the file name: the files it matches, in name order.
            WIN32_FIND_DATAW fd{};
            const HANDLE h = ::FindFirstFileW(p.c_str(), &fd);
            std::vector<std::filesystem::path> found;
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                        found.push_back(p.parent_path() / fd.cFileName);
                } while (::FindNextFileW(h, &fd));
                ::FindClose(h);
            }
            std::sort(found.begin(), found.end());
            files.insert(files.end(), found.begin(), found.end());
        }
        return files;
    };
    auto known = [&](const std::string& name) {
        return std::any_of(hosts.begin(), hosts.end(), [&](const Host& x) {
            return x.alias == name || std::find(x.aliases.begin(), x.aliases.end(), name) != x.aliases.end();
        });
    };
    std::function<void(const std::filesystem::path&, int)> parse = [&](const std::filesystem::path& file, int depth) {
        std::ifstream in(file, std::ios::binary);
        if (!in)
            return;
        int cur = -1;  // index into hosts; -1: lines belong to no pickable host
        for (std::string raw; std::getline(in, raw);) {
            const std::string line = trimmed(raw);
            // ^([^\s#=]+)\s*=?\s*(.*)$
            std::size_t k = 0;
            while (k < line.size() && !std::isspace(static_cast<unsigned char>(line[k])) && line[k] != '#' && line[k] != '=')
                ++k;
            if (k == 0)
                continue;
            std::string key = line.substr(0, k);
            for (auto& c : key)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::size_t v = k;
            while (v < line.size() && std::isspace(static_cast<unsigned char>(line[v])))
                ++v;
            if (v < line.size() && line[v] == '=')
                ++v;
            const std::string value = trimmed(line.substr(v));
            if (key == "host" || key == "match") {
                std::vector<std::string> names;
                if (key == "host") {
                    std::stringstream ss(value);
                    for (std::string h; ss >> h;)
                        if (h.find_first_of("*?!") == std::string::npos && !known(h) &&
                            std::find(names.begin(), names.end(), h) == names.end())
                            names.push_back(h);
                }
                cur = -1;
                if (!names.empty()) {
                    hosts.push_back({names[0], std::vector<std::string>(names.begin() + 1, names.end()), {}, {}});
                    cur = static_cast<int>(hosts.size()) - 1;
                }
            } else if (key == "include" && depth < 16) {
                for (const auto& f : include_files(value))
                    parse(f, depth + 1);
            } else if (cur >= 0 && key == "hostname") {
                hosts[static_cast<std::size_t>(cur)].hostname = value;
            } else if (cur >= 0 && key == "user") {
                hosts[static_cast<std::size_t>(cur)].user = value;
            }
        }
    };
    try {
        parse(home / L".ssh" / L"config", 0);
    } catch (const std::exception&) {
        // a name that is not UTF-8: keep what was read
    }
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& h : hosts) {
        std::string sub = h.user.empty() ? h.hostname : h.hostname.empty() ? h.user : h.user + "@" + h.hostname;
        if (sub.empty() && !h.aliases.empty()) {
            sub = "also ";
            for (std::size_t i = 0; i < h.aliases.size(); ++i)
                sub += (i ? ", " : "") + h.aliases[i];
        }
        out.emplace_back(h.alias, sub);
    }
    return out;
}

// herdr machine-add shellArg(): a target as a shell reads it.
std::string shell_arg(const std::string& target) {
    const bool plain = !target.empty() && std::all_of(target.begin(), target.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || std::string_view("_@%+=:,./-").find(c) != std::string_view::npos;
    });
    if (plain)
        return target;
    std::string out = "'";
    for (const char c : target)
        out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

void draw_machine_add(App::State& s) {
    if (s.show_machine_add) {
        ImGui::OpenPopup("##machineadd");
        s.show_machine_add = false;
        s.ssh_hosts = ssh_config_hosts();
        s.machine_buf[0] = 0;
        std::erase_if(s.machine_add_state, [](const auto& kv) { return kv.second != "adding"; });
        s.machine_add_pending.clear();
    }
    center_next(px(560), 0.12f);
    push_overlay_style();
    const bool open = ImGui::BeginPopupModal("##machineadd", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    pop_overlay_style();
    if (!open)
        return;
    bool close = overlay_title(s, "Add a machine");
    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
    ImGui::TextWrapped("A server, a desktop at home: AgentDesk reaches it over ssh, and agents there keep running while "
                       "this PC sleeps. It needs AgentDesk there and key-based ssh login.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, px(6)));
    // A machine counts as added once it answers (herdr: `hc machine add` succeeded).
    auto machine_of_target = [&](const std::string& target) -> const MachineConfig* {
        for (const auto& m : s.settings.machines)
            if (m.target == target || m.name == target)
                return &m;
        return nullptr;
    };
    auto added = [&](const std::string& target) {
        const auto* m = machine_of_target(target);
        return m != nullptr && !s.machine_add_state.contains(target);
    };
    // herdr add(): connect at once; "Connecting" until it answers or says why not.
    auto add = [&](std::string target) {
        target = trimmed(target);
        if (target.empty() || s.machine_add_state[target] == "adding")
            return;
        s.machine_add_pending.erase(target);
        s.machine_add_state[target] = "adding";
        if (machine_of_target(target) == nullptr) {
            const auto at = target.find('@');
            std::string name = at == std::string::npos ? target : target.substr(at + 1);
            while (std::any_of(s.settings.machines.begin(), s.settings.machines.end(),
                               [&](const MachineConfig& m) { return m.name == name; }))
                name += "-2";
            s.settings.machines.push_back({name, target, ""});
            save_settings(s.settings);
            start_remotes(s);
        }
    };
    // Follow the connections being made: answered -> added; refused -> not added, and why.
    for (auto it = s.machine_add_state.begin(); it != s.machine_add_state.end();) {
        const std::string target = it->first;
        const auto* m = machine_of_target(target);
        if (it->second != "adding" || m == nullptr) {
            ++it;
            continue;
        }
        if (s.machine_connected(m->name)) {
            if (trimmed(s.machine_buf) == target)
                s.machine_buf[0] = 0;
            it = s.machine_add_state.erase(it);
            continue;
        }
        std::string why;
        {
            std::lock_guard lock(s.remotes_mutex);
            for (const auto& r : s.remotes)
                if (r.cfg.name == m->name)
                    why = r.host->last_error();
        }
        if (!why.empty()) {
            std::string low = why;
            for (auto& c : low)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            const bool answer = low.find("password") != std::string::npos || low.find("passphrase") != std::string::npos ||
                                low.find("host key") != std::string::npos ||
                                low.find("permission denied") != std::string::npos || low.find("login") != std::string::npos;
            if (answer) {
                s.machine_add_pending.insert(target);
                it->second = "Couldn't add " + target +
                             " here: it needs you to answer something (a password, a new host key, or installing "
                             "AgentDesk there). Run `ssh " + shell_arg(target) +
                             "` in a terminal, then add it again.";
            } else {
                it->second = why;
            }
            // Not added: it stays out of the machine list until it answers.
            const std::string name = m->name;
            std::erase_if(s.settings.machines, [&](const MachineConfig& x) { return x.name == name; });
            save_settings(s.settings);
            start_remotes(s);
        }
        ++it;
    }
    // One row (herdr row()): server icon, name, where it goes, then Added / Connecting / Add.
    auto row = [&](const std::string& target, const std::string& label, const std::string& sub) {
        const auto st = s.machine_add_state.find(target);
        const bool done = added(target);
        const bool adding = st != s.machine_add_state.end() && st->second == "adding";
        const bool idle = !done && st == s.machine_add_state.end();
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::PushID(target.c_str());
        const float rw = ImGui::GetContentRegionAvail().x;
        bool clicked = ImGui::InvisibleButton("##h", ImVec2(rw, px(40)));
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* d = ImGui::GetWindowDrawList();
        if (hov)
            d->AddRectFilled(a, ImVec2(a.x + rw, a.y + px(40)), g_pal->hover, px(8));
        const float cy = a.y + px(20);
        draw_icon(s, d, ui::icon::kServer, fpx(15), ImVec2(a.x + px(17), cy), g_pal->dim);
        d->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + px(35), cy - fpx(13) * 0.6f), g_pal->text, label.c_str());
        const float lw = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, label.c_str()).x;
        // machine-add/main.js 247-266: "Added" (ansi2), a 12 px working spinner and
        // "Connecting" (12 px muted), else the bordered "Add" button (px 10, h 26, rounded 7,
        // raised, 12 px medium) -- also after a failed add, when it is the way to try again.
        const char* right = done ? "\xE2\x9C\x93 Added" : adding ? "Connecting" : "Add";
        const bool button = !done && !adding;
        const float rtw = (button ? s.fonts.ui_medium : s.fonts.ui)->CalcTextSizeA(fpx(12), FLT_MAX, 0, right).x;
        const float rw_right = button ? rtw + px(20) : adding ? px(12) + px(6) + rtw : rtw;
        text_clipped(d, s.fonts.ui, fpx(12), ImVec2(a.x + px(45) + lw, cy - fpx(12) * 0.6f), g_pal->faint, sub,
                     std::max(0.0f, rw - px(45) - lw - rw_right - px(20)));
        const ImVec2 ra(a.x + rw - px(10) - rw_right, cy - px(13));
        if (button) {
            const bool bhov = hov && ImGui::IsMouseHoveringRect(ra, ImVec2(ra.x + rw_right, ra.y + px(26)));
            d->AddRectFilled(ra, ImVec2(ra.x + rw_right, ra.y + px(26)), bhov ? g_pal->hover : raised_color(), px(7));
            d->AddRect(ra, ImVec2(ra.x + rw_right, ra.y + px(26)), g_pal->border, px(7));
            d->AddText(s.fonts.ui_medium, fpx(12), ImVec2(ra.x + px(10), cy - fpx(12) * 0.6f), g_pal->text, right);
        } else if (adding) {
            status_mark(s, d, ra.x - px(1), cy, Attention::kWorking, true);
            d->AddText(s.fonts.ui, fpx(12), ImVec2(ra.x + px(12) + px(6), cy - fpx(12) * 0.6f), g_pal->dim, right);
        } else {
            d->AddText(s.fonts.ui, fpx(12), ImVec2(ra.x, cy - fpx(12) * 0.6f), g_pal->done, right);
        }
        // The row adds while idle; after a failed add only its Add button does (main.js 268, 252).
        if (clicked && (idle || (button && ImGui::IsMouseHoveringRect(ra, ImVec2(ra.x + rw_right, ra.y + px(26))))))
            add(target);
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + px(40)));
        if (st != s.machine_add_state.end() && st->second != "adding") {
            // main.js 233-243, 287: px 35, pb 6; the message (12 px blocked) and, when it needs
            // an answer, Copy.
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(35));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + rw - px(70));
            ImGui::PushFont(s.fonts.ui, 12);
            ImGui::TextColored(v4(g_pal->needs_you), "%s", st->second.c_str());
            ImGui::PopFont();
            ImGui::PopTextWrapPos();
            if (s.machine_add_pending.contains(target)) {
                const std::string command = "ssh " + shell_arg(target);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(35));
                if (ImGui::SmallButton(s.machine_add_copied == command ? "Copied" : "Copy")) {
                    ImGui::SetClipboardText(command.c_str());
                    s.machine_add_copied = command;
                }
            }
            ImGui::Dummy(ImVec2(0, px(6)));
        }
        ImGui::PopID();
    };
    // machine-add/main.js 320, 335: the section titles, 11.5 px medium dim, as written (pt 16 /
    // pt 14, pb 6).
    auto section = [&](const char* text, float pt) {
        ImGui::Dummy(ImVec2(0, std::max(0.0f, pt - ImGui::GetStyle().ItemSpacing.y)));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, text);
        ImGui::GetWindowDrawList()->AddText(s.fonts.ui_medium, fpx(11.5f), p, g_pal->faint, text);
        ImGui::Dummy(ImVec2(t.x, t.y + px(6) - ImGui::GetStyle().ItemSpacing.y));
    };
    section(s.ssh_hosts.empty() ? "Your ssh config lists no hosts" : "From your ssh config", px(16) - px(6));
    // herdr ma-hosts: at most 280 px, then it scrolls.
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, px(280)));
    ImGui::BeginChild("##ma-hosts", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoBackground);
    for (const auto& [alias, where] : s.ssh_hosts)
        if (!added(alias))
            row(alias, alias, where);
    ImGui::EndChild();
    for (const auto& m : s.settings.machines)
        if (added(m.target))
            row(m.target, m.name, m.target);
    section("Or type how ssh reaches it", px(14));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - px(90));
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();  // herdr machine-add/main.js 73: the field has focus on open
    const bool enter = ImGui::InputTextWithHint("##target", "user@host", s.machine_buf, sizeof s.machine_buf,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine(0, px(8));
    const std::string typed = trimmed(s.machine_buf);
    ImGui::BeginDisabled(typed.empty());
    primary_button_style(true);
    const bool go = ImGui::Button("Add", ImVec2(px(80), 0)) || enter;
    primary_button_style(false);
    ImGui::EndDisabled();
    if (go && !typed.empty())
        add(typed);
    // The typed target's state under the box (herdr: Connecting, why not, or what to answer).
    if (const auto st = s.machine_add_state.find(typed); !typed.empty() && st != s.machine_add_state.end()) {
        ImGui::PushTextWrapPos(0.0f);
        if (st->second == "adding") {
            // As the rows show it (machine-add/main.js 251): a spinner and "Connecting".
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* d = ImGui::GetWindowDrawList();
            const float h = std::round(fpx(12) * 1.3f);
            status_mark(s, d, p.x - px(1), p.y + h * 0.5f, Attention::kWorking, true);
            d->AddText(s.fonts.ui, fpx(12), ImVec2(p.x + px(12) + px(6), p.y + (h - fpx(12)) * 0.5f), g_pal->dim,
                       "Connecting");
            ImGui::Dummy(ImVec2(px(100), h));
        } else

            ImGui::TextColored(v4(g_pal->needs_you), "%s", st->second.c_str());
        ImGui::PopTextWrapPos();
        if (s.machine_add_pending.contains(typed)) {
            const std::string command = "ssh " + shell_arg(typed);
            if (ImGui::SmallButton(s.machine_add_copied == command ? "Copied##t" : "Copy##t")) {
                ImGui::SetClipboardText(command.c_str());
                s.machine_add_copied = command;
            }
        }
    }
    if (close || ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click())
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- confirm (mods/confirm)
// ------------------------------------------------------------------ What's new / feedback
// herdr app.whats_new (release notes, shown once after an update) and app.feedback (a draft the
// person reviews). AgentDesk has no server: the draft is copied or saved for them to send.
constexpr const char* kWhatsNew[] = {
    "A new look: sidebar with spaces and agents, tab bar with the window buttons, Files and Changes "
    "panels, picker, command palette, settings and welcome.",
    "Bigger interface text by default (15 px), adjustable in Settings > Look; Geist and JetBrains Mono are "
    "bundled.",
    "New tabs, spaces and splits open a shell in the focused folder; spaces and branches follow cd.",
    "WSL distributions are listed as shells, and Linux cd is followed too.",
    "Programs that read the mouse get clicks and the wheel; Shift keeps the mouse for selecting.",
    "Settle tab puts a running tab away until you show it or it needs you.",
    "Duplicate or reset a layout, duplicate a mod.",
};

void draw_notes(App::State& s) {
    if (s.show_notes) {
        ImGui::OpenPopup("##notes");
        s.show_notes = false;
    }
    const bool feedback = s.notes_kind == "feedback";
    if (feedback) {
        // window.rs 3584: 680 wide; feedback/main.js 86: 650 high.
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float fw = std::min(px(680), vp->WorkSize.x - px(16));
        const float fh = std::min(px(650), vp->WorkSize.y - px(16));
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - fw) * 0.5f, vp->WorkPos.y + (vp->WorkSize.y - fh) * 0.5f));
        ImGui::SetNextWindowSize(ImVec2(fw, fh));
    } else {
        center_next(px(520.0f), 0.16f);
    }
    push_overlay_style();
    if (feedback)
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::BeginPopupModal("##notes", nullptr,
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                 (feedback ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0));
    if (feedback)
        ImGui::PopStyleVar();
    pop_overlay_style();
    if (!open)
        return;
    bool close = false;
    if (!feedback) {
        const std::string title = std::string("What's new in ") + AGENTDESK_VERSION_STRING;
        close = overlay_title(s, title.c_str());
    }
    if (!feedback) {
        for (const char* line : kWhatsNew) {
            ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->focus));
            ImGui::TextUnformatted("\xE2\x80\xA2");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", line);
            ImGui::Dummy(ImVec2(0, px(2)));
        }
        ImGui::Dummy(ImVec2(0, px(6)));
        const float bw = px(84);
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw - px(18));
        primary_button_style(true);
        close = ImGui::Button("Got it", ImVec2(bw, px(30))) || close;
        primary_button_style(false);
    } else {
        // feedback/main.js 86-112. AgentDesk has no server: Copy and Save to file stand in for
        // Send, and the line herdr shows when a build can't send says so.
        const ImVec2 wp = ImGui::GetWindowPos();
        const float ww = ImGui::GetWindowWidth();
        const float wh = ImGui::GetWindowHeight();
        ImDrawList* d = ImGui::GetWindowDrawList();
        // Title (98): px 24, py 18, 19 px semibold.
        const float title_h = px(18) + line_h(19) + px(18);
        d->AddText(s.fonts.ui_bold, fpx(19), ImVec2(wp.x + px(24), wp.y + px(18)), g_pal->text, "Send feedback");
        // 99: px 24, pb 12, 13 px accent.
        const char* kNoServer = "AgentDesk sends nothing by itself: copy the draft or save it to a file, then send it "
                                "the way you like.";
        float y = wp.y + title_h;
        {
            const char* t = kNoServer;
            const char* end = t + std::strlen(t);
            while (t < end) {
                const char* brk = s.fonts.ui->CalcWordWrapPosition(fpx(13), t, end, ww - px(48));
                if (brk == t)
                    brk = t + 1;
                d->AddText(s.fonts.ui, fpx(13), ImVec2(wp.x + px(24), y), g_pal->focus, t, brk);
                y += line_h(13);
                t = brk;
                while (t < end && *t == ' ')
                    ++t;
            }
            y += px(12);
        }
        OSVERSIONINFOEXW os{};
        os.dwOSVersionInfoSize = sizeof os;
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
        if (auto fn_ptr = reinterpret_cast<RtlGetVersionFn>(
                ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")))
            fn_ptr(&os);
        const std::string about = std::string("AgentDesk ") + AGENTDESK_VERSION_STRING + ", Windows " +
                                  std::to_string(os.dwMajorVersion) + "." + std::to_string(os.dwMinorVersion) + "." +
                                  std::to_string(os.dwBuildNumber) + ", layout " + s.settings.layout;
        const std::string draft = std::string(s.feedback_buf) + "\n\n-- " + about + "\n";
        // Footer (110-112): px 24, py 16, gap 10, at the end: Cancel, then (ours) Save to file and
        // Copy, the primary. Buttons (61-66): px 14, py 7, rounded 7, a border; raised and muted,
        // or accent; a disabled primary looks like the others.
        const float btn_h = px(7) + line_h(13) + px(7) + 2.0f;
        const float foot_y = wp.y + wh - px(16) - btn_h;
        const float note_h = s.feedback_note.empty() ? 0.0f : px(10) + line_h(13);
        // Body (100-108): px 24, gap 10, scrolls between the title and the footer.
        ImGui::SetCursorScreenPos(ImVec2(wp.x, y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(24), 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(10), 0));
        ImGui::BeginChild("##fb-body", ImVec2(ww, std::max(px(40), foot_y - px(16) - note_h - y)),
                          ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
        ImDrawList* bd = ImGui::GetWindowDrawList();
        const float bw_full = ImGui::GetContentRegionAvail().x;
        auto label = [&](const char* text) {  // 95: 13 px muted
            const ImVec2 p = ImGui::GetCursorScreenPos();
            bd->AddText(s.fonts.ui, fpx(13), p, g_pal->dim, text);
            ImGui::Dummy(ImVec2(bw_full, line_h(13) + px(10)));
        };
        label("Your message (up to 4000 characters).");
        // 96, 104: p 10, rounded 7, a border, the background, 14 px; 130 high.
        {
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushFont(s.fonts.ui, 14);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), px(10)));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(7));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(g_pal->bg));
            ImGui::PushStyleColor(ImGuiCol_Border, v4(g_pal->border));
            ImGui::InputTextMultiline("##fb", s.feedback_buf, std::min<std::size_t>(sizeof s.feedback_buf, 4001),
                                      ImVec2(bw_full, px(130)));
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            ImGui::PopFont();
            // 9: the placeholder.
            if (s.feedback_buf[0] == '\0')
                bd->AddText(s.fonts.ui, fpx(14), ImVec2(p.x + px(10), p.y + px(10)), g_pal->faint,
                            "What happened? What did you expect?");
            ImGui::Dummy(ImVec2(0, px(10)));
        }
        label("What gets copied or saved: exactly this, nothing else.");
        // 107-108: p 10, rounded 7, a border, the background; 11 px lines of 15 px, dim.
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            int lines = 1;
            for (const char ch : draft)
                lines += ch == '\n' ? 1 : 0;
            const float bh = px(10) * 2 + px(15) * static_cast<float>(lines);
            bd->AddRectFilled(p, ImVec2(p.x + bw_full, p.y + bh), g_pal->bg, px(7));
            bd->AddRect(p, ImVec2(p.x + bw_full, p.y + bh), g_pal->border, px(7));
            bd->PushClipRect(p, ImVec2(p.x + bw_full - px(10), p.y + bh), true);
            float ly = p.y + px(10);
            for (std::size_t at = 0; at <= draft.size();) {
                const auto nl = draft.find('\n', at);
                const std::string line = draft.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
                bd->AddText(s.fonts.ui, fpx(11), ImVec2(p.x + px(10), ly + (px(15) - fpx(11)) * 0.5f), g_pal->faint,
                            line.empty() ? " " : line.c_str());
                ly += px(15);
                if (nl == std::string::npos)
                    break;
                at = nl + 1;
            }
            bd->PopClipRect();
            ImGui::Dummy(ImVec2(bw_full, bh));
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        // 109: px 24, pt 10, 13 px accent (the note after Copy or Save).
        if (!s.feedback_note.empty())
            d->AddText(s.fonts.ui, fpx(13), ImVec2(wp.x + px(24), foot_y - px(16) - line_h(13)), g_pal->focus,
                       s.feedback_note.c_str());
        const bool empty = s.feedback_buf[0] == '\0';
        auto button = [&](const char* id, const char* text, bool primary, bool disabled, float x) {
            const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, text);
            const ImVec2 a(x, foot_y);
            const ImVec2 sz(t.x + px(28) + 2.0f, btn_h);
            ImGui::SetCursorScreenPos(a);
            const bool hit = ImGui::InvisibleButton(id, sz) && !disabled;
            const bool on = primary && !disabled;
            d->AddRectFilled(a, ImVec2(a.x + sz.x, a.y + sz.y), on ? g_pal->focus : raised_color(), px(7));
            d->AddRect(a, ImVec2(a.x + sz.x, a.y + sz.y), g_pal->border, px(7));
            d->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + px(14) + 1.0f, a.y + (sz.y - t.y) * 0.5f),
                       on ? accent_foreground() : g_pal->dim, text);
            return hit;
        };
        auto width_of = [&](const char* text) {
            return s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, text).x + px(28) + 2.0f;
        };
        float bx = wp.x + ww - px(24) - width_of("Copy");
        const float copy_x = bx;
        bx -= px(10) + width_of("Save to file");
        const float save_x = bx;
        bx -= px(10) + width_of("Cancel");
        close = button("##fb-cancel", "Cancel", false, false, bx) || close;
        if (button("##fb-save", "Save to file", false, empty, save_x)) {
            const auto dir = data_dir() / L"feedback";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            SYSTEMTIME t{};
            ::GetLocalTime(&t);
            wchar_t name[64]{};
            std::swprintf(name, 64, L"feedback-%04u%02u%02u-%02u%02u%02u.md", t.wYear, t.wMonth, t.wDay, t.wHour,
                          t.wMinute, t.wSecond);
            std::ofstream(dir / name, std::ios::binary) << draft;
            ::ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            s.feedback_note = "Saved in the feedback folder.";
        }
        if (button("##fb-copy", "Copy", true, empty, copy_x)) {
            ImGui::SetClipboardText(draft.c_str());
            s.feedback_note = "Copied. Paste it where you want to send it.";
        }
    }
    if (close || ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click()) {
        ImGui::CloseCurrentPopup();
        s.focus_view(s.ws.focused_session());
    }
    ImGui::EndPopup();
}

void draw_confirm(App::State& s) {
    if (s.show_confirm) {
        ImGui::OpenPopup("##confirm");
        s.show_confirm = false;
    }
    center_next(px(380), 0.24f);
    push_overlay_style();
    const bool open = ImGui::BeginPopupModal("##confirm", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    pop_overlay_style();
    if (!open)
        return;
    bool cancel = overlay_title(s, s.confirm_title.c_str(), EscChip::kPlain);  // confirm/main.js 42
    if (!s.confirm_detail.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
        ImGui::TextWrapped("%s", s.confirm_detail.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, px(8)));
    const float bw = px(84);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw * 2 - px(26));
    ImGui::PushStyleColor(ImGuiCol_Button, v4(raised_color()));
    cancel = ImGui::Button("Cancel", ImVec2(bw, px(30))) || cancel || ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    primary_button_style(true);
    // herdr confirm/main.js: Cancel and Confirm.
    const bool ok = ImGui::Button("Confirm", ImVec2(bw, px(30))) || ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    primary_button_style(false);
    if (cancel)
        ImGui::CloseCurrentPopup();
    if (ok) {
        ImGui::CloseCurrentPopup();
        run_command(s, s.confirm_command);
    }
    ImGui::EndPopup();
}

void draw_exit(App::State& s) {
    if (s.show_exit) {
        ImGui::OpenPopup("##exit");
        s.show_exit = false;
    }
    center_next(px(520), 0.2f);
    if (!ImGui::BeginPopupModal("##exit", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
        return;
    heading(s, "Exit and stop all sessions");
    ImGui::TextDisabled("These sessions will be force-stopped and agent-host will exit:");
    int running = 0;
    for (const auto& sess : s.sessions) {
        const auto& v = s.views_by_id[sess->id()];
        if (v.process_state != "Running" && v.process_state != "Stopping" && v.process_state != "Starting")
            continue;
        ++running;
        ImGui::BulletText("%s  (%s)  %s", s.pane_title(sess->id()).c_str(), v.process_state.c_str(),
                          display_path(v.cwd).c_str());
    }
    if (running == 0)
        ImGui::TextDisabled("(no running sessions)");
    if (!s.exit_error.empty())
        ImGui::TextColored(v4(g_pal->needs_you), "%s", s.exit_error.c_str());
    ImGui::PushStyleColor(ImGuiCol_Button, v4(alpha(g_pal->needs_you, 210)));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(IM_COL32(255, 255, 255, 255)));
    if (ImGui::Button("Stop all and exit")) {
        auto* exit_now = &s.exit_now;
        auto* error = &s.exit_error;
        auto wake = s.config.wake;
        s.host->shutdown_host(true, [exit_now, error, wake](std::string e) {
            if (e.empty())
                *exit_now = true;
            else
                *error = e;
            if (wake)
                wake();
        });
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void draw_close_notice(App::State& s) {
    if (s.show_close_notice) {
        ImGui::OpenPopup("##closenotice");
        s.show_close_notice = false;
    }
    center_next(px(520), 0.25f);
    if (!ImGui::BeginPopupModal("##closenotice", nullptr,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
        return;
    heading(s, "Sessions keep running");
    ImGui::TextWrapped(
        "Closing this window only detaches. Your sessions keep running in agent-host, and reopening "
        "AgentDesk "
        "brings back the same spaces, tabs and panes. Use \"Exit and stop all\" in the sidebar to end "
        "everything.");
    primary_button_style(true);
    if (ImGui::Button("Close window")) {
        s.settings.close_notice_shown = true;  // shown once (design doc 4.3)
        save_settings(s.settings);
        s.exit_now = true;
    }
    primary_button_style(false);
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ------------------------------------------------------------------ Herdr 98 (herdr-app win98)
#include "win98_kit.inl"
#include "win98_dialogs.inl"
#include "win98_chrome.inl"

// ------------------------------------------------------------------ the layout, drawn
// herdr window.rs 4439-4502 render_node: each shown node of the layout tree gets its rect
// (layout::place); a node's background and border are painted under its children (5232-5253
// apply_look), and each block is drawn by the renderer its content names. herdr runs mods for
// those; AgentDesk draws its built-in ones in C++ (below) and a C++ DLL mod's panels for a block
// naming that mod.

// A layout color: one of the theme's names (herdr themes: canvas, chrome, surface, ...) or
// #rrggbb / #rrggbbaa.
std::optional<ImU32> layout_color(const std::string& c) {
    if (!c.empty() && c[0] == '#' && (c.size() == 7 || c.size() == 9)) {
        unsigned v[4] = {0, 0, 0, 255};
        for (std::size_t i = 0; i + 1 < c.size(); i += 2) {
            const unsigned long x = std::strtoul(c.substr(i + 1, 2).c_str(), nullptr, 16);
            v[i / 2] = static_cast<unsigned>(x);
        }
        return IM_COL32(v[0], v[1], v[2], v[3]);
    }
    static const std::map<std::string, ImU32 Palette::*> kNames = {
        {"canvas", &Palette::bg},         {"chrome", &Palette::sidebar},  {"surface", &Palette::card},
        {"raised", &Palette::hover},      {"background", &Palette::term_bg}, {"border", &Palette::border},
        {"accent", &Palette::focus},      {"hover", &Palette::hover},     {"selected", &Palette::selected},
        {"foreground", &Palette::text},   {"muted", &Palette::dim},       {"dim", &Palette::faint},
        {"terminal_foreground", &Palette::term_fg}, {"selection", &Palette::selection}};
    if (const auto it = kNames.find(c); it != kNames.end())
        return g_pal->*(it->second);
    return std::nullopt;
}

// window.rs 4611-4631 builtin_tabs: `"header": {"content": "tabs"}` on a stack: its children's
// titles (else their number); a click selects one (layout.select).
void draw_stack_tabs(App::State& s, const lay::Node& stack, const lay::Rect& r) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.right(), r.bottom()), g_pal->card);
    const lay::Node* active = stack.active_child();
    float x = r.x;
    for (std::size_t i = 0; i < stack.children.size(); ++i) {
        const auto& ch = stack.children[i];
        const std::string label = ch.title.value_or(std::to_string(i + 1));
        const float w = ImGui::CalcTextSize(label.c_str()).x + px(24);
        const bool on = active == &ch;
        if (on)
            dl->AddRectFilled(ImVec2(x, r.y), ImVec2(x + w, r.bottom()), g_pal->hover);
        dl->AddText(ImVec2(x + px(12), r.y + (r.h - ImGui::GetFontSize()) * 0.5f), on ? g_pal->text : g_pal->dim,
                    label.c_str());
        ImGui::SetCursorScreenPos(ImVec2(x, r.y));
        ImGui::PushID(("##stacktab-" + stack.id + "/" + ch.id).c_str());
        if (ImGui::InvisibleButton("##t", ImVec2(w, std::max(1.0f, r.h))))
            run_command(s, "layout.select:" + Json{{"stack", stack.id}, {"child", ch.id}}.dump());
        ImGui::PopID();
        x += w;
    }
}

void draw_layout_block(App::State& s, const LayoutSpec& l, const lay::Node& n, const lay::Placed& p, bool connected) {
    const lay::Rect& r = p.inner;
    if (r.w <= 0 || r.h <= 0)
        return;
    const ImVec2 pos(r.x, r.y);
    const ImVec2 size(r.w, r.h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool rail = r.w < px(160);  // workspace-chrome: the narrow sidebar
    switch (n.content.type) {
        case lay::Content::Type::kTab: {
            if (l.style == "win98" && s.ws.active_tab() == nullptr)
                return;
            // AgentDesk's details pane (not herdr's): beside the panes when on.
            const bool details = l.style.empty() && l.details.value_or(s.settings.details);
            const float gap = px(8);
            const float details_w = details ? std::min(px(320), (r.w + 2 * gap) * 0.3f) : 0.0f;
            const ImVec2 panes(size.x - (details_w > 0 ? details_w + gap : 0.0f), size.y);
            draw_panes(s, pos, panes, connected);
            if (details_w > 0)
                draw_details(s, ImVec2(pos.x + panes.x + gap, pos.y), ImVec2(details_w, size.y), connected);
            return;
        }
        case lay::Content::Type::kMod:
            break;
        case lay::Content::Type::kTerminal:
        case lay::Content::Type::kAgent:
            return;  // terminals live in tabs here (not as layout blocks)
    }
    const std::string& m = n.content.value;
    const Json& props = n.content.props;
    if (m == "workspace-chrome") {
        if (props.is_object() && props.value("footer", false))
            draw_sidebar_footer(s, pos, size.x, rail);
        else
            draw_brand(s, pos, size.x, rail);
    } else if (m == "spaces") {
        refresh_git_facts(s);
        draw_spaces(s, pos, size.x, size.y, rail);
    } else if (m == "agents") {
        draw_agents(s, pos, size.x, size.y, rail);
    } else if (m == "tabs") {
        draw_tab_bar(s, pos, size.x, size.y);
    } else if (m == "terminals-bar") {
        draw_terminals_bar(s, pos, size.x, size.y);
    } else if (m == "workspace-status") {
        draw_status_bar(s, pos, size.x, size.y);
    } else if (m == "files" || m == "changes") {
        draw_side_panel(s, pos, size);
    } else if (m == "win98") {
        const std::string role = n.role();
        const w98::Box b{r.x, r.y, r.right(), r.bottom()};
        if (role == "desktop")
            w98::desktop(s, dl, b);
        else if (role == "taskbar")
            w98::taskbar(s, dl, b);
    } else if (s.mods != nullptr) {
        // A C++ DLL mod: its panels, in the block.
        ImGui::SetCursorScreenPos(pos);
        ImGui::BeginChild(("##modblock-" + n.id).c_str(), size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        draw_mod_panels(s, "sidebar", m);
        draw_mod_panels(s, "details", m);
        ImGui::EndChild();
    }
}

void draw_layout(App::State& s, ImVec2 origin, ImVec2 full, bool connected) {
    LayoutSpec& l = s.layout();
    sync_panel(s, l);
    const bool win98 = l.style == "win98";
    if (win98) {
        w98::begin(s);
        w98::fit(s, l.tree);
    }
    s.placed = lay::place(l.tree, lay::Rect{origin.x, origin.y, full.x, full.y}, g_scale, {}, true);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Backgrounds and borders, parents first.
    for (const auto& id : s.placed.order) {
        const lay::Node* n = l.tree.find(id);
        const lay::Placed& p = s.placed.nodes.at(id);
        const ImVec2 a(p.outer.x, p.outer.y);
        const ImVec2 b(p.outer.right(), p.outer.bottom());
        const float radius = n->radius ? px(*n->radius) : 0.0f;
        if (win98 && n == &l.tree) {
            // Herdr 98's wallpaper: the root's background (a click in herdr, layout_sync.rs 324).
            w98::background(s, dl, w98::Box{a.x, a.y, b.x, b.y});
        } else if (l.look_applies && n->background.is_string()) {
            if (const auto c = layout_color(n->background.get<std::string>()))
                dl->AddRectFilled(a, b, *c, radius);
        } else if (l.look_applies && n->background.is_object() && n->background.contains("gradient")) {
            // layout.rs 107-112: two colors at an angle; drawn top to bottom or left to right.
            const auto& g = n->background["gradient"];
            const auto c0 = layout_color(g[0].get<std::string>());
            const auto c1 = layout_color(g[1].get<std::string>());
            if (c0 && c1) {
                const float angle = n->background.value("angle", 180.0f);
                const bool across = std::fmod(std::abs(angle), 180.0f) > 45.0f && std::fmod(std::abs(angle), 180.0f) < 135.0f;
                if (across)  // 90 / 270: left to right
                    dl->AddRectFilledMultiColor(a, b, *c0, *c1, *c1, *c0);
                else
                    dl->AddRectFilledMultiColor(a, b, *c0, *c0, *c1, *c1);
            }
        }
        if (const lay::Line* line = lay::node_border(*n, false))
            if (const auto c = layout_color(line->color))
                dl->AddRect(a, b, *c, radius, 0, px(line->width));
    }
    // The blocks, in tree order.
    for (const auto& id : s.placed.order) {
        const lay::Node* n = l.tree.find(id);
        if (n->is_block())
            draw_layout_block(s, l, *n, s.placed.nodes.at(id), connected);
    }
    for (const auto& [id, r] : s.placed.headers) {
        const lay::Node* n = l.tree.find(id);
        if (n != nullptr && n->header && n->header->content.is_string())
            draw_stack_tabs(s, *n, r);
    }
    // Separators (window.rs 4633-4669): the divider line, and a drag handle where the container
    // is resizable.
    for (const auto& sep : s.placed.separators) {
        const lay::Node* c = l.tree.find(sep.container);
        const bool row = sep.axis == lay::Axis::kRow;
        if (c != nullptr && c->divider) {
            if (const auto col = layout_color(c->divider->color)) {
                const float lw = px(c->divider->width);
                // A line lw wide down the middle of the separator (window.rs 63-69 separator_line).
                if (row) {
                    const float x = sep.box.x + (sep.box.w - lw) * 0.5f;
                    dl->AddLine(ImVec2(x, sep.box.y), ImVec2(x, sep.box.bottom()), *col, lw);
                } else {
                    const float y = sep.box.y + (sep.box.h - lw) * 0.5f;
                    dl->AddLine(ImVec2(sep.box.x, y), ImVec2(sep.box.right(), y), *col, lw);
                }
            }
        }
        if (!sep.resizable || sep.hit.w <= 0 || sep.hit.h <= 0)
            continue;
        ImGui::SetCursorScreenPos(ImVec2(sep.hit.x, sep.hit.y));
        ImGui::PushID(("##sep-" + sep.container + "/" + std::to_string(sep.index)).c_str());
        ImGui::InvisibleButton("##h", ImVec2(sep.hit.w, sep.hit.h));
        const bool active = ImGui::IsItemActive();
        if (ImGui::IsItemHovered() || active)
            ImGui::SetMouseCursor(row ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        if (active) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            lay::drag(l.tree, sep.container, sep.index, sep.axis, s.placed, row ? m.x : m.y, g_scale);
        }
        if (ImGui::IsItemDeactivated())
            save_layout_state(l);
        ImGui::PopID();
    }
}

}  // namespace

// ------------------------------------------------------------------ frame
namespace {

ImGuiKey input_key(const std::string& n) {
    static const std::pair<const char*, ImGuiKey> kKeys[] = {
        {"enter", ImGuiKey_Enter}, {"esc", ImGuiKey_Escape},  {"tab", ImGuiKey_Tab},          {"up", ImGuiKey_UpArrow},
        {"down", ImGuiKey_DownArrow}, {"left", ImGuiKey_LeftArrow}, {"right", ImGuiKey_RightArrow},
        {"home", ImGuiKey_Home},   {"end", ImGuiKey_End},     {"pgup", ImGuiKey_PageUp},     {"pgdn", ImGuiKey_PageDown},
        {"back", ImGuiKey_Backspace}, {"del", ImGuiKey_Delete}, {"space", ImGuiKey_Space},    {"f2", ImGuiKey_F2}};
    for (const auto& [name, key] : kKeys)
        if (n == name)
            return key;
    if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z')
        return static_cast<ImGuiKey>(ImGuiKey_A + (n[0] - 'a'));
    if (n.size() == 1 && n[0] >= '0' && n[0] <= '9')
        return static_cast<ImGuiKey>(ImGuiKey_0 + (n[0] - '0'));
    return ImGuiKey_None;
}

// --input: one step per turn, after the --run commands, once sessions exist.
void run_input_script(App::State& s) {
    if (s.config.input_script.empty() || s.next_dev_command < s.config.run_commands.size() || s.sessions.empty() ||
        !s.doc_loaded)
        return;
    std::vector<std::string> steps;
    for (std::size_t p = 0; p <= s.config.input_script.size();) {
        const auto q = s.config.input_script.find(';', p);
        steps.push_back(s.config.input_script.substr(p, q == std::string::npos ? std::string::npos : q - p));
        if (q == std::string::npos)
            break;
        p = q + 1;
    }
    const auto now = Clock::now();
    if (s.config.wake)
        s.config.wake();
    if (s.input_shot_wait) {
        std::error_code ec;
        if (std::filesystem::exists(s.config.shot_signal, ec) && now - s.input_shot_since < std::chrono::seconds(10))
            return;
        s.input_shot_wait = false;
        s.input_at = now + std::chrono::milliseconds(100);
    }
    if (s.input_at == Clock::time_point{})
        s.input_at = now + std::chrono::milliseconds(1500);  // first frames settle
    if (s.input_up >= 0 && now >= s.input_at) {
        ImGui::GetIO().AddMouseButtonEvent(s.input_up, false);
        const int b = s.input_up;
        s.input_up = -1;
        s.input_at = now + std::chrono::milliseconds(80);
        if (s.input_clicks_left > 0) {
            --s.input_clicks_left;
            ImGui::GetIO().AddMouseButtonEvent(b, true);
            s.input_up = b;
        }
        return;
    }
    if (now < s.input_at || s.next_input >= steps.size())
        return;
    const std::string step = steps[s.next_input++];
    const auto colon = step.find(':');
    const std::string op = step.substr(0, colon);
    const std::string arg = colon == std::string::npos ? std::string() : step.substr(colon + 1);
    ImGuiIO& io = ImGui::GetIO();
    s.input_at = now + std::chrono::milliseconds(300);
    auto xy = [&] {
        const auto c = arg.find(',');
        return ImVec2(static_cast<float>(std::atof(arg.substr(0, c).c_str())),
                      static_cast<float>(std::atof(arg.substr(c + 1).c_str())));
    };
    if (op == "wait") {
        s.input_at = now + std::chrono::milliseconds(std::atoi(arg.c_str()));
    } else if (op == "move" || op == "click" || op == "rclick" || op == "mclick" || op == "dblclick") {
        const ImVec2 p = xy();
        s.input_mouse = p;
        io.AddMousePosEvent(p.x, p.y);
        const int button = op == "rclick" ? 1 : op == "mclick" ? 2 : 0;
        if (op != "move") {
            // Press now, release a few frames later (and press again for a double click).
            s.input_at = now + std::chrono::milliseconds(120);
            io.AddMouseButtonEvent(button, true);
            s.input_up = button;
            s.input_clicks_left = op == "dblclick" ? 1 : 0;
        }
    } else if (op == "press" || op == "release") {
        // A drag: "press:x,y", then "move:x,y" steps, then "release:x,y".
        const ImVec2 p = xy();
        s.input_mouse = p;
        io.AddMousePosEvent(p.x, p.y);
        io.AddMouseButtonEvent(0, op == "press");
    } else if (op == "key") {
        if (const ImGuiKey k = input_key(arg); k != ImGuiKey_None) {
            io.AddKeyEvent(k, true);
            io.AddKeyEvent(k, false);
        }
    } else if (op == "type") {
        io.AddInputCharactersUTF8(arg.c_str());
    } else if (op == "shot" && !s.config.shot_signal.empty()) {
        std::ofstream(s.config.shot_signal, std::ios::binary) << arg;
        s.input_shot_wait = true;
        s.input_shot_since = now;
    }
}

}  // namespace

void App::before_new_frame() {
    // The Win32 backend reads the real cursor while the window has focus; a running --input
    // script wins over it.
    if (s_->input_mouse && !s_->config.input_script.empty())
        ImGui::GetIO().AddMousePosEvent(s_->input_mouse->x, s_->input_mouse->y);
}

void App::frame() {
    State& s = *s_;
    for (const auto& cmd : std::exchange(s.chord_commands, {}))
        run_command(s, cmd);
    g_scale = s.dpi * ui_scale(s.settings);
    s.fonts.mono_size = s.layout().font_size.value_or(s.settings.font_size) / ui_text_factor(s.settings);
    const bool win98 = s.layout().style == "win98";
    if (win98)  // kit.js 451-452: the MS-DOS font, 32 px with Large Fonts
        s.fonts.mono_size = w98::terminal_font_size(s) / ui_text_factor(s.settings);
    if (s.fonts_win98 != win98) {  // a layout with fonts of its own (Herdr 98), or back from it
        s.fonts_win98 = win98;
        apply_terminal_font(s);
        apply_ui_font(s);
    }
    if (!win98)
        w98::hidden(s);
    if (Clock::now() - s.keys_checked > std::chrono::milliseconds(400)) {
        s.keys_checked = Clock::now();
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(data_dir() / L"keys.json", ec);
        if (!ec && t != s.keys_mtime) {
            load_keys(s);
            if (s.keymap_errors.empty())
                s.toast("keys.json applied", "", "", g_pal->info, 2);
            else
                s.toast("keys.json not applied", s.keymap_errors.front(), "", g_pal->needs_you, 8);
        }
        // herdr window.rs 982-991: programs.json is read again when it changes.
        const auto pt = std::filesystem::last_write_time(data_dir() / L"programs.json", ec);
        if (ec)
            ec.clear();
        if (pt != g_programs_mtime)
            if (const std::string problem = load_programs(); !problem.empty())
                s.toast("programs.json not applied", problem, "", g_pal->needs_you, 8);
    }
    if (!g_settings_problem.empty()) {
        s.toast(g_settings_problem, "", "", g_pal->needs_you, 8);
        g_settings_problem.clear();
    }
    s.keymap.set_layout(s.settings.layout);  // keys.json "layout:<name>" section
    const auto theme_pair = layout_theme_pair(s.settings);
    if (const Palette* want = effective_palette(s.settings.appearance, theme_pair.first, theme_pair.second,
                                                s.layout().theme);
        want != g_pal) {
        g_pal = want;
        apply_style(s.dpi, ui_scale(s.settings));
    }
    const bool connected = s.host->connected();
    // window.rs 879-887 note_core: on connect and every reconnect, what agent-host says it is.
    if (connected && s.host->host_instance_id() != s.noted_host) {
        s.noted_host = s.host->host_instance_id();
        const std::string v = s.host->host_version();
        ui::updates::set_core(v.empty() ? std::nullopt : std::optional<std::string>(v), std::nullopt);
    }
    // window.rs 795-799: lost the core.
    if (s.was_connected && !connected)
        ui::telemetry::count("core_lost");
    s.was_connected = connected;
    // window.rs 4142-4147: keep a dismissed core error dismissed until it changes.
    reconcile_core_update_error(s.core_update_error_dismissed, ui::updates::core());
    // settings main.js 65-71: after "Check now", follow the look, then "Up to date" for 3 s.
    if (s.updates_asked) {
        if (ui::updates::status().checking)
            s.updates_asked = Clock::now();
        else if (Clock::now() - *s.updates_asked >= std::chrono::milliseconds(3000))
            s.updates_asked.reset();
    }

    if (auto doc = s.host->take_ui_state()) {
        if (!s.doc_loaded || doc->dump() != s.last_sent_doc)
            adopt_doc(s, *doc);
        s.doc_loaded = true;
    }
    s.sessions = s.host->sessions();
    s.machine_of.clear();
    for (const auto& sess : s.sessions)
        s.machine_of[sess->id()] = "";
    for (auto& r : s.remotes) {
        for (const auto& sess : r.host->sessions()) {
            s.sessions.push_back(sess);
            s.machine_of[sess->id()] = r.cfg.name;
        }
    }
    s.views_by_id.clear();
    s.by_id.clear();
    for (const auto& sess : s.sessions) {
        s.views_by_id[sess->id()] = sess->view();
        s.by_id[sess->id()] = sess;
    }
    if (s.doc_loaded || !connected)
        sync_sessions(s);
    if (!s.started_dev && !s.config.start_profile.empty() && connected && s.doc_loaded) {
        s.started_dev = true;
        Placement p;
        p.target = LaunchTarget::kTab;
        create_session(s, s.config.start_profile, s.launch_cwd, "", p);
    }
    update_attention(s);
    // window.rs 4155-4158: every 25 polls of 400 ms, what is open.
    if (Clock::now() - s.observed_at >= std::chrono::seconds(10)) {
        s.observed_at = Clock::now();
        observe(s);
    }
    run_mods(s);
    for (const auto& c : std::exchange(s.pending_mod_commands, {}))
        run_command(s, c, false);
    run_input_script(s);
    if (s.started_dev || s.config.start_profile.empty()) {
        if (s.next_dev_command < s.config.run_commands.size() && s.doc_loaded && !s.launch_busy &&
            Clock::now() >= s.next_dev_at && !s.sessions.empty()) {
            if (s.next_dev_at == Clock::time_point{}) {
                s.next_dev_at = Clock::now() + std::chrono::milliseconds(800);
            } else {
                run_command(s, s.config.run_commands[s.next_dev_command++], false);
                s.next_dev_at = Clock::now() + std::chrono::milliseconds(700);
            }
        }
    }

    // Global shortcuts while no terminal holds the keyboard (a focused terminal reports them).
    if (!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        // The layout's section ("layout:<name>") first, then global (herdr keys.rs tab()).
        std::set<ImGuiKeyChord> chords;
        for (const auto& [chord, cmd] : s.keymap.global_map())
            chords.insert(chord);
        for (const auto& [chord, cmd] : s.keymap.layout_map())
            chords.insert(chord);
        for (const ImGuiKeyChord chord : chords) {
            if (ImGui::IsKeyChordPressed(chord))
                if (const auto cmd = s.keymap.global(chord))
                    run_command(s, *cmd);
        }
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);
    const ImVec2 origin = vp->WorkPos;
    const ImVec2 full = vp->WorkSize;
    // The shown layout's tree (herdr main.json: the sidebar, a 1 px divider and the work column of
    // tabs, the panes with the panel, and the status bar; terminals.json; win98.json).
    ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + full.x, origin.y + full.y), g_pal->bg);
    draw_layout(s, origin, full, connected);
    ImGui::End();
    if (win98)  // its floats, windows and menus (herdr overlays)
        w98::overlays(s);

    draw_peek(s);
    draw_launcher(s);
    draw_rename(s);
    draw_worktree(s);
    draw_settings(s);
    draw_exit(s);
    draw_welcome(s);
    draw_machine_add(s);
    draw_confirm(s);
    draw_notes(s);
    draw_worktree_open(s);
    draw_close_notice(s);
    draw_palette(s);
    draw_picker(s);
    draw_help(s);
    draw_toasts(s);

    // Window title follows what is shown.
    {
        static std::string last;
        std::string title = "AgentDesk";
        if (auto* sp = s.ws.active())
            title = s.ws.space_title(s.ws.active_space, s.session_cwds()) + " - AgentDesk";
        if (title != last) {
            last = title;
            ::SetWindowTextW(s.config.hwnd, win::to_wide(title).c_str());
        }
    }
    persist(s);
    // window.rs 832: the window is up.
    if (!s.window_opened) {
        s.window_opened = true;
        ui::telemetry::window_open();
    }
}

}  // namespace agentdesk::desktop
