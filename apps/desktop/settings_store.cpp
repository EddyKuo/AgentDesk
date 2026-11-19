// ui.json: the Settings record, reading it (with old-key migration) and writing it back without
// losing keys or overwriting a file that has errors.
#include "app_internal.hpp"

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

namespace {
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

}  // namespace

// The shown layout's {dark, light} themes (herdr: on the layout's root), else the defaults.
std::pair<std::string, std::string> layout_theme_pair(const Settings& st) {
    if (const auto it = st.layout_themes.find(st.layout); it != st.layout_themes.end())
        return it->second;
    return {st.theme_dark, st.theme_light};
}

namespace {
// ui.json parsed but a key has the wrong type: the defaults are used, and nothing is written over
// the person's file until they fix it (as when it does not parse).
bool g_settings_unreadable = false;

Settings load_settings_unchecked();

}  // namespace

Settings load_settings() {
    try {
        return load_settings_unchecked();
    } catch (const Json::exception&) {
        g_settings_unreadable = true;
        return Settings{};
    }
}

namespace {
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

}  // namespace

// Interface text size relative to herdr's 13 px; the whole chrome scales with it and the zoom.
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
}  // namespace detail

}  // namespace agentdesk::desktop
