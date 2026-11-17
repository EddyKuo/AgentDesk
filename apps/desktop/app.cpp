// AgentDesk window contents. Feature set follows herdr's app (spaces, tabs, split panes, agent
// attention, notifications, command palette, worktrees; observed from its public guide
// defaults/AGENTS.md and keys.json), with AgentDesk's own look, names and code (ADR-12/ADR-13).
#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <dwmapi.h>
#include <imgui.h>
#include <mmsystem.h>
#include <shellapi.h>

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

void apply_style(float dpi, float zoom) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    if (g_pal->light)
        ImGui::StyleColorsLight(&s);
    else
        ImGui::StyleColorsDark(&s);
    s.WindowRounding = 12;
    s.ChildRounding = 10;
    s.FrameRounding = 7;
    s.PopupRounding = 10;
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
    c[ImGuiCol_PopupBg] = v4(g_pal->card);
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
    std::set<std::string> mods_enabled = {"tab-timer"};
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

Settings load_settings() {
    Settings s;
    std::ifstream in(data_dir() / L"ui.json", std::ios::binary);
    if (!in)
        return s;
    std::stringstream ss;
    ss << in.rdbuf();
    const auto j = Json::parse(ss.str(), nullptr, false);
    if (j.is_discarded() || j.value("schema_version", 0) != 1)
        return s;
    s.close_notice_shown = j.value("close_notice_shown", false);
    s.last_cwd = j.value("last_cwd", "");
    s.theme = j.value("theme", s.theme);
    s.zoom = std::clamp(j.value("zoom", 1.0f), 0.6f, 2.5f);
    s.font_size = std::clamp(j.value("font_size", 14.0f), 9.0f, 28.0f);
    s.sounds = j.value("sounds", true);
    s.toasts = j.value("toasts", true);
    s.copy_on_select = j.value("copy_on_select", true);
    s.scroll_speed = std::clamp(j.value("scroll_speed", 1.0f), 0.1f, 5.0f);
    s.sidebar = j.value("sidebar", true);
    s.details = j.value("details", false);
    s.worktree_dir = j.value("worktree_dir", "");
    s.shell_profile = j.value("shell_profile", "");
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

void save_settings(const Settings& s) {
    const auto path = data_dir() / L"ui.json";
    const auto tmp = std::filesystem::path(path).concat(L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << Json{
            {"schema_version", 1},
            {"close_notice_shown", s.close_notice_shown},
            {"last_cwd", s.last_cwd},
            {"theme", s.theme},
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
            {"mods_enabled", std::vector<std::string>(s.mods_enabled.begin(), s.mods_enabled.end())},
            {"layout", s.layout},
            {"machines", [&] {
                 Json list = Json::array();
                 for (const auto& m : s.machines)
                     list.push_back({{"name", m.name}, {"target", m.target}, {"ctl", m.ctl}});
                 return list;
             }()}}.dump(2);
    }
    ::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
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
constexpr const char* kTabTimerMod = R"(-- Shows how long each tab's agent has been working
local started = {}

agentdesk.on("agent_state", function(ev)
  if ev.state == "Working" then
    started[ev.session] = started[ev.session] or os.time()
  else
    started[ev.session] = nil
  end
end)

local function since(t)
  local s = os.time() - t
  if s < 60 then return s .. "s" end
  return math.floor(s / 60) .. "m"
end

agentdesk.decorate("tab", function(tab)
  local oldest
  for _, id in ipairs(tab.sessions) do
    local t = started[id]
    if t and (not oldest or t < oldest) then oldest = t end
  end
  if oldest then return since(oldest) end
  return ""
end)
)";

constexpr const char* kNtfyMod = R"(-- Phone notification through ntfy.sh when an agent needs you
-- Set your topic in settings.json next to this file, then enable the mod in Settings > Mods.
local topic = agentdesk.setting("topic", "")

agentdesk.on("agent_state", function(ev)
  if topic == "" then return end
  if ev.state == "Blocked" or (ev.previous == "Working" and ev.state == "Idle") then
    local what = ev.state == "Blocked" and "needs you" or "finished"
    agentdesk.exec({"curl", "-s", "-d", ev.title .. " " .. what, "https://ntfy.sh/" .. topic})
  end
end)

agentdesk.command("test", "Send a test notification", function()
  if topic == "" then
    agentdesk.notify("ntfy: no topic", "Put {"topic": "..."} in mods\ntfy-notify\settings.json")
  else
    agentdesk.exec({"curl", "-s", "-d", "AgentDesk test", "https://ntfy.sh/" .. topic})
  end
end)
)";

constexpr const char* kAgentCountMod = R"(-- Sidebar panel: how many agents are working, waiting or done
agentdesk.panel{ id = "summary", title = "Agents now", where = "sidebar", draw = function(ui)
  local n = { Working = 0, Blocked = 0, Idle = 0, Done = 0 }
  for _, s in ipairs(agentdesk.sessions()) do
    if n[s.state] then n[s.state] = n[s.state] + 1 end
  end
  ui:row("Need you", tostring(n.Blocked), n.Blocked > 0 and "bad" or "dim")
  ui:row("Working", tostring(n.Working), n.Working > 0 and "warn" or "dim")
  ui:row("Idle / done", tostring(n.Idle + n.Done), "dim")
end }
)";

void write_example_mods() {
    const auto dir = data_dir() / L"mods";
    std::error_code ec;
    if (std::filesystem::exists(dir, ec))
        return;
    auto put = [&](const wchar_t* name, const char* lua, const char* settings) {
        std::filesystem::create_directories(dir / name, ec);
        std::ofstream(dir / name / L"main.lua", std::ios::binary) << lua;
        if (settings != nullptr)
            std::ofstream(dir / name / L"settings.json", std::ios::binary) << settings;
    };
    put(L"tab-timer", kTabTimerMod, nullptr);
    put(L"ntfy-notify", kNtfyMod, "{\n  \"topic\": \"\"\n}\n");
    put(L"agent-summary", kAgentCountMod, nullptr);
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
    if (starts("bash") || starts("wsl"))
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
    {"pane.close", "Close pane (session keeps running)"},
    {"pane.rename", "Rename pane"},
    {"tab.rename", "Rename tab"},
    {"space.rename", "Rename space"},
    {"tab.close", "Close tab (sessions keep running)"},
    {"space.close", "Close space (sessions keep running)"},
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

const char* command_label(const std::string& id) {
    for (const auto& c : kCommands)
        if (id == c.id)
            return c.label;
    return nullptr;
}

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

struct Remote {
    MachineConfig cfg;
    std::unique_ptr<ui::HostClient> host;
};

struct App::State {
    AppConfig config;
    std::unique_ptr<ui::HostClient> host;           // this PC
    std::vector<Remote> remotes;                    // other machines over ssh
    std::map<std::string, std::string> machine_of;  // session id -> machine name ("" here)
    std::string launch_machine;
    Settings settings;
    ui::Keymap keymap;
    std::vector<std::string> keymap_errors;
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
    [[nodiscard]] std::map<std::string, std::string> session_cwds() const {
        std::map<std::string, std::string> m;
        for (const auto& [id, v] : views_by_id)
            m[id] = v.cwd;
        return m;
    }
    [[nodiscard]] std::string focused_cwd() const {
        const auto id = ws.focused_session();
        const auto it = views_by_id.find(id);
        if (it != views_by_id.end())
            return it->second.cwd;
        return settings.last_cwd;
    }
    void toast(std::string title, std::string body, std::string session, ImU32 color, int seconds = 6) {
        if (!settings.toasts && !session.empty())
            return;
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
}  // namespace

App::App(AppConfig config, std::unique_ptr<ui::HostClient> host) : s_(std::make_unique<State>()) {
    s_->config = std::move(config);
    s_->host = std::move(host);
    s_->settings = load_settings();
    g_pal = palette_by_id(s_->settings.theme);
    s_->keymap = ui::Keymap::defaults();
    {
        std::ifstream in(data_dir() / L"keys.json", std::ios::binary);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            const auto j = Json::parse(ss.str(), nullptr, false);
            if (j.is_discarded())
                s_->keymap_errors.emplace_back("keys.json is not valid JSON");
            else
                s_->keymap_errors = s_->keymap.apply(j);
        }
    }
    std::string cwd = s_->settings.last_cwd;
    if (cwd.empty()) {
        const auto u8 = std::filesystem::current_path().u8string();
        cwd.assign(u8.begin(), u8.end());
    }
    std::snprintf(s_->launch_cwd, sizeof s_->launch_cwd, "%s", cwd.c_str());
    start_remotes(*s_);
    write_example_mods();
    s_->layouts = load_layouts();
    s_->mods = std::make_unique<ui::ModHost>(data_dir() / L"mods");
    s_->mods->load(s_->settings.mods_enabled);
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
    s.remotes = std::move(keep);  // removed machines disconnect here
}
}  // namespace

