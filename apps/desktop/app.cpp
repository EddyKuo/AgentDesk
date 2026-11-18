// AgentDesk window contents. Feature set follows herdr's app (spaces, tabs, split panes, agent
// attention, notifications, command palette, worktrees; observed from its public guide
// defaults/AGENTS.md and keys.json), with AgentDesk's own look, names and code (ADR-12/ADR-13).
#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstdio>
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
#include "agentdesk/ui/git_info.hpp"
#include "agentdesk/ui/keymap.hpp"
#include "agentdesk/ui/mods.hpp"
#include "agentdesk/ui/terminal_view.hpp"
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
};

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
    for (const auto& p : kPalettes)
        if (id == p.id)
            return &p;
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
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, g_pal->light ? 0.25f : 0.55f);
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
    float line_height = 1.0f;    // terminal cell height multiplier
    float cell_width = 1.0f;     // terminal cell width multiplier
    std::string sound_done;      // MP3 replacing the bundled sounds
    std::string sound_blocked;
    std::string welcomed;        // "", "finished" or "skipped"
    std::string seen_version;    // What's new was shown for this version (herdr: once after an update)
    std::vector<std::string> folded_machines;  // machine headers folded in the sidebar
    std::vector<std::string> panels = {"files", "changes"};  // panel buttons in the tab bar
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

Settings load_settings() {
    Settings s;
    std::ifstream in(data_dir() / L"ui.json", std::ios::binary);
    if (!in) {
        s.zoom = first_run_zoom();
        return s;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const auto j = Json::parse(ss.str(), nullptr, false);
    if (j.is_discarded() || j.value("schema_version", 0) != 1)
        return s;
    s.close_notice_shown = j.value("close_notice_shown", false);
    s.last_cwd = j.value("last_cwd", "");
    s.theme = j.value("theme", s.theme);
    s.terminal_weight = std::clamp(j.value("terminal_weight", s.terminal_weight), 100, 900);
    s.ui_font = j.value("ui_font", "");
    // Appearance: files from before it existed keep their one theme as the fixed choice.
    {
        const bool light = std::string(palette_by_id(s.theme)->id) == s.theme && palette_by_id(s.theme)->light;
        s.appearance = j.value("appearance", j.contains("theme") ? (light ? "light" : "dark") : "system");
        s.theme_dark = j.value("theme_dark", light ? std::string("midnight") : s.theme);
        s.theme_light = j.value("theme_light", light ? s.theme : std::string("paper"));
    }
    s.zoom = std::clamp(j.value("zoom", 1.0f), 0.5f, 3.0f);
    s.font_size = std::clamp(j.value("font_size", 14.0f), 9.0f, 28.0f);
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
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << doc.dump(2, ' ', false, Json::error_handler_t::replace) << "\n";
        }
        bool again_ok = true;
        const std::string now = exists ? read_text_file(path, &again_ok) : std::string();
        if (now != before) {  // changed while we wrote: start over from the new text
            std::filesystem::remove(tmp, ec);
            continue;
        }
        ::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        return;
    }
    g_settings_problem = "Not saved to ui.json";
}

// ------------------------------------------------------------------ layouts
// A layout is one whole window: which chrome is shown and what fills the main area. Built-ins
// below; <data>\layouts\<name>.json adds or overrides by name:
//   {"name": "...", "main": "panes|board|grid", "sidebar": true, "details": false,
//    "tab_bar": true, "status_bar": true, "theme": "ember"}
struct LayoutSpec {
    std::string name;
    std::string main = "panes";
    bool sidebar = true;
    std::optional<bool> details;  // unset: the person's toggle
    bool tab_bar = true;
    bool status_bar = true;
    std::string theme;  // optional
};

