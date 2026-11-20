// Colour palettes: AgentDesk's own, the preset themes and theme files, the active palette and
// the ImGui style built from it.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// Defined in this one translation unit, so a Palette* compares equal to the same entry wherever
// it was taken (g_pal, palette_by_id, the Settings lists).
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
};

// Status colours (working, blocked, done) of the dark presets.
constexpr std::array<std::uint32_t, 3> kPresetDarkStatus = {0xe5c07b, 0xf0705f, 0x76a8ff};

}  // namespace

std::span<const Palette> builtin_palettes() {
    return kPalettes;
}

const std::array<std::pair<const char*, PresetPalette>, 4>& preset_palettes() {
    static const std::array<std::pair<const char*, PresetPalette>, 4> k = {{
        {"graphite",
         {0x17191d, 0xd2d4d9, 0x394454, std::nullopt,
          {0x45484f, 0xdc8c95, 0xa6bd91, 0xd8be87, 0x96b2d7, 0xb5a2ce, 0x8cbdc3, 0xd2d4d9, 0x818793, 0xf0a5ad,
           0xbad5a2, 0xead2a1, 0xb0cbef, 0xcbb7e6, 0xa4d5db, 0xf0f0f2},
          kPresetDarkStatus}},
        {"ink",
         {0x111720, 0xcdd6e4, 0x344861, std::nullopt,
          {0x414b5a, 0xe58c9a, 0x89c5aa, 0xd9be82, 0x91b6ee, 0xb9a0e2, 0x85c4d0, 0xcdd6e4, 0x7f91a7, 0xf5a5b2,
           0xa5dec3, 0xedd39c, 0xaccbfa, 0xd0b7f4, 0xa0dce5, 0xf0f5fc},
          kPresetDarkStatus}},
        {"moss",
         {0x151c19, 0xcdd7ce, 0x354a40, std::nullopt,
          {0x435248, 0xdf978d, 0xa0c4a2, 0xd4bc8b, 0x99b9cd, 0xbba4c8, 0x8bbdb5, 0xcdd7ce, 0x82968a, 0xf1afa3,
           0xb8dab9, 0xe9d2a5, 0xb1d0e2, 0xd1bddc, 0xa4d6ca, 0xeff5ee},
          kPresetDarkStatus}},
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

ImU32 token_col(const ui::theme::Token& t) {
    return rgb_col(t.rgb, t.alpha);
}

// A loaded theme: the presets and the theme files. The file and where it came from are kept for
// Settings (edit, delete) and so a saved theme can be read again in place.
struct OwnedPalette {
    std::string id, label;
    std::array<ImU32, 16> ansi{};
    Palette p{};
    std::filesystem::path file;  // empty for a preset
    bool user = false;           // from <data>\themes (the person's own)
    bool removed = false;        // deleted this run: no longer listed
};
std::deque<OwnedPalette> g_loaded_palettes;  // a deque keeps addresses stable: g_pal may point in

ui::theme::ThemeFile file_of_preset(const PresetPalette& h) {
    ui::theme::ThemeFile t;
    t.background = h.background;
    t.foreground = h.foreground;
    t.selection = h.selection;
    t.cursor = h.cursor;
    t.ansi = h.ansi;
    t.status = h.status;
    return t;
}

// Fills `o` (its id and label already set) from a theme: the derived interface colours with the
// file's "colors" overrides (ui::theme::derive).
void fill_palette(OwnedPalette& o, const ui::theme::ThemeFile& h) {
    const ui::theme::Derived d = ui::theme::derive(h);
    auto t = [&](const char* k) { return token_col(d.tokens.at(k)); };
    for (std::size_t i = 0; i < 16; ++i)
        o.ansi[i] = rgb_col(h.ansi[i]);
    const ImU32 sel = (t("selection") & 0x00FFFFFFu) | (static_cast<ImU32>(d.light ? 0xa0 : 0x90) << 24);
    o.p = Palette{o.id.c_str(), o.label.c_str(), d.light,       t("canvas"),  t("chrome"),
                  t("surface"), t("border"),     t("accent"),   t("hover"),   t("selected"),
                  t("foreground"), t("muted"),   t("dim"),      t("blocked"), t("done"),
                  t("working"), t("idle"),       t("info"),     t("background"), t("terminal_foreground"),
                  t("cursor"),  sel,             &o.ansi};
}

void add_preset_palette(const std::string& id, const std::string& label, const ui::theme::ThemeFile& h,
                        const std::filesystem::path& file = {}, bool user = false) {
    OwnedPalette& o = g_loaded_palettes.emplace_back();
    o.id = id;
    o.label = label;
    o.file = file;
    o.user = user;
    fill_palette(o, h);
}

// A theme file; nullopt (skipped, not listed) when it does not have the theme file's shape.
std::optional<ui::theme::ThemeFile> read_theme_file(const std::filesystem::path& f) {
    return ui::theme::parse(read_text_file(f));
}

// The file's optional "name", else its file name with spaces ("atom-one-dark" -> "atom one dark").
std::string label_of(std::string name, const ui::theme::ThemeFile& t) {
    if (const auto it = t.raw.find("name"); it != t.raw.end() && it->is_string() && !it->get<std::string>().empty())
        return it->get<std::string>();
    std::replace(name.begin(), name.end(), '-', ' ');
    return name;
}

bool g_themes_loaded = false;

// The presets, then the theme files by name (a file in <data>\themes wins over a shipped one of
// the same name). Loaded once per run, so the addresses g_pal holds stay valid; reload_theme()
// updates one in place after the theme editor saves it.
void load_theme_files() {
    g_themes_loaded = true;
    if (!g_loaded_palettes.empty())
        return;
    for (const auto& [name, pal] : preset_palettes())
        add_preset_palette(std::string(kPresetPrefix) + name, name, file_of_preset(pal));
    std::map<std::string, std::pair<std::filesystem::path, bool>> files;
    std::error_code ec;
    for (const auto& [dir, user] : {std::pair{exe_dir() / L"themes", false}, {data_dir() / L"themes", true}})
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == L".json") {
                const auto stem = e.path().stem().u8string();
                const std::string name(stem.begin(), stem.end());
                files[name] = {e.path(), user};
            }
    for (const auto& [name, file] : files)
        if (const auto t = read_theme_file(file.first))
            add_preset_palette(name, label_of(name, *t), *t, file.first, file.second);
}

OwnedPalette* owned(const std::string& id) {
    for (auto& o : g_loaded_palettes)
        if (o.id == id && !o.removed)
            return &o;
    return nullptr;
}

}  // namespace

std::filesystem::path theme_file_path(const std::string& id) {
    if (!g_themes_loaded)
        load_theme_files();
    const OwnedPalette* o = owned(id);
    return o != nullptr ? o->file : std::filesystem::path();
}

bool is_user_theme(const std::string& id) {
    if (!g_themes_loaded)
        load_theme_files();
    const OwnedPalette* o = owned(id);
    return o != nullptr && o->user;
}

std::string reload_theme(const std::string& name) {
    if (!g_themes_loaded)
        load_theme_files();
    const auto file = data_dir() / L"themes" / win::to_wide(name + ".json");
    std::string why;
    const auto t = ui::theme::parse(read_text_file(file), &why);
    if (!t)
        return "themes\\" + name + ".json can't be read: " + why;
    // A deleted entry of that name is taken back (its address may still be in a setting).
    for (auto& o : g_loaded_palettes)
        if (o.id == name) {
            o.file = file;
            o.user = true;
            o.removed = false;
            o.label = label_of(name, *t);
            fill_palette(o, *t);  // sets the Palette's id and label pointers again
            return {};
        }
    add_preset_palette(name, label_of(name, *t), *t, file, true);
    return {};
}

void forget_theme(const std::string& id) {
    if (OwnedPalette* o = owned(id))
        o->removed = true;
}

Palette palette_from_theme(const ui::theme::ThemeFile& t, const char* id, const char* label,
                           std::array<ImU32, 16>& ansi_storage) {
    OwnedPalette o;
    o.id = id;
    o.label = label;
    fill_palette(o, t);
    ansi_storage = o.ansi;
    Palette p = o.p;
    p.id = id;
    p.label = label;
    p.ansi = &ansi_storage;
    return p;
}

std::uint32_t rgb_of(ImU32 c) {
    return ((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff);
}

ui::color::PaletteColors check_colors_of(const Palette& p) {
    auto flat = [](ImU32 c, std::uint32_t under) { return ui::color::over(rgb_of(c), static_cast<int>(c >> 24), under); };
    ui::color::PaletteColors c;
    c.canvas = flat(p.bg, 0);
    c.chrome = flat(p.sidebar, 0);
    c.surface = flat(p.card, 0);
    c.selected = flat(p.selected, c.chrome);
    c.foreground = rgb_of(p.text);
    c.muted = rgb_of(p.dim);
    c.dim = rgb_of(p.faint);
    c.accent = rgb_of(p.focus);
    c.blocked = rgb_of(p.needs_you);
    c.working = rgb_of(p.working);
    c.done = rgb_of(p.done);
    c.idle = rgb_of(p.idle);
    c.term_bg = flat(p.term_bg, 0);
    c.term_fg = rgb_of(p.term_fg);
    return c;
}

std::string theme_id(std::string id) {
    const auto dash = id.find('-');
    if (dash == std::string::npos || id.starts_with(kPresetPrefix))
        return id;
    for (const auto& [name, pal] : preset_palettes())
        if (id.compare(dash + 1, std::string::npos, name) == 0)
            return std::string(kPresetPrefix) + name;
    return id;
}

// Ours, then the presets and theme files.
std::vector<const Palette*> all_palettes() {
    if (!g_themes_loaded)
        load_theme_files();
    std::vector<const Palette*> out;
    for (const auto& p : kPalettes)
        out.push_back(&p);
    for (const auto& o : g_loaded_palettes)
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

// Appearance "system" follows Windows; "light" and "dark" use the chosen theme of that kind. A
// layout that names a theme overrides both.
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
    apply_style_colors();
    s.WindowRounding = 8;  // overlays: radius 8, popover background, 1 px border
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
    s.ScaleAllSizes(dpi * zoom);
    s.FontScaleDpi = dpi;
    s.FontScaleMain = zoom;
}

// Every ImGui colour from g_pal; sizes stay as they are.
void apply_style_colors() {
    ImGuiStyle& s = ImGui::GetStyle();
    if (g_pal->light)
        ImGui::StyleColorsLight(&s);
    else
        ImGui::StyleColorsDark(&s);
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
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.35f);  // black at 35 % behind modal dialogs
    c[ImGuiCol_TitleBg] = v4(g_pal->card);
    c[ImGuiCol_TitleBgActive] = v4(g_pal->card);
    c[ImGuiCol_NavCursor] = v4(g_pal->focus);
    c[ImGuiCol_TextSelectedBg] = v4(alpha(g_pal->focus, 90));
}
}  // namespace detail

}  // namespace agentdesk::desktop
