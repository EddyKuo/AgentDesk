// Colour palettes: AgentDesk's own, the preset themes from themes\, the active palette and the
// ImGui style built from it.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
constexpr std::array<std::uint32_t, 3> kHerdrDarkStatus = {0xe5c07b, 0xf0705f, 0x76a8ff};  // settings.rs 31

}  // namespace

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

namespace {
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

}  // namespace

std::string theme_id(std::string id) {
    const auto dash = id.find('-');
    if (dash == std::string::npos || id.starts_with(kPresetPrefix))
        return id;
    for (const auto& [name, pal] : herdr_presets())
        if (id.compare(dash + 1, std::string::npos, name) == 0)
            return std::string(kPresetPrefix) + name;
    return id;
}

namespace {
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

}  // namespace

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
}  // namespace detail

}  // namespace agentdesk::desktop