std::vector<LayoutSpec> load_layouts() {
    std::vector<LayoutSpec> list = {
        {"workspace", "panes", true, std::nullopt, true, true, ""},
        {"board", "board", true, false, false, true, ""},
        {"grid", "grid", false, false, true, true, ""},
        // herdr's second shipped layout (layouts/terminals.json): no sidebar, the terminal tabs.
        {"terminals", "panes", false, false, true, false, ""},
        {"focus", "panes", false, false, false, false, ""},
    };
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(data_dir() / L"layouts", ec)) {
        if (e.path().extension() != L".json")
            continue;
        std::ifstream in(e.path(), std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const auto j = Json::parse(ss.str(), nullptr, false);
        if (!j.is_object())
            continue;
        LayoutSpec l;
        const auto stem = e.path().stem().u8string();
        l.name = j.value("name", std::string(stem.begin(), stem.end()));
        l.main = j.value("main", "panes");
        if (l.main != "panes" && l.main != "board" && l.main != "grid")
            l.main = "panes";
        l.sidebar = j.value("sidebar", true);
        if (j.contains("details") && j["details"].is_boolean())
            l.details = j["details"].get<bool>();
        l.tab_bar = j.value("tab_bar", true);
        l.status_bar = j.value("status_bar", true);
        l.theme = j.value("theme", "");
        std::erase_if(list, [&](const LayoutSpec& x) { return x.name == l.name; });
        list.push_back(l);
    }
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
struct ProgramInfo {
    std::string name;
    ImU32 color;
    std::string glyph;  // one or two letters shown in the program badge
};

ProgramInfo program_info(const std::string& program, const std::string& profile_id) {
    const std::string p = !program.empty() ? program : profile_id;
    auto starts = [&](const char* x) { return p.rfind(x, 0) == 0; };
    if (starts("claude"))
        return {"Claude Code", IM_COL32(226, 125, 92, 255), "Cl"};
    if (starts("codex"))
        return {"Codex", IM_COL32(120, 140, 250, 255), "Cx"};
    if (starts("gemini"))
        return {"Gemini", IM_COL32(80, 160, 240, 255), "Ge"};
    if (starts("opencode"))
        return {"OpenCode", IM_COL32(200, 200, 210, 255), "Oc"};
    if (starts("cursor") || p == "agent")
        return {"Cursor", IM_COL32(170, 170, 180, 255), "Cu"};
    if (starts("aider"))
        return {"Aider", IM_COL32(110, 200, 120, 255), "Ai"};
    if (starts("pwsh"))
        return {"PowerShell", IM_COL32(86, 156, 230, 255), "PS"};
    if (starts("powershell"))
        return {"Windows PowerShell", IM_COL32(60, 120, 200, 255), "PS"};
    if (starts("cmd"))
        return {"Command Prompt", IM_COL32(150, 156, 168, 255), ">_"};
    if (starts("wsl"))
        return {"WSL", IM_COL32(233, 130, 70, 255), "$"};
    if (starts("bash"))
        return {"Bash", IM_COL32(120, 190, 120, 255), "$"};
    if (starts("node"))
        return {"Node.js", IM_COL32(110, 180, 90, 255), "N"};
    if (starts("python") || starts("py"))
        return {"Python", IM_COL32(230, 200, 80, 255), "Py"};
    if (starts("git"))
        return {"Git", IM_COL32(230, 100, 70, 255), "G"};
    if (starts("nvim") || starts("vim"))
        return {"Vim", IM_COL32(90, 170, 90, 255), "V"};
    std::string name = p.empty() ? std::string("program") : p;
    std::uint32_t h = 2166136261u;
    for (const unsigned char ch : name)
        h = (h ^ ch) * 16777619u;
    static constexpr ImU32 kColors[] = {IM_COL32(94, 177, 140, 255),  IM_COL32(230, 140, 80, 255),
                                        IM_COL32(150, 125, 235, 255), IM_COL32(236, 104, 146, 255),
                                        IM_COL32(86, 164, 232, 255),  IM_COL32(214, 178, 72, 255)};
    std::string glyph(1, static_cast<char>(std::toupper(static_cast<unsigned char>(name[0]))));
    return {name, kColors[h % std::size(kColors)], glyph};
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

void program_badge(ImDrawList* dl, ImFont* bold, float size, ImVec2 min, float side, const ProgramInfo& p) {
    dl->AddCircleFilled(ImVec2(min.x + side * 0.5f, min.y + side * 0.5f), side * 0.5f, p.color);
    const float ls = size * (p.glyph.size() > 1 ? 0.62f : 0.75f);
    const ImVec2 t = bold->CalcTextSizeA(ls, FLT_MAX, 0, p.glyph.c_str());
    dl->AddText(bold, ls, ImVec2(min.x + (side - t.x) * 0.5f, min.y + (side - t.y) * 0.5f),
                IM_COL32(255, 255, 255, 240), p.glyph.c_str());
}

void space_badge(ImDrawList* dl, ImFont* bold, float size, ImVec2 min, float side, const std::string& name,
                 bool active) {
    std::uint32_t h = 2166136261u;
    for (const unsigned char ch : name)
        h = (h ^ ch) * 16777619u;
    static constexpr ImU32 kColors[] = {IM_COL32(124, 112, 240, 255), IM_COL32(58, 180, 160, 255),
                                        IM_COL32(220, 120, 80, 255),  IM_COL32(210, 90, 140, 255),
                                        IM_COL32(80, 150, 230, 255),  IM_COL32(200, 170, 60, 255)};
    const ImU32 col = kColors[h % std::size(kColors)];
    dl->AddRectFilled(min, ImVec2(min.x + side, min.y + side), active ? col : alpha(col, 150), side * 0.32f);
    std::string letter = name.empty() ? std::string("?") : name.substr(0, 1);
    if (static_cast<unsigned char>(letter[0]) >= 0x80) {
        std::size_t len = 1;
        while (len < name.size() && (static_cast<unsigned char>(name[len]) & 0xC0) == 0x80)
            ++len;
        letter = name.substr(0, len);
    } else {
        letter[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(letter[0])));
    }
    const float ls = size * 0.8f;
    const ImVec2 t = bold->CalcTextSizeA(ls, FLT_MAX, 0, letter.c_str());
    dl->AddText(bold, ls, ImVec2(min.x + (side - t.x) * 0.5f, min.y + (side - t.y) * 0.5f),
                IM_COL32(255, 255, 255, 240), letter.c_str());
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

// Command names keys.json may bind (herdr validates actions and reports unknown ones). A
// "<mod>.<command>" action is accepted when that mod folder exists.
bool known_command(std::string_view name) {
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
        "window.minimize", "worktree.new", "worktree.open", "worktree.remove", "app.quit", "overlay.close"};
    if (kNames.contains(name))
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
struct GitInfo {
    bool repo = false;
    std::string top;         // checkout top (a worktree's own folder)
    std::string common_top;  // main checkout's top (differs for a linked worktree)
    bool worktree = false;
    std::string branch;      // "HEAD" when detached
    int ahead = 0;
    int behind = 0;
};

class GitCache {
public:
    GitCache() : worker_([this](std::stop_token st) { run(st); }) {}
    ~GitCache() {
        worker_.request_stop();
        cv_.notify_all();
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
        if (!e.queued && (e.at == Clock::time_point{} || now - e.at > std::chrono::seconds(60))) {
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

private:
    struct Entry {
        GitInfo info;
        Clock::time_point at{};
        bool queued = false;
    };

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
            e.info = info;
            e.at = Clock::now();
            e.queued = false;
        }
    }

    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<std::string> queue_;
    std::map<std::string, Entry> entries_;
    std::jthread worker_;
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
        std::string msg = commit_msg;
        while (!msg.empty() && (msg.back() == ' ' || msg.back() == '\n'))
            msg.pop_back();
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
                std::string out = r.output;
                while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
                    out.pop_back();
                commit_note = "Commit failed: " + (r.started ? out : std::string("git not found"));
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
    static char letter_of(const std::string& xy) {
        if (xy.find('U') != std::string::npos || xy == "AA" || xy == "DD")
            return 'U';
        if (xy.find('D') != std::string::npos)
            return 'D';
        if (xy == "??" || xy.find('A') != std::string::npos)
            return 'A';
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
                m[rel] = letter_of(xy);
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
    std::map<std::string, Attention> last_att;
    std::map<std::string, Clock::time_point> changed_at;
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
    bool show_welcome = false;
    bool welcome_panels_seen = false;
    bool welcome_copied = false;
    bool show_notes = false;  // What's new / Send feedback dialog
    std::string notes_kind;   // "whats_new" or "feedback"
    char feedback_buf[4096] = {};
    std::string feedback_note;
    int welcome_page = 0;
    bool show_machine_add = false;
    std::vector<std::pair<std::string, std::string>> ssh_hosts;
    char machine_buf[256] = {};
    std::string confirm_title, confirm_detail, confirm_ok, confirm_command;
    std::string settings_tab;  // tab to select when the settings window opens
    bool show_worktree_open = false;
    std::vector<WorktreeEntry> worktree_list;
    std::string worktree_machine;  // machine the listed worktrees are on
    char wt_filter[128] = {};
    int wt_sel = 0;

    // ---------------------------------------------------------------- helpers
    [[nodiscard]] std::string title_of(const ui::SessionView& v) const {
        if (!v.name.empty())
            return v.name;
        return program_info(v.program, v.profile_id).name;
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
        if (it != views_by_id.end() && (it->second.lost || it->second.process_state != "Running"))
            return Attention::kNone;
        return attention.get(id);
    }
    // Folder that names an unnamed space (herdr): the repo's top folder, else the live folder.
    [[nodiscard]] std::map<std::string, std::string> session_cwds() const {
        std::map<std::string, std::string> m;
        for (const auto& [id, v] : views_by_id) {
            const auto mi = machine_of.find(id);
            const bool remote = mi != machine_of.end() && !mi->second.empty();
            const std::string top = remote ? std::string() : ui::git_top(v.cwd);
            m[id] = top.empty() ? v.cwd : top;
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
        if (!settings.toasts && !session.empty())
            return;
        if (seconds <= 0)
            seconds = color == g_pal->needs_you || color == g_pal->working ? 8 : 5;
        toasts.push_back({std::move(title), std::move(body), std::move(session), color,
                          Clock::now() + std::chrono::seconds(seconds)});
        while (toasts.size() > 5)
            toasts.pop_front();
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

}  // namespace

App::App(AppConfig config, std::unique_ptr<ui::HostClient> host) : s_(std::make_unique<State>()) {
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
    s_->layouts = load_layouts();
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
        h->call(
            method, params,
            [done](Json r, std::string error) {
                done->set_value(error.empty() ? std::optional<Json>(std::move(r)) : std::nullopt);
            },
            std::chrono::seconds(60));
        if (result.wait_for(std::chrono::seconds(65)) != std::future_status::ready)
            return std::nullopt;
        return result.get();
    };
    s_->git.remote = s_->panels->remote;
    s_->mods = std::make_unique<ui::ModHost>(data_dir() / L"mods");
    s_->mods->load(s_->settings.mods_enabled);
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

App::~App() = default;

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
    for (const auto& [id, v] : s_->views_by_id) {
        if (v.agent_state == "Working")
            ms = std::min<long long>(ms, 50);  // the working mark spins
    }
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

void create_session(App::State& s, const std::string& profile, const std::string& cwd,
                    const std::string& name, Placement placement) {
    Json params{{"profile_id", profile}, {"cwd", cwd}};
    if (!name.empty())
        params["name"] = name;
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
                s.ws.add_tab(p.session, sp);
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
        present.insert(sess->id());
        s.awaiting_list.erase(sess->id());
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
        if (v.process_state == "Exited" && !initial)
            continue;  // finished elsewhere; listed in the sidebar, not opened
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
        // A program that ends with an error right after it was started here: say why.
        const auto prev = s.prev_process_state[id];
        if (prev != v.process_state) {
            s.prev_process_state[id] = v.process_state;
            const auto l = s.launched_here.find(id);
            if (v.process_state == "Exited" && v.exit_code && *v.exit_code != 0 &&
                l != s.launched_here.end() && Clock::now() - l->second < std::chrono::seconds(4)) {
                std::string tail;
                if (const auto it = s.by_id.find(id); it != s.by_id.end()) {
                    const auto snap = it->second->snapshot();
                    std::vector<std::string> lines;
                    for (int r = 0; r < snap->rows; ++r) {
                        auto t = snap->row_text(r);
                        if (!t.empty())
                            lines.push_back(t);
                    }
                    for (std::size_t i = lines.size() > 3 ? lines.size() - 3 : 0; i < lines.size(); ++i)
                        tail += (tail.empty() ? "" : "\n") + lines[i];
                }
                char head[96];
                std::snprintf(head, sizeof head, "%s exited with code %u (0x%08X)", s.pane_title(id).c_str(),
                              *v.exit_code, *v.exit_code);
                s.toast(head, tail, id, g_pal->needs_you, 10);
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
    }
    return r;
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
    for (std::string line; std::getline(ss, line);) {
        // " <sha> <path> (<describe>)"
        std::stringstream ls(line.size() > 1 ? line.substr(1) : std::string());
        std::string sha, path;
        ls >> sha >> path;
        if (path.empty())
            continue;
        r.submodules = true;
        const auto sp = p / win::to_wide(path);
        const auto ahead = git_at(s, machine, win::to_utf8(sp.native()), {L"rev-list", L"--count", L"@{u}..HEAD"});
        if (ahead.started && (ahead.exit_code != 0 || std::atoi(ahead.output.c_str()) > 0))
            r.unpushed.push_back(path);  // no upstream counts as unpushed
    }
    return r;
}

std::string worktree_remove_text(const std::string& top, const WorktreeRisk& r) {
    std::string t = "Closes the space and deletes " + display_path(top) + ". The branch stays.";
    if (r.dirty)
        t += "\n\nIt has uncommitted changes: they will be lost.";
    if (!r.unpushed.empty()) {
        t += "\n\nSubmodules with commits that are not pushed: ";
        for (std::size_t i = 0; i < r.unpushed.size(); ++i)
            t += (i ? ", " : "") + r.unpushed[i];
        t += ". They will be lost.";
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
    s.show_rename = true;
    s.rename_kind = kind;
    s.rename_target = target;
    std::snprintf(s.rename_buf, sizeof s.rename_buf, "%s", current.c_str());
}

void run_command(App::State& s, const std::string& cmd) {
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
    if (name == "layout.toggle" && arg == "sidebar") {  // herdr ctrl-shift-b
        run_command(s, "sidebar.toggle");
        return;
    }
    if (const auto al = kHerdrNames.find(name); al != kHerdrNames.end()) {
        run_command(s, arg.empty() ? al->second : al->second + ":" + arg);
        return;
    }
    if (name == "mod.open" && arg == "picker") {
        run_command(s, "app.picker");
        return;
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
        return;
    }
    if (name == "space.move") {  // herdr space.move {index} (1 = first): the active space
        const int to = std::atoi(arg.c_str()) - 1;
        if (to >= 0 && s.ws.move_space(s.ws.active_space, to)) {
            s.doc_dirty = true;
            s.doc_dirty_at = Clock::now();
        }
        return;
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
        if (!focused.empty()) {
            const auto* t = s.ws.active_tab();
            if (t != nullptr && s.ws.sessions_in(*t).size() == 1) {
                run_command(s, "tab.close");
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
        s.settings.sidebar = true;
        s.settings.sidebar_width = 268;
        save_settings(s.settings);
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
        if (!focused.empty())
            open_rename(s, "pane", focused, s.pane_title(focused));
    } else if (name == "tab.rename") {
        if (auto* t = s.ws.active_tab())
            open_rename(s, "tab", t->id, t->name);
    } else if (name == "space.rename") {
        if (auto* sp = s.ws.active())
            open_rename(s, "space", sp->id, sp->name);
    } else if (name == "tab.close" && s.ws.active() != nullptr && s.ws.active()->tabs.size() == 1) {
        // Closing the last tab closes the space: herdr asks first (window.rs 3120-3131).
        confirm_close_space(s, s.ws.active_space);
    } else if (name == "tab.close") {
        if (auto* t = s.ws.active_tab()) {
            for (const auto& id : s.ws.sessions_in(*t)) {  // herdr: ends the tab's terminals
                if (auto it = s.by_id.find(id); it != s.by_id.end())
                    it->second->stop(true);
                s.hidden.insert(id);
            }
            s.ws.close_tab(s.ws.active_space, s.ws.active()->active_tab);
            s.focus_view(s.ws.focused_session());
        }
    } else if (name == "space.close") {
        // herdr asks first ("Close space?"), then ends its terminals.
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
        // herdr toggleSidebar(): remember the width and shrink to 44, or go back to it.
        if (!s.settings.sidebar)
            s.settings.sidebar = true;
        else if (s.settings.sidebar_width >= 160) {
            s.settings.sidebar_restore = s.settings.sidebar_width;
            s.settings.sidebar_width = 44;
        } else {
            s.settings.sidebar_width = std::max(160.0f, s.settings.sidebar_restore);
        }
        save_settings(s.settings);
    } else if (name == "app.picker") {
        s.show_picker = !s.show_picker;
        s.picker_buf[0] = '\0';
        s.picker_sel = -1;
        s.picker_reset = true;
    } else if (name == "tab.goto" || name == "tab.close_at") {
        const auto c = arg.find(':');
        const int sp = std::atoi(arg.substr(0, c).c_str());
        const int t = c == std::string::npos ? 0 : std::atoi(arg.substr(c + 1).c_str());
        s.ws.select_space(sp);
        s.ws.select_tab(t);
        if (name == "tab.close_at")
            run_command(s, "tab.close");
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
        const std::string cwd = s.focused_cwd();
        const std::string machine = s.machine_of[s.ws.focused_session()];
        s.worktree_machine = machine;
        const auto g = s.git.get(cwd, machine);
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
                        e.main = _stricmp(e.path.c_str(), g.common_top.c_str()) == 0;
                        const auto t = git_at(s, machine, e.path, {L"log", L"-1", L"--format=%ct"},
                                              std::chrono::seconds(5));
                        e.time = std::atoll(t.output.c_str());
                        const long long age = static_cast<long long>(std::time(nullptr)) - e.time;
                        e.age = e.time == 0        ? std::string()
                                : age < 3600       ? std::to_string(std::max(1LL, age / 60)) + "m"
                                : age < 86400      ? std::to_string(age / 3600) + "h"
                                : age < 86400 * 60 ? std::to_string(age / 86400) + "d"
                                                   : std::to_string(age / (86400 * 30)) + "mo";
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
        if (s.worktree_list.empty())
            s.toast("Not a git checkout", "Open worktree needs a terminal in a git repository.", "", g_pal->info);
        else
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
            return;
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
        if (g.repo && !g.worktree) {
            s.toast("Not a worktree", "This is the repository's main checkout, not a worktree.", "", g_pal->working);
        } else if (g.repo) {
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
            run_command(s, force + machine + "|" + top);
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
        const auto r = git_at(s, machine, main, argv);
        if (!r.started || r.exit_code != 0)
            s.toast("Could not remove the worktree", r.started ? r.output : std::string("git did not run"), "",
                    g_pal->needs_you, 10);
        s.git.invalidate(top);
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
            s.views[focused].view.request(name == "terminal.paste_image" ? std::string("terminal.paste") : cmd);
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
        s.settings.sidebar = !s.settings.sidebar;
        save_settings(s.settings);
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
        const Palette* p = palette_by_id(arg);
        (p->light ? s.settings.theme_light : s.settings.theme_dark) = p->id;
        if (s.settings.appearance != "system")
            s.settings.appearance = p->light ? "light" : "dark";
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
    } else if (name == "app.whats_new" || name == "app.feedback") {
        s.show_notes = true;
        s.notes_kind = name == "app.whats_new" ? "whats_new" : "feedback";
        s.feedback_note.clear();
        if (name == "app.feedback" && !arg.empty() && s.feedback_buf[0] == '\0')
            std::snprintf(s.feedback_buf, sizeof s.feedback_buf, "%s", arg.c_str());
    } else if (name == "layout.clone") {
        // herdr: duplicate a layout as <name>-copy and show it (a file you can edit).
        const std::string from = arg.empty() ? s.settings.layout : arg;
        const auto it =
            std::find_if(s.layouts.begin(), s.layouts.end(), [&](const LayoutSpec& l) { return l.name == from; });
        if (it != s.layouts.end()) {
            const auto dir = data_dir() / L"layouts";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            auto taken = [&](const std::string& n) {
                return std::any_of(s.layouts.begin(), s.layouts.end(), [&](const LayoutSpec& l) { return l.name == n; });
            };
            std::string to = from + "-copy";
            for (int n = 2; taken(to); ++n)
                to = from + "-copy-" + std::to_string(n);
            Json j{{"name", to},
                   {"main", it->main},
                   {"sidebar", it->sidebar},
                   {"tab_bar", it->tab_bar},
                   {"status_bar", it->status_bar}};
            if (it->details)
                j["details"] = *it->details;
            if (!it->theme.empty())
                j["theme"] = it->theme;
            std::ofstream(dir / win::to_wide(to + ".json"), std::ios::binary) << j.dump(2);
            s.layouts = load_layouts();
            run_command(s, "layout:" + to);
            s.toast("Layout duplicated", "layouts\\" + to + ".json", "", g_pal->info);
        }
    } else if (name == "layout.reset") {
        // herdr: reset a shipped layout and its sizes; a file that changed it is kept aside.
        const std::string which = arg.empty() ? s.settings.layout : arg;
        const auto dir = data_dir() / L"layouts";
        std::error_code ec;
        int aside = 0;
        std::vector<std::filesystem::path> files;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == L".json")
                files.push_back(e.path());
        for (const auto& f : files) {
            std::string text;
            {
                std::ifstream in(f, std::ios::binary);
                std::stringstream ss;
                ss << in.rdbuf();
                text = ss.str();
            }
            const auto j = Json::parse(text, nullptr, false);
            const auto stem = f.stem().u8string();
            const std::string file_name(stem.begin(), stem.end());
            const std::string n = j.is_object() ? j.value("name", file_name) : file_name;
            if (n != which)
                continue;
            auto to = f;
            to += L".aside";
            std::filesystem::rename(f, to, ec);
            aside += ec ? 0 : 1;
        }
        if (which == "workspace" || which == "terminals" || which == "board" || which == "grid" || which == "focus") {
            const Settings d;
            s.settings.sidebar_width = d.sidebar_width;
            s.settings.panel_width = d.panel_width;
            save_settings(s.settings);
        }
        s.layouts = load_layouts();
        if (std::none_of(s.layouts.begin(), s.layouts.end(),
                         [&](const LayoutSpec& l) { return l.name == s.settings.layout; }))
            run_command(s, "layout:workspace");
        s.toast("Layout reset: " + which, aside > 0 ? "Your file was kept aside as <name>.json.aside." : "", "",
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
        s.layouts = load_layouts();
        s.mods->load(s.settings.mods_enabled);
        s.toast("Mods reloaded", "", "", g_pal->info, 2);
    } else if (name == "mods.folder") {
        const auto dir = data_dir() / L"mods";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        ::ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (name == "mod") {
        const auto c2 = arg.find(':');
        if (c2 != std::string::npos)
            s.mods->run_command(arg.substr(0, c2), arg.substr(c2 + 1));
    }
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
void draw_mod_panels(App::State& s, const std::string& where) {
    const float inset = px(18);
    for (const auto& panel : s.mods->panels(where)) {
        ImGui::PushID((panel.mod + "/" + panel.id).c_str());
        ImGui::Dummy(ImVec2(0, px(8)));
        ImGui::Indent(inset);
        if (!panel.title.empty()) {
            ImGui::PushFont(s.fonts.ui_medium, fpx(11.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
            ImGui::TextUnformatted(panel.title.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, px(2)));
        }
        ImGui::PushFont(s.fonts.ui, fpx(12.5f));
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
bool begin_menu(const char* id) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(5), px(5)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(px(200), 0), ImVec2(FLT_MAX, FLT_MAX));
    const bool open = ImGui::BeginPopupContextItem(id);
    ImGui::PopStyleVar(2);
    return open;
}
bool menu_row(App::State& s, const char* glyph, const char* label, const std::string& chord = {}, bool enabled = true) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float w = std::max(ImGui::GetContentRegionAvail().x, px(190));
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(w, px(31)));
    ImGui::EndDisabled();
    const bool hov = enabled && ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hov)
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + px(31)), g_pal->selected, px(7));
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

// "<1m", "5m", "2h", "3d" (agents.js 67-73).
std::string ago(Clock::time_point t) {
    const auto m = std::chrono::duration_cast<std::chrono::minutes>(Clock::now() - t).count();
    if (m < 1)
        return "<1m";
    if (m < 60)
        return std::to_string(m) + "m";
    if (m < 60 * 24)
        return std::to_string(m / 60) + "h";
    return std::to_string(m / (60 * 24)) + "d";
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

// "Close space?" (window.rs 3067-3080): "<name> — N tabs, M terminals".
void confirm_close_space(App::State& s, int index) {
    if (index < 0 || index >= static_cast<int>(s.ws.spaces.size()))
        return;
    const auto& sp = s.ws.spaces[static_cast<std::size_t>(index)];
    std::size_t terms = 0;
    for (const auto& t : sp.tabs)
        terms += s.ws.sessions_in(t).size();
    char detail[256];
    std::snprintf(detail, sizeof detail, "%s \xE2\x80\x94 %zu tab%s, %zu terminal%s",
                  s.ws.space_title(index, s.session_cwds()).c_str(), sp.tabs.size(), sp.tabs.size() == 1 ? "" : "s",
                  terms, terms == 1 ? "" : "s");
    open_confirm(s, "Close space?", detail, "Close", "space.close_now:" + sp.id);
}

// ---------------------------------------------------------------- sidebar: brand row
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
    if (icon_button(s, "##sidebar-toggle", ui::icon::kSidebar, ImVec2(bx, cy - b * 0.5f), b, fpx(15),
                    rail ? "Expand sidebar" : "Collapse sidebar", g_pal->dim))
        run_command(s, "sidebar.rail");
    if (!rail) {
        bx -= b + px(4);
        const std::string tip = "Find a space or agent  " + s.keymap.chord_for("app.picker");
        if (icon_button(s, "##search", ui::icon::kSearch, ImVec2(bx, cy - b * 0.5f), b, fpx(15), tip.c_str(),
                        g_pal->dim))
            run_command(s, "app.picker");
    }
    window_drag_area(s, pos, ImVec2(pos.x + w, pos.y + h));
}

// ---------------------------------------------------------------- sidebar: spaces
struct SpaceRow {
    int index = -1;           // space index, or -1 for a machine header
    std::string machine;      // header: machine name ("" = this computer)
    int tree = 0;             // 0 = top level, 1 = worktree "├─", 2 = last worktree "└─"
};

std::vector<SpaceRow> space_rows(App::State& s, const std::map<std::string, std::string>& cwds) {
    // Folder of each space's first terminal, and its git facts.
    std::vector<GitInfo> info(s.ws.spaces.size());
    for (std::size_t i = 0; i < s.ws.spaces.size(); ++i) {
        const auto& sp = s.ws.spaces[i];
        for (const auto& t : sp.tabs) {
            const auto ids = s.ws.sessions_in(t);
            if (!ids.empty()) {
                const auto it = s.views_by_id.find(ids.front());
                if (it != s.views_by_id.end())
                    info[i] = s.git.get(it->second.cwd, sp.machine);
                break;
            }
        }
    }
    (void)cwds;
    std::vector<std::string> machines = {""};
    for (const auto& r : s.remotes)
        machines.push_back(r.cfg.name);
    for (const auto& sp : s.ws.spaces)
        if (std::find(machines.begin(), machines.end(), sp.machine) == machines.end())
            machines.push_back(sp.machine);
    std::vector<SpaceRow> rows;
    for (const auto& m : machines) {
        if (!s.remotes.empty())
            rows.push_back({-1, m, 0});
        if (s.folded_machines.contains(m))
            continue;
        // Linked worktrees go directly under the first main-checkout space of their repo.
        std::vector<int> top;
        std::map<int, std::vector<int>> nested;
        for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i) {
            if (s.ws.spaces[static_cast<std::size_t>(i)].machine != m)
                continue;
            const auto& g = info[static_cast<std::size_t>(i)];
            int parent = -1;
            if (g.repo && g.worktree) {
                for (int j = 0; j < static_cast<int>(s.ws.spaces.size()) && parent < 0; ++j) {
                    const auto& h = info[static_cast<std::size_t>(j)];
                    if (j != i && s.ws.spaces[static_cast<std::size_t>(j)].machine == m && h.repo && !h.worktree &&
                        _stricmp(h.top.c_str(), g.common_top.c_str()) == 0)
                        parent = j;
                }
            }
            if (parent >= 0)
                nested[parent].push_back(i);
            else
                top.push_back(i);
        }
        for (const int i : top) {
            rows.push_back({i, m, 0});
            const auto& kids = nested[i];
            for (std::size_t k = 0; k < kids.size(); ++k)
                rows.push_back({kids[k], m, k + 1 == kids.size() ? 2 : 1});
        }
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
    if (menu_row(s, ui::icon::kCommand, "Rename", s.keymap.chord_for("space.rename"))) {
        s.ws.select_space(i);
        open_rename(s, "space", sp.id, sp.name);
    }
    if (g.repo) {
        if (menu_row(s, ui::icon::kWorktree, "New worktree", s.keymap.chord_for("worktree.new"))) {
            s.ws.select_space(i);
            run_command(s, "worktree.new");
        }
        if (menu_row(s, ui::icon::kFolderOpen, "Open worktree")) {
            s.ws.select_space(i);
            run_command(s, "worktree.open");
        }
        if (g.worktree && menu_row(s, ui::icon::kClose, "Remove worktree")) {
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
    const auto cwds = s.session_cwds();
    s.ws.assign_colors();
    // Header (31 px): "Spaces" + "+".
    if (!rail) {
        const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, "Spaces");
        dl->AddText(s.fonts.ui_medium, fpx(11.5f), ImVec2(pos.x + px(16), pos.y + (px(31) - t.y) * 0.5f), g_pal->faint,
                    "Spaces");
    }
    {
        const float b = px(26);
        const ImVec2 bp = rail ? ImVec2(pos.x + (w - b) * 0.5f, pos.y + px(2)) : ImVec2(pos.x + w - px(10) - b, pos.y + px(2.5f));
        const std::string tip = "New space  " + s.keymap.chord_for("space.new");
        if (icon_button(s, "##space-new", ui::icon::kPlus, bp, b, fpx(14), tip.c_str(), g_pal->dim))
            run_command(s, "space.new");
    }
    const float head = px(31);
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + head));
    ImGui::BeginChild("##spaces", ImVec2(w, std::max(px(20), h - head)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float pad = rail ? 0.0f : px(8);
    const float rw = ImGui::GetContentRegionAvail().x - pad * 2;
    const auto rows = space_rows(s, cwds);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int drop_target = -1;
    float drop_y = 0;
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
                if (hov)
                    ImGui::SetTooltip("%s", s.machine_label(row.machine).c_str());
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
                if (over_plus)
                    ImGui::SetTooltip("New space on %s", s.machine_label(row.machine).c_str());
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
            // herdr: an empty, unfolded machine shows "+ New space there" (or "Loading..." while
            // it connects).
            const bool empty = ri + 1 >= rows.size() || rows[ri + 1].index < 0;
            if (empty && !rail && !s.folded_machines.contains(row.machine)) {
                const ImVec2 e(a0.x + pad, a.y + hh);
                ImGui::SetCursorScreenPos(e);
                ImGui::PushID(("e:" + row.machine).c_str());
                const bool clicked = ImGui::InvisibleButton("##empty", ImVec2(rw, px(28)));
                const bool eh = on && ImGui::IsItemHovered();
                ImGui::PopID();
                if (eh)
                    d->AddRectFilled(e, ImVec2(e.x + rw, e.y + px(28)), g_pal->hover, px(6));
                const std::string text = on ? std::string("+ New space there") : std::string("Loading\xE2\x80\xA6");
                d->AddText(s.fonts.ui, fpx(12), ImVec2(e.x + px(28), e.y + px(14) - fpx(12) * 0.6f), g_pal->dim, text.c_str());
                if (clicked && on)
                    run_command(s, "space.new_on:" + row.machine);
                ImGui::SetCursorScreenPos(ImVec2(a0.x, e.y + px(28)));
            }
            continue;
        }
        const int i = row.index;
        const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
        const bool active = i == s.ws.active_space;
        const std::string title = s.ws.space_title(i, cwds);
        ImGui::PushID(sp.id.c_str());
        if (rail) {
            // 32x26 cell: position number, status glyph at the right (spaces.js 501-515).
            const float cw = px(32), ch = px(26);
            const ImVec2 c(a0.x + (ImGui::GetContentRegionAvail().x - cw) * 0.5f, a0.y);
            ImGui::SetCursorScreenPos(c);
            if (ImGui::InvisibleButton("##sr", ImVec2(cw, ch))) {
                s.ws.select_space(i);
                s.focus_view(s.ws.focused_session());
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", title.c_str());
            if (active)
                d->AddRectFilled(c, ImVec2(c.x + cw, c.y + ch), g_pal->selected, px(6));
            const std::string num = std::to_string(i + 1);
            const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fpx(12), FLT_MAX, 0, num.c_str());
            d->AddText(s.fonts.ui_medium, fpx(12), ImVec2(c.x + (cw - t.x) * 0.5f, c.y + (ch - t.y) * 0.5f),
                       active ? g_pal->text : g_pal->dim, num.c_str());
            std::vector<Attention> atts;
            for (const auto& t2 : sp.tabs)
                for (const auto& id : s.ws.sessions_in(t2))
                    atts.push_back(s.attention_of(id));
            const Attention att = ui::most_urgent(atts);
            const bool strong = att == Attention::kBlocked || att == Attention::kDone;
            d->AddCircleFilled(ImVec2(c.x + cw - px(5), c.y + ch * 0.5f), strong ? px(2.5f) : px(1.2f),
                               att == Attention::kNone ? g_pal->faint : attention_color(att));
            ImGui::SetCursorScreenPos(ImVec2(a0.x, c.y + ch + px(2)));
            ImGui::PopID();
            continue;
        }
        const float rh = row.tree > 0 ? px(28) : px(30);
        ImGui::SetCursorScreenPos(a);
        ImGui::InvisibleButton("##space", ImVec2(rw, rh), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hov = ImGui::IsItemHovered();
        const ImVec2 b(a.x + rw, a.y + rh);
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
        if (row.tree > 0) {
            d->AddText(s.fonts.ui, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f), faded(g_pal->faint),
                       row.tree == 2 ? "\xE2\x94\x94\xE2\x94\x80" : "\xE2\x94\x9C\xE2\x94\x80");
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
        GitInfo g;
        for (const auto& t2 : sp.tabs) {
            const auto ids = s.ws.sessions_in(t2);
            if (!ids.empty() && s.views_by_id.count(ids.front())) {
                g = s.git.get(s.views_by_id[ids.front()].cwd, sp.machine);
                break;
            }
        }
        const GitTail tail = git_tail(g, title, row.tree > 0);
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
        if (hov && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            ImGui::SetTooltip("%s", title.c_str());
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
    const auto pi = program_info(v.program, v.profile_id);
    if (title.empty() || title.find('@') != std::string::npos || title.find(":\\") != std::string::npos ||
        title.front() == '/' || title.front() == '~' || _stricmp(title.c_str(), pi.name.c_str()) == 0 ||
        _stricmp(title.c_str(), v.program.c_str()) == 0)
        return {};
    return title;
}

void draw_agents(App::State& s, ImVec2 pos, float w, float h, bool rail) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(pos.x, pos.y), ImVec2(pos.x + w, pos.y), g_pal->border);
    const bool grouped = s.settings.agents_sort == "space";
    // Agents and their sub-agents.
    std::map<std::string, std::vector<std::string>> children;
    std::vector<std::string> list;
    for (const auto& sess : s.sessions) {
        const auto& v = s.views_by_id[sess->id()];
        if (!is_agent(v) || v.lost)
            continue;
        if (!v.parent.empty() && s.views_by_id.count(v.parent) && !s.ws.pinned.contains(sess->id()))
            children[v.parent].push_back(sess->id());
        else
            list.push_back(sess->id());
    }
    // Last state change, done and idle counting as one (agents.js 95-110).
    for (const auto& id : list) {
        Attention a = s.attention_of(id);
        if (a == Attention::kDone)
            a = Attention::kIdle;
        const auto it = s.last_att.find(id);
        if (it == s.last_att.end() || it->second != a) {
            s.last_att[id] = a;
            if (it != s.last_att.end() || !s.changed_at.count(id))
                s.changed_at[id] = Clock::now();
        }
    }
    auto rollup = [&](const std::string& id) {
        int u = urgency(s.attention_of(id));
        for (const auto& c : children[id])
            u = std::max(u, urgency(s.attention_of(c)));
        return u;
    };
    std::vector<std::string> order = list;
    if (!grouped) {
        std::stable_sort(order.begin(), order.end(), [&](const std::string& a, const std::string& b) {
            const int ua = rollup(a), ub = rollup(b);
            if (ua != ub)
                return ua > ub;
            return s.changed_at[a] > s.changed_at[b];
        });
    } else {
        // Model order: space, tab, pane.
        const auto all = s.ws.all_sessions();
        std::stable_sort(order.begin(), order.end(), [&](const std::string& a, const std::string& b) {
            const auto ia = std::find(all.begin(), all.end(), a) - all.begin();
            const auto ib = std::find(all.begin(), all.end(), b) - all.begin();
            return ia < ib;
        });
    }
    std::stable_partition(order.begin(), order.end(), [&](const std::string& id) { return s.ws.pinned.contains(id); });
    s.agent_order.clear();
    for (const auto& id : order) {
        s.agent_order.push_back(id);
        for (const auto& c : children[id])
            s.agent_order.push_back(c);
    }

    float y = pos.y;
    if (!rail) {
        // Header (41 px: pt 14, pb 5): "Agents" + sort button.
        const float hy = pos.y + px(14);
        const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, "Agents");
        dl->AddText(s.fonts.ui_medium, fpx(11.5f), ImVec2(pos.x + px(16), hy + (px(22) - t.y) * 0.5f), g_pal->faint,
                    "Agents");
        const std::string tip = std::string("Sort agents: ") + (grouped ? "Grouped by space" : "Priority");
        if (icon_button(s, "##agents-sort", ui::icon::kSort, ImVec2(pos.x + w - px(16) - px(22), hy), px(22), fpx(14),
                        tip.c_str(), g_pal->faint))
            ImGui::OpenPopup("##sortagents");
        ImGui::SetNextWindowSize(ImVec2(px(200), 0));
        if (ImGui::BeginPopup("##sortagents")) {
            ImGui::TextDisabled("Sort agents");
            if (ImGui::MenuItem("Priority", nullptr, !grouped)) {
                s.settings.agents_sort = "priority";
                save_settings(s.settings);
            }
            if (ImGui::MenuItem("Grouped by space", nullptr, grouped)) {
                s.settings.agents_sort = "space";
                save_settings(s.settings);
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
    const auto cwds = s.session_cwds();

    if (order.empty() && !rail) {
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + px(12), ImGui::GetCursorScreenPos().y));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - px(36));
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->faint));
        ImGui::PushFont(s.fonts.ui, fpx(12));
        ImGui::TextWrapped("No agent status yet. Agents show up here when they report their status.");
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }

    auto agent_row = [&](const std::string& id, bool full, bool child, bool last_child) {
        const auto& v = s.views_by_id[id];
        ImGui::PushID(id.c_str());
        const ImVec2 a0 = ImGui::GetCursorScreenPos();
        if (rail) {
            const ImVec2 a(a0.x + px(6), a0.y);
            const float rh = px(26);
            ImGui::SetCursorScreenPos(a);
            if (ImGui::InvisibleButton("##ar", ImVec2(rw, rh)))
                s.focus(id);
            const bool hov = ImGui::IsItemHovered();
            if (id == s.ws.focused_session())
                d->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), g_pal->selected, px(6));
            else if (hov)
                d->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), g_pal->hover, px(6));
            if (hov)
                ImGui::SetTooltip("%s", s.pane_title(id).c_str());
            const int n = static_cast<int>(std::find(s.agent_order.begin(), s.agent_order.end(), id) -
                                           s.agent_order.begin()) + 1;
            const std::string num = std::to_string(n);
            const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, num.c_str());
            d->AddText(s.fonts.ui, fpx(11), ImVec2(a.x + px(4), a.y + (rh - t.y) * 0.5f), g_pal->faint, num.c_str());
            status_mark(s, d, a.x + rw - px(16), a.y + rh * 0.5f, s.attention_of(id), true);
            ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + rh));
            ImGui::PopID();
            return;
        }
        const float rh = child ? px(26) : px(30);
        const ImVec2 a(a0.x + pad, a0.y);
        const ImVec2 b(a.x + rw, a.y + rh);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton("##agent", ImVec2(rw, rh));
        const bool hov = ImGui::IsItemHovered();
        const bool shown = id == s.ws.focused_session();
        if (shown)
            d->AddRectFilled(a, b, g_pal->selected, px(8));
        else if (hov)
            d->AddRectFilled(a, b, g_pal->hover, px(8));
        const float cy = a.y + rh * 0.5f;
        const Attention att = s.attention_of(id);
        float x = a.x + px(8);
        if (child) {
            // Tree line: 9x17 box, 1.5 px left/bottom border (agents.js 182-206).
            const float tx = a.x + px(14);
            d->AddLine(ImVec2(tx, a.y - px(4)), ImVec2(tx, last_child ? cy : b.y), g_pal->border, px(1.5f));
            d->AddLine(ImVec2(tx, cy), ImVec2(tx + px(9), cy), g_pal->border, px(1.5f));
            x = a.x + px(31);
        }
        const float isz = child ? px(14) : px(16);
        program_badge(d, s.fonts.ui_bold, fpx(child ? 11.0f : 12.5f), ImVec2(x, cy - isz * 0.5f), isz,
                      program_info(v.program, v.profile_id));
        x += isz + px(9);
        // Right side: status mark, then time or pin (on hover).
        float rx = b.x - px(8);
        const bool compact = !full;
        const float mw = status_mark(s, d, 0, cy, att, compact, true);
        status_mark(s, d, rx - mw, cy, att, compact);
        rx -= mw + px(6);
        if (!child) {
            const auto& kids = children[id];
            if (!kids.empty()) {
                const bool open = !s.folded_agents.contains(id);
                const float cw = px(18);
                ImGui::SetCursorScreenPos(ImVec2(rx - cw, cy - px(9)));
                if (ImGui::InvisibleButton("##fold", ImVec2(cw, px(18)))) {
                    if (open)
                        s.folded_agents.insert(id);
                    else
                        s.folded_agents.erase(id);
                }
                if (open) {
                    draw_icon(s, d, ui::icon::kChevronDown, fpx(11), ImVec2(rx - cw * 0.5f, cy), g_pal->faint);
                } else {
                    int u = 0;
                    for (const auto& c : kids)
                        u = std::max(u, urgency(s.attention_of(c)));
                    const std::string n = std::to_string(kids.size());
                    d->AddText(s.fonts.ui, fpx(11), ImVec2(rx - cw - px(8), cy - fpx(11) * 0.6f),
                               u >= 3 ? g_pal->done : g_pal->faint, n.c_str());
                    draw_icon(s, d, ui::icon::kChevronRight, fpx(11), ImVec2(rx - cw * 0.5f, cy), g_pal->faint);
                }
                rx -= cw + px(10);
            }
            const bool pinned = s.ws.pinned.contains(id);
            if (hov || ImGui::IsPopupOpen("##pin")) {
                const float pb = px(20);
                if (icon_button(s, "##pinbtn", pinned ? ui::icon::kPinned : ui::icon::kPin,
                                ImVec2(rx - pb, cy - pb * 0.5f), pb, fpx(13), pinned ? "Unpin" : "Pin to the top",
                                g_pal->faint)) {
                    if (pinned)
                        s.ws.pinned.erase(id);
                    else
                        s.ws.pinned.insert(id);
                    s.doc_dirty = true;
                    s.doc_dirty_at = Clock::now();
                }
                rx -= pb + px(6);
            } else {
                const std::string t = s.changed_at.count(id) ? ago(s.changed_at[id]) : std::string("<1m");
                const float tw = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, t.c_str()).x;
                d->AddText(s.fonts.ui, fpx(11), ImVec2(rx - tw, cy - fpx(11) * 0.6f), g_pal->faint, t.c_str());
                rx -= tw + px(8);
            }
            if (s.machine_of.count(id) && !s.machine_of[id].empty()) {
                const std::string& m = s.machine_of[id];
                const float mw2 = s.fonts.ui->CalcTextSizeA(fpx(10), FLT_MAX, 0, m.c_str()).x;
                d->AddText(s.fonts.ui, fpx(10), ImVec2(rx - mw2, cy - fpx(10) * 0.6f), g_pal->faint, m.c_str());
                rx -= mw2 + px(6);
            }
        }
        // Name: "<space> · <detail>" (priority / pinned) or the detail (grouped).
        const ImU32 name_col = att == Attention::kIdle && !shown ? g_pal->dim : g_pal->text;
        std::string space_name;
        const auto loc = s.ws.find(id);
        if (loc.space >= 0)
            space_name = s.ws.space_title(loc.space, cwds);
        else if (!v.parent.empty())
            space_name = s.pane_title(v.parent);
        else
            space_name = folder_name(v.cwd);
        std::string detail = agent_detail(s, id, v);
        if (const auto deco = s.mods->decorate("agent", {{"id", id},
                                                         {"title", s.pane_title(id)},
                                                         {"state", v.agent_state},
                                                         {"program", v.program},
                                                         {"cwd", v.cwd}});
            !deco.empty())
            detail = deco + (detail.empty() ? "" : "  " + detail);
        const float fs = child ? fpx(12.5f) : fpx(13);
        if (child) {
            text_clipped(d, s.fonts.ui, fs, ImVec2(x, cy - fs * 0.6f), shown ? g_pal->text : g_pal->dim,
                         v.name.empty() ? s.pane_title(id) : v.name, rx - x);
        } else if (full) {
            const float nw = text_clipped(d, s.fonts.ui, fs, ImVec2(x, cy - fs * 0.6f), name_col, space_name,
                                          std::min(detail.empty() ? px(200) : px(120), rx - x));
            if (!detail.empty())
                text_clipped(d, s.fonts.ui, fs, ImVec2(x + nw, cy - fs * 0.6f), g_pal->faint,
                             " \xC2\xB7 " + detail, rx - x - nw);
        } else {
            text_clipped(d, s.fonts.ui, fs, ImVec2(x, cy - fs * 0.6f), name_col,
                         detail.empty() ? program_info(v.program, v.profile_id).name : detail,
                         std::min(px(200), rx - x));
        }
        if (clicked) {
            if (s.ws.find(id).space < 0 && !v.parent.empty())
                run_command(s, "agent.peek:" + id);  // a sub-agent without a pane: peek
            else
                s.focus(id);
        }
        if (hov && !v.agent_message.empty())
            ImGui::SetTooltip("%s", v.agent_message.c_str());
        ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + rh));
        ImGui::PopID();
    };

    auto group_header = [&](const std::string& key, const std::string& label, ImU32 col, int count, int fold_urgency) {
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
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool folded = s.folded_groups.contains(key);
        const float cy = a.y + hh * 0.5f;
        const float tw = text_clipped(d, s.fonts.ui_medium, fpx(11), ImVec2(a.x + px(8), cy - fpx(11) * 0.6f),
                                      hov ? g_pal->text : col, label, rw - px(60));
        const std::string n = std::to_string(count);
        d->AddText(s.fonts.ui, fpx(11), ImVec2(a.x + px(8) + tw + px(6), cy - fpx(11) * 0.6f), g_pal->faint, n.c_str());
        if (folded) {
            float x = a.x + rw - px(8) - px(11);
            draw_icon(s, d, ui::icon::kChevronRight, fpx(11), ImVec2(x + px(5.5f), cy), g_pal->faint);
            if (grouped && fold_urgency >= 2) {
                const Attention fa = fold_urgency == 4   ? Attention::kBlocked
                                     : fold_urgency == 3 ? Attention::kDone
                                                         : Attention::kWorking;
                status_mark(s, d, x - px(20), cy, fa, true);
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + hh));
        return !folded;
    };

    if (rail) {
        for (const auto& id : order)
            agent_row(id, false, false, false);
    } else {
        // Groups: Pinned, then Needs you / Done / Working / Idle, or one per space.
        struct Group {
            std::string key, label;
            ImU32 color;
            std::vector<std::string> ids;
        };
        std::vector<Group> groups;
        for (const auto& id : order) {
            std::string key, label;
            ImU32 col = g_pal->faint;
            if (s.ws.pinned.contains(id)) {
                key = "pinned";
                label = "Pinned";
            } else if (grouped) {
                const auto loc = s.ws.find(id);
                key = loc.space >= 0 ? "space:" + s.ws.spaces[static_cast<std::size_t>(loc.space)].id : "space:-";
                label = loc.space >= 0 ? s.ws.space_title(loc.space, cwds) : std::string("Other");
            } else {
                const int u = rollup(id);
                key = u == 4 ? "blocked" : u == 3 ? "done" : u == 2 ? "working" : "idle";
                label = u == 4 ? "Needs you" : u == 3 ? "Done" : u == 2 ? "Working" : "Idle";
                col = u == 4 ? g_pal->needs_you : u == 3 ? g_pal->done : u == 2 ? g_pal->working : g_pal->faint;
            }
            if (groups.empty() || groups.back().key != key)
                groups.push_back({key, label, col, {}});
            groups.back().ids.push_back(id);
        }
        for (const auto& g : groups) {
            int fu = 0;
            for (const auto& id : g.ids)
                fu = std::max(fu, rollup(id));
            if (!group_header(g.key, g.label, g.color, static_cast<int>(g.ids.size()), fu))
                continue;
            for (const auto& id : g.ids) {
                const bool full = g.key == "pinned" || !grouped;
                agent_row(id, full, false, false);
                if (!s.folded_agents.contains(id)) {
                    const auto& kids = children[id];
                    for (std::size_t k = 0; k < kids.size(); ++k)
                        agent_row(kids[k], false, true, k + 1 == kids.size());
                }
            }
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
void draw_sidebar_footer(App::State& s, ImVec2 pos, float w, bool rail) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(pos, ImVec2(pos.x + w, pos.y), g_pal->border);
    float y = pos.y + px(4);
    auto item = [&](const char* id, const char* glyph, const char* label, ImU32 icol, const char* cmd) {
        const float rh = px(28);
        const float x0 = rail ? pos.x + (w - px(32)) * 0.5f : pos.x + px(8);
        const float iw = rail ? px(32) : w - px(16);
        ImGui::SetCursorScreenPos(ImVec2(x0, y));
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(iw, rh));
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + iw, y + rh), g_pal->hover, px(8));
        const float cy = y + rh * 0.5f;
        if (rail) {
            draw_icon(s, dl, glyph, fpx(14), ImVec2(x0 + iw * 0.5f, cy), hov ? g_pal->text : icol);
            if (hov)
                ImGui::SetTooltip("%s", label);
        } else {
            draw_icon(s, dl, glyph, fpx(14), ImVec2(x0 + px(8) + px(7), cy), hov ? g_pal->text : icol);
            dl->AddText(s.fonts.ui, fpx(13), ImVec2(x0 + px(8) + px(14) + px(9), cy - fpx(13) * 0.6f),
                        hov ? g_pal->text : g_pal->dim, label);
        }
        if (clicked)
            run_command(s, cmd);
        y += rh;
    };
    item("##f-settings", ui::icon::kSettings, "Settings", g_pal->dim, "app.settings");
    item("##f-machine", ui::icon::kPlus, "Add machine", g_pal->faint, "machine.add");
}