App::~App() = default;

ui::HostClient& App::host() {
    return *s_->host;
}

void App::load_fonts() {
    s_->fonts = ui::load_fonts(s_->settings.font_size, 14.0f);
    ImGui::GetIO().FontDefault = s_->fonts.ui;
}

void App::apply_scale(float dpi) {
    s_->dpi = dpi;
    g_scale = dpi * s_->settings.zoom;
    apply_style(dpi, s_->settings.zoom);
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
            const bool blocked = v.agent_state == "Blocked";
            const std::string title = s.pane_title(id);
            s.toast(blocked ? title + " needs you" : title + " finished",
                    v.agent_message.empty() ? display_path(v.cwd) : v.agent_message, id,
                    blocked ? g_pal->needs_you : g_pal->done);
            if (s.settings.sounds)
                ::PlaySoundW(blocked ? L"SystemExclamation" : L"SystemAsterisk", nullptr,
                             SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
            if (!s.window_focused) {
                FLASHWINFO fi{sizeof(fi), s.config.hwnd, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
                ::FlashWindowEx(&fi);
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
    } else if (name == "tab.close") {
        if (auto* t = s.ws.active_tab()) {
            for (const auto& id : s.ws.sessions_in(*t))
                s.hidden.insert(id);
            s.ws.close_tab(s.ws.active_space, s.ws.active()->active_tab);
            s.focus_view(s.ws.focused_session());
        }
    } else if (name == "space.close") {
        if (auto* sp = s.ws.active()) {
            for (const auto& t : sp->tabs)
                for (const auto& id : s.ws.sessions_in(t))
                    s.hidden.insert(id);
            s.ws.close_space(s.ws.active_space);
        }
    } else if (name == "tab.move") {
        if (auto* sp = s.ws.active())
            s.ws.move_tab(s.ws.active_space, sp->active_tab, sp->active_tab + (arg == "left" ? -1 : 1));
    } else if (name == "tab.next" || name == "tab.prev") {
        s.ws.cycle_tab(name == "tab.next" ? 1 : -1);
        s.focus_view(s.ws.focused_session());
    } else if (name == "tab.select") {
        s.ws.select_tab(std::atoi(arg.c_str()) - 1);
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
    } else if (name == "app.zoom_in" || name == "app.zoom_out" || name == "app.zoom_reset") {
        s.settings.zoom =
            name == "app.zoom_reset"
                ? 1.0f
                : std::clamp(s.settings.zoom + (name == "app.zoom_in" ? 0.1f : -0.1f), 0.6f, 2.5f);
        save_settings(s.settings);
        g_scale = s.dpi * s.settings.zoom;
        apply_style(s.dpi, s.settings.zoom);
    } else if (name == "app.keys") {
        const auto path = data_dir() / L"keys.json";
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            std::ofstream(path) << "{\n  \"global\": {\n  },\n  \"terminal\": {\n  }\n}\n";
        ::ShellExecuteW(nullptr, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
        s.toast("keys.json opened", "Changes apply the next time AgentDesk starts.", "", g_pal->info);
    } else if (name == "app.exit_all") {
        s.show_exit = true;
        s.exit_error.clear();
    } else if (name == "theme") {
        g_pal = palette_by_id(arg);
        s.settings.theme = g_pal->id;
        save_settings(s.settings);
        apply_style(s.dpi, s.settings.zoom);
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
        if (const auto& l = s.layout(); !l.theme.empty()) {
            g_pal = palette_by_id(l.theme);
            apply_style(s.dpi, s.settings.zoom);
        }
        save_settings(s.settings);
        s.toast("Layout: " + next, "", "", g_pal->info, 2);
        s.focus_view(s.ws.focused_session());
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

void draw_mod_panels(App::State& s, const std::string& where) {
    for (const auto& panel : s.mods->panels(where)) {
        ImGui::PushID((panel.mod + "/" + panel.id).c_str());
        if (!panel.title.empty())
            ImGui::TextDisabled("%s", panel.title.c_str());
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
                    ImGui::ProgressBar(w.value, ImVec2(-1, px(6)), "");
                    ImGui::PopStyleColor();
                    if (!w.text.empty())
                        ImGui::TextDisabled("%s", w.text.c_str());
                    break;
                case ui::ModWidget::Kind::kRow: {
                    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
                    ImGui::TextUnformatted(w.text.c_str());
                    ImGui::PopStyleColor();
                    const float rw = ImGui::CalcTextSize(w.text2.c_str()).x;
                    ImGui::SameLine(ImGui::GetContentRegionMax().x - rw - px(4));
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
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, px(4)));
    }
}

void row_bg(ImDrawList* dl, ImVec2 a, ImVec2 b, bool selected, bool hovered) {
    if (selected) {
        dl->AddRectFilled(a, b, g_pal->selected, px(8));
        dl->AddRectFilled(ImVec2(a.x, a.y + px(6)), ImVec2(a.x + px(3), b.y - px(6)), g_pal->focus, px(2));
    } else if (hovered) {
        dl->AddRectFilled(a, b, g_pal->hover, px(8));
    }
}

void section_label(ImDrawList* dl, ImFont* bold, float fs, ImVec2 p, const char* text) {
    // Small spaced capitals: our own section style.
    std::string spaced;
    for (const char* c = text; *c != 0; ++c) {
        spaced += static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
        if (c[1] != 0)
            spaced += ' ';
    }
    dl->AddText(bold, fs * 0.72f, p, g_pal->faint, spaced.c_str());
}

void draw_sidebar(App::State& s, ImVec2 pos, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->sidebar);
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(14)));
    ImGui::BeginChild("##sidebar", size, ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), px(2)));
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImFont* uif = s.fonts.ui;
    ImFont* bold = s.fonts.ui_bold;
    const float fs = ImGui::GetFontSize();
    const float row_h = px(34);
    const float w = ImGui::GetContentRegionAvail().x;

    // Brand row.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float m = px(18);
        d->AddRectFilled(ImVec2(p.x, p.y + px(3)), ImVec2(p.x + m, p.y + px(3) + m), g_pal->focus, px(5));
        d->AddRectFilled(ImVec2(p.x + px(5), p.y + px(8)), ImVec2(p.x + px(13), p.y + px(11)), g_pal->card,
                         px(1));
        d->AddRectFilled(ImVec2(p.x + px(5), p.y + px(13)), ImVec2(p.x + px(10), p.y + px(16)), g_pal->card,
                         px(1));
        d->AddText(bold, fs * 1.08f, ImVec2(p.x + px(26), p.y + px(2)), g_pal->text, "AgentDesk");
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - px(28), p.y));
        ImGui::PushStyleColor(ImGuiCol_Button, v4(alpha(g_pal->focus, 40)));
        ImGui::BeginDisabled(!s.host->connected());
        if (ImGui::Button("+##brandnew", ImVec2(px(28), px(26))))
            run_command(s, "tab.new");
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("New terminal  %s", s.keymap.chord_for("tab.new").c_str());
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + px(42)));
    }

    // Spaces.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        section_label(d, bold, fs, p, "Spaces");
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - px(22), p.y - px(4)));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::SmallButton("+##spacenew"))
            run_command(s, "space.new");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("New space (opens a terminal)  %s", s.keymap.chord_for("space.new").c_str());
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + px(22)));
    }
    const auto cwds = s.session_cwds();
    if (s.ws.spaces.empty())
        ImGui::TextDisabled("No spaces yet");
    std::vector<std::string> machines_shown = {""};
    for (const auto& r : s.remotes)
        machines_shown.push_back(r.cfg.name);
    for (const auto& sp : s.ws.spaces)
        if (std::find(machines_shown.begin(), machines_shown.end(), sp.machine) == machines_shown.end())
            machines_shown.push_back(sp.machine);
    std::vector<int> order;
    for (const auto& m : machines_shown) {
        if (!s.remotes.empty()) {
            order.push_back(-1 - static_cast<int>(&m - machines_shown.data()));  // machine header
        }
        for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i)
            if (s.ws.spaces[static_cast<std::size_t>(i)].machine == m)
                order.push_back(i);
    }
    for (const int i : order) {
        if (i < 0) {
            const std::string& m = machines_shown[static_cast<std::size_t>(-1 - i)];
            const ImVec2 hp = ImGui::GetCursorScreenPos();
            const bool on = s.machine_connected(m);
            d->AddCircleFilled(ImVec2(hp.x + px(5), hp.y + px(10)), px(3.5f),
                               on ? g_pal->done : g_pal->faint);
            d->AddText(bold, fs * 0.85f, ImVec2(hp.x + px(14), hp.y + px(3)), on ? g_pal->dim : g_pal->faint,
                       s.machine_label(m).c_str());
            ImGui::Dummy(ImVec2(w, px(22)));
            if (ImGui::IsItemHovered()) {
                std::string why;
                for (const auto& r : s.remotes)
                    if (r.cfg.name == m && !r.host->connected())
                        why = r.host->last_error();
                if (!why.empty())
                    ImGui::SetTooltip("%s: not connected\n%s", m.c_str(), why.c_str());
            }
            continue;
        }
        const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
        ImGui::PushID(sp.id.c_str());
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("##space", ImVec2(w, row_h));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 b(a.x + w, a.y + row_h);
        const bool active = i == s.ws.active_space;
        if (clicked) {
            s.ws.select_space(i);
            s.focus_view(s.ws.focused_session());
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            open_rename(s, "space", sp.id, sp.name);
        if (ImGui::BeginPopupContextItem("##spacectx")) {
            if (ImGui::MenuItem("Rename..."))
                open_rename(s, "space", sp.id, sp.name);
            if (ImGui::MenuItem("New tab here")) {
                s.ws.select_space(i);
                run_command(s, "tab.new");
            }
            if (ImGui::MenuItem("New worktree...")) {
                s.ws.select_space(i);
                run_command(s, "worktree.new");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close space (sessions keep running)")) {
                s.ws.select_space(i);
                run_command(s, "space.close");
            }
            ImGui::EndPopup();
        }
        row_bg(d, a, b, active, hovered);
        const std::string title = s.ws.space_title(i, cwds);
        const float side = px(20);
        space_badge(d, bold, fs, ImVec2(a.x + px(10), a.y + (row_h - side) * 0.5f), side, title, active);
        // Attention of the space: most urgent agent in it.
        std::vector<Attention> list;
        std::string first_cwd;
        for (const auto& t : sp.tabs)
            for (const auto& id : s.ws.sessions_in(t)) {
                list.push_back(s.attention_of(id));
                if (first_cwd.empty() && cwds.count(id))
                    first_cwd = cwds.at(id);
            }
        const Attention att = ui::most_urgent(list);
        float right = b.x - px(10);
        if (att != Attention::kNone && att != Attention::kIdle) {
            attention_mark(d, ImVec2(right - px(5), a.y + row_h * 0.5f), px(5), att);
            right -= px(16);
        }
        const std::string branch = first_cwd.empty() ? std::string() : ui::git_branch(first_cwd);
        float bw = 0;
        if (!branch.empty()) {
            const std::string bt = std::string("\xE2\x8E\x87 ") + branch;  // branch sign
            bw = std::min(ImGui::CalcTextSize(bt.c_str()).x, w * 0.4f);
            text_clipped(d, uif, fs * 0.9f, ImVec2(right - bw, a.y + (row_h - fs * 0.9f) * 0.5f),
                         g_pal->faint, bt, bw);
            right -= bw + px(6);
        }
        text_clipped(d, active ? bold : uif, fs, ImVec2(a.x + px(38), a.y + (row_h - fs) * 0.5f),
                     active ? g_pal->text : g_pal->dim, title, right - (a.x + px(38)));
        ImGui::PopID();
    }

    // Mod panels for the sidebar, below the spaces.
    ImGui::Dummy(ImVec2(0, px(6)));
    draw_mod_panels(s, "sidebar");

    // Agents: grouped by attention, most urgent first; pinned to the bottom part.
    struct Group {
        const char* label;
        Attention att;
        std::vector<std::string> ids;
    };
    std::vector<Group> groups = {{"Needs you", Attention::kBlocked, {}},
                                 {"Done", Attention::kDone, {}},
                                 {"Working", Attention::kWorking, {}},
                                 {"Idle", Attention::kIdle, {}},
                                 {"Shells & other", Attention::kNone, {}}};
    std::vector<std::string> stopped;
    std::map<std::string, std::vector<std::string>> children;
    for (const auto& sess : s.sessions) {
        const auto& v = s.views_by_id[sess->id()];
        if (!v.parent.empty() && s.views_by_id.count(v.parent))
            children[v.parent].push_back(sess->id());
    }
    for (const auto& sess : s.sessions) {
        const auto& v = s.views_by_id[sess->id()];
        if (!v.parent.empty() && s.views_by_id.count(v.parent))
            continue;  // drawn under its parent
        if (v.lost || v.process_state != "Running") {
            stopped.push_back(sess->id());
            continue;
        }
        const Attention a = s.attention_of(sess->id());
        for (auto& g : groups)
            if (g.att == a) {
                g.ids.push_back(sess->id());
                break;
            }
    }
    s.agent_order.clear();
    for (const auto& g : groups)
        for (const auto& id : g.ids) {
            s.agent_order.push_back(id);
            for (const auto& c : children[id])
                s.agent_order.push_back(c);
        }

    int rows = 0;
    int headers = 0;
    for (const auto& [p, list] : children)
        rows += static_cast<int>(list.size());
    for (const auto& g : groups)
        if (!g.ids.empty()) {
            rows += static_cast<int>(g.ids.size());
            ++headers;
        }
    if (!stopped.empty()) {
        rows += static_cast<int>(stopped.size());
        ++headers;
    }
    const float footer_h = px(70);
    const float list_h =
        px(26) + static_cast<float>(headers) * px(24) + static_cast<float>(rows) * (row_h + px(2));
    const float bottom = pos.y + size.y - px(14);
    const float top = std::max(ImGui::GetCursorScreenPos().y + px(18), bottom - footer_h - list_h);
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, top));
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        d->AddLine(ImVec2(pos.x + px(12), p.y - px(8)), ImVec2(pos.x + size.x - px(12), p.y - px(8)),
                   g_pal->border);
        section_label(d, bold, fs, p, "Agents");
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + px(22)));
    }
    const float list_bottom = bottom - footer_h;
    ImGui::BeginChild("##agents", ImVec2(w, std::max(px(40), list_bottom - ImGui::GetCursorScreenPos().y)),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* ad = ImGui::GetWindowDrawList();
    auto agent_row = [&](const std::string& id, float indent) {
        const auto& v = s.views_by_id[id];
        const bool is_child = indent > 0;
        ImGui::PushID(id.c_str());
        const ImVec2 a0 = ImGui::GetCursorScreenPos();
        const float aw = ImGui::GetContentRegionAvail().x;
        const bool clicked = ImGui::InvisibleButton("##agent", ImVec2(aw, row_h));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 a(a0.x + indent, a0.y);
        const ImVec2 b(a0.x + aw, a0.y + row_h);
        if (is_child) {  // tree line from the parent
            ad->AddLine(ImVec2(a0.x + px(20), a0.y - px(4)), ImVec2(a0.x + px(20), a0.y + row_h * 0.5f),
                        g_pal->border, px(1.2f));
            ad->AddLine(ImVec2(a0.x + px(20), a0.y + row_h * 0.5f), ImVec2(a.x + px(6), a0.y + row_h * 0.5f),
                        g_pal->border, px(1.2f));
        }
        if (clicked) {
            if (is_child && s.ws.find(id).space < 0)
                run_command(s, "agent.peek:" + id);  // sub-agents without a pane: peek
            else
                s.focus(id);
        }
        if (hovered)
            ImGui::SetTooltip("%s\n%s%s%s", s.pane_title(id).c_str(), display_path(v.cwd).c_str(),
                              v.agent_message.empty() ? "" : "\n", v.agent_message.c_str());
        if (ImGui::BeginPopupContextItem("##agentctx")) {
            if (ImGui::MenuItem("Show"))
                s.focus(id);
            if (ImGui::MenuItem("Peek"))
                run_command(s, "agent.peek:" + id);
            if (s.ws.find(id).space < 0 && ImGui::MenuItem("Open as pane"))
                run_command(s, "agent.keep:" + id);
            if (ImGui::MenuItem("Send Ctrl+C", nullptr, false, v.process_state == "Running"))
                if (auto it = s.by_id.find(id); it != s.by_id.end())
                    it->second->send(host::RawInput{std::string("\x03", 1)});
            if (ImGui::MenuItem("Stop", nullptr, false, v.process_state == "Running"))
                if (auto it = s.by_id.find(id); it != s.by_id.end())
                    it->second->stop(false);
            ImGui::EndPopup();
        }
        row_bg(ad, a, b, id == s.ws.focused_session(), hovered);
        const auto pi = program_info(v.program, v.profile_id);
        const float side = px(20);
        program_badge(ad, bold, fs, ImVec2(a.x + px(10), a.y + (row_h - side) * 0.5f), side, pi);
        const float cy = a.y + row_h * 0.5f;
        const Attention att = s.attention_of(id);
        attention_mark(ad, ImVec2(b.x - px(12), cy), px(5), att);
        const std::string age = elapsed_since(v.created_at);
        const float agw = ImGui::CalcTextSize(age.c_str()).x;
        ad->AddText(uif, fs * 0.9f, ImVec2(b.x - px(26) - agw, cy - fs * 0.45f), g_pal->faint, age.c_str());
        const float tx = a.x + px(38);
        const float max_w = b.x - px(32) - agw - tx;
        const float nw = text_clipped(ad, bold, fs, ImVec2(tx, cy - fs * 0.5f),
                                      v.lost || v.process_state != "Running" ? g_pal->dim : g_pal->text,
                                      s.pane_title(id), max_w);
        std::string sub = folder_name(v.cwd);
        if (const auto deco = s.mods->decorate("agent", {{"id", id},
                                                         {"title", s.pane_title(id)},
                                                         {"state", v.agent_state},
                                                         {"program", v.program},
                                                         {"cwd", v.cwd}});
            !deco.empty())
            sub = deco + "  " + sub;
        text_clipped(ad, uif, fs * 0.9f, ImVec2(tx + nw + px(6), cy - fs * 0.45f), g_pal->faint, sub,
                     max_w - nw - px(6));
        ImGui::PopID();
    };
    for (const auto& g : groups) {
        if (g.ids.empty())
            continue;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ad->AddCircleFilled(ImVec2(p.x + px(4), p.y + fs * 0.5f + px(2)), px(3), attention_color(g.att));
        char head[64];
        std::snprintf(head, sizeof head, "%s  %d", g.label, static_cast<int>(g.ids.size()));
        ad->AddText(bold, fs * 0.85f, ImVec2(p.x + px(14), p.y + px(2)), attention_color(g.att), head);
        ImGui::Dummy(ImVec2(0, px(22)));
        for (const auto& id : g.ids) {
            agent_row(id, 0);
            for (const auto& c : children[id])
                agent_row(c, px(22));
        }
    }
    if (!stopped.empty()) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        char head[64];
        std::snprintf(head, sizeof head, "Stopped  %d", static_cast<int>(stopped.size()));
        ad->AddText(bold, fs * 0.85f, ImVec2(p.x + px(14), p.y + px(2)), g_pal->faint, head);
        ImGui::Dummy(ImVec2(0, px(22)));
        for (const auto& id : stopped) {
            agent_row(id, 0);
            for (const auto& c : children[id])
                agent_row(c, px(22));
        }
    }
    if (s.sessions.empty())
        ImGui::TextDisabled("Nothing running");
    ImGui::EndChild();

    // Footer.
    ImGui::SetCursorScreenPos(ImVec2(pos.x + px(12), bottom - footer_h + px(8)));
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        d->AddLine(ImVec2(pos.x + px(12), p.y - px(4)), ImVec2(pos.x + size.x - px(12), p.y - px(4)),
                   g_pal->border);
    }
    auto footer_item = [&](const char* id, const char* text, const char* cmd) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton(id, ImVec2(w, px(28))))
            run_command(s, cmd);
        if (ImGui::IsItemHovered())
            d->AddRectFilled(a, ImVec2(a.x + w, a.y + px(28)), g_pal->hover, px(7));
        d->AddText(uif, fs, ImVec2(a.x + px(10), a.y + (px(28) - fs) * 0.5f), g_pal->dim, text);
    };
    footer_item("##f-settings", "Settings", "app.settings");
    footer_item("##f-exit", "Exit and stop all...", "app.exit_all");
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

