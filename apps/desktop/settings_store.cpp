// ui.json: the Settings record, reading it (with old-key migration) and writing it back without
// losing keys or overwriting a file that has errors. Also the data and executable folders.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "app_internal.hpp"

// GetDpiForMonitor; app_internal.hpp brings in windows.h first.
#include <shellscalingapi.h>

namespace agentdesk::desktop {
namespace detail {

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

std::filesystem::path exe_dir() {
    wchar_t exe[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return std::filesystem::path(exe).parent_path();
}

namespace {
// The first run's zoom: Windows' text size (Accessibility TextScaleFactor, 100..225 %), x1.15 when
// the primary screen is 2400 DIPs or wider, rounded to tenths. DIPs, not pixels: Windows' display
// scale already draws everything bigger, so a 4K screen at 200 % (1920 DIPs) gets no extra.
float first_run_zoom() {
    DWORD factor = 100, size = sizeof(factor);
    ::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Accessibility", L"TextScaleFactor", RRF_RT_REG_DWORD,
                   nullptr, &factor, &size);
    float z = std::clamp(static_cast<float>(factor) / 100.0f, 1.0f, 2.0f);
    UINT dpi = 96, dpi_y = 96;
    const HMONITOR primary = ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (FAILED(::GetDpiForMonitor(primary, MDT_EFFECTIVE_DPI, &dpi, &dpi_y)) || dpi == 0)
        dpi = 96;
    if (::GetSystemMetrics(SM_CXSCREEN) * 96.0 / dpi >= 2400.0)
        z *= 1.15f;
    return std::clamp(std::round(z * 10.0f) / 10.0f, 0.5f, 3.0f);
}

}  // namespace

// The shown layout's {dark, light} themes, else the defaults.
std::pair<std::string, std::string> layout_theme_pair(const Settings& st) {
    if (const auto it = st.layout_themes.find(st.layout); it != st.layout_themes.end())
        return it->second;
    return {st.theme_dark, st.theme_light};
}

namespace {
// ui.json parsed but a key has the wrong type: the defaults are used, and nothing is written over
// the person's file until they fix it (as when it does not parse).
bool g_settings_unreadable = false;

// The theme keys. Files from before "appearance" existed keep their one theme as the fixed
// choice; ids of the older preset spelling are read as today's (theme_id).
void read_themes(const Json& j, Settings& s) {
    s.theme = theme_id(j.value("theme", s.theme));
    const bool light = std::string(palette_by_id(s.theme)->id) == s.theme && palette_by_id(s.theme)->light;
    s.appearance = j.value("appearance", j.contains("theme") ? (light ? "light" : "dark") : "system");
    s.theme_dark = theme_id(j.value("theme_dark", light ? std::string("midnight") : s.theme));
    s.theme_light = theme_id(j.value("theme_light", light ? s.theme : std::string("paper")));
    if (const auto lt = j.find("layout_themes"); lt != j.end() && lt->is_object())
        for (const auto& [name, pair] : lt->items())
            if (pair.is_object())
                s.layout_themes[name] = {theme_id(pair.value("dark", s.theme_dark)),
                                         theme_id(pair.value("light", s.theme_light))};
}

// The string elements of `list`, in order; anything else in it is skipped.
template <class Out>
void add_strings(const Json& list, Out& out) {
    for (const auto& v : list)
        if (v.is_string())
            out.insert(out.end(), v.get<std::string>());
}

// The list keys. "panels" and "mods_enabled" replace the defaults only when present as a list.
void read_lists(const Json& j, Settings& s) {
    add_strings(j.value("folded_machines", Json::array()), s.folded_machines);
    if (const auto it = j.find("panels"); it != j.end() && it->is_array()) {
        s.panels.clear();
        add_strings(*it, s.panels);
    }
    add_strings(j.value("palette_recent", Json::array()), s.palette_recent);
    if (const auto it = j.find("agents_changed"); it != j.end() && it->is_object())
        s.agents_changed = *it;
    if (const auto it = j.find("mods_enabled"); it != j.end() && it->is_array()) {
        s.mods_enabled.clear();
        add_strings(*it, s.mods_enabled);
    }
}

// Throws Json::exception when a key has the wrong type (load_settings catches it).
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
    // The telemetry choice is honoured even when the rest of the file can't be used.
    if (j.is_object() && j.contains("telemetry") && j["telemetry"].is_boolean())
        s.telemetry = j["telemetry"].get<bool>();
    if (!j.is_object() || j.value("schema_version", 0) != 1)
        return s;
    s.close_notice_shown = j.value("close_notice_shown", false);
    s.last_cwd = j.value("last_cwd", "");
    s.terminal_weight = std::clamp(j.value("terminal_weight", s.terminal_weight), 100, 900);
    s.ui_font = j.value("ui_font", "");
    s.window_buttons = j.value("window_buttons", "mac") == "windows" ? "windows" : "mac";
    read_themes(j, s);
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
    s.focus_on = j.value("focus_on", false);
    s.focus_alerts = ui::focus::alerts_mode(j.value("focus_alerts", std::string("edge_sound")));
    s.layout_return = j.value("layout_return", "");
    s.mission_filter = j.value("mission_filter", std::string("all"));
    s.mission_sub_agents = j.value("mission_sub_agents", false);
    s.mission_shells = j.value("mission_shells", false);
    read_lists(j, s);
    s.layout = j.value("layout", s.layout);
    for (const auto& m : j.value("machines", Json::array())) {
        if (m.is_object() && !m.value("name", "").empty() && !m.value("target", "").empty())
            s.machines.push_back({m.value("name", ""), m.value("target", ""), m.value("ctl", "")});
    }
    return s;
}

}  // namespace

Settings load_settings() {
    try {
        return load_settings_unchecked();
    } catch (const Json::exception&) {
        g_settings_unreadable = true;
        return Settings{};
    }
}

// Interface text size relative to the 13 px the chrome is designed at; the whole chrome scales
// with it and the zoom.
float ui_text_factor(const Settings& s) {
    return s.ui_text / 13.0f;
}
float ui_scale(const Settings& s) {
    return s.zoom * ui_text_factor(s);
}

std::string g_settings_problem;  // why the last write did not happen (shown once as a toast)

std::string read_text_file(const std::filesystem::path& p, bool* ok) {
    std::ifstream in(p, std::ios::binary);
    if (ok != nullptr)
        *ok = static_cast<bool>(in);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Re-reads the file, applies our keys onto it (keys we do not own stay), writes only if nobody
// changed it meanwhile (3 tries), and never overwrites a file that does not parse.
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
            {"window_buttons", s.window_buttons},
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
            {"focus_on", s.focus_on},
            {"focus_alerts", s.focus_alerts},
            {"layout_return", s.layout_return},
            {"mission_filter", s.mission_filter},
            {"mission_sub_agents", s.mission_sub_agents},
            {"mission_shells", s.mission_shells},
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
        // Telemetry off writes false; on deletes the key (on is the default).
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
}  // namespace detail

}  // namespace agentdesk::desktop