void draw_sidebar(App::State& s, ImVec2 pos, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->sidebar);
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginChild("##sidebar", size, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    const bool rail = size.x < px(160);
    const float w = size.x;
    draw_brand(s, pos, w, rail);
    const float footer = px(69);
    const float lists_top = pos.y + px(46);
    const float lists_h = size.y - px(46) - footer;
    // Spaces and agents share the lists area (weight 1 each, min 90); the boundary drags.
    const float min_h = px(90);
    float spaces_h = std::clamp(lists_h * s.settings.spaces_share, min_h, std::max(min_h, lists_h - min_h));
    draw_spaces(s, ImVec2(pos.x, lists_top), w, spaces_h, rail);
    draw_agents(s, ImVec2(pos.x, lists_top + spaces_h), w, lists_h - spaces_h, rail);
    {
        ImGui::SetCursorScreenPos(ImVec2(pos.x, lists_top + spaces_h - px(3)));
        ImGui::InvisibleButton("##lists-split", ImVec2(w, px(6)));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive()) {
            const float my = ImGui::GetIO().MousePos.y - lists_top;
            s.settings.spaces_share = std::clamp(my / std::max(1.0f, lists_h), 0.1f, 0.9f);
        }
        if (ImGui::IsItemDeactivated())
            save_settings(s.settings);
    }
    draw_sidebar_footer(s, ImVec2(pos.x, pos.y + size.y - footer), w, rail);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