void draw_tab_bar(App::State& s, ImVec2 pos, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fs = ImGui::GetFontSize();
    dl->AddLine(ImVec2(pos.x, pos.y + height - 1), ImVec2(pos.x + width, pos.y + height - 1), g_pal->border);
    float x = pos.x + px(12);
    auto* sp = s.ws.active();
    if (sp != nullptr) {
        // Breadcrumb: the space this tab row belongs to.
        const std::string title = s.ws.space_title(s.ws.active_space, s.session_cwds());
        const float tw = std::min(s.fonts.ui_bold->CalcTextSizeA(fs, FLT_MAX, 0, title.c_str()).x, px(180));
        text_clipped(dl, s.fonts.ui_bold, fs, ImVec2(x, pos.y + (height - fs) * 0.5f), g_pal->text, title,
                     tw);
        x += tw + px(8);
        dl->AddText(s.fonts.ui, fs, ImVec2(x, pos.y + (height - fs) * 0.5f), g_pal->faint, "/");
        x += px(14);
        int close_tab = -1;
        for (int i = 0; i < static_cast<int>(sp->tabs.size()); ++i) {
            auto& tab = sp->tabs[static_cast<std::size_t>(i)];
            const bool active = i == sp->active_tab;
            std::string label = ui::Workspace::tab_title(tab, i);
            const auto ids = s.ws.sessions_in(tab);
            if (tab.name.empty() && !ids.empty())
                label += "  " + s.pane_title(tab.focused.empty() ? ids.front() : tab.focused);
            if (ids.size() > 1)
                label += "  +" + std::to_string(ids.size() - 1);
            if (const auto deco = s.mods->decorate("tab", {{"title", label}, {"sessions", ids}});
                !deco.empty())
                label += "  \xC2\xB7 " + deco;
            ImFont* f = active ? s.fonts.ui_bold : s.fonts.ui;
            const float lw = std::min(f->CalcTextSizeA(fs, FLT_MAX, 0, label.c_str()).x, px(220));
            const float w = lw + px(56);
            const ImVec2 a(x, pos.y + px(8));
            const ImVec2 b(x + w, pos.y + height - px(8));
            if (b.x > pos.x + width - px(150))
                break;
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(tab.id.c_str());
            if (ImGui::InvisibleButton("##tab", ImVec2(w - px(22), b.y - a.y))) {
                s.ws.select_tab(i);
                s.focus_view(s.ws.focused_session());
            }
            const bool hovered = ImGui::IsItemHovered();
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                open_rename(s, "tab", tab.id, tab.name);
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
                close_tab = i;
            if (ImGui::BeginPopupContextItem("##tabctx")) {
                if (ImGui::MenuItem("Rename...")) {
                    s.ws.select_tab(i);
                    open_rename(s, "tab", tab.id, tab.name);
                }
                if (ImGui::MenuItem("Move left"))
                    s.ws.move_tab(s.ws.active_space, i, i - 1);
                if (ImGui::MenuItem("Move right"))
                    s.ws.move_tab(s.ws.active_space, i, i + 1);
                ImGui::Separator();
                if (ImGui::MenuItem("Close tab (sessions keep running)"))
                    close_tab = i;
                ImGui::EndPopup();
            }
            if (active) {
                dl->AddRectFilled(a, b, g_pal->card, px(8));
                dl->AddRect(a, b, g_pal->border, px(8));
                dl->AddLine(ImVec2(a.x + px(10), b.y - px(1)), ImVec2(b.x - px(10), b.y - px(1)),
                            g_pal->focus, px(2));
            } else if (hovered) {
                dl->AddRectFilled(a, b, g_pal->hover, px(8));
            }
            std::vector<Attention> atts;
            for (const auto& id : ids)
                atts.push_back(s.attention_of(id));
            const float cy = (a.y + b.y) * 0.5f;
            attention_mark(dl, ImVec2(a.x + px(14), cy), px(4), ui::most_urgent(atts));
            text_clipped(dl, f, fs, ImVec2(a.x + px(26), cy - fs * 0.5f), active ? g_pal->text : g_pal->dim,
                         label, lw);
            ImGui::SetCursorScreenPos(ImVec2(b.x - px(22), a.y));
            if (ImGui::InvisibleButton("##x", ImVec2(px(18), b.y - a.y)))
                close_tab = i;
            const ImU32 xc = ImGui::IsItemHovered() ? g_pal->text : g_pal->faint;
            const ImVec2 xm(b.x - px(13), cy);
            dl->AddLine(ImVec2(xm.x - px(3.5f), xm.y - px(3.5f)), ImVec2(xm.x + px(3.5f), xm.y + px(3.5f)),
                        xc, px(1.4f));
            dl->AddLine(ImVec2(xm.x + px(3.5f), xm.y - px(3.5f)), ImVec2(xm.x - px(3.5f), xm.y + px(3.5f)),
                        xc, px(1.4f));
            ImGui::PopID();
            x = b.x + px(4);
        }
        if (close_tab >= 0) {
            s.ws.select_tab(close_tab);
            run_command(s, "tab.close");
        }
    }
    // Right side.
    float rx = pos.x + width - px(10);
    auto icon_button = [&](const char* id, const char* text, const char* cmd, const char* tip,
                           bool on = false) {
        const float bw = ImGui::CalcTextSize(text).x + px(20);
        rx -= bw;
        ImGui::SetCursorScreenPos(ImVec2(rx, pos.y + px(8)));
        ImGui::PushStyleColor(ImGuiCol_Button, on ? v4(g_pal->selected) : ImVec4(0, 0, 0, 0));
        char full[64];
        std::snprintf(full, sizeof full, "%s##%s", text, id);
        if (ImGui::Button(full, ImVec2(bw, height - px(16))))
            run_command(s, cmd);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s  %s", tip, s.keymap.chord_for(cmd).c_str());
        rx -= px(4);
    };
    icon_button("details", "Details", "details.toggle", "Show / hide details", s.settings.details);
    icon_button("palette", "Commands", "app.palette", "Command palette");
    icon_button("splitd", "\xE2\x96\xA4", "pane.split:down", "Split down");
    icon_button("splitr", "\xE2\x96\xA5", "pane.split:right", "Split right");
    icon_button("agent", "Run...", "agent.new", "Start a program (Claude Code, Codex, ...)");
    icon_button("new", "+", "tab.new", "New terminal");
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
    dl->AddRectFilled(pos, max, g_pal->card, px(12));
    ImU32 border = focused ? g_pal->focus : g_pal->border;
    if (att == Attention::kBlocked)
        border = g_pal->needs_you;
    dl->AddRect(pos, max, border, px(12), 0, focused || att == Attention::kBlocked ? px(1.6f) : px(1));

    const float fs = ImGui::GetFontSize();
    const float header_h = px(38);
    const float cy = pos.y + header_h * 0.5f;
    const auto pi = program_info(v.program, v.profile_id);
    program_badge(dl, s.fonts.ui_bold, fs, ImVec2(pos.x + px(12), cy - px(10)), px(20), pi);
    const std::string title = s.pane_title(id);
    const float tw = text_clipped(dl, s.fonts.ui_bold, fs, ImVec2(pos.x + px(40), cy - fs * 0.5f),
                                  g_pal->text, title, std::max(r.w * 0.35f, r.w - px(120)) - px(40));

    ImGui::PushID(id.c_str());
    float rx = max.x - px(10);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    auto hbtn = [&](const char* label, const char* cmd, const char* tip) {
        const float bw = ImGui::CalcTextSize(label, nullptr, true).x + px(14);
        rx -= bw;
        ImGui::SetCursorScreenPos(ImVec2(rx, cy - px(12)));
        if (ImGui::Button(label, ImVec2(bw, px(24)))) {
            s.ws.focus(id);
            run_command(s, cmd);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s  %s", tip, s.keymap.chord_for(cmd).c_str());
        rx -= px(2);
    };
    // Narrow panes keep the most important controls: close, then zoom, then the split buttons.
    hbtn("\xC3\x97##close", "pane.close", "Close pane (the session keeps running)");
    if (r.w > px(300))
        hbtn("\xE2\xA4\xA2##zoom", "pane.zoom", "Zoom pane");
    if (r.w > px(440)) {
        hbtn("\xE2\x96\xA4##sd", "pane.split:down", "Split down");
        hbtn("\xE2\x96\xA5##sr", "pane.split:right", "Split right");
    }
    ImGui::PopStyleColor();

    // State chip.
    std::string state =
        v.lost ? std::string("Lost")
               : (v.process_state == "Running" ? std::string(ui::attention_label(att)) : v.process_state);
    if (v.process_state == "Running" && att == Attention::kNone)
        state = is_shell(v.program) ? "shell" : "running";
    const float sw = ImGui::CalcTextSize(state.c_str()).x + px(26);
    if (rx - sw - px(6) > pos.x + px(48) + tw) {
        rx -= sw + px(6);
        dl->AddRectFilled(ImVec2(rx, cy - px(11)), ImVec2(rx + sw, cy + px(11)),
                          alpha(attention_color(att), 38), px(11));
        attention_mark(dl, ImVec2(rx + px(11), cy), px(4), att);
        dl->AddText(s.fonts.ui, fs * 0.92f, ImVec2(rx + px(20), cy - fs * 0.46f),
                    att == Attention::kNone ? g_pal->dim : attention_color(att), state.c_str());
    }
    // Folder and branch.
    std::string where = display_path(v.cwd);
    const std::string branch = ui::git_branch(v.cwd);
    if (!branch.empty())
        where += "  \xE2\x8E\x87 " + branch;
    text_clipped(dl, s.fonts.ui, fs * 0.92f, ImVec2(pos.x + px(48) + tw, cy - fs * 0.46f), g_pal->faint,
                 where, rx - (pos.x + px(56) + tw));

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

    const ImVec2 body_min(pos.x + px(12), top);
    const ImVec2 body_size(r.w - px(24), max.y - top - px(10));
    if (body_size.x > px(20) && body_size.y > px(10)) {
        ImGui::SetCursorScreenPos(body_min);
        ImGui::BeginChild(
            "##body", body_size, ImGuiChildFlags_None,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
        vs.view.keymap = &s.keymap;
        vs.view.copy_on_select = s.settings.copy_on_select;
        vs.view.scroll_speed = s.settings.scroll_speed;
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
    for (const auto& [id, r] : ui::layout_tree(tab->root, area, gap))
        draw_pane(s, id, r, connected);
    // Draggable borders between panes.
    int n = 0;
    for (const auto& d : ui::dividers(tab->root, area, gap)) {
        ImGui::PushID(n++);
        ImGui::SetCursorScreenPos(ImVec2(d.bar.x, d.bar.y));
        ImGui::InvisibleButton("##div", ImVec2(std::max(d.bar.w, 2.0f), std::max(d.bar.h, 2.0f)));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(d.vertical ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) {
            if (ui::Node* node = ui::node_at(tab->root, d.path)) {
                const ImVec2 m = ImGui::GetIO().MousePos;
                const float f = d.vertical ? (m.y - d.parent.y) / std::max(1.0f, d.parent.h)
                                           : (m.x - d.parent.x) / std::max(1.0f, d.parent.w);
                node->ratio = std::clamp(f, 0.1f, 0.9f);
            }
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

void draw_status_bar(App::State& s, ImVec2 pos, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), g_pal->sidebar);
    dl->AddLine(pos, ImVec2(pos.x + width, pos.y), g_pal->border);
    const float fs = ImGui::GetFontSize() * 0.92f;
    const float ty = pos.y + (height - fs) * 0.5f;
    float x = pos.x + px(14);
    auto item = [&](const std::string& text, ImU32 color) {
        dl->AddText(s.fonts.ui, fs, ImVec2(x, ty), color, text.c_str());
        x += s.fonts.ui->CalcTextSizeA(fs, FLT_MAX, 0, text.c_str()).x + px(16);
    };
    const bool connected = s.host->connected();
    dl->AddCircleFilled(ImVec2(x + px(4), pos.y + height * 0.5f), px(3.5f),
                        connected ? g_pal->done : g_pal->needs_you);
    x += px(14);
    item(connected ? "This PC" : "agent-host disconnected: " + s.host->last_error(),
         connected ? g_pal->dim : g_pal->needs_you);
    if (!connected) {
        ImGui::SetCursorScreenPos(ImVec2(x, pos.y + px(3)));
        if (ImGui::SmallButton("Start host"))
            s.host->request_start_host();
        x += px(90);
    }
    if (s.host->host_dies_with_launcher())
        item("host ends when the launching terminal closes", g_pal->working);
    for (const auto& r : s.remotes) {
        dl->AddCircleFilled(ImVec2(x + px(4), pos.y + height * 0.5f), px(3.5f),
                            r.host->connected() ? g_pal->done : g_pal->faint);
        x += px(14);
        item(r.cfg.name, r.host->connected() ? g_pal->dim : g_pal->faint);
    }
    int working = 0;
    int needs = 0;
    for (const auto& [id, v] : s.views_by_id) {
        const Attention a = s.attention_of(id);
        working += a == Attention::kWorking ? 1 : 0;
        needs += a == Attention::kBlocked ? 1 : 0;
    }
    item(std::to_string(s.sessions.size()) + (s.sessions.size() == 1 ? " session" : " sessions"), g_pal->dim);
    if (working > 0)
        item(std::to_string(working) + " working", g_pal->working);
    if (needs > 0)
        item(std::to_string(needs) + " need you", g_pal->needs_you);
    const auto id = s.ws.focused_session();
    if (const auto it = s.by_id.find(id); it != s.by_id.end()) {
        const auto& v = s.views_by_id[id];
        const auto snap = it->second->snapshot();
        item(std::to_string(snap->cols) + "\xC3\x97" + std::to_string(snap->rows), g_pal->faint);
        if (!v.is_controller)
            item("read-only", g_pal->info);
        if (v.history_truncated)
            item("history truncated", g_pal->working);
        if (v.backpressure_waits > 0)
            item("output throttled", g_pal->working);
    }
    const std::string keys = s.keymap.chord_for("app.palette") + "  commands     " +
                             s.keymap.chord_for("app.help") + "  shortcuts";
    const float kw = s.fonts.ui->CalcTextSizeA(fs, FLT_MAX, 0, keys.c_str()).x;
    if (pos.x + width - kw - px(14) > x)
        dl->AddText(s.fonts.ui, fs, ImVec2(pos.x + width - kw - px(14), ty), g_pal->faint, keys.c_str());
}

void draw_toasts(App::State& s) {
    const auto now = Clock::now();
    while (!s.toasts.empty() && s.toasts.front().until < now)
        s.toasts.pop_front();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->WorkPos.y + vp->WorkSize.y - px(44);
    const float w = px(340);
    int n = 0;
    for (auto it = s.toasts.rbegin(); it != s.toasts.rend(); ++it, ++n) {
        const auto& t = *it;
        const float fs = ImGui::GetFontSize();
        const float body_h =
            t.body.empty() ? 0 : ImGui::CalcTextSize(t.body.c_str(), nullptr, false, w - px(40)).y;
        const float h = px(22) + fs + (t.body.empty() ? 0 : body_h + px(6));
        y -= h;
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - w - px(18), y));
        ImGui::SetNextWindowSize(ImVec2(w, h));
        char name[32];
        std::snprintf(name, sizeof name, "##toast%d", n);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(10)));
        ImGui::Begin(name, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoMove);
        ImGui::PopStyleVar();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        dl->AddRectFilled(ImVec2(wp.x, wp.y + px(10)), ImVec2(wp.x + px(4), wp.y + h - px(10)), t.color,
                          px(2));
        ImGui::PushFont(s.fonts.ui_bold, 0.0f);
        ImGui::TextUnformatted(t.title.c_str());
        ImGui::PopFont();
        if (!t.body.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
            ImGui::PushTextWrapPos(w - px(26));
            ImGui::TextUnformatted(t.body.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (!t.session.empty())
                s.focus(t.session);
            it->until = now;  // dismiss
        }
        ImGui::End();
        y -= px(8);
    }
}

// Floating view of a sub-agent over the panes, with "open as pane" and close.
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
    const ui::Rect a = s.pane_area;
    const float w = std::max(px(420), a.w * 0.72f);
    const float h = std::max(px(300), a.h * 0.72f);
    ImGui::SetNextWindowPos(ImVec2(a.x + (a.w - w) * 0.5f, a.y + (a.h - h) * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(12)));
    ImGui::Begin("##peek", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float fs = ImGui::GetFontSize();
    program_badge(dl, s.fonts.ui_bold, fs, p, px(20), program_info(v.program, v.profile_id));
    std::string head = s.pane_title(s.peek);
    if (const auto pit = s.views_by_id.find(v.parent); pit != s.views_by_id.end())
        head += "   \xE2\x80\xB9 sub-agent of " + s.pane_title(v.parent);
    dl->AddText(s.fonts.ui_bold, fs, ImVec2(p.x + px(28), p.y + px(2)), g_pal->text, head.c_str());
    ImGui::SetCursorScreenPos(
        ImVec2(p.x + w - px(28) - ImGui::CalcTextSize("Open as pane").x - px(70), p.y - px(2)));
    if (ImGui::Button("Open as pane"))
        run_command(s, "agent.keep:" + s.peek);
    ImGui::SameLine();
    const bool close = ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + px(32)));
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
        for (const auto& cmd : vs.view.take_commands())
            run_command(s, cmd);
        ImGui::EndChild();
    }
    ImGui::End();
    if (close)
        s.peek.clear();
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
    ImGui::PushFont(s.fonts.ui_bold, 16.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void primary_button_style(bool push) {
    if (push) {
        ImGui::PushStyleColor(ImGuiCol_Button, v4(g_pal->focus));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(alpha(g_pal->focus, 220)));
        ImGui::PushStyleColor(ImGuiCol_Text, v4(IM_COL32(255, 255, 255, 255)));
    } else {
        ImGui::PopStyleColor(3);
    }
}