// ---------------------------------------------------------------- tab bar (tabs.js)
// The "+" menu (new-menu): Terminal, the installed agents (1-9), New space, worktrees.
void draw_new_menu(App::State& s) {
    ImGui::SetNextWindowSize(ImVec2(px(250), 0));
    if (!ImGui::BeginPopup("##newmenu"))
        return;
    auto row = [&](const char* glyph, const std::string& label, const std::string& hint, const std::string& cmd) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const bool clicked = ImGui::Selectable(("##" + label).c_str(), false, 0, ImVec2(w, px(31) - ImGui::GetStyle().ItemSpacing.y));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float cy = a.y + (px(31) - ImGui::GetStyle().ItemSpacing.y) * 0.5f;
        if (glyph != nullptr)
            draw_icon(s, dl, glyph, fpx(14), ImVec2(a.x + px(9), cy), g_pal->dim);
        dl->AddText(s.fonts.ui, fpx(12.8f), ImVec2(a.x + px(26), cy - fpx(12.8f) * 0.6f), g_pal->text, label.c_str());
        if (!hint.empty()) {
            const float hw = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, hint.c_str()).x;
            dl->AddText(s.fonts.ui, fpx(11), ImVec2(a.x + w - hw - px(4), cy - fpx(11) * 0.6f), g_pal->faint, hint.c_str());
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
    // Other shells (WSL distributions): a tab in the focused folder, like Terminal.
    for (const auto& [id, name] : profiles)
        if (id.starts_with("wsl-") && id != pick_shell(s.settings.shell_profile, profiles))
            row(ui::icon::kTerminal, name, "", "launch:tab:" + id);
    ImGui::Dummy(ImVec2(0, px(2)));
    ImGui::PushFont(s.fonts.ui_medium, fpx(11));
    ImGui::TextDisabled("Start an agent");
    ImGui::PopFont();
    // Installed agents (profiles that are not shells), numbered 1-9; then up to two known
    // agents that are not installed, disabled.
    int n = 0;
    std::string have;
    for (const auto& [id, name] : profiles) {
        if (is_shell_profile(id))
            continue;
        for (const char ch : id + " " + name + " ")
            have += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        ++n;
        const std::string key = n <= 9 ? std::to_string(n) : std::string();
        row(nullptr, name, key, "launch:tab:" + id);
        if (n <= 9 && ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + n - 1), false)) {
            ImGui::CloseCurrentPopup();
            run_command(s, "launch:tab:" + id);
        }
    }
    if (profiles.empty()) {
        ImGui::PushFont(s.fonts.ui, fpx(12));
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
            menu_row(s, nullptr, label, "not installed", false);
        }
    }
    ImGui::Separator();
    row(ui::icon::kLayers, "New space", s.keymap.chord_for("space.new"), "space.new");
    row(ui::icon::kWorktree, "New worktree\xE2\x80\xA6", s.keymap.chord_for("worktree.new"), "worktree.new");
    row(ui::icon::kFolderOpen, "Open worktree\xE2\x80\xA6", "", "worktree.open");
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
    right -= px(6);

    float x = pos.x + px(10);
    const float tabs_right = right - px(26) - px(6);
    int close_tab = -1;
    if (sp != nullptr) {
        // Keep the active tab in view: start from an offset that shows it.
        struct T {
            int i;
            float w;
            std::string label;
        };
        std::vector<T> items;
        for (int i = 0; i < static_cast<int>(sp->tabs.size()); ++i) {
            auto& tab = sp->tabs[static_cast<std::size_t>(i)];
            if (tab.settled)
                continue;  // put away (herdr tab.settle): back from the picker or when it needs you
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
        dl->PushClipRect(ImVec2(x, pos.y), ImVec2(tabs_right, pos.y + height), true);
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
                open_rename(s, "tab", tab.id, tab.name);
            if (begin_menu("##tabctx")) {
                if (menu_row(s, ui::icon::kCommand, "Rename", s.keymap.chord_for("tab.rename"))) {
                    s.ws.select_tab(it.i);
                    open_rename(s, "tab", tab.id, tab.name);
                }
                if (menu_row(s, ui::icon::kPlus, "New tab", s.keymap.chord_for("tab.new")))
                    run_command(s, "tab.new");
                menu_separator();
                if (menu_row(s, ui::icon::kClose, "Close", s.keymap.chord_for("tab.close")))
                    close_tab = it.i;
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
                program_badge(dl, s.fonts.ui_bold, fpx(10.5f), ImVec2(cx, cy - px(7)), px(14),
                              program_info(v.program, v.profile_id));
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
                    close_tab = it.i;
                const bool xh = ImGui::IsItemHovered();
                if (xh)
                    dl->AddRectFilled(ImVec2(cx - px(3), cy - px(9)), ImVec2(cx + px(15), cy + px(9)), g_pal->hover, px(4));
                draw_icon(s, dl, ui::icon::kClose, fpx(12), ImVec2(cx + px(6), cy), xh ? g_pal->text : g_pal->dim);
            }
            ImGui::PopID();
        }
        dl->PopClipRect();
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
        x = std::min(tx, tabs_right) + px(3);
    }
    // "+" → new-menu.
    if (icon_button(s, "##tab-new", ui::icon::kPlus, ImVec2(std::min(x, tabs_right) + px(2), pos.y + (height - px(26)) * 0.5f),
                    px(26), fpx(14), "New", g_pal->faint))
        ImGui::OpenPopup("##newmenu");
    draw_new_menu(s);
    if (close_tab >= 0) {
        s.ws.select_tab(close_tab);
        run_command(s, "tab.close");
    }
    window_drag_area(s, pos, ImVec2(pos.x + width, pos.y + height));
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
        if (!is_agent(v) || v.lost || v.process_state != "Running")
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
    const std::string hint = s.keymap.chord_for("app.palette") + "  commands";
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
    dl->AddRectFilled(pos, max, g_pal->term_bg, px(10));
    dl->AddRect(pos, max, focused ? g_pal->focus : g_pal->border, px(10), 0, 1.0f);

    const float fs = ImGui::GetFontSize();
    const float header_h = px(30);
    const float cy = pos.y + header_h * 0.5f;
    const bool pane_hovered = ImGui::IsMouseHoveringRect(pos, ImVec2(max.x, pos.y + header_h), false) &&
                              ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    float hx = pos.x + px(10);
    program_badge(dl, s.fonts.ui_bold, fpx(10.5f), ImVec2(hx, cy - px(7)), px(14), program_info(v.program, v.profile_id));
    hx += px(14) + px(8);
    const std::string title = s.pane_title(id);
    const float tw = text_clipped(dl, s.fonts.ui_medium, fpx(12.5f), ImVec2(hx, cy - fpx(12.5f) * 0.6f), g_pal->text, title,
                                  std::min(px(260), r.w * 0.4f));
    hx += tw + px(8);

    ImGui::PushID(id.c_str());
    float rx = max.x - px(8);
    if (pane_hovered) {
        auto hbtn = [&](const char* bid, const char* glyph, const char* cmd, const char* tip) {
            const float b = px(20);  // 14 px icon, 3 px padding
            rx -= b;
            const std::string t = std::string(tip) + "  " + s.keymap.chord_for(cmd);
            if (icon_button(s, bid, glyph, ImVec2(rx, cy - b * 0.5f), b, fpx(14), t.c_str(), g_pal->dim)) {
                s.ws.focus(id);
                run_command(s, cmd);
            }
            rx -= px(2);
        };
        const auto* tab = s.ws.active_tab();
        const bool zoomed = tab != nullptr && tab->zoomed == id;
        hbtn("##close", ui::icon::kClose, "pane.close", "Close");
        hbtn("##zoom", zoomed ? ui::icon::kUnzoom : ui::icon::kZoom, "pane.zoom", zoomed ? "Unzoom" : "Zoom");
        hbtn("##sd", ui::icon::kSplitDown, "pane.split:down", "Split down");
        hbtn("##sr", ui::icon::kSplitRight, "pane.split:right", "Split right");
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
    text_clipped(dl, s.fonts.ui, fpx(11.5f), ImVec2(hx, cy - fpx(11.5f) * 0.6f), g_pal->faint, display_path(v.cwd), rx - hx);
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

    float top = pos.y + header_h;
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

    // herdr main.json terminal_padding [14, 12].
    const ImVec2 body_min(pos.x + px(14), top + px(2));
    const ImVec2 body_size(r.w - px(28), max.y - top - px(2) - px(12));
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
            if (pick(ui::icon::kZoom, "Zoom", "pane.zoom")) {
                run_command(s, "pane.zoom");
                return true;
            }
            if (pick(ui::icon::kCommand, "Rename", "pane.rename")) {
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
                if (pick(ui::icon::kArrowLeft, label.c_str(), nullptr)) {
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

// Board layout: agents as cards in columns by what they need from you.
void draw_board(App::State& s, ImVec2 pos, ImVec2 size) {
    struct Column {
        const char* title;
        Attention att;
        std::vector<std::string> ids;
    };
    std::vector<Column> cols = {{"Needs you", Attention::kBlocked, {}},
                                {"Done", Attention::kDone, {}},
                                {"Working", Attention::kWorking, {}},
                                {"Idle & shells", Attention::kIdle, {}}};
    for (const auto& sess : s.sessions) {
        const auto& v = s.views_by_id[sess->id()];
        if (v.lost || v.process_state != "Running")
            continue;
        const Attention a = s.attention_of(sess->id());
        auto& col = a == Attention::kBlocked   ? cols[0]
                    : a == Attention::kDone    ? cols[1]
                    : a == Attention::kWorking ? cols[2]
                                               : cols[3];
        col.ids.push_back(sess->id());
    }
    const float gap = px(10);
    const float cw = (size.x - gap * 3) / 4;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fs = ImGui::GetFontSize();
    for (int c = 0; c < 4; ++c) {
        auto& col = cols[static_cast<std::size_t>(c)];
        const ImVec2 cp(pos.x + static_cast<float>(c) * (cw + gap), pos.y);
        dl->AddRectFilled(cp, ImVec2(cp.x + cw, cp.y + size.y), alpha(g_pal->card, 200), px(12));
        attention_mark(dl, ImVec2(cp.x + px(16), cp.y + px(20)), px(5), col.att);
        char head[64];
        std::snprintf(head, sizeof head, "%s  %d", col.title, static_cast<int>(col.ids.size()));
        dl->AddText(s.fonts.ui_bold, fs, ImVec2(cp.x + px(30), cp.y + px(20) - fs * 0.5f),
                    attention_color(col.att), head);
        ImGui::SetCursorScreenPos(ImVec2(cp.x + px(8), cp.y + px(40)));
        ImGui::PushID(c);
        ImGui::BeginChild("##col", ImVec2(cw - px(16), size.y - px(48)), ImGuiChildFlags_None);
        for (const auto& id : col.ids) {
            const auto& v = s.views_by_id[id];
            ImGui::PushID(id.c_str());
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            const float h = px(92);
            const bool clicked = ImGui::InvisibleButton("##card", ImVec2(w, h));
            const bool hovered = ImGui::IsItemHovered();
            ImDrawList* d = ImGui::GetWindowDrawList();
            d->AddRectFilled(a, ImVec2(a.x + w, a.y + h), hovered ? g_pal->hover : g_pal->bg, px(10));
            d->AddRect(a, ImVec2(a.x + w, a.y + h),
                       col.att == Attention::kBlocked ? g_pal->needs_you : g_pal->border, px(10));
            program_badge(d, s.fonts.ui_bold, fs, ImVec2(a.x + px(10), a.y + px(10)), px(20),
                          program_info(v.program, v.profile_id));
            text_clipped(d, s.fonts.ui_bold, fs, ImVec2(a.x + px(38), a.y + px(12)), g_pal->text,
                         s.pane_title(id), w - px(90));
            const std::string age = elapsed_since(v.created_at);
            d->AddText(s.fonts.ui, fs * 0.9f,
                       ImVec2(a.x + w - px(10) - ImGui::CalcTextSize(age.c_str()).x, a.y + px(13)),
                       g_pal->faint, age.c_str());
            text_clipped(d, s.fonts.ui, fs * 0.9f, ImVec2(a.x + px(38), a.y + px(34)), g_pal->dim,
                         display_path(v.cwd), w - px(48));
            // Last words on its screen: what it is doing or asking.
            std::string last = v.agent_message;
            if (last.empty()) {
                if (const auto it = s.by_id.find(id); it != s.by_id.end()) {
                    const auto snap = it->second->snapshot();
                    for (int r = snap->rows - 1; r >= 0 && last.empty(); --r) {
                        auto t = snap->row_text(r);
                        while (!t.empty() && t.front() == ' ')
                            t.erase(t.begin());
                        last = t;
                    }
                }
            }
            text_clipped(d, s.fonts.mono, fs * 0.9f, ImVec2(a.x + px(12), a.y + px(60)), g_pal->text, last,
                         w - px(24));
            if (clicked)
                run_command(s, "agent.peek:" + id);
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                s.peek.clear();
                run_command(s, "layout:workspace");
                s.focus(id);
            }
            if (hovered)
                ImGui::SetTooltip("Click: peek   Double-click: open in the workspace layout");
            ImGui::Dummy(ImVec2(0, px(6)));
            ImGui::PopID();
        }
        if (col.ids.empty())
            ImGui::TextDisabled("Nothing here");
        ImGui::EndChild();
        ImGui::PopID();
    }
}

// Grid layout: every running session of the active space (all of them when the space has
// none) as equal tiles, each a live terminal.
void draw_grid(App::State& s, ImVec2 pos, ImVec2 size, bool connected) {
    s.pane_area = {pos.x, pos.y, size.x, size.y};
    std::vector<std::string> ids;
    if (auto* sp = s.ws.active())
        for (const auto& t : sp->tabs)
            for (const auto& id : s.ws.sessions_in(t))
                ids.push_back(id);
    if (ids.empty())
        for (const auto& sess : s.sessions)
            if (s.views_by_id[sess->id()].process_state == "Running")
                ids.push_back(sess->id());
    if (ids.empty()) {
        draw_panes(s, pos, size, connected);  // empty state
        return;
    }
    const int n = static_cast<int>(ids.size());
    const int cols = static_cast<int>(
        std::ceil(std::sqrt(static_cast<double>(n) * size.x / std::max(1.0f, size.y) / 1.6)));
    const int c = std::clamp(cols, 1, n);
    const int rows = (n + c - 1) / c;
    const float gap = px(8);
    const float tw = (size.x - gap * static_cast<float>(c - 1)) / static_cast<float>(c);
    const float th = (size.y - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows);
    for (int i = 0; i < n; ++i) {
        const int r = i / c;
        const int k = i % c;
        draw_pane(s, ids[static_cast<std::size_t>(i)],
                  {std::round(pos.x + static_cast<float>(k) * (tw + gap)),
                   std::round(pos.y + static_cast<float>(r) * (th + gap)), std::floor(tw), std::floor(th)},
                  connected);
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
        // Read-only preview in the terminal font at 12 (files.js 130-148).
        ImGui::BeginChild("##preview", ImVec2(size.x - 2, pos.y + size.y - top - px(4)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);
        ImGui::PushFont(s.fonts.mono, fpx(12));
        ImGui::Dummy(ImVec2(0, px(4)));
        if (!p.preview_message.empty() && p.preview_text.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->faint));
            ImGui::SetCursorPosX(px(10));
            ImGui::TextUnformatted(p.preview_message.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGuiListClipper clip;
            // Line starts, computed once per frame (files up to 400k characters).
            std::vector<std::size_t> starts = {0};
            for (std::size_t i = 0; i < p.preview_text.size(); ++i)
                if (p.preview_text[i] == '\n')
                    starts.push_back(i + 1);
            clip.Begin(static_cast<int>(starts.size()));
            while (clip.Step()) {
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                    const std::size_t a = starts[static_cast<std::size_t>(i)];
                    std::size_t b = i + 1 < static_cast<int>(starts.size()) ? starts[static_cast<std::size_t>(i) + 1] - 1
                                                                              : p.preview_text.size();
                    if (b > a && p.preview_text[b - 1] == '\r')
                        --b;
                    const std::string num = std::to_string(i + 1);
                    ImGui::SetCursorPosX(px(10));
                    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->faint));
                    ImGui::TextUnformatted(num.c_str());
                    ImGui::PopStyleColor();
                    ImGui::SameLine(px(48));
                    ImGui::TextUnformatted(p.preview_text.c_str() + a, p.preview_text.c_str() + b);
                }
            }
        }
        ImGui::PopFont();
        ImGui::EndChild();
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
        ImGui::PushFont(s.fonts.ui, fpx(12));
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
    if (!p.changes.empty()) {
        const std::string badge = std::to_string(p.changes.size()) + (p.changes.size() == 1 ? " file" : " files");
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
    const float body_h = pos.y + size.y - footer_h - body_top;
    // File list: grows to fit (28 per row + 12), at least 44, then the diff takes the rest.
    const float list_h = std::clamp(static_cast<float>(p.changes.size()) * px(28) + px(12) + (p.changes_more ? px(20) : 0.0f),
                                    px(44), std::max(px(44), body_h * 0.45f));
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
            ImGui::PushFont(s.fonts.ui, fpx(12));
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
        if (p.changes_more)
            note("Showing the first 1,000 changed files");
    }
    ImGui::EndChild();
    dl->AddLine(ImVec2(pos.x, body_top + list_h), ImVec2(pos.x + size.x, body_top + list_h), g_pal->border);

    // Diff: unified, one number column, +/- prefix removed, 9 % green/red rows, terminal font 11.
    const float diff_top = body_top + list_h + 1;
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
            ImGui::PushFont(s.fonts.mono, fpx(11));
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
    bool commit = ImGui::InputTextWithHint("##commitmsg", "Commit message", p.commit_msg, sizeof p.commit_msg,
                                           ImGuiInputTextFlags_EnterReturnsTrue);
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
    const auto pi = program_info(v.program, v.profile_id);
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

// Title row with a clickable "Esc" chip at the right; returns true when the chip is clicked.
bool overlay_title(App::State& s, const char* title, float size = 15.0f) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(size), FLT_MAX, 0, title);
    dl->AddText(s.fonts.ui_bold, fpx(size), p, g_pal->text, title);
    const ImVec2 e = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, "Esc");
    const ImVec2 ea(p.x + w - e.x - px(10), p.y + (t.y - e.y - px(4)) * 0.5f);
    ImGui::SetCursorScreenPos(ea);
    const bool clicked = ImGui::InvisibleButton("##esc", ImVec2(e.x + px(10), e.y + px(4)));
    dl->AddRect(ea, ImVec2(ea.x + e.x + px(10), ea.y + e.y + px(4)), g_pal->border, px(4));
    dl->AddText(s.fonts.ui, fpx(11), ImVec2(ea.x + px(5), ea.y + px(2)), ImGui::IsItemHovered() ? g_pal->text : g_pal->faint,
                "Esc");
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + t.y + px(10)));
    return clicked;
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

void draw_palette(App::State& s) {
    if (!s.show_palette)
        return;
    struct Entry {
        std::string title, cmd, group, chord;
        float score = 0;
        std::vector<int> hits;
    };
    std::vector<Entry> all;
    auto add = [&](std::string title, std::string cmd, std::string group) {
        all.push_back({std::move(title), cmd, std::move(group), s.keymap.chord_for(cmd)});
    };
    // herdr palette.js 22-97: the named entries, titles and order first.
    static constexpr std::array<std::array<const char*, 3>, 45> kNamed = {{
        {"tab.new", "New tab", "Tab"},
        {"app.picker", "Go to space or tab\xE2\x80\xA6", "Tab"},
        {"tab.rename", "Rename tab", "Tab"},
        {"tab.close", "Close tab", "Tab"},
        {"tab.next", "Next tab", "Tab"},
        {"tab.prev", "Previous tab", "Tab"},
        {"pane.split:right", "Split right", "Pane"},
        {"pane.split:down", "Split down", "Pane"},
        {"pane.close", "Close pane", "Pane"},
        {"pane.zoom", "Zoom pane", "Pane"},
        {"pane.go:left", "Focus pane left", "Pane"},
        {"pane.go:right", "Focus pane right", "Pane"},
        {"pane.go:up", "Focus pane up", "Pane"},
        {"pane.go:down", "Focus pane down", "Pane"},
        {"pane.swap:left", "Swap pane left", "Pane"},
        {"pane.swap:right", "Swap pane right", "Pane"},
        {"pane.swap:up", "Swap pane up", "Pane"},
        {"pane.swap:down", "Swap pane down", "Pane"},
        {"space.new", "New space", "Space"},
        {"space.rename", "Rename space", "Space"},
        {"space.close", "Close space", "Space"},
        {"space.next", "Next space", "Space"},
        {"space.prev", "Previous space", "Space"},
        {"layout.switch", "Next layout", "Layout"},
        {"layout.reset", "Reset layout", "Layout"},
        {"layout.clone", "Duplicate layout", "Layout"},
        {"mod.clone", "Duplicate mod", "Layout"},
        {"terminal.open", "New terminal", "Terminal"},
        {"terminal.copy", "Copy", "Terminal"},
        {"terminal.paste", "Paste", "Terminal"},
        {"agent.next", "Next agent", "Agent"},
        {"agent.prev", "Previous agent", "Agent"},
        {"window.minimize", "Minimize window", "Window"},
        {"window.maximize", "Maximize window", "Window"},
        {"window.close", "Close window", "Window"},
        {"app.settings", "Settings", "App"},
        {"app.help", "Keys", "App"},
        {"app.welcome", "Welcome", "App"},
        {"app.whats_new", "What's new", "App"},
        {"app.reset", "Reset to the default screen", "App"},
        {"app.zoom_in", "Zoom in", "App"},
        {"app.zoom_out", "Zoom out", "App"},
        {"app.zoom_reset", "Reset zoom", "App"},
        {"app.quit", "Quit AgentDesk", "App"},
        {"app.palette", "", ""},  // not listed (the palette itself)
    }};
    std::set<std::string> named;
    for (const auto& [cmd, title, group] : kNamed) {
        named.insert(cmd);
        if (*title != '\0')
            add(title, cmd, group);
        if (std::string_view(cmd) == "app.quit")
            for (const auto& p : kPalettes)
                add(std::string("Theme: ") + p.label, std::string("theme:") + p.id, "App");
    }
    for (const auto& l : s.layouts)
        add("Layout: " + l.name, "layout:" + l.name, "Layout");
    // Then every other command, in its group (herdr adds the app's other commands the same way).
    for (const auto& c : kCommands)
        if (!named.contains(c.id))
            add(c.label, c.id, command_group(c.id));
    for (const auto& c : s.mods->commands())
        add(c.mod + ": " + c.label, "mod:" + c.mod + ":" + c.id, "Mods");
    std::string q;
    for (const char* c = s.palette_buf; *c != 0; ++c)
        if (*c != ' ')
            q += *c;
    std::vector<Entry> shown;
    for (auto& e : all) {
        if (auto sc = palette_score(e.title, q, &e.hits)) {
            e.score = *sc;
            shown.push_back(e);
        } else if (auto sc2 = palette_score(e.group + " " + e.title + " " + e.cmd, q, nullptr)) {
            e.score = *sc2 - 4;
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
        // Recent (5 of the last 20), then groups in herdr's order.
        int n = 0;
        for (const auto& cmd : s.settings.palette_recent) {
            if (n >= 5)
                break;
            for (const auto& e : shown)
                if (e.cmd == cmd) {
                    if (n == 0)
                        rows.push_back({nullptr, "Recent"});
                    rows.push_back({&e, {}});
                    order.push_back(&e);
                    ++n;
                    break;
                }
        }
        for (const char* g : {"Tab", "Pane", "Space", "Layout", "Terminal", "Agent", "Window", "App", "Mods"}) {
            bool head = false;
            for (const auto& e : shown) {
                if (e.group != g)
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
    push_overlay_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(10), px(10)));
    ImGui::Begin("##palette", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    // Search field, 36 px, search icon, "Esc" hint.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 sp = ImGui::GetCursorScreenPos();
    const float fw = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(34), (px(36) - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(7));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(alpha(g_pal->text, 0)));
    ImGui::SetNextItemWidth(fw);
    if (ImGui::InputTextWithHint("##q", "Type a command\xE2\x80\xA6", s.palette_buf, sizeof s.palette_buf))
        s.palette_sel = 0;
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    draw_icon(s, dl, ui::icon::kSearch, fpx(14), ImVec2(sp.x + px(17), sp.y + px(18)), g_pal->faint);
    dl->AddText(s.fonts.ui, fpx(11), ImVec2(sp.x + fw - px(30), sp.y + px(18) - fpx(11) * 0.6f), g_pal->faint, "Esc");
    dl->AddLine(ImVec2(sp.x - px(10), sp.y + px(40)), ImVec2(sp.x + fw + px(10), sp.y + px(40)), g_pal->border);
    ImGui::SetCursorScreenPos(ImVec2(sp.x, sp.y + px(46)));
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
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginChild("##list", ImVec2(fw, std::min(px(320) + px(60), std::max(px(40), static_cast<float>(rows.size()) * px(32)))),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    int idx = -1;
    for (const auto& row : rows) {
        if (row.e == nullptr) {
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(fw, px(26)));
            d->AddText(s.fonts.ui_medium, fpx(11), ImVec2(a.x + px(10), a.y + px(13) - fpx(11) * 0.3f), g_pal->faint,
                       row.header.c_str());
            continue;
        }
        ++idx;
        const auto* e = row.e;
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::PushID(idx);
        if (ImGui::InvisibleButton("##e", ImVec2(fw - px(4), px(32))))
            run = e->cmd;
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hov && ImGui::GetIO().MouseDelta.x * ImGui::GetIO().MouseDelta.x + ImGui::GetIO().MouseDelta.y * ImGui::GetIO().MouseDelta.y > 0)
            s.palette_sel = idx;
        const bool sel = idx == s.palette_sel;
        if (sel) {
            d->AddRectFilled(a, ImVec2(a.x + fw - px(4), a.y + px(32)), g_pal->selected, px(7));
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
                ImGui::IsKeyPressed(ImGuiKey_PageDown) || ImGui::IsKeyPressed(ImGuiKey_PageUp))
                ImGui::SetScrollHereY();
        }
        const float cy = a.y + px(16);
        // Title with matched letters in accent semibold.
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
            d->AddText(f, fpx(13), ImVec2(x, cy - fpx(13) * 0.6f), hit ? g_pal->focus : g_pal->text, b, b + len);
            x += f->CalcTextSizeA(fpx(13), FLT_MAX, 0, b, b + len).x;
            i += len;
        }
        float right = a.x + fw - px(14);
        right -= key_chips(s, d, right, cy, e->chord);
        if (!q.empty()) {
            const float gw = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, e->group.c_str()).x;
            d->AddText(s.fonts.ui, fpx(11), ImVec2(right - gw - px(8), cy - fpx(11) * 0.6f), g_pal->faint, e->group.c_str());
        }
    }
    if (n == 0) {
        ImGui::SetCursorPosX(px(10));
        ImGui::TextDisabled("Nothing matches \"%s\". Places are in the picker.", s.palette_buf);
    }
    ImGui::EndChild();
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
        int urgency;
        std::vector<Row> tabs;
    };
    std::vector<SpaceRows> busy, quiet;
    for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i) {
        const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
        const std::string sname = s.ws.space_title(i, cwds);
        SpaceRows sr{i, 0, {}};
        for (int t = 0; t < static_cast<int>(sp.tabs.size()); ++t) {
            const auto& tab = sp.tabs[static_cast<std::size_t>(t)];
            const auto ids = s.ws.sessions_in(tab);
            std::vector<Attention> atts;
            std::string programs;
            int subs = 0;
            for (const auto& id : ids) {
                atts.push_back(s.attention_of(id));
                if (s.views_by_id.count(id)) {
                    const auto& v = s.views_by_id[id];
                    const auto pn = program_info(v.program, v.profile_id).name;
                    if (programs.find(pn) == std::string::npos)
                        programs += (programs.empty() ? "" : " \xC2\xB7 ") + pn;
                }
            }
            for (const auto& [cid, cv] : s.views_by_id)
                if (!cv.parent.empty() && std::find(ids.begin(), ids.end(), cv.parent) != ids.end() &&
                    s.ws.find(cid).space < 0)
                    ++subs;
            if (subs > 0)
                programs += " +" + std::to_string(subs);
            std::string tname = tab.name;
            if (tname.empty() && !ids.empty())
                tname = s.pane_title(tab.focused.empty() ? ids.front() : tab.focused);
            std::string hay = sname + " " + tname + " " + programs;
            for (auto& c : hay)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!q.empty() && hay.find(q) == std::string::npos)
                continue;
            Row r{Row::kTab, i, t, tname, programs, "", ui::most_urgent(atts)};
            sr.urgency = std::max(sr.urgency, urgency(r.att));
            sr.tabs.push_back(std::move(r));
        }
        if (sr.tabs.empty())
            continue;
        (sr.urgency >= 2 ? busy : quiet).push_back(std::move(sr));
    }
    std::stable_sort(busy.begin(), busy.end(), [](const SpaceRows& a, const SpaceRows& b) {
        return (a.urgency == 4) > (b.urgency == 4);
    });
    std::vector<Row> rows;
    auto put = [&](const SpaceRows& sr) {
        rows.push_back({Row::kSpace, sr.index, -1, s.ws.space_title(sr.index, cwds), "", "", Attention::kNone});
        for (const auto& t : sr.tabs)
            rows.push_back(t);
    };
    for (const auto& sr : busy)
        put(sr);
    const bool show_quiet = s.picker_earlier || !q.empty() || busy.empty();
    if (!quiet.empty() && !busy.empty() && q.empty()) {
        char b[64];
        std::snprintf(b, sizeof b, "earlier: %zu quiet space%s %s", quiet.size(), quiet.size() == 1 ? "" : "s",
                      s.picker_earlier ? "\xE2\x96\xB4" : "\xE2\x96\xBE");
        rows.push_back({Row::kEarlier, -1, -1, b, "", "", Attention::kNone});
    }
    if (show_quiet)
        for (const auto& sr : quiet)
            put(sr);
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
        list_h += r.kind == Row::kSpace ? px(29) : r.kind == Row::kEarlier ? px(35) : r.kind == Row::kNew ? px(36) : px(32);
    ImGui::BeginChild("##plist", ImVec2(fw, std::min(px(320), list_h + px(2))), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    int k = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& r = rows[static_cast<std::size_t>(i)];
        const ImVec2 a = ImGui::GetCursorScreenPos();
        if (r.kind == Row::kSpace) {
            ImGui::Dummy(ImVec2(fw, px(29)));
            const auto& spc = s.ws.spaces[static_cast<std::size_t>(r.space)];
            const float side = px(18), cy = a.y + px(16);
            d->AddRectFilled(ImVec2(a.x + px(6), cy - side * 0.5f), ImVec2(a.x + px(6) + side, cy + side * 0.5f),
                             r.label == "~" ? g_pal->faint : avatar_color(spc.color), px(5));
            const std::string letter = r.label.empty() ? std::string("?") : std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(r.label[0]))));
            const ImVec2 lt = s.fonts.ui_bold->CalcTextSizeA(fpx(10.5f), FLT_MAX, 0, letter.c_str());
            d->AddText(s.fonts.ui_bold, fpx(10.5f), ImVec2(a.x + px(6) + (side - lt.x) * 0.5f, cy - lt.y * 0.5f),
                       IM_COL32(255, 255, 255, 255), letter.c_str());
            d->AddText(s.fonts.ui_medium, fpx(13), ImVec2(a.x + px(6) + side + px(9), cy - fpx(13) * 0.6f), g_pal->text,
                       r.label.c_str());
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
            float x = gx + px(16);
            const float tw = text_clipped(d, here ? s.fonts.ui_bold : s.fonts.ui, fpx(13), ImVec2(x, cy - fpx(13) * 0.6f),
                                          g_pal->text, r.label, px(220));
            x += tw + px(8);
            if (r.att == Attention::kBlocked || r.att == Attention::kDone) {
                const char* word = r.att == Attention::kBlocked ? "needs you" : "finished";
                d->AddText(s.fonts.ui, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f), attention_color(r.att), word);
                x += s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, word).x + px(8);
            }
            text_clipped(d, s.fonts.ui, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f), g_pal->faint, r.detail,
                         a.x + fw - px(12) - x);
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
            (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
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
void draw_toasts(App::State& s) {
    const auto now = Clock::now();
    while (!s.toasts.empty() && s.toasts.front().until < now)
        s.toasts.pop_front();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->WorkPos.y + vp->WorkSize.y - px(16);
    const float w = px(340);
    int n = 0;
    for (auto it = s.toasts.rbegin(); it != s.toasts.rend(); ++it, ++n) {
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
        const float ty = wp.y + (t.body.empty() ? h * 0.5f - fpx(13) * 0.6f : h * 0.5f - px(17));
        dl->AddCircleFilled(ImVec2(wp.x + px(18), ty + fpx(13) * 0.55f), px(4), t.color);
        text_clipped(dl, s.fonts.ui_bold, fpx(13), ImVec2(wp.x + px(32), ty), g_pal->text, t.title, w - px(46));
        if (!t.body.empty()) {
            std::string body = t.body;
            std::replace(body.begin(), body.end(), '\n', ' ');
            text_clipped(dl, s.fonts.ui, fpx(12), ImVec2(wp.x + px(32), ty + px(20)), g_pal->dim, body, w - px(46));
        }
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (!t.session.empty())
                s.focus(t.session);
            it->until = now;  // dismiss
        }
        if (ImGui::IsWindowHovered() && !t.body.empty())
            ImGui::SetTooltip("%s", t.body.c_str());
        ImGui::End();
        pop_overlay_style();
        y -= px(8);
    }
}