void draw_palette(App::State& s) {
    if (!s.show_palette)
        return;
    struct Entry {
        std::string label;
        std::string cmd;
        std::string hint;
    };
    std::vector<Entry> entries;
    for (const auto& c : kCommands)
        entries.push_back({c.label, c.id, s.keymap.chord_for(c.id)});
    for (const auto& p : kPalettes)
        entries.push_back({std::string("Theme: ") + p.label, std::string("theme:") + p.id, ""});
    for (const auto& l : s.layouts)
        entries.push_back(
            {"Layout: " + l.name, "layout:" + l.name, l.name == s.settings.layout ? "current" : ""});
    for (const auto& c : s.mods->commands())
        entries.push_back({c.mod + ": " + c.label, "mod:" + c.mod + ":" + c.id, ""});
    for (const auto& sess : s.sessions) {
        const auto& v = s.views_by_id[sess->id()];
        const bool child_without_pane = !v.parent.empty() && s.ws.find(sess->id()).space < 0;
        entries.push_back({(child_without_pane ? "Peek " : "Go to ") + s.pane_title(sess->id()) +
                               "  \xC2\xB7 " + folder_name(v.cwd),
                           (child_without_pane ? "agent.peek:" : "agent.focus:") + sess->id(),
                           v.agent_state});
    }
    for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i)
        entries.push_back(
            {"Space: " + s.ws.space_title(i, s.session_cwds()), "space.select:" + std::to_string(i + 1), ""});
    std::vector<const Entry*> shown;
    for (const auto& e : entries)
        if (fuzzy_match(e.label, s.palette_buf))
            shown.push_back(&e);
    s.palette_sel = std::clamp(s.palette_sel, 0, std::max(0, static_cast<int>(shown.size()) - 1));

    center_next(px(560), 0.12f);
    ImGui::Begin("##palette", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_AlwaysAutoResize);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##q", "Type a command, agent or space", s.palette_buf,
                                 sizeof s.palette_buf))
        s.palette_sel = 0;
    const bool typing = ImGui::IsItemActive();
    if (typing && ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        ++s.palette_sel;
    if (typing && ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        --s.palette_sel;
    s.palette_sel = std::clamp(s.palette_sel, 0, std::max(0, static_cast<int>(shown.size()) - 1));
    std::string run;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), px(1)));
    ImGui::BeginChild("##list",
                      ImVec2(0, std::min(px(380), static_cast<float>(shown.size()) * px(29) + px(4))));
    for (int i = 0; i < static_cast<int>(shown.size()); ++i) {
        const auto* e = shown[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        const bool sel = i == s.palette_sel;
        if (ImGui::Selectable("##e", sel, 0, ImVec2(0, px(26))))
            run = e->cmd;
        if (sel && typing)
            ImGui::SetScrollHereY();
        ImGui::SameLine(px(10));
        ImGui::TextUnformatted(e->label.c_str());
        if (!e->hint.empty()) {
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(e->hint.c_str()).x - px(8));
            ImGui::TextDisabled("%s", e->hint.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    if (typing && ImGui::IsKeyPressed(ImGuiKey_Enter) && !shown.empty())
        run = shown[static_cast<std::size_t>(s.palette_sel)]->cmd;
    const bool close =
        ImGui::IsKeyPressed(ImGuiKey_Escape) || (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                                                 ImGui::IsMouseClicked(ImGuiMouseButton_Left));
    ImGui::End();
    if (!run.empty() || close) {
        s.show_palette = false;
        s.focus_view(s.ws.focused_session());
    }
    if (!run.empty())
        run_command(s, run);
}

void draw_help(App::State& s) {
    if (!s.show_help)
        return;
    center_next(px(620), 0.08f);
    ImGui::Begin("##help", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_AlwaysAutoResize);
    heading(s, "Keyboard shortcuts");
    ImGui::TextDisabled(
        "Change them in keys.json (Commands > Edit keys.json). These keys are not sent to programs.");
    ImGui::BeginChild("##keys", ImVec2(0, px(460)));
    if (ImGui::BeginTable("##k", 2, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthFixed, px(170));
        auto row = [&](const std::string& label, const std::string& keys) {
            if (keys.empty())
                return;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", keys.c_str());
        };
        // In the palette's order; numbered and directional families on one line each.
        for (const auto& c : kCommands) {
            const std::string id = c.id;
            if (id.rfind("pane.go:", 0) == 0 || id.rfind("pane.swap:", 0) == 0)
                continue;
            row(c.label, s.keymap.chord_for(id));
        }
        auto family = [&](const char* label, const std::vector<std::string>& cmds) {
            std::string keys;
            for (const auto& cmd : cmds) {
                const auto k = s.keymap.chord_for(cmd);
                if (!k.empty())
                    keys += (keys.empty() ? "" : "  ") + k;
            }
            row(label, keys);
        };
        family("Focus pane left / down / up / right",
               {"pane.go:left", "pane.go:down", "pane.go:up", "pane.go:right"});
        family("Swap pane left / down / up / right",
               {"pane.swap:left", "pane.swap:down", "pane.swap:up", "pane.swap:right"});
        family("Resize pane",
               {"pane.resize:left", "pane.resize:right", "pane.resize:up", "pane.resize:down"});
        {
            const auto t1 = s.keymap.chord_for("tab.select:1");
            const auto s1 = s.keymap.chord_for("space.select:1");
            if (!t1.empty())
                row("Go to tab 1-9", t1.substr(0, t1.size() - 1) + "1...9");
            if (!s1.empty())
                row("Go to space 1-9", s1.substr(0, s1.size() - 1) + "1...9");
        }
        for (const auto& [label, cmd] :
             std::vector<std::pair<const char*, const char*>>{{"Copy", "terminal.copy"},
                                                              {"Paste", "terminal.paste"},
                                                              {"Find in terminal", "terminal.search"},
                                                              {"Select all", "terminal.select_all"}}) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", s.keymap.chord_for(cmd).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::TextDisabled(
        "Mouse: drag between panes to resize, double-click a tab or space to rename, right-click for "
        "menus, double/triple-click selects a word/line.");
    const bool close = ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape);
    ImGui::End();
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

void draw_rename(App::State& s) {
    if (s.show_rename) {
        ImGui::OpenPopup("##rename");
        s.show_rename = false;
    }
    center_next(px(420), 0.22f);
    if (!ImGui::BeginPopupModal("##rename", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
        return;
    heading(s, s.rename_kind == "space" ? "Rename space"
               : s.rename_kind == "tab" ? "Rename tab"
                                        : "Rename pane");
    ImGui::TextDisabled("Leave empty to go back to the automatic name.");
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    const bool enter =
        ImGui::InputText("##n", s.rename_buf, sizeof s.rename_buf, ImGuiInputTextFlags_EnterReturnsTrue);
    primary_button_style(true);
    const bool ok = ImGui::Button("Rename") || enter;
    primary_button_style(false);
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    if (ok) {
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
                auto& t =
                    s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
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

void draw_worktree(App::State& s) {
    if (s.show_worktree) {
        ImGui::OpenPopup("##worktree");
        s.show_worktree = false;
    }
    center_next(px(560), 0.16f);
    if (!ImGui::BeginPopupModal("##worktree", nullptr,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
        return;
    heading(s, "New git worktree");
    ImGui::TextDisabled("Creates a checkout of a branch in its own folder and opens it as a new space,");
    ImGui::TextDisabled(
        "so several agents can work on the same repository without touching each other's files.");
    ImGui::Dummy(ImVec2(0, px(2)));
    ImGui::TextDisabled("Repository folder");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##repo", s.wt_repo, sizeof s.wt_repo);
    ImGui::TextDisabled("Branch (new or existing)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##branch", "fix/login-timeout", s.wt_branch, sizeof s.wt_branch);
    const std::string wt_machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
    ui::HostClient* wthost = s.host_for(wt_machine);
    const auto profiles = wthost->profiles();
    s.wt_profile = std::clamp(s.wt_profile, 0, std::max(0, static_cast<int>(profiles.size()) - 1));
    if (!wt_machine.empty())
        ImGui::TextDisabled("On %s", wt_machine.c_str());
    ImGui::TextDisabled("Start in it");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##prof", profiles.empty()
                                        ? "(none)"
                                        : profiles[static_cast<std::size_t>(s.wt_profile)].second.c_str())) {
        for (int i = 0; i < static_cast<int>(profiles.size()); ++i)
            if (ImGui::Selectable(profiles[static_cast<std::size_t>(i)].second.c_str(), i == s.wt_profile))
                s.wt_profile = i;
        ImGui::EndCombo();
    }
    std::string base = s.settings.worktree_dir;
    ImGui::TextDisabled("Checkouts go to %s\\<repo>\\<branch>",
                        base.empty() ? "%USERPROFILE%\\.agentdesk\\worktrees" : base.c_str());
    if (!s.wt_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->needs_you));
        ImGui::TextWrapped("%s", s.wt_error.c_str());
        ImGui::PopStyleColor();
    }
    if (s.wt_busy)
        ImGui::TextDisabled("Creating...");
    ImGui::BeginDisabled(s.wt_busy || s.wt_branch[0] == '\0' || profiles.empty() || !wthost->connected());
    primary_button_style(true);
    const bool create = ImGui::Button("Create");
    primary_button_style(false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    if (create) {
        s.wt_busy = true;
        s.wt_error.clear();
        Json params{{"repo", std::string(s.wt_repo)}, {"branch", std::string(s.wt_branch)}};
        if (!base.empty()) {
            std::string safe = s.wt_branch;
            for (char& c : safe)
                if (c == '/' || c == '\\')
                    c = '-';
            params["path"] = base + "\\" + folder_name(s.wt_repo) + "\\" + safe;
        }
        const std::string profile = profiles[static_cast<std::size_t>(s.wt_profile)].first;
        const std::string branch = s.wt_branch;
        wthost->call(
            "worktree.add", params,
            [st = &s, profile, branch, wthost, wt_machine](Json result, std::string error) {
                if (!error.empty()) {
                    std::lock_guard lock(st->placements_mutex);
                    st->wt_error = error;
                    st->wt_busy = false;
                    return;
                }
                const std::string path = result.value("path", "");
                Json p{{"profile_id", profile}, {"cwd", path}};
                wthost->call(
                    "session.create", p,
                    [st, branch, wt_machine](Json r, std::string e) {
                        std::lock_guard lock(st->placements_mutex);
                        st->wt_busy = false;
                        if (!e.empty()) {
                            st->wt_error = e;
                            return;
                        }
                        Placement pl;
                        pl.session = r.value("session_id", "");
                        pl.target = LaunchTarget::kSpace;
                        pl.space_name = branch;
                        pl.machine = wt_machine;
                        st->placements.push_back(pl);
                        st->wt_error = "done";
                    },
                    std::chrono::seconds(15));
            },
            std::chrono::seconds(70));
    }
    {
        std::lock_guard lock(s.placements_mutex);
        if (s.wt_error == "done") {
            s.wt_error.clear();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

void draw_settings(App::State& s) {
    if (s.show_settings) {
        ImGui::OpenPopup("##settings");
        s.show_settings = false;
    }
    center_next(px(560), 0.1f);
    if (!ImGui::BeginPopupModal("##settings", nullptr,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
        return;
    heading(s, "Settings");
    bool changed = false;
    if (ImGui::BeginTabBar("##st")) {
        if (ImGui::BeginTabItem("Look")) {
            ImGui::TextDisabled("Theme");
            for (const auto& p : kPalettes) {
                if (ImGui::RadioButton(p.label, g_pal == &p)) {
                    g_pal = &p;
                    s.settings.theme = p.id;
                    apply_style(s.dpi, s.settings.zoom);
                    changed = true;
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::TextDisabled("Zoom (whole window)");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##zoom", &s.settings.zoom, 0.6f, 2.0f, "%.1fx")) {
                g_scale = s.dpi * s.settings.zoom;
                apply_style(s.dpi, s.settings.zoom);
                changed = true;
            }
            ImGui::TextDisabled("Terminal font size");
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::SliderFloat("##font", &s.settings.font_size, 9.0f, 28.0f, "%.0f px");
            s.fonts.mono_size = s.settings.font_size;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Terminal")) {
            {
                const auto profiles = s.host->profiles();
                const std::string current = pick_shell(s.settings.shell_profile, profiles);
                std::string label = current.empty() ? std::string("(none found)") : current;
                for (const auto& [id, pname] : profiles)
                    if (id == current)
                        label = pname;
                ImGui::TextDisabled("Shell for new spaces, tabs and splits");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##shell", label.c_str())) {
                    for (const auto& [id, pname] : profiles) {
                        if (ImGui::Selectable(pname.c_str(), id == current)) {
                            s.settings.shell_profile = id;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::TextDisabled("Claude Code, Codex and other programs: %s, or type them in the shell.",
                                    s.keymap.chord_for("agent.new").c_str());
            }
            changed |=
                ImGui::Checkbox("Copy selection when the mouse is released", &s.settings.copy_on_select);
            ImGui::TextDisabled("Wheel scroll speed");
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::SliderFloat("##scroll", &s.settings.scroll_speed, 0.2f, 5.0f, "%.1fx");
            if (ImGui::Button("Edit keys.json"))
                run_command(s, "app.keys");
            for (const auto& e : s.keymap_errors)
                ImGui::TextColored(v4(g_pal->needs_you), "keys.json: cannot use \"%s\"", e.c_str());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Alerts")) {
            ImGui::TextWrapped("When an agent needs you, or finishes while you are not looking at it:");
            changed |= ImGui::Checkbox("Play a sound", &s.settings.sounds);
            changed |= ImGui::Checkbox("Show a notification in the corner", &s.settings.toasts);
            if (ImGui::Button("Test: needs you")) {
                ::PlaySoundW(L"SystemExclamation", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
                s.toast("An agent needs you", "Example notification", "", g_pal->needs_you);
            }
            ImGui::SameLine();
            if (ImGui::Button("Test: finished")) {
                ::PlaySoundW(L"SystemAsterisk", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
                s.toast("An agent finished", "Example notification", "", g_pal->done);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Mods")) {
            ImGui::TextWrapped(
                "Mods are small Lua programs in the mods folder (one folder per mod, main.lua). "
                "They can react to agents, add commands, panels and tab text. They run in a "
                "sandbox without file access; agentdesk.exec can start programs.");
            for (const auto& m : s.mods->mods()) {
                ImGui::PushID(m.name.c_str());
                bool on = s.settings.mods_enabled.contains(m.name);
                if (ImGui::Checkbox(m.name.c_str(), &on)) {
                    if (on)
                        s.settings.mods_enabled.insert(m.name);
                    else
                        s.settings.mods_enabled.erase(m.name);
                    changed = true;
                    s.mods->load(s.settings.mods_enabled);
                }
                if (!m.description.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", m.description.c_str());
                }
                if (!m.error.empty())
                    ImGui::TextColored(v4(g_pal->needs_you), "  %s", m.error.c_str());
                ImGui::PopID();
            }
            if (s.mods->mods().empty())
                ImGui::TextDisabled("No mods yet.");
            if (ImGui::Button("Reload"))
                run_command(s, "mods.reload");
            ImGui::SameLine();
            if (ImGui::Button("Open mods folder"))
                run_command(s, "mods.folder");
            ImGui::Separator();
            ImGui::TextDisabled("Layout");
            for (const auto& l : s.layouts) {
                if (ImGui::RadioButton(l.name.c_str(), l.name == s.settings.layout))
                    run_command(s, "layout:" + l.name);
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::TextDisabled(
                "Add your own in the layouts folder next to mods (one JSON file per layout).");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Machines")) {
            ImGui::TextWrapped(
                "Other computers reachable over SSH. AgentDesk runs \"ssh <target> agentctl bridge\" "
                "and shows that machine's spaces and agents here; they keep running there when this "
                "PC sleeps. The other computer needs AgentDesk installed and key-based ssh login.");
            int remove = -1;
            for (int i = 0; i < static_cast<int>(s.settings.machines.size()); ++i) {
                const auto& m = s.settings.machines[static_cast<std::size_t>(i)];
                ImGui::PushID(i);
                const bool on = s.machine_connected(m.name);
                ImGui::TextColored(v4(on ? g_pal->done : g_pal->faint), "%s", on ? "connected" : "offline");
                ImGui::SameLine(px(100));
                ImGui::Text("%s  (%s)", m.name.c_str(), m.target.c_str());
                ImGui::SameLine(ImGui::GetContentRegionMax().x - px(70));
                if (ImGui::SmallButton("Remove"))
                    remove = i;
                if (!on) {
                    for (const auto& r : s.remotes)
                        if (r.cfg.name == m.name && !r.host->last_error().empty())
                            ImGui::TextColored(v4(g_pal->needs_you), "  %s", r.host->last_error().c_str());
                    ImGui::TextDisabled(
                        "  If it asks for a password or a host key, run  ssh %s  once in your own terminal.",
                        m.target.c_str());
                }
                ImGui::PopID();
            }
            if (remove >= 0) {
                s.settings.machines.erase(s.settings.machines.begin() + remove);
                changed = true;
                start_remotes(s);
            }
            ImGui::Separator();
            static char name[64], target[128], ctl[256];
            ImGui::TextDisabled("Add a machine");
            ImGui::SetNextItemWidth(px(150));
            ImGui::InputTextWithHint("##mname", "name", name, sizeof name);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(px(200));
            ImGui::InputTextWithHint("##mtarget", "user@host or ssh alias", target, sizeof target);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##mctl", "agentctl path there (optional; default: agentctl on PATH)",
                                     ctl, sizeof ctl);
            ImGui::BeginDisabled(name[0] == '\0' || target[0] == '\0');
            if (ImGui::Button("Add machine")) {
                s.settings.machines.push_back({name, target, ctl});
                name[0] = target[0] = ctl[0] = '\0';
                changed = true;
                start_remotes(s);
            }
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Worktrees")) {
            static char buf[512];
            if (ImGui::IsItemActivated() || buf[0] == '\0')
                std::snprintf(buf, sizeof buf, "%s", s.settings.worktree_dir.c_str());
            ImGui::TextDisabled("Folder for new worktrees (empty: %%USERPROFILE%%\\.agentdesk\\worktrees)");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##wt", buf, sizeof buf)) {
                s.settings.worktree_dir = buf;
                changed = true;
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (changed)
        save_settings(s.settings);
    ImGui::Separator();
    if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
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
    g_scale = s.dpi * s.settings.zoom;
    s.fonts.mono_size = s.settings.font_size;
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
    const float sidebar_w = show_sidebar ? px(256) : 0.0f;
    const float status_h = layout.status_bar ? px(28) : 0.0f;
    const float tabbar_h = layout.tab_bar ? px(48) : 0.0f;
    const float gap = px(10);
    if (show_sidebar)
        draw_sidebar(s, origin, ImVec2(sidebar_w, full.y - status_h));
    const ImVec2 main_pos(origin.x + sidebar_w, origin.y);
    const float main_w = full.x - sidebar_w;
    if (layout.tab_bar)
        draw_tab_bar(s, main_pos, main_w, tabbar_h);
    const float details_w = show_details ? std::min(px(320), main_w * 0.3f) : 0.0f;
    const ImVec2 content_pos(main_pos.x + gap, main_pos.y + tabbar_h + gap);
    const ImVec2 content_size(main_w - gap * 2 - (details_w > 0 ? details_w + gap : 0.0f),
                              full.y - status_h - tabbar_h - gap * 2);
    if (layout.main == "board")
        draw_board(s, content_pos, content_size);
    else if (layout.main == "grid")
        draw_grid(s, content_pos, content_size, connected);
    else
        draw_panes(s, content_pos, content_size, connected);
    if (details_w > 0)
        draw_details(s, ImVec2(content_pos.x + content_size.x + gap, content_pos.y),
                     ImVec2(details_w, content_size.y), connected);
    if (layout.status_bar)
        draw_status_bar(s, ImVec2(origin.x, origin.y + full.y - status_h), full.x, status_h);
    ImGui::End();

    draw_peek(s);
    draw_launcher(s);
    draw_rename(s);
    draw_worktree(s);
    draw_settings(s);
    draw_exit(s);
    draw_close_notice(s);
    draw_palette(s);
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