// Peek (mods/peek): a dialog 80 % x 80 % with a 34 px bar over the live terminal.
void draw_peek(App::State& s) {
    if (s.peek.empty())
        return;
    const auto vit = s.views_by_id.find(s.peek);
    const auto sit = s.by_id.find(s.peek);
    if (vit == s.views_by_id.end() || sit == s.by_id.end()) {
        s.peek.clear();
        return;
    }
    const auto& v = vit->second;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::GetForegroundDrawList(ImGui::GetMainViewport())->AddRectFilled(vp->WorkPos,
        ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y), IM_COL32(0, 0, 0, 0));
    const float w = vp->WorkSize.x * 0.8f;
    const float h = vp->WorkSize.y * 0.8f;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + (vp->WorkSize.y - h) * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    push_overlay_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(8), px(4)));
    ImGui::Begin("##peek", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float bw = ImGui::GetContentRegionAvail().x;
    const float cy = p.y + px(17);
    const Attention att = s.attention_of(s.peek);
    float x = p.x + px(6);
    x += status_mark(s, dl, x, cy, att == Attention::kNone ? Attention::kIdle : att, true) + px(8);
    program_badge(dl, s.fonts.ui_bold, fpx(10.5f), ImVec2(x, cy - px(7)), px(14), program_info(v.program, v.profile_id));
    x += px(14) + px(8);
    x += text_clipped(dl, s.fonts.ui_bold, fpx(13), ImVec2(x, cy - fpx(13) * 0.6f), g_pal->text, s.pane_title(s.peek),
                      px(260)) + px(10);
    const char* word = att == Attention::kWorking   ? "working"
                       : att == Attention::kBlocked ? "needs you"
                       : att == Attention::kDone    ? "finished"
                                                    : "idle";
    dl->AddText(s.fonts.ui, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f), att == Attention::kNone ? g_pal->faint : attention_color(att),
                word);
    x += s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, word).x + px(10);
    if (const auto pit = s.views_by_id.find(v.parent); pit != s.views_by_id.end())
        text_clipped(dl, s.fonts.ui, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f), g_pal->faint, "from " + s.pane_title(v.parent),
                     px(200));
    const bool has_pane = s.ws.find(s.peek).space >= 0;
    const char* go = has_pane ? "\xE2\x86\x97 go to" : "\xE2\x86\x97 keep";
    const float gw = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, go).x + px(14);
    ImGui::SetCursorScreenPos(ImVec2(p.x + bw - px(28) - gw - px(6), cy - px(11)));
    std::string cmd;
    if (ImGui::InvisibleButton("##go", ImVec2(gw, px(22))))
        cmd = has_pane ? "agent.focus:" + s.peek : "agent.keep:" + s.peek;
    dl->AddText(s.fonts.ui, fpx(12), ImVec2(p.x + bw - px(28) - gw - px(6) + px(7), cy - fpx(12) * 0.6f),
                ImGui::IsItemHovered() ? g_pal->text : g_pal->dim, go);
    const bool close = icon_button(s, "##peekx", ui::icon::kClose, ImVec2(p.x + bw - px(26), cy - px(11)), px(22), fpx(12),
                                   nullptr, g_pal->dim) ||
                       ImGui::IsKeyPressed(ImGuiKey_Escape);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + px(34)));
    if (!s.peek.empty()) {
        ImGui::BeginChild("##peekbody", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        auto& vs = s.views[s.peek];
        vs.view.keymap = &s.keymap;
        vs.view.theme.default_bg = g_pal->term_bg;
        vs.view.theme.default_fg = g_pal->term_fg;
        vs.view.theme.cursor = g_pal->cursor;
        vs.view.theme.selection = g_pal->selection;
        vs.view.draw("peek", *sit->second, s.fonts, vs.focus_next);
        vs.focus_next = false;
        for (const auto& c : vs.view.take_commands())
            run_command(s, c);
        ImGui::EndChild();
    }
    ImGui::End();
    pop_overlay_style();
    if (close || !cmd.empty())
        s.peek.clear();
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
    ImGui::PushFont(s.fonts.ui_bold, fpx(15));
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

// Keybinds (mods/help): 640 x 520, filter, grouped spaces / tabs / panes / agents / terminal / window.
void draw_help(App::State& s) {
    if (!s.show_help)
        return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = std::min(px(640), vp->WorkSize.x - px(16));
    const float h = std::min(px(520), vp->WorkSize.y - px(16));
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + (vp->WorkSize.y - h) * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    push_overlay_style();
    ImGui::Begin("##help", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    bool close = overlay_title(s, "Keybinds");
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), (px(32) - ImGui::GetFontSize()) * 0.5f));
    ImGui::InputTextWithHint("##hf", "Type to filter\xE2\x80\xA6", s.help_buf, sizeof s.help_buf);
    ImGui::PopStyleVar();
    std::string f = s.help_buf;
    for (auto& c : f)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    struct Item {
        std::string key, label;
    };
    std::map<std::string, std::vector<Item>> groups;
    auto label_of = [](const std::string& cmd) {
        if (cmd.rfind("send:", 0) == 0)
            return "type \"" + cmd.substr(5) + "\"";
        std::string l = cmd;
        for (auto& c : l)
            if (c == '.' || c == ':' || c == '_')
                c = ' ';
        return l;
    };
    auto group_of = [](const std::string& cmd) -> std::string {
        if (cmd.rfind("space.", 0) == 0)
            return "spaces";
        if (cmd.rfind("tab.", 0) == 0)
            return "tabs";
        if (cmd.rfind("pane.", 0) == 0)
            return "panes";
        if (cmd.rfind("agent.", 0) == 0)
            return "agents";
        return "window";
    };
    for (const auto& [chord, cmd] : s.keymap.global_map())
        groups[group_of(cmd)].push_back({ui::Keymap::format(chord), label_of(cmd)});
    for (const char* c : {"terminal.copy", "terminal.paste", "terminal.search", "terminal.select_all"})
        groups["terminal"].push_back({s.keymap.chord_for(c) + " (terminal)", label_of(c)});
    ImGui::BeginChild("##keys", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    for (const char* g : {"spaces", "tabs", "panes", "agents", "terminal", "window"}) {
        bool head = false;
        for (const auto& it : groups[g]) {
            std::string hay = it.key + " " + it.label;
            for (auto& c : hay)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!f.empty() && hay.find(f) == std::string::npos)
                continue;
            if (!head) {
                ImGui::Dummy(ImVec2(0, px(6)));
                ImGui::PushFont(s.fonts.ui_medium, fpx(11));
                ImGui::TextDisabled("%s", g);
                ImGui::PopFont();
                head = true;
            }
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, px(32)));
            ImDrawList* d = ImGui::GetWindowDrawList();
            d->AddText(s.fonts.ui_medium, fpx(12.5f), ImVec2(a.x, a.y + px(16) - fpx(12.5f) * 0.6f), g_pal->focus, it.key.c_str());
            d->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + px(190), a.y + px(16) - fpx(13) * 0.6f), g_pal->text, it.label.c_str());
        }
    }
    ImGui::EndChild();
    close = close || ImGui::IsKeyPressed(ImGuiKey_Escape);
    ImGui::End();
    pop_overlay_style();
    if (close)
        s.show_help = false;
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
        const auto pi = program_info("", profiles[static_cast<std::size_t>(i)].first);
        program_badge(dl, s.fonts.ui_bold, fs, ImVec2(a.x + px(8), a.y + px(7)), px(20), pi);
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
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
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
    if (!open)
        return;
    const bool mod_clone = s.rename_kind == "mod.clone";
    const char* title = mod_clone                  ? "Duplicate mod"
                        : s.rename_kind == "space" ? "Rename space"
                        : s.rename_kind == "tab"   ? "Rename tab"
                                                   : "Rename pane";
    bool cancel = overlay_title(s, title);
    ImGui::PushFont(s.fonts.ui, fpx(12));
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
    ImGui::PushFont(s.fonts.ui, fpx(11.5f));
    ImGui::TextDisabled("%s", mod_clone                  ? "its folder name in the mods folder"
                              : s.rename_kind == "space" ? "empty: named after its folder"
                              : s.rename_kind == "tab" ? "empty: its number"
                                                       : "empty: unnamed");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(6)));
    const float bw = px(80);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw * 2 - px(26));
    ImGui::PushStyleColor(ImGuiCol_Button, v4(raised_color()));
    cancel = ImGui::Button("Cancel", ImVec2(bw, px(30))) || cancel || ImGui::IsKeyPressed(ImGuiKey_Escape);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    primary_button_style(true);
    const bool ok = ImGui::Button(mod_clone ? "Duplicate" : "Save", ImVec2(bw, px(30))) || enter;
    primary_button_style(false);
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
    std::string out;
    bool dash = false;
    for (const char ch : b) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) || c == '.' || c == '_' || c == '-' || c >= 0x80) {
            out += ch;
            dash = false;
        } else if (!dash) {
            out += '-';
            dash = true;
        }
    }
    while (!out.empty() && out.front() == '-')
        out.erase(out.begin());
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    if (out.empty()) {
        std::uint32_t h = 2166136261u;
        for (const unsigned char c : b)
            h = (h ^ c) * 16777619u;
        char buf[16];
        std::snprintf(buf, sizeof buf, "branch-%08x", h);
        out = buf;
    }
    return out;
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
    bool cancel = overlay_title(s, g.repo ? ("New worktree of " + repo).c_str() : "New worktree");
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
        ImGui::SetNextItemWidth(-1);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), (px(36) - ImGui::GetFontSize()) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, v4(g_pal->focus));
        create = ImGui::InputTextWithHint("##branch", "branch name", s.wt_branch, sizeof s.wt_branch,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
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
    cancel = cancel || ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (create && !s.wt_busy) {
        std::string branch = s.wt_branch;
        while (!branch.empty() && branch.back() == ' ')
            branch.pop_back();
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
    const GitInfo g = s.git.get(s.focused_cwd());
    const std::string repo = folder_name(g.common_top.empty() ? g.top : g.common_top);
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
    bool close = overlay_title(s, ("Open worktree of " + repo).c_str());
    ImGui::TextDisabled("%s", count);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##wf", "filter worktrees", s.wt_filter, sizeof s.wt_filter))
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
    ImGui::BeginChild("##wtl", ImVec2(0, std::min(px(330), static_cast<float>(n) * px(44) + px(4))), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
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
        bool shown_in_space = false;
        for (const auto& [id, cwd] : cwds)
            if (_strnicmp(cwd.c_str(), e.path.c_str(), e.path.size()) == 0)
                shown_in_space = true;
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
    close = close || ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (!go.empty()) {
        close = true;
        run_command(s, "worktree.open_at:" + s.worktree_machine + "|" + go);
    }
    if (close)
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ------------------------------------------------------------------ settings (herdr settings.js)
// Modal 620 px high, left navigation 190 px; every change applies and saves at once (no Save).
void settings_section(App::State& s, const char* title) {
    ImGui::Dummy(ImVec2(0, px(10)));
    ImGui::PushFont(s.fonts.ui_medium, fpx(11));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->faint));
    std::string up;
    for (const char* c = title; *c != 0; ++c)
        up += static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
    ImGui::TextUnformatted(up.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(2)));
}

// A labelled row: label + optional note on the left, control on the right (call the control
// after this returns, the cursor is placed for it).
void settings_row(App::State& s, const char* label, const char* note, float control_w) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(p.x, p.y + px(4)), g_pal->text, label);
    float h = px(30);
    if (note != nullptr && *note != 0) {
        text_clipped(dl, s.fonts.ui, fpx(11.5f), ImVec2(p.x, p.y + px(22)), g_pal->faint, note, w - control_w - px(16));
        h = px(44);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - control_w, p.y + px(2)));
    ImGui::SetNextItemWidth(control_w);
    s.settings_row_end = p.y + h;
}
void settings_row_done(App::State& s) {
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, s.settings_row_end));
    ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x);
    ImGui::Dummy(ImVec2(0, px(2)));
}

// "−  NN%  +" stepper (herdr zoom / size steppers). Returns true when the value changed.
bool stepper(App::State& s, const char* id, float& v, float step, float lo, float hi, const char* fmt, float scale = 1.0f) {
    bool changed = false;
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button, v4(raised_color()));
    if (ImGui::Button("\xE2\x88\x92", ImVec2(px(28), px(26)))) {
        v = std::clamp(std::round((v - step) / step) * step, lo, hi);
        changed = true;
    }
    ImGui::SameLine(0, px(4));
    char b[32];
    std::snprintf(b, sizeof b, fmt, v * scale);
    const float tw = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, b).x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(px(56), px(26)));
    ImGui::GetWindowDrawList()->AddText(s.fonts.ui, fpx(13), ImVec2(p.x + (px(56) - tw) * 0.5f, p.y + px(13) - fpx(13) * 0.6f),
                                        g_pal->text, b);
    ImGui::SameLine(0, px(4));
    if (ImGui::Button("+", ImVec2(px(28), px(26)))) {
        v = std::clamp(std::round((v + step) / step) * step, lo, hi);
        changed = true;
    }
    ImGui::PopStyleColor();
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

// Terminal font: family (empty: bundled JetBrains Mono) at the chosen weight, bold from the
// family's 700 face.
void apply_terminal_font(App::State& s) {
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

void draw_settings(App::State& s) {
    if (s.show_settings) {
        ImGui::OpenPopup("##settings");
        s.show_settings = false;
        if (!s.settings_tab.empty())
            s.settings_section = s.settings_tab;
        s.settings_tab.clear();
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = std::min(px(820), vp->WorkSize.x - px(16));
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
    dl->AddText(s.fonts.ui_bold, fpx(15), ImVec2(wp.x + px(18), wp.y + px(18)), g_pal->text, "Settings");
    struct Section {
        const char* name;
        const char* glyph;
    };
    static constexpr Section kSections[] = {{"Look", ui::icon::kPalette},  {"Machines", ui::icon::kServer},
                                            {"Alerts", ui::icon::kWarning}, {"Mods", ui::icon::kExtensions},
                                            {"Updates", ui::icon::kClock},  {"Privacy", ui::icon::kCheck}};
    if (s.settings_section.empty())
        s.settings_section = "Look";
    float ny = wp.y + px(56);
    for (const auto& sec : kSections) {
        const ImVec2 a(wp.x + px(8), ny);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(sec.name);
        if (ImGui::InvisibleButton("##nav", ImVec2(nav_w - px(16), px(30))))
            s.settings_section = sec.name;
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool on = s.settings_section == sec.name;
        if (on || hov)
            dl->AddRectFilled(a, ImVec2(a.x + nav_w - px(16), a.y + px(30)), on ? g_pal->selected : g_pal->hover, px(7));
        draw_icon(s, dl, sec.glyph, fpx(14), ImVec2(a.x + px(17), a.y + px(15)), on || hov ? g_pal->text : g_pal->dim);
        dl->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + px(34), a.y + px(15) - fpx(13) * 0.6f), on || hov ? g_pal->text : g_pal->dim,
                    sec.name);
        ny += px(32);
    }
    // herdr: "Send feedback" at the bottom of the nav.
    {
        const ImVec2 a(wp.x + px(8), wp.y + h - px(44));
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton("##feedback", ImVec2(nav_w - px(16), px(30))))
            run_command(s, "app.feedback");
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(a, ImVec2(a.x + nav_w - px(16), a.y + px(30)), g_pal->hover, px(7));
        draw_icon(s, dl, ui::icon::kComment, fpx(14), ImVec2(a.x + px(17), a.y + px(15)), hov ? g_pal->text : g_pal->dim);
        dl->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + px(34), a.y + px(15) - fpx(13) * 0.6f), hov ? g_pal->text : g_pal->dim,
                    "Send feedback");
    }
    // Content.
    ImGui::SetCursorScreenPos(ImVec2(wp.x + nav_w + 1, wp.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(24), px(18)));
    ImGui::BeginChild("##settings-body", ImVec2(w - nav_w - 1, h), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    bool close = overlay_title(s, s.settings_section.c_str());
    const std::string& sec = s.settings_section;
    if (sec == "Look") {
        settings_section(s, "Layout");
        for (const auto& l : s.layouts) {
            const bool on = l.name == s.settings.layout;
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const float cw = px(120), ch = px(56);
            ImGui::PushID(l.name.c_str());
            if (ImGui::InvisibleButton("##lay", ImVec2(cw, ch)))
                run_command(s, "layout:" + l.name);
            ImGui::PopID();
            ImDrawList* d = ImGui::GetWindowDrawList();
            d->AddRectFilled(a, ImVec2(a.x + cw, a.y + ch), raised_color(), px(8));
            d->AddRect(a, ImVec2(a.x + cw, a.y + ch), on ? g_pal->focus : g_pal->border, px(8), 0, on ? 2.0f : 1.0f);
            std::string label = l.name == "workspace" ? std::string("AgentDesk") : l.name;
            label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
            d->AddText(s.fonts.ui_medium, fpx(12.5f), ImVec2(a.x + px(10), a.y + ch - px(22)), g_pal->text, label.c_str());
            ImGui::SameLine(0, px(8));
            if (ImGui::GetContentRegionAvail().x < cw)
                ImGui::NewLine();
        }
        ImGui::NewLine();
        settings_section(s, "Theme");
        settings_row(s, "Appearance", "Light or dark, or as your system is now.", px(240));
        if (segmented(s, "##appearance", s.settings.appearance, {{"system", "System"}, {"light", "Light"}, {"dark", "Dark"}}))
            changed = true;
        settings_row_done(s);
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->faint));
        ImGui::PushFont(s.fonts.ui, fpx(11.5f));
        ImGui::TextWrapped("A layout file with its own \"theme\" uses that one instead.");
        ImGui::PopFont();
        ImGui::PopStyleColor();
        for (const bool light_cards : {false, true}) {
        ImGui::Dummy(ImVec2(0, px(4)));
        ImGui::PushFont(s.fonts.ui_medium, fpx(12));
        ImGui::TextDisabled("%s", light_cards ? "Light" : "Dark");
        ImGui::PopFont();
        for (const auto& p : kPalettes) {
            if (p.light != light_cards)
                continue;
            const bool on = (light_cards ? s.settings.theme_light : s.settings.theme_dark) == p.id;
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const float cw = px(120), ch = px(64);
            ImGui::PushID(p.id);
            if (ImGui::InvisibleButton("##theme", ImVec2(cw, ch))) {
                run_command(s, std::string("theme:") + p.id);
                changed = true;
            }
            ImGui::PopID();
            ImDrawList* d = ImGui::GetWindowDrawList();
            d->AddRectFilled(a, ImVec2(a.x + cw, a.y + ch), p.bg, px(8));
            d->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + px(30), a.y + ch), p.sidebar, px(8), ImDrawFlags_RoundCornersLeft);
            d->AddCircleFilled(ImVec2(a.x + px(46), a.y + px(16)), px(4), p.focus);
            d->AddCircleFilled(ImVec2(a.x + px(58), a.y + px(16)), px(4), p.done);
            d->AddCircleFilled(ImVec2(a.x + px(70), a.y + px(16)), px(4), p.needs_you);
            d->AddRect(a, ImVec2(a.x + cw, a.y + ch), on ? g_pal->focus : g_pal->border, px(8), 0, on ? 2.0f : 1.0f);
            d->AddText(s.fonts.ui_medium, fpx(12), ImVec2(a.x + px(38), a.y + ch - px(22)), p.text, p.label);
            ImGui::SameLine(0, px(8));
            if (ImGui::GetContentRegionAvail().x < cw)
                ImGui::NewLine();
        }
        ImGui::NewLine();
        }
        settings_section(s, "Size and fonts");
        settings_row(s, "Zoom", "Everything: terminals, panels, text.", px(180) + px(70));
        if (stepper(s, "zoom", s.settings.zoom, 0.1f, 0.5f, 3.0f, "%.0f%%", 100.0f)) {
            g_scale = s.dpi * ui_scale(s.settings);
            apply_style(s.dpi, ui_scale(s.settings));
            changed = true;
        }
        ImGui::SameLine(0, px(8));
        ImGui::BeginDisabled(std::abs(s.settings.zoom - 1.0f) < 0.001f);
        if (ImGui::Button("Reset", ImVec2(px(62), 0))) {  // herdr: Zoom stepper + Reset
            run_command(s, "app.zoom_reset");
        }
        ImGui::EndDisabled();
        settings_row_done(s);
        settings_row(s, "Interface text", "Sidebar, tabs, menus and panels, at 100% zoom.", px(180));
        if (stepper(s, "uitext", s.settings.ui_text, 1.0f, 11.0f, 24.0f, "%.0f px")) {
            g_scale = s.dpi * ui_scale(s.settings);
            apply_style(s.dpi, ui_scale(s.settings));
            changed = true;
        }
        settings_row_done(s);
        {
            settings_row(s, "Terminal font", nullptr, px(200) + px(10) + px(120));
            if (font_picker(s, "##tfont", s.settings.terminal_font, "Default (JetBrains Mono)", px(200))) {
                apply_terminal_font(s);
                changed = true;
            }
            ImGui::SameLine(0, px(10));
            if (stepper(s, "tsize", s.settings.font_size, 1.0f, 8.0f, 32.0f, "%.0f px"))
                changed = true;
            settings_row_done(s);
            // Terminal font weight: the weights the family has.
            settings_row(s, "Terminal font weight", nullptr, px(200));
            const auto* fam =
                find_family(s.settings.terminal_font.empty() ? std::string("JetBrains Mono") : s.settings.terminal_font);
            static constexpr std::pair<int, const char*> kWeightNames[] = {
                {100, "Thin"}, {200, "Extra light"}, {300, "Light"}, {400, "Regular"}, {500, "Medium"},
                {600, "Semibold"}, {700, "Bold"}, {800, "Extra bold"}, {900, "Black"}};
            auto weight_label = [&](int w) {
                for (const auto& [v, n] : kWeightNames)
                    if (v == w)
                        return std::string(n) + " " + std::to_string(w);
                return std::to_string(w);
            };
            ImGui::SetNextItemWidth(px(200));
            if (ImGui::BeginCombo("##tweight", weight_label(s.settings.terminal_weight).c_str())) {
                if (fam != nullptr)
                    for (const auto& [fw, file] : fam->weights)
                        if (ImGui::Selectable(weight_label(fw).c_str(), fw == s.settings.terminal_weight)) {
                            s.settings.terminal_weight = fw;
                            apply_terminal_font(s);
                            changed = true;
                        }
                ImGui::EndCombo();
            }
            settings_row_done(s);
        }
        settings_row(s, "Terminal line height", nullptr, px(180));
        changed |= stepper(s, "lh", s.settings.line_height, 0.1f, 0.8f, 2.0f, "%.0f%%", 100.0f);
        settings_row_done(s);
        settings_row(s, "Terminal cell width", nullptr, px(180));
        changed |= stepper(s, "cw", s.settings.cell_width, 0.05f, 0.8f, 2.0f, "%.0f%%", 100.0f);
        settings_row_done(s);
        settings_row(s, "Terminal scroll speed", nullptr, px(180));
        changed |= stepper(s, "ss", s.settings.scroll_speed, 0.1f, 0.1f, 5.0f, "%.0f%%", 100.0f);
        settings_row_done(s);
        settings_row(s, "Interface font", nullptr, px(200));
        if (font_picker(s, "##uifont", s.settings.ui_font, "Default (Geist)", px(200))) {
            apply_ui_font(s);
            changed = true;
        }
        settings_row_done(s);
        settings_section(s, "Terminals");
        {
            const auto profiles = s.host->profiles();
            const std::string cur = pick_shell(s.settings.shell_profile, profiles);
            std::string label = cur.empty() ? std::string("(none found)") : cur;
            for (const auto& [id, pname] : profiles)
                if (id == cur)
                    label = pname;
            settings_row(s, "Shell", "New spaces, tabs and splits open it in the focused folder.", px(220));
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
        settings_row(s, "Copy on select", "Copy the selection when the mouse is released.", px(40));
        changed |= ImGui::Checkbox("##cos", &s.settings.copy_on_select);
        settings_row_done(s);
        settings_row(s, "Keys", "keys.json: change any shortcut.", px(120));
        if (ImGui::Button("Edit keys.json", ImVec2(px(120), 0)))
            run_command(s, "app.keys");
        settings_row_done(s);
        for (const auto& e : s.keymap_errors)
            ImGui::TextColored(v4(g_pal->needs_you), "keys.json: cannot use \"%s\"", e.c_str());
        {
            static char buf[512];
            if (ImGui::IsWindowAppearing() || buf[0] == '\0')
                std::snprintf(buf, sizeof buf, "%s", s.settings.worktree_dir.c_str());
            settings_row(s, "Worktrees folder", "Empty: %USERPROFILE%\\.agentdesk\\worktrees", px(260));
            if (ImGui::InputText("##wt", buf, sizeof buf)) {
                s.settings.worktree_dir = buf;
                changed = true;
            }
            settings_row_done(s);
        }
    } else if (sec == "Machines") {
        ImGui::TextWrapped("Other computers reachable over ssh. AgentDesk runs \"ssh <target> agentctl bridge\" and "
                           "shows that machine's spaces and agents here; they keep running there while this PC "
                           "sleeps. The other computer needs AgentDesk and key-based ssh login.");
        settings_section(s, "Machines");
        int remove = -1;
        for (int i = 0; i < static_cast<int>(s.settings.machines.size()); ++i) {
            const auto& m = s.settings.machines[static_cast<std::size_t>(i)];
            ImGui::PushID(i);
            const bool on = s.machine_connected(m.name);
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, px(40)));
            ImDrawList* d = ImGui::GetWindowDrawList();
            draw_icon(s, d, ui::icon::kServer, fpx(14), ImVec2(a.x + px(10), a.y + px(20)), g_pal->dim);
            d->AddText(s.fonts.ui_medium, fpx(13), ImVec2(a.x + px(28), a.y + px(6)), g_pal->text, m.name.c_str());
            d->AddText(s.fonts.ui, fpx(11.5f), ImVec2(a.x + px(28), a.y + px(23)), g_pal->faint, m.target.c_str());
            d->AddCircleFilled(ImVec2(a.x + ImGui::GetContentRegionAvail().x - px(90), a.y + px(20)), px(3),
                               on ? g_pal->done : g_pal->faint);
            ImGui::SetCursorScreenPos(ImVec2(a.x + ImGui::GetContentRegionAvail().x - px(76), a.y + px(8)));
            if (ImGui::SmallButton("Remove"))
                remove = i;
            ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + px(40)));
            if (!on)
                for (const auto& r : s.remotes)
                    if (r.cfg.name == m.name && !r.host->last_error().empty())
                        ImGui::TextColored(v4(g_pal->needs_you), "  %s", r.host->last_error().c_str());
            ImGui::PopID();
        }
        if (s.settings.machines.empty())
            ImGui::TextDisabled("No machines yet.");
        if (remove >= 0) {
            s.settings.machines.erase(s.settings.machines.begin() + remove);
            changed = true;
            start_remotes(s);
        }
        ImGui::Dummy(ImVec2(0, px(8)));
        primary_button_style(true);
        if (ImGui::Button("Add a machine\xE2\x80\xA6"))
            s.show_machine_add = true;
        primary_button_style(false);
    } else if (sec == "Alerts") {
        settings_section(s, "Sounds");
        settings_row(s, "Play sounds",
                     "When an agent needs you, and when one finishes while you're not looking. A note shows in the "
                     "corner either way.",
                     px(40));
        changed |= ImGui::Checkbox("##snd", &s.settings.sounds);
        settings_row_done(s);
        auto sound_row = [&](const char* label, const std::string& file, bool blocked) {
            settings_row(s, label, file.empty() ? "AgentDesk's sound" : file.c_str(), px(70));
            if (ImGui::Button(blocked ? "Play##b" : "Play##d", ImVec2(px(70), 0)))
                play_alert(s, blocked);
            settings_row_done(s);
        };
        sound_row("An agent needs you", s.settings.sound_blocked, true);
        sound_row("An agent finished", s.settings.sound_done, false);
        ImGui::TextDisabled("Use your own: set sound_done / sound_blocked in ui.json to an MP3 file.");
        settings_section(s, "Notes");
        settings_row(s, "Show a note in the corner", "For agents in your spaces; click it to go there.", px(40));
        changed |= ImGui::Checkbox("##toasts", &s.settings.toasts);
        settings_row_done(s);
    } else if (sec == "Mods") {
        ImGui::TextWrapped("Mods are C++ DLLs in the mods folder (one folder per mod, mod.dll). They can react to "
                           "agents, add commands, panels and tab text, and start programs. A mod runs with your "
                           "rights like any program you install, so only turn on mods you trust; one that crashes "
                           "is turned off. The sdk folder of the zip has the headers and the examples' sources.");
        settings_section(s, "Installed");
        for (const auto& m : s.mods->mods()) {
            ImGui::PushID(m.name.c_str());
            bool on = s.settings.mods_enabled.contains(m.name);
            settings_row(s, m.name.c_str(), m.description.c_str(), px(40));
            if (ImGui::Checkbox("##on", &on)) {
                if (on)
                    s.settings.mods_enabled.insert(m.name);
                else
                    s.settings.mods_enabled.erase(m.name);
                changed = true;
                s.mods->load(s.settings.mods_enabled);
            }
            settings_row_done(s);
            if (!m.error.empty())
                ImGui::TextColored(v4(g_pal->needs_you), "  %s", m.error.c_str());
            ImGui::PopID();
        }
        if (s.mods->mods().empty())
            ImGui::TextDisabled("No mods yet.");
        ImGui::Dummy(ImVec2(0, px(6)));
        if (ImGui::Button("Reload"))
            run_command(s, "mods.reload");
        ImGui::SameLine();
        if (ImGui::Button("Open mods folder"))
            run_command(s, "mods.folder");
    } else if (sec == "Updates") {
        settings_section(s, "Versions");
        ImGui::Text("AgentDesk %s", AGENTDESK_VERSION_STRING);
        ImGui::TextDisabled("agent-host: protocol v1 over a named pipe");
        ImGui::Dummy(ImVec2(0, px(6)));
        ImGui::TextWrapped("AgentDesk ships as a portable zip. To update, close it (agents keep running in "
                           "agent-host), unpack the new zip over the old one and start it again.");
        ImGui::Dummy(ImVec2(0, px(6)));
        if (ImGui::Button("What's new"))
            run_command(s, "app.whats_new");
    } else if (sec == "Privacy") {
        settings_section(s, "Usage data");
        ImGui::TextWrapped("AgentDesk sends nothing anywhere: no usage reports, no crash uploads. Crashes leave "
                           "ui-crash.txt in the data folder for you to look at.");
    }
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
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = std::min(px(660), vp->WorkSize.x - px(16));
    const float h = std::min(px(600), vp->WorkSize.y - px(16));
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + (vp->WorkSize.y - h) * 0.5f));
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
    auto title = [&](const char* t, const char* sub) {
        ImGui::PushFont(s.fonts.ui_bold, fpx(20));
        ImGui::TextWrapped("%s", t);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, px(4)));
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
        ImGui::TextWrapped("%s", sub);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, px(14)));
    };
    auto feature = [&](const char* glyph, const char* head, const char* text) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        draw_icon(s, ImGui::GetWindowDrawList(), glyph, fpx(16), ImVec2(a.x + px(10), a.y + px(10)), g_pal->focus);
        ImGui::SetCursorScreenPos(ImVec2(a.x + px(32), a.y));
        ImGui::BeginGroup();
        ImGui::PushFont(s.fonts.ui_medium, fpx(13.5f));
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
    bool changed = false;
    if (page == 0) {
        title("AgentDesk runs your terminals in the background",
              "Close the window and your agents keep working; open it again and everything is where you left it.");
        feature(ui::icon::kLayers, "Spaces, tabs and panes", "A space per project; split it into the terminals you need.");
        feature(ui::icon::kTerminal, "Agents report what they're doing",
                "Claude Code and Codex tell AgentDesk when they work, finish or need you.");
        feature(ui::icon::kChanges, "Files and Changes", "Look at what your agents changed, and commit it.");
        ImGui::Dummy(ImVec2(0, px(6)));
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
        for (const auto& m : s.settings.machines)
            ImGui::BulletText("%s  (%s)", m.name.c_str(), m.target.c_str());
        primary_button_style(true);
        if (ImGui::Button("Add a machine\xE2\x80\xA6"))
            s.show_machine_add = true;
        primary_button_style(false);
    } else if (page == 2) {
        title("Choose your panels", "Buttons at the right of the tab bar open them beside your terminals.");
        // herdr welcome.js 396-413: the first visit to this page starts with both off; skipping
        // the welcome leaves both on.
        if (!s.welcome_panels_seen) {
            s.welcome_panels_seen = true;
            s.settings.panels.clear();
            changed = true;
        }
        auto toggle = [&](const char* id, const char* label, const char* text) {
            bool on = std::find(s.settings.panels.begin(), s.settings.panels.end(), id) != s.settings.panels.end();
            if (ImGui::Checkbox(label, &on)) {
                if (on)
                    s.settings.panels.push_back(id);
                else
                    s.settings.panels.erase(std::remove(s.settings.panels.begin(), s.settings.panels.end(), id),
                                            s.settings.panels.end());
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", text);
        };
        toggle("files", "Files", "Browse the focused terminal's folder.");
        toggle("changes", "Changes", "What your agents changed: every file with its diff, and a commit box.");
    } else {
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
    }
    // Footer: dots, Back / Next.
    const float fy = ImGui::GetWindowPos().y + h - px(56);
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
    if (!typing && (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)))
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
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        s.settings.welcomed = "skipped";
        changed = true;
        ImGui::CloseCurrentPopup();
    }
    if (changed)
        save_settings(s.settings);
    ImGui::EndPopup();
}

// ------------------------------------------------------------------ add a machine (herdr machine-add)
// Hosts from ~/.ssh/config (Include followed), or typed "user@host"; adding connects at once.
std::vector<std::pair<std::string, std::string>> ssh_config_hosts() {
    std::vector<std::pair<std::string, std::string>> out;  // alias, user@hostname
    wchar_t home[MAX_PATH]{};
    if (::GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH) == 0)
        return out;
    const std::filesystem::path ssh = std::filesystem::path(home) / L".ssh";
    std::set<std::string> seen;
    std::function<void(const std::filesystem::path&, int)> read = [&](const std::filesystem::path& file, int depth) {
        if (depth > 16)
            return;
        std::ifstream in(file);
        std::vector<std::string> current;
        std::string user, hostname;
        auto flush = [&] {
            for (const auto& alias : current) {
                if (alias.find_first_of("*?!") != std::string::npos || seen.contains(alias))
                    continue;
                seen.insert(alias);
                out.emplace_back(alias, (user.empty() ? "" : user + "@") + (hostname.empty() ? alias : hostname));
            }
            current.clear();
            user.clear();
            hostname.clear();
        };
        for (std::string line; std::getline(in, line);) {
            std::stringstream ss(line);
            std::string key;
            ss >> key;
            for (auto& c : key)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (key == "host") {
                flush();
                for (std::string a; ss >> a;)
                    current.push_back(a);
            } else if (key == "hostname") {
                ss >> hostname;
            } else if (key == "user") {
                ss >> user;
            } else if (key == "include") {
                for (std::string inc; ss >> inc;) {
                    std::filesystem::path p(win::to_wide(inc));
                    if (p.is_relative())
                        p = ssh / p;
                    std::error_code ec;
                    if (std::filesystem::is_regular_file(p, ec))
                        read(p, depth + 1);
                }
            }
        }
        flush();
    };
    read(ssh / L"config", 0);
    return out;
}

void draw_machine_add(App::State& s) {
    if (s.show_machine_add) {
        ImGui::OpenPopup("##machineadd");
        s.show_machine_add = false;
        s.ssh_hosts = ssh_config_hosts();
        s.machine_buf[0] = 0;
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
    auto added = [&](const std::string& target) {
        for (const auto& m : s.settings.machines)
            if (m.target == target || m.name == target)
                return true;
        return false;
    };
    auto add = [&](const std::string& name, const std::string& target) {
        s.settings.machines.push_back({name, target, ""});
        save_settings(s.settings);
        start_remotes(s);
    };
    settings_section(s, s.ssh_hosts.empty() ? "Your ssh config lists no hosts" : "From your ssh config");
    for (const auto& [alias, where] : s.ssh_hosts) {
        if (added(alias))
            continue;
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::PushID(alias.c_str());
        const float rw = ImGui::GetContentRegionAvail().x;
        const bool clicked = ImGui::InvisibleButton("##h", ImVec2(rw, px(40)));
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* d = ImGui::GetWindowDrawList();
        if (hov)
            d->AddRectFilled(a, ImVec2(a.x + rw, a.y + px(40)), g_pal->hover, px(7));
        draw_icon(s, d, ui::icon::kServer, fpx(14), ImVec2(a.x + px(14), a.y + px(20)), g_pal->dim);
        d->AddText(s.fonts.ui_medium, fpx(13), ImVec2(a.x + px(32), a.y + px(20) - fpx(13) * 0.6f), g_pal->text, alias.c_str());
        const float aw = s.fonts.ui_medium->CalcTextSizeA(fpx(13), FLT_MAX, 0, alias.c_str()).x;
        d->AddText(s.fonts.ui, fpx(12), ImVec2(a.x + px(40) + aw, a.y + px(20) - fpx(12) * 0.6f), g_pal->faint, where.c_str());
        d->AddText(s.fonts.ui_medium, fpx(12), ImVec2(a.x + rw - px(40), a.y + px(20) - fpx(12) * 0.6f), g_pal->focus, "Add");
        if (clicked)
            add(alias, alias);
        ImGui::PopID();
    }
    for (const auto& m : s.settings.machines) {
        ImGui::TextDisabled("%s", m.name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(v4(g_pal->done), "\xE2\x9C\x93 Added");
    }
    settings_section(s, "Or type how ssh reaches it");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - px(90));
    const bool enter = ImGui::InputTextWithHint("##target", "user@host", s.machine_buf, sizeof s.machine_buf,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine(0, px(8));
    ImGui::BeginDisabled(s.machine_buf[0] == 0);
    primary_button_style(true);
    const bool go = ImGui::Button("Add", ImVec2(px(80), 0)) || enter;
    primary_button_style(false);
    ImGui::EndDisabled();
    if (go && s.machine_buf[0] != 0) {
        std::string t = s.machine_buf;
        const auto at = t.find('@');
        add(at == std::string::npos ? t : t.substr(at + 1), t);
        s.machine_buf[0] = 0;
    }
    if (close || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- confirm (mods/confirm)
// ------------------------------------------------------------------ What's new / feedback
// herdr app.whats_new (release notes, shown once after an update) and app.feedback (a draft the
// person reviews). AgentDesk has no server: the draft is copied or saved for them to send.
constexpr const char* kWhatsNew[] = {
    "Looks and works like herdr's desktop app: sidebar with spaces and agents, tab bar with the window "
    "buttons, Files and Changes panels, picker, command palette, settings and welcome.",
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
    center_next(px(feedback ? 560.0f : 520.0f), 0.16f);
    push_overlay_style();
    const bool open = ImGui::BeginPopupModal("##notes", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    pop_overlay_style();
    if (!open)
        return;
    std::string title = feedback ? std::string("Send feedback") : std::string("What's new in ") + AGENTDESK_VERSION_STRING;
    bool close = overlay_title(s, title.c_str());
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
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
        ImGui::TextWrapped("What works, what doesn't, what you'd like. AgentDesk sends nothing by itself: copy the "
                           "draft or save it to a file, then send it the way you like.");
        ImGui::PopStyleColor();
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::InputTextMultiline("##fb", s.feedback_buf, sizeof s.feedback_buf, ImVec2(-1, px(180)));
        ImGui::PopStyleVar();
        OSVERSIONINFOEXW os{};
        os.dwOSVersionInfoSize = sizeof os;
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
        if (auto fn_ptr = reinterpret_cast<RtlGetVersionFn>(
                ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")))
            fn_ptr(&os);
        const std::string about = std::string("AgentDesk ") + AGENTDESK_VERSION_STRING + ", Windows " +
                                  std::to_string(os.dwMajorVersion) + "." + std::to_string(os.dwMinorVersion) + "." +
                                  std::to_string(os.dwBuildNumber) + ", layout " + s.settings.layout;
        ImGui::PushFont(s.fonts.ui, fpx(11.5f));
        ImGui::TextDisabled("Added at the end: %s", about.c_str());
        ImGui::PopFont();
        const std::string draft = std::string(s.feedback_buf) + "\n\n-- " + about + "\n";
        if (!s.feedback_note.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->focus));
            ImGui::TextWrapped("%s", s.feedback_note.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0, px(4)));
        const float bw = px(100);
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw * 3 - px(34));
        ImGui::PushStyleColor(ImGuiCol_Button, v4(raised_color()));
        close = ImGui::Button("Close", ImVec2(bw, px(30))) || close;
        ImGui::SameLine();
        const bool empty = s.feedback_buf[0] == '\0';
        ImGui::BeginDisabled(empty);
        if (ImGui::Button("Save to file", ImVec2(bw, px(30)))) {
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
        ImGui::PopStyleColor();
        ImGui::SameLine();
        primary_button_style(true);
        if (ImGui::Button("Copy", ImVec2(bw, px(30)))) {
            ImGui::SetClipboardText(draft.c_str());
            s.feedback_note = "Copied. Paste it where you want to send it.";
        }
        primary_button_style(false);
        ImGui::EndDisabled();
    }
    if (close || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
    bool cancel = overlay_title(s, s.confirm_title.c_str());
    if (!s.confirm_detail.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
        ImGui::TextWrapped("%s", s.confirm_detail.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, px(8)));
    const float bw = px(84);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw * 2 - px(26));
    ImGui::PushStyleColor(ImGuiCol_Button, v4(raised_color()));
    cancel = ImGui::Button("Cancel", ImVec2(bw, px(30))) || cancel || ImGui::IsKeyPressed(ImGuiKey_Escape);
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

}  // namespace

// ------------------------------------------------------------------ frame
void App::frame() {
    State& s = *s_;
    g_scale = s.dpi * ui_scale(s.settings);
    s.fonts.mono_size = s.settings.font_size / ui_text_factor(s.settings);
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
    }
    if (!g_settings_problem.empty()) {
        s.toast(g_settings_problem, "", "", g_pal->needs_you, 8);
        g_settings_problem.clear();
    }
    s.keymap.set_layout(s.settings.layout);  // keys.json "layout:<name>" section
    if (const Palette* want = effective_palette(s.settings.appearance, s.settings.theme_dark, s.settings.theme_light,
                                                s.layout().theme);
        want != g_pal) {
        g_pal = want;
        apply_style(s.dpi, ui_scale(s.settings));
    }
    const bool connected = s.host->connected();

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
    run_mods(s);
    for (const auto& c : std::exchange(s.pending_mod_commands, {}))
        run_command(s, c);
    if (s.started_dev || s.config.start_profile.empty()) {
        if (s.next_dev_command < s.config.run_commands.size() && s.doc_loaded && !s.launch_busy &&
            Clock::now() >= s.next_dev_at && !s.sessions.empty()) {
            if (s.next_dev_at == Clock::time_point{}) {
                s.next_dev_at = Clock::now() + std::chrono::milliseconds(800);
            } else {
                run_command(s, s.config.run_commands[s.next_dev_command++]);
                s.next_dev_at = Clock::now() + std::chrono::milliseconds(700);
            }
        }
    }

    // Global shortcuts while no terminal holds the keyboard (a focused terminal reports them).
    if (!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        for (const auto& [chord, cmd] : s.keymap.global_map()) {
            if (ImGui::IsKeyChordPressed(chord))
                run_command(s, cmd);
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
    const LayoutSpec& layout = s.layout();
    const bool show_sidebar = layout.sidebar && s.settings.sidebar;
    const bool show_details = layout.details.value_or(s.settings.details);
    // herdr main.json: root row = sidebar (268, 44..480, chrome) | 1 px border | work column
    // (tabs 42, body padding 8 gap 8 with the right panel 368, status 28).
    ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + full.x, origin.y + full.y), g_pal->bg);
    const float sidebar_w = show_sidebar ? px(s.settings.sidebar_width) : 0.0f;
    const float status_h = layout.status_bar ? px(28) : 0.0f;
    const float tabbar_h = layout.tab_bar ? px(42) : 0.0f;
    const float gap = px(8);
    if (show_sidebar) {
        draw_sidebar(s, origin, ImVec2(sidebar_w, full.y));
        ImGui::GetWindowDrawList()->AddLine(ImVec2(origin.x + sidebar_w, origin.y),
                                            ImVec2(origin.x + sidebar_w, origin.y + full.y), g_pal->border);
        // The sidebar edge drags between 44 and 480 px (6 px hit strip).
        ImGui::SetCursorScreenPos(ImVec2(origin.x + sidebar_w - px(3), origin.y + px(46)));
        ImGui::InvisibleButton("##sidebar-edge", ImVec2(px(6), full.y - px(46)));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive())
            s.settings.sidebar_width = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / g_scale, 44.0f, 480.0f);
        if (ImGui::IsItemDeactivated())
            save_settings(s.settings);
    }
    const float work_x = origin.x + sidebar_w + (show_sidebar ? 1.0f : 0.0f);
    const ImVec2 main_pos(work_x, origin.y);
    const float main_w = origin.x + full.x - work_x;
    if (layout.tab_bar)
        draw_tab_bar(s, main_pos, main_w, tabbar_h);
    const auto* active_space = s.ws.active();
    const bool show_panel = layout.main != "board" && active_space != nullptr && !active_space->panel.empty();
    const float panel_w = show_panel ? std::min(px(s.settings.panel_width), main_w * 0.6f) : 0.0f;
    const float details_w = show_details ? std::min(px(320), main_w * 0.3f) : 0.0f;
    const ImVec2 content_pos(main_pos.x + gap, main_pos.y + tabbar_h + gap);
    const ImVec2 content_size(main_w - gap * 2 - (details_w > 0 ? details_w + gap : 0.0f) -
                                  (panel_w > 0 ? panel_w + gap : 0.0f),
                              full.y - status_h - tabbar_h - gap * 2);
    if (layout.main == "board")
        draw_board(s, content_pos, content_size);
    else if (layout.main == "grid")
        draw_grid(s, content_pos, content_size, connected);
    else
        draw_panes(s, content_pos, content_size, connected);
    float right_x = content_pos.x + content_size.x + gap;
    if (panel_w > 0) {
        // The boundary between the panes and the panel drags (280..900 px).
        ImGui::SetCursorScreenPos(ImVec2(right_x - gap * 0.5f - px(3), content_pos.y));
        ImGui::InvisibleButton("##panel-edge", ImVec2(px(6), content_size.y));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive())
            s.settings.panel_width =
                std::clamp((main_pos.x + main_w - gap - ImGui::GetIO().MousePos.x) / g_scale, 280.0f, 900.0f);
        if (ImGui::IsItemDeactivated())
            save_settings(s.settings);
        draw_side_panel(s, ImVec2(right_x, content_pos.y), ImVec2(panel_w, content_size.y));
        right_x += panel_w + gap;
    }
    if (details_w > 0)
        draw_details(s, ImVec2(right_x, content_pos.y), ImVec2(details_w, content_size.y), connected);
    if (layout.status_bar)
        draw_status_bar(s, ImVec2(work_x, origin.y + full.y - status_h), main_w, status_h);
    ImGui::End();

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
}

}  // namespace agentdesk::desktop
