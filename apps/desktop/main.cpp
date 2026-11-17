// agent-ui.exe: Win32 + DX11 + Dear ImGui docking front end of agent-host.exe (design doc 4).
// The UI holds no process or ConPTY: every session lives in the host. Closing the window only
// detaches; "Exit and stop all sessions" is the explicit way to end the host (design doc 4.3).
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
// shellapi.h (CommandLineToArgvW) needs windows.h first.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <dwmapi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <nlohmann/json.hpp>
#include <shellapi.h>

#include "agentdesk/ipc/host_launcher.hpp"
#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/ui/fonts.hpp"
#include "agentdesk/ui/host_client.hpp"
#include "agentdesk/ui/imgui_adapter.hpp"
#include "agentdesk/ui/terminal_view.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace {

namespace win = agentdesk::platform::win;
namespace ui = agentdesk::ui;
namespace ipc = agentdesk::ipc;
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;

// ---------------------------------------------------------------- D3D11 plumbing
struct Gfx {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swap = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    UINT pending_w = 0;
    UINT pending_h = 0;
    bool occluded = false;
} g_gfx;

HANDLE g_wake = nullptr;  // set by IPC threads; the UI loop wakes and redraws
float g_pending_dpi = 0.0f;
std::atomic<bool> g_close_requested{false};

void create_rtv() {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g_gfx.swap->GetBuffer(0, IID_PPV_ARGS(&back)))) {
        g_gfx.device->CreateRenderTargetView(back, nullptr, &g_gfx.rtv);
        back->Release();
    }
}

void release_rtv() {
    if (g_gfx.rtv) {
        g_gfx.rtv->Release();
        g_gfx.rtv = nullptr;
    }
}

bool create_device(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_gfx.swap, &g_gfx.device, &got,
                                               &g_gfx.context);
    if (hr == DXGI_ERROR_UNSUPPORTED) {  // no hardware device: WARP software rasterizer
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
                                           D3D11_SDK_VERSION, &sd, &g_gfx.swap, &g_gfx.device, &got,
                                           &g_gfx.context);
    }
    if (FAILED(hr))
        return false;
    create_rtv();
    return true;
}

void destroy_device() {
    release_rtv();
    if (g_gfx.swap)
        g_gfx.swap->Release();
    if (g_gfx.context)
        g_gfx.context->Release();
    if (g_gfx.device)
        g_gfx.device->Release();
    g_gfx = {};
}

LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
        return 1;
    switch (msg) {
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED) {
                g_gfx.pending_w = LOWORD(lparam);
                g_gfx.pending_h = HIWORD(lparam);
            }
            return 0;
        case WM_DPICHANGED: {
            const auto* r = reinterpret_cast<RECT*>(lparam);  // Win32 contract for this message
            ::SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                           SWP_NOZORDER | SWP_NOACTIVATE);
            g_pending_dpi = static_cast<float>(HIWORD(wparam)) / 96.0f;
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wparam & 0xfff0) == SC_KEYMENU)
                return 0;  // Alt must reach the terminal
            break;
        case WM_CLOSE:
            g_close_requested = true;  // window X = detach; the app decides (first-run notice)
            return 0;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

// ---------------------------------------------------------------- UI settings (ui.json)
struct UiSettings {
    bool close_notice_shown = false;
    std::string last_cwd;
};

std::filesystem::path ui_settings_path() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    auto dir =
        (n > 0 ? std::filesystem::path(std::wstring(buf, n)) : std::filesystem::temp_directory_path()) /
        L"AgentDesk";
    std::filesystem::create_directories(dir);
    return dir / L"ui.json";
}

UiSettings load_ui_settings() {
    UiSettings s;
    std::ifstream in(ui_settings_path(), std::ios::binary);
    if (!in)
        return s;
    std::stringstream ss;
    ss << in.rdbuf();
    const auto j = Json::parse(ss.str(), nullptr, false);
    if (j.is_discarded() || j.value("schema_version", 0) != 1)
        return s;
    s.close_notice_shown = j.value("close_notice_shown", false);
    s.last_cwd = j.value("last_cwd", "");
    return s;
}

void save_ui_settings(const UiSettings& s) {
    const auto path = ui_settings_path();
    const auto tmp = std::filesystem::path(path).concat(L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << Json{{"schema_version", 1},
                    {"close_notice_shown", s.close_notice_shown},
                    {"last_cwd", s.last_cwd}}
                   .dump(2);
    }
    ::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

// ---------------------------------------------------------------- look (herdr-style dark chrome)
// Layout follows herdr's default window as observed on https://herdr.dev/gallery/ (left sidebar
// with spaces and an agents list, tab row, card panes with headers, thin status bar); colors and
// code are our own.
namespace pal {
constexpr ImU32 kBg = IM_COL32(12, 12, 14, 255);
constexpr ImU32 kSidebar = IM_COL32(17, 17, 20, 255);
constexpr ImU32 kCard = IM_COL32(17, 17, 19, 255);
constexpr ImU32 kBorder = IM_COL32(38, 38, 44, 255);
constexpr ImU32 kFocus = IM_COL32(104, 134, 248, 255);
constexpr ImU32 kHover = IM_COL32(28, 28, 33, 255);
constexpr ImU32 kSelected = IM_COL32(36, 36, 43, 255);
constexpr ImU32 kText = IM_COL32(232, 232, 237, 255);
constexpr ImU32 kDim = IM_COL32(139, 139, 150, 255);
constexpr ImU32 kFaint = IM_COL32(92, 92, 104, 255);
constexpr ImU32 kGreen = IM_COL32(110, 201, 138, 255);
constexpr ImU32 kAmber = IM_COL32(230, 182, 84, 255);
constexpr ImU32 kRed = IM_COL32(242, 106, 94, 255);
constexpr ImU32 kBlue = IM_COL32(110, 160, 246, 255);
}  // namespace pal

float g_dpi = 1.0f;
float px(float v) {
    return std::round(v * g_dpi);
}

ImVec4 to_vec4(ImU32 c) {
    return ImGui::ColorConvertU32ToFloat4(c);
}

void apply_style(float dpi) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 10;
    s.ChildRounding = 8;
    s.FrameRounding = 6;
    s.PopupRounding = 8;
    s.GrabRounding = 6;
    s.ScrollbarRounding = 6;
    s.WindowBorderSize = 1;
    s.ChildBorderSize = 0;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.WindowPadding = ImVec2(18, 16);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(8, 8);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 10;
    s.SelectableTextAlign = ImVec2(0, 0.5f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = to_vec4(pal::kText);
    c[ImGuiCol_TextDisabled] = to_vec4(pal::kDim);
    c[ImGuiCol_WindowBg] = to_vec4(pal::kBg);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = to_vec4(IM_COL32(22, 22, 26, 255));
    c[ImGuiCol_Border] = to_vec4(pal::kBorder);
    c[ImGuiCol_FrameBg] = to_vec4(IM_COL32(26, 26, 31, 255));
    c[ImGuiCol_FrameBgHovered] = to_vec4(IM_COL32(32, 32, 38, 255));
    c[ImGuiCol_FrameBgActive] = to_vec4(IM_COL32(38, 38, 46, 255));
    c[ImGuiCol_Button] = to_vec4(IM_COL32(32, 32, 38, 255));
    c[ImGuiCol_ButtonHovered] = to_vec4(IM_COL32(44, 44, 52, 255));
    c[ImGuiCol_ButtonActive] = to_vec4(IM_COL32(54, 54, 64, 255));
    c[ImGuiCol_Header] = to_vec4(pal::kSelected);
    c[ImGuiCol_HeaderHovered] = to_vec4(pal::kHover);
    c[ImGuiCol_HeaderActive] = to_vec4(pal::kSelected);
    c[ImGuiCol_CheckMark] = to_vec4(pal::kFocus);
    c[ImGuiCol_Separator] = to_vec4(pal::kBorder);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = to_vec4(IM_COL32(48, 48, 56, 255));
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    c[ImGuiCol_TitleBg] = to_vec4(IM_COL32(22, 22, 26, 255));
    c[ImGuiCol_TitleBgActive] = to_vec4(IM_COL32(22, 22, 26, 255));
    c[ImGuiCol_NavCursor] = to_vec4(pal::kFocus);
    s.ScaleAllSizes(dpi);
    s.FontScaleDpi = dpi;
}

// ---------------------------------------------------------------- app state
struct Tab {
    std::vector<std::string> panes;  // session ids, laid out left to right
    int focused = 0;
};

struct ViewState {
    ui::TerminalView view;
    bool focus_next = true;
};

struct App {
    std::unique_ptr<ui::HostClient> host;
    UiSettings settings;
    std::map<std::string, ViewState> views;  // by session id
    std::set<std::string> known;             // sessions seen at least once
    std::vector<Tab> tabs;
    int active_tab = 0;
    bool split_next = false;  // the next new session joins the active tab as a pane
    bool show_details = true;
    std::string selected;
    ui::FontSet fonts;
    char cwd_buf[512] = {};
    char name_buf[128] = {};
    int profile_index = 0;
    std::string create_error;
    bool show_new_dialog = false;
    bool show_exit_dialog = false;
    bool show_close_notice = false;
    bool exit_now = false;
    std::string exit_error;
};

constexpr std::size_t kMaxPanesPerTab = 3;

const char* agent_label(const std::string& s) {
    if (s == "Idle" || s == "Working" || s == "Blocked" || s == "Done" || s == "Error")
        return s.c_str();
    return "Unknown";  // neutral; never shown as done (design doc 4.1)
}

ImU32 agent_color(const std::string& s) {
    if (s == "Blocked")
        return pal::kRed;
    if (s == "Working")
        return pal::kAmber;
    if (s == "Done")
        return pal::kGreen;
    if (s == "Error")
        return pal::kRed;
    return pal::kDim;
}

// Text + shape status mark (never color alone, design doc 4.1).
void draw_state_mark(ImDrawList* dl, ImVec2 center, float r, const std::string& state) {
    const ImU32 c = agent_color(state);
    if (state == "Working") {
        dl->AddCircleFilled(center, r, c);
    } else if (state == "Blocked") {
        dl->AddTriangleFilled(ImVec2(center.x, center.y - r * 1.1f),
                              ImVec2(center.x + r * 1.1f, center.y + r * 0.9f),
                              ImVec2(center.x - r * 1.1f, center.y + r * 0.9f), c);
    } else if (state == "Done") {
        dl->AddLine(ImVec2(center.x - r, center.y), ImVec2(center.x - r * 0.25f, center.y + r * 0.8f), c,
                    px(1.8f));
        dl->AddLine(ImVec2(center.x - r * 0.25f, center.y + r * 0.8f),
                    ImVec2(center.x + r, center.y - r * 0.8f), c, px(1.8f));
    } else if (state == "Error") {
        dl->AddLine(ImVec2(center.x - r * 0.8f, center.y - r * 0.8f),
                    ImVec2(center.x + r * 0.8f, center.y + r * 0.8f), c, px(1.8f));
        dl->AddLine(ImVec2(center.x + r * 0.8f, center.y - r * 0.8f),
                    ImVec2(center.x - r * 0.8f, center.y + r * 0.8f), c, px(1.8f));
    } else if (state == "Idle") {
        dl->AddCircle(center, r, c, 0, px(1.4f));
    } else {
        dl->AddCircle(center, r, pal::kFaint, 0, px(1.2f));
        dl->AddLine(ImVec2(center.x - r * 0.4f, center.y), ImVec2(center.x + r * 0.4f, center.y), pal::kFaint,
                    px(1.2f));
    }
}

ImU32 profile_color(const std::string& profile_id) {
    if (profile_id.starts_with("claude"))
        return IM_COL32(217, 119, 87, 255);
    if (profile_id.starts_with("codex"))
        return IM_COL32(142, 140, 245, 255);
    if (profile_id.starts_with("pwsh") || profile_id.starts_with("powershell"))
        return IM_COL32(90, 169, 230, 255);
    return IM_COL32(140, 146, 156, 255);
}

ImU32 name_color(const std::string& name) {
    static constexpr ImU32 kColors[] = {IM_COL32(94, 177, 140, 255),  IM_COL32(230, 140, 80, 255),
                                        IM_COL32(150, 125, 235, 255), IM_COL32(236, 104, 146, 255),
                                        IM_COL32(86, 164, 232, 255),  IM_COL32(214, 178, 72, 255)};
    std::uint32_t h = 2166136261u;
    for (const unsigned char ch : name)
        h = (h ^ ch) * 16777619u;
    return kColors[h % std::size(kColors)];
}

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

std::string session_title(const ui::SessionView& v) {
    return v.name.empty() ? v.profile_id : v.name;
}

// Text clipped to max_w with an ellipsis; returns the drawn width.
float draw_text_clipped(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col,
                        const std::string& text, float max_w) {
    if (max_w <= 0)
        return 0;
    const ImVec2 full = font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str());
    if (full.x <= max_w) {
        dl->AddText(font, size, pos, col, text.c_str());
        return full.x;
    }
    const char* ell = "...";
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

// A full-width clickable row; returns true when clicked. Draws hover/selected backgrounds.
bool row_button(const char* id, float height, bool selected, ImVec2* out_min, ImVec2* out_max) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, height));
    const ImVec2 q(p.x + w, p.y + height);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected)
        dl->AddRectFilled(p, q, pal::kSelected, px(6));
    else if (ImGui::IsItemHovered())
        dl->AddRectFilled(p, q, pal::kHover, px(6));
    *out_min = p;
    *out_max = q;
    return clicked;
}

void draw_badge(ImDrawList* dl, ImFont* bold, float size, ImVec2 min, float side, ImU32 color,
                const std::string& name) {
    dl->AddRectFilled(min, ImVec2(min.x + side, min.y + side), color, px(4));
    std::string letter = name.empty() ? std::string("?") : name.substr(0, 1);
    if (!letter.empty() && static_cast<unsigned char>(letter[0]) >= 0x80) {
        std::size_t len = 1;
        while (len < name.size() && (static_cast<unsigned char>(name[len]) & 0xC0) == 0x80)
            ++len;
        letter = name.substr(0, len);
    } else if (!letter.empty()) {
        letter[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(letter[0])));
    }
    const float ls = size * 0.85f;
    const ImVec2 t = bold->CalcTextSizeA(ls, FLT_MAX, 0, letter.c_str());
    dl->AddText(bold, ls, ImVec2(min.x + (side - t.x) * 0.5f, min.y + (side - t.y) * 0.5f),
                IM_COL32(255, 255, 255, 235), letter.c_str());
}

// ---------------------------------------------------------------- tabs and panes
int tab_of(const App& app, const std::string& id) {
    for (int i = 0; i < static_cast<int>(app.tabs.size()); ++i) {
        const auto& p = app.tabs[static_cast<std::size_t>(i)].panes;
        if (std::find(p.begin(), p.end(), id) != p.end())
            return i;
    }
    return -1;
}

void focus_session(App& app, const std::string& id) {
    int t = tab_of(app, id);
    if (t < 0) {
        app.tabs.push_back(Tab{{id}, 0});
        t = static_cast<int>(app.tabs.size()) - 1;
    }
    app.active_tab = t;
    auto& tab = app.tabs[static_cast<std::size_t>(t)];
    tab.focused = static_cast<int>(std::find(tab.panes.begin(), tab.panes.end(), id) - tab.panes.begin());
    app.selected = id;
    app.views[id].focus_next = true;
}

void close_pane(App& app, const std::string& id) {  // the session keeps running (design doc 4.3)
    const int t = tab_of(app, id);
    if (t < 0)
        return;
    auto& panes = app.tabs[static_cast<std::size_t>(t)].panes;
    panes.erase(std::find(panes.begin(), panes.end(), id));
    if (panes.empty())
        app.tabs.erase(app.tabs.begin() + t);
    app.active_tab = std::clamp(app.active_tab, 0, std::max(0, static_cast<int>(app.tabs.size()) - 1));
    if (!app.tabs.empty()) {
        auto& tab = app.tabs[static_cast<std::size_t>(app.active_tab)];
        tab.focused = std::clamp(tab.focused, 0, static_cast<int>(tab.panes.size()) - 1);
        focus_session(app, tab.panes[static_cast<std::size_t>(tab.focused)]);
    }
}

// New sessions get a tab (or join the active tab when "Split" asked for it); vanished sessions
// leave their panes.
void sync_tabs(App& app, const std::vector<std::shared_ptr<ui::RemoteSession>>& sessions) {
    std::set<std::string> present;
    for (const auto& s : sessions) {
        present.insert(s->id());
        if (app.known.insert(s->id()).second) {
            if (app.split_next && !app.tabs.empty() &&
                app.tabs[static_cast<std::size_t>(app.active_tab)].panes.size() < kMaxPanesPerTab) {
                app.tabs[static_cast<std::size_t>(app.active_tab)].panes.push_back(s->id());
                app.split_next = false;
            } else {
                app.tabs.push_back(Tab{{s->id()}, 0});
            }
            focus_session(app, s->id());
        }
    }
    for (auto& tab : app.tabs) {
        std::erase_if(tab.panes, [&](const std::string& id) { return !present.contains(id); });
        tab.focused = std::clamp(tab.focused, 0, std::max(0, static_cast<int>(tab.panes.size()) - 1));
    }
    std::erase_if(app.tabs, [](const Tab& t) { return t.panes.empty(); });
    std::erase_if(app.known, [&](const std::string& id) { return !present.contains(id); });
    app.active_tab = std::clamp(app.active_tab, 0, std::max(0, static_cast<int>(app.tabs.size()) - 1));
}

void cycle_tab(App& app, int delta) {
    if (app.tabs.empty())
        return;
    const int n = static_cast<int>(app.tabs.size());
    app.active_tab = ((app.active_tab + delta) % n + n) % n;
    const auto& tab = app.tabs[static_cast<std::size_t>(app.active_tab)];
    focus_session(app, tab.panes[static_cast<std::size_t>(tab.focused)]);
}

void open_new_session(App& app, bool split) {
    app.split_next = split;
    app.create_error.clear();
    app.show_new_dialog = true;
}

// ---------------------------------------------------------------- dialogs
void draw_new_session(App& app) {
    if (app.show_new_dialog) {
        ImGui::OpenPopup("New session");
        app.show_new_dialog = false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.35f),
        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(520), 0));
    if (!ImGui::BeginPopupModal("New session", nullptr,
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
        return;
    ImGui::PushFont(app.fonts.ui_bold, app.fonts.ui_size * 1.15f);
    ImGui::TextUnformatted(app.split_next ? "Split: new session" : "New session");
    ImGui::PopFont();
    ImGui::TextDisabled("Runs in agent-host and keeps running when this window closes.");
    ImGui::Dummy(ImVec2(0, px(4)));
    const auto profiles = app.host->profiles();
    app.profile_index = std::clamp(app.profile_index, 0, std::max(0, static_cast<int>(profiles.size()) - 1));
    ImGui::TextDisabled("Profile");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##profile",
                          profiles.empty()
                              ? "(none)"
                              : profiles[static_cast<std::size_t>(app.profile_index)].second.c_str())) {
        for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
            if (ImGui::Selectable(profiles[static_cast<std::size_t>(i)].second.c_str(),
                                  i == app.profile_index))
                app.profile_index = i;
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("Working directory");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##cwd", app.cwd_buf, sizeof(app.cwd_buf));
    ImGui::TextDisabled("Name (optional)");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##name", app.name_buf, sizeof(app.name_buf));
    if (!app.create_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              to_vec4(app.create_error == "Starting..." ? pal::kDim : pal::kRed));
        ImGui::TextWrapped("%s", app.create_error.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    const bool can_start = app.host->connected() && !profiles.empty();
    ImGui::BeginDisabled(!can_start);
    ImGui::PushStyleColor(ImGuiCol_Button, to_vec4(IM_COL32(232, 232, 237, 255)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, to_vec4(IM_COL32(255, 255, 255, 255)));
    ImGui::PushStyleColor(ImGuiCol_Text, to_vec4(IM_COL32(18, 18, 22, 255)));
    if (ImGui::Button("Start") || (can_start && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
        app.create_error = "Starting...";
        app.settings.last_cwd = app.cwd_buf;  // reused next time; never silently replaced (4.2)
        save_ui_settings(app.settings);
        auto* error = &app.create_error;
        app.host->create_session(profiles[static_cast<std::size_t>(app.profile_index)].first, app.cwd_buf,
                                 app.name_buf, [error](std::string e) {
                                     *error = e.empty() ? std::string("ok") : e;
                                     ::SetEvent(g_wake);
                                 });
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        app.split_next = false;
        ImGui::CloseCurrentPopup();
    }
    if (app.create_error == "ok") {  // started: close; failures stay visible (4.2)
        app.create_error.clear();
        app.name_buf[0] = '\0';
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_exit_dialog(App& app, const std::vector<std::shared_ptr<ui::RemoteSession>>& sessions) {
    if (app.show_exit_dialog) {
        ImGui::OpenPopup("Exit and stop all sessions");
        app.show_exit_dialog = false;
        app.exit_error.clear();
    }
    ImGui::SetNextWindowSize(ImVec2(px(520), 0));
    if (!ImGui::BeginPopupModal("Exit and stop all sessions", nullptr,
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
        return;
    ImGui::PushFont(app.fonts.ui_bold, app.fonts.ui_size * 1.15f);
    ImGui::TextUnformatted("Exit and stop all sessions");
    ImGui::PopFont();
    ImGui::TextDisabled("These sessions will be force-stopped and agent-host will exit:");
    int running = 0;
    for (const auto& s : sessions) {
        const auto v = s->view();
        if (v.process_state != "Running" && v.process_state != "Stopping" && v.process_state != "Starting")
            continue;
        ++running;
        ImGui::BulletText("%s  (%s)  %s", session_title(v).c_str(), v.process_state.c_str(),
                          display_path(v.cwd).c_str());
    }
    if (running == 0)
        ImGui::TextDisabled("(no running sessions)");
    if (!app.exit_error.empty())
        ImGui::TextColored(to_vec4(pal::kRed), "%s", app.exit_error.c_str());
    ImGui::PushStyleColor(ImGuiCol_Button, to_vec4(IM_COL32(170, 60, 52, 255)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, to_vec4(IM_COL32(200, 72, 62, 255)));
    if (ImGui::Button("Stop all and exit")) {
        auto* exit_now = &app.exit_now;
        auto* error = &app.exit_error;
        app.host->shutdown_host(true, [exit_now, error](std::string e) {
            if (e.empty())
                *exit_now = true;
            else
                *error = e;
            ::SetEvent(g_wake);
        });
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void draw_close_notice(App& app) {
    if (app.show_close_notice) {
        ImGui::OpenPopup("Sessions keep running");
        app.show_close_notice = false;
    }
    ImGui::SetNextWindowSize(ImVec2(px(520), 0));
    if (!ImGui::BeginPopupModal("Sessions keep running", nullptr,
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
        return;
    ImGui::PushFont(app.fonts.ui_bold, app.fonts.ui_size * 1.15f);
    ImGui::TextUnformatted("Sessions keep running");
    ImGui::PopFont();
    ImGui::TextWrapped(
        "Closing this window only detaches. Your sessions keep running in agent-host, and reopening "
        "AgentDesk reconnects to them. Use \"Exit and stop all\" in the sidebar to end everything.");
    if (ImGui::Button("Close window")) {
        app.settings.close_notice_shown = true;  // shown once (design doc 4.3)
        save_ui_settings(app.settings);
        app.exit_now = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- sidebar
void draw_sidebar(App& app, const std::vector<std::shared_ptr<ui::RemoteSession>>& sessions, ImVec2 pos,
                  ImVec2 size) {
    ImDrawList* bg = ImGui::GetWindowDrawList();
    bg->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), pal::kSidebar);
    bg->AddLine(ImVec2(pos.x + size.x - 1, pos.y), ImVec2(pos.x + size.x - 1, pos.y + size.y), pal::kBorder);
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(10), px(12)));
    ImGui::BeginChild("##sidebar", size, ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), px(2)));  // tight rows, like herdr
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* ui_font = app.fonts.ui;
    ImFont* bold = app.fonts.ui_bold;
    const float fs = ImGui::GetFontSize();
    const float row_h = px(30);

    // Header: product name + new-session button.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        dl->AddRectFilled(ImVec2(p.x + px(4), p.y + px(5)), ImVec2(p.x + px(18), p.y + px(19)), pal::kFocus,
                          px(4));
        dl->AddText(bold, fs * 1.1f, ImVec2(p.x + px(26), p.y + px(2)), pal::kText, "AgentDesk");
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - px(26), p.y));
        if (ImGui::Button("+##new", ImVec2(px(26), px(24))))
            open_new_session(app, false);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("New session (Ctrl+Shift+T)");
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + px(36)));
    }

    // Spaces: sessions grouped by working directory.
    ImGui::TextDisabled("Spaces");
    ImGui::Dummy(ImVec2(0, px(2)));
    std::vector<std::string> spaces;
    std::map<std::string, int> space_count;
    for (const auto& s : sessions) {
        const auto v = s->view();
        if (space_count[v.cwd]++ == 0)
            spaces.push_back(v.cwd);
    }
    std::string selected_cwd;
    for (const auto& s : sessions) {
        if (s->id() == app.selected)
            selected_cwd = s->view().cwd;
    }
    if (spaces.empty())
        ImGui::TextDisabled("  No sessions yet");
    for (const auto& cwd : spaces) {
        ImGui::PushID(cwd.c_str());
        ImVec2 a, b;
        if (row_button("##space", row_h, cwd == selected_cwd, &a, &b)) {
            for (const auto& s : sessions) {
                if (s->view().cwd == cwd) {
                    focus_session(app, s->id());
                    break;
                }
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", cwd.c_str());
        const std::string name = folder_name(cwd);
        const float side = px(18);
        draw_badge(dl, bold, fs, ImVec2(a.x + px(6), a.y + (row_h - side) * 0.5f), side, name_color(name),
                   name);
        const std::string count = std::to_string(space_count[cwd]);
        const float cw = ImGui::CalcTextSize(count.c_str()).x;
        dl->AddText(ui_font, fs, ImVec2(b.x - cw - px(8), a.y + (row_h - fs) * 0.5f), pal::kDim,
                    count.c_str());
        draw_text_clipped(dl, bold, fs, ImVec2(a.x + px(32), a.y + (row_h - fs) * 0.5f), pal::kText, name,
                          b.x - a.x - px(32) - cw - px(16));
        ImGui::PopID();
    }

    // Agents: grouped by state, most urgent first, pinned to the bottom of the sidebar.
    struct Group {
        const char* label;
        ImU32 color;
        std::vector<ui::SessionView> items;
    };
    std::vector<Group> groups = {{"Blocked", pal::kRed, {}},  {"Working", pal::kAmber, {}},
                                 {"Idle", pal::kDim, {}},     {"Done", pal::kGreen, {}},
                                 {"Error", pal::kRed, {}},    {"Unknown", pal::kDim, {}},
                                 {"Stopped", pal::kFaint, {}}};
    int total = 0;
    for (const auto& s : sessions) {
        const auto v = s->view();
        ++total;
        if (v.lost || v.process_state == "Exited" || v.process_state == "Lost" ||
            v.process_state == "FailedToStart") {
            groups[6].items.push_back(v);
            continue;
        }
        const std::string st = agent_label(v.agent_state);
        for (auto& g : groups) {
            if (st == g.label) {
                g.items.push_back(v);
                break;
            }
        }
    }
    float agents_h = px(30) + px(70);  // label + footer
    for (const auto& g : groups) {
        if (!g.items.empty())
            agents_h += px(28) + (row_h + px(2)) * static_cast<float>(g.items.size());
    }
    const float bottom = pos.y + size.y - px(12);
    const float agents_top = std::max(ImGui::GetCursorScreenPos().y + px(16), bottom - agents_h);
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, agents_top));
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(pos.x, p.y - px(8)), ImVec2(pos.x + size.x - 1, p.y - px(8)), pal::kBorder);
    }
    ImGui::TextDisabled("Agents");
    if (total == 0)
        ImGui::TextDisabled("  Nothing running");
    for (const auto& g : groups) {
        if (g.items.empty())
            continue;
        ImGui::Dummy(ImVec2(0, px(4)));
        ImGui::PushStyleColor(ImGuiCol_Text, to_vec4(g.color));
        ImGui::Text("%s %d", g.label, static_cast<int>(g.items.size()));
        ImGui::PopStyleColor();
        for (const auto& v : g.items) {
            ImGui::PushID(v.id.c_str());
            ImVec2 a, b;
            if (row_button("##agent", row_h, v.id == app.selected, &a, &b))
                focus_session(app, v.id);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n%s | %s%s%s", session_title(v).c_str(), v.process_state.c_str(),
                                  v.agent_state.c_str(), v.agent_message.empty() ? "" : "\n",
                                  v.agent_message.c_str());
            }
            const float cy = a.y + row_h * 0.5f;
            dl->AddRectFilled(ImVec2(a.x + px(8), cy - px(5)), ImVec2(a.x + px(18), cy + px(5)),
                              profile_color(v.profile_id), px(2));
            draw_state_mark(dl, ImVec2(b.x - px(12), cy), px(4.5f),
                            g.label == std::string("Stopped") ? "Unknown" : v.agent_state);
            const std::string age = elapsed_since(v.created_at);
            const float aw = ImGui::CalcTextSize(age.c_str()).x;
            dl->AddText(ui_font, fs, ImVec2(b.x - px(26) - aw, cy - fs * 0.5f), pal::kDim, age.c_str());
            const float text_x = a.x + px(26);
            const float max_w = b.x - px(34) - aw - text_x;
            const float nw = draw_text_clipped(dl, bold, fs, ImVec2(text_x, cy - fs * 0.5f), pal::kText,
                                               session_title(v), max_w);
            draw_text_clipped(dl, ui_font, fs, ImVec2(text_x + nw + px(4), cy - fs * 0.5f), pal::kDim,
                              "\xC2\xB7 " + folder_name(v.cwd), max_w - nw - px(4));
            ImGui::PopID();
        }
    }

    // Footer actions.
    ImGui::SetCursorScreenPos(
        ImVec2(ImGui::GetCursorScreenPos().x, std::max(ImGui::GetCursorScreenPos().y, bottom - px(60))));
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(pos.x, p.y - px(6)), ImVec2(pos.x + size.x - 1, p.y - px(6)), pal::kBorder);
    }
    ImVec2 a, b;
    const bool connected = app.host->connected();
    ImGui::BeginDisabled(!connected);
    if (row_button("##foot-new", px(26), false, &a, &b))
        open_new_session(app, false);
    dl->AddText(ui_font, fs, ImVec2(a.x + px(8), a.y + (px(26) - fs) * 0.5f), pal::kDim, "+   New session");
    if (row_button("##foot-exit", px(26), false, &a, &b))
        app.show_exit_dialog = true;
    dl->AddText(ui_font, fs, ImVec2(a.x + px(8), a.y + (px(26) - fs) * 0.5f), pal::kDim,
                "\xC3\x97   Exit and stop all...");
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

// ---------------------------------------------------------------- tab bar
void draw_tab_bar(App& app, const std::vector<std::shared_ptr<ui::RemoteSession>>& sessions, ImVec2 pos,
                  float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fs = ImGui::GetFontSize();
    float x = pos.x + px(8);
    int close_tab = -1;
    for (int i = 0; i < static_cast<int>(app.tabs.size()); ++i) {
        auto& tab = app.tabs[static_cast<std::size_t>(i)];
        std::string title;
        std::string profile;
        std::string state = "Unknown";
        for (const auto& rs : sessions) {
            if (rs->id() == tab.panes[static_cast<std::size_t>(tab.focused)]) {
                const auto v = rs->view();
                title = session_title(v);
                profile = v.profile_id;
                state = v.agent_state;
            }
        }
        if (tab.panes.size() > 1)
            title += "  +" + std::to_string(tab.panes.size() - 1);
        const std::string label = std::to_string(i + 1) + "  " + title;
        ImFont* tab_font = i == app.active_tab ? app.fonts.ui_bold : app.fonts.ui;
        const float text_w = std::min(tab_font->CalcTextSizeA(fs, FLT_MAX, 0, label.c_str()).x, px(220));
        const float w = text_w + px(64);
        const ImVec2 a(x, pos.y + px(7));
        const ImVec2 b(x + w, pos.y + height - px(7));
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##tab", ImVec2(w - px(24), b.y - a.y))) {
            app.active_tab = i;
            focus_session(app, tab.panes[static_cast<std::size_t>(tab.focused)]);
        }
        const bool hovered = ImGui::IsItemHovered();
        const bool active = i == app.active_tab;
        if (active || hovered)
            dl->AddRectFilled(a, b, active ? pal::kSelected : pal::kHover, px(6));
        const float cy = (a.y + b.y) * 0.5f;
        dl->AddRectFilled(ImVec2(a.x + px(10), cy - px(4)), ImVec2(a.x + px(18), cy + px(4)),
                          profile_color(profile), px(2));
        draw_text_clipped(dl, tab_font, fs, ImVec2(a.x + px(26), cy - fs * 0.5f),
                          active ? pal::kText : pal::kDim, label, text_w);
        draw_state_mark(dl, ImVec2(a.x + px(32) + text_w, cy), px(3.5f), state);
        ImGui::SetCursorScreenPos(ImVec2(b.x - px(22), a.y));
        if (ImGui::InvisibleButton("##close", ImVec2(px(18), b.y - a.y)))
            close_tab = i;
        const ImU32 xc = ImGui::IsItemHovered() ? pal::kText : pal::kFaint;
        const ImVec2 xm(b.x - px(13), cy);
        dl->AddLine(ImVec2(xm.x - px(3.5f), xm.y - px(3.5f)), ImVec2(xm.x + px(3.5f), xm.y + px(3.5f)), xc,
                    px(1.4f));
        dl->AddLine(ImVec2(xm.x + px(3.5f), xm.y - px(3.5f)), ImVec2(xm.x - px(3.5f), xm.y + px(3.5f)), xc,
                    px(1.4f));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Close tab (sessions keep running)");
        ImGui::PopID();
        x = b.x + px(4);
        if (x > pos.x + width - px(200))
            break;
    }
    if (close_tab >= 0) {
        const auto panes = app.tabs[static_cast<std::size_t>(close_tab)].panes;
        for (const auto& id : panes)
            close_pane(app, id);
    }
    // Right side: new session, details toggle.
    const float bw = px(28);
    float rx = pos.x + width - px(10);
    rx -= ImGui::CalcTextSize("Details").x + px(24);
    ImGui::SetCursorScreenPos(ImVec2(rx, pos.y + px(7)));
    ImGui::PushStyleColor(ImGuiCol_Button, to_vec4(app.show_details ? pal::kSelected : IM_COL32(0, 0, 0, 0)));
    if (ImGui::Button("Details", ImVec2(0, height - px(14))))
        app.show_details = !app.show_details;
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(ImVec2(rx - bw - px(6), pos.y + px(7)));
    ImGui::PushStyleColor(ImGuiCol_Button, to_vec4(IM_COL32(0, 0, 0, 0)));
    ImGui::BeginDisabled(!app.host->connected());
    if (ImGui::Button("+##tabnew", ImVec2(bw, height - px(14))))
        open_new_session(app, false);
    ImGui::EndDisabled();
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("New session (Ctrl+Shift+T)");
    dl->AddLine(ImVec2(pos.x, pos.y + height - 1), ImVec2(pos.x + width, pos.y + height - 1), pal::kBorder);
}

// ---------------------------------------------------------------- panes
void draw_pane(App& app, const std::shared_ptr<ui::RemoteSession>& s, Tab& tab, int index, ImVec2 pos,
               ImVec2 size, bool connected, std::string* close_request) {
    const auto v = s->view();
    auto& vs = app.views[v.id];
    const bool focused_pane = app.selected == v.id;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilled(pos, max, pal::kCard, px(10));
    dl->AddRect(pos, max, focused_pane ? pal::kFocus : pal::kBorder, px(10), 0,
                focused_pane ? px(1.5f) : px(1));

    const float fs = ImGui::GetFontSize();
    const float header_h = px(36);
    const float cy = pos.y + header_h * 0.5f;
    dl->AddRectFilled(ImVec2(pos.x + px(14), cy - px(5)), ImVec2(pos.x + px(24), cy + px(5)),
                      profile_color(v.profile_id), px(2));
    const std::string title = session_title(v);
    const float tw = draw_text_clipped(dl, app.fonts.ui_bold, fs, ImVec2(pos.x + px(32), cy - fs * 0.5f),
                                       pal::kText, title, size.x * 0.4f);

    // Right side of the header: state, split, close.
    ImGui::PushID(v.id.c_str());
    float rx = max.x - px(10);
    ImGui::PushStyleColor(ImGuiCol_Button, to_vec4(IM_COL32(0, 0, 0, 0)));
    rx -= px(24);
    ImGui::SetCursorScreenPos(ImVec2(rx, cy - px(11)));
    if (ImGui::Button("\xC3\x97##close", ImVec2(px(24), px(22))))
        *close_request = v.id;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Close pane (Ctrl+Shift+W); the session keeps running");
    rx -= ImGui::CalcTextSize("Split").x + px(18);
    ImGui::SetCursorScreenPos(ImVec2(rx, cy - px(11)));
    ImGui::BeginDisabled(!connected || tab.panes.size() >= kMaxPanesPerTab);
    if (ImGui::Button("Split", ImVec2(0, px(22)))) {
        app.active_tab = tab_of(app, v.id);
        open_new_session(app, true);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Start a new session beside this one");
    ImGui::PopStyleColor();
    const std::string state_text =
        v.lost ? std::string("Lost")
               : (v.process_state == "Running" ? std::string(agent_label(v.agent_state)) : v.process_state);
    const float sw = ImGui::CalcTextSize(state_text.c_str()).x;
    rx -= sw + px(30);
    draw_state_mark(dl, ImVec2(rx + px(8), cy), px(4),
                    v.lost || v.process_state != "Running" ? "Unknown" : v.agent_state);
    dl->AddText(app.fonts.ui, fs, ImVec2(rx + px(18), cy - fs * 0.5f),
                v.process_state == "Running" && !v.lost ? agent_color(v.agent_state) : pal::kDim,
                state_text.c_str());
    draw_text_clipped(dl, app.fonts.ui, fs, ImVec2(pos.x + px(40) + tw, cy - fs * 0.5f), pal::kDim,
                      display_path(v.cwd), rx - (pos.x + px(48) + tw));

    // Banner: why input is not available.
    float top = pos.y + header_h;
    std::string banner;
    ImU32 banner_color = pal::kAmber;
    if (!connected) {
        banner = "Disconnected from agent-host";
    } else if (v.lost) {
        banner = "Lost: owned by a previous agent-host; nothing was re-run";
    } else if (!v.is_controller && v.process_state == "Running") {
        banner = "Read-only: another window controls this session";
        banner_color = pal::kBlue;
    }
    if (!banner.empty()) {
        dl->AddRectFilled(ImVec2(pos.x + px(10), top), ImVec2(max.x - px(10), top + px(28)),
                          (banner_color & 0x00FFFFFF) | 0x22000000, px(6));
        dl->AddText(app.fonts.ui, fs, ImVec2(pos.x + px(20), top + (px(28) - fs) * 0.5f), banner_color,
                    banner.c_str());
        if (connected && !v.lost && !v.is_controller) {
            ImGui::SetCursorScreenPos(ImVec2(max.x - px(120), top + px(3)));
            if (ImGui::SmallButton("Take control"))
                s->take_control();
        }
        top += px(34);
    }

    // Terminal body.
    const ImVec2 body_min(pos.x + px(12), top);
    const ImVec2 body_size(size.x - px(24), max.y - top - px(10));
    ImGui::SetCursorScreenPos(body_min);
    ImGui::BeginChild(
        "##body", body_size, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
    const bool focused = vs.view.draw("grid", *s, app.fonts, vs.focus_next);
    vs.focus_next = false;
    if (focused) {
        app.selected = v.id;
        tab.focused = index;
    }
    switch (vs.view.take_shortcut()) {
        case ui::AppShortcut::kNewSession:
            open_new_session(app, false);
            break;
        case ui::AppShortcut::kNextTab:
            cycle_tab(app, 1);
            break;
        case ui::AppShortcut::kPrevTab:
            cycle_tab(app, -1);
            break;
        case ui::AppShortcut::kClosePane:
            *close_request = v.id;
            break;
        case ui::AppShortcut::kNone:
            break;
    }
    ImGui::EndChild();
    ImGui::PopID();
}

void draw_empty_state(App& app, ImVec2 pos, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilled(pos, max, pal::kCard, px(10));
    dl->AddRect(pos, max, pal::kBorder, px(10));
    const float fs = ImGui::GetFontSize();
    const char* title = app.host->connected() ? "No open panes" : "Connecting to agent-host...";
    const ImVec2 tsz = app.fonts.ui_bold->CalcTextSizeA(fs * 1.2f, FLT_MAX, 0, title);
    const float cx = pos.x + size.x * 0.5f;
    const float cy = pos.y + size.y * 0.42f;
    dl->AddText(app.fonts.ui_bold, fs * 1.2f, ImVec2(cx - tsz.x * 0.5f, cy - tsz.y), pal::kText, title);
    const char* hint = "Start Claude Code, Codex or a shell. It keeps running after you close this window.";
    const ImVec2 hsz = ImGui::CalcTextSize(hint);
    dl->AddText(app.fonts.ui, fs, ImVec2(cx - hsz.x * 0.5f, cy + px(8)), pal::kDim, hint);
    if (app.host->connected()) {
        const char* label = "New session   Ctrl+Shift+T";
        const float bw = ImGui::CalcTextSize(label).x + px(32);
        ImGui::SetCursorScreenPos(ImVec2(cx - bw * 0.5f, cy + px(40)));
        if (ImGui::Button(label, ImVec2(bw, px(32))))
            open_new_session(app, false);
    }
}

// ---------------------------------------------------------------- details card
void draw_details(App& app, const std::shared_ptr<ui::RemoteSession>& selected, bool connected, ImVec2 pos,
                  ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilled(pos, max, pal::kCard, px(10));
    dl->AddRect(pos, max, pal::kBorder, px(10));
    ImGui::SetCursorScreenPos(ImVec2(pos.x + px(16), pos.y + px(14)));
    ImGui::BeginChild("##details", ImVec2(size.x - px(32), size.y - px(28)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    if (!selected) {
        ImGui::TextDisabled("No session selected");
        ImGui::EndChild();
        return;
    }
    const auto v = selected->view();
    ImGui::PushFont(app.fonts.ui_bold, app.fonts.ui_size * 1.15f);
    ImGui::TextUnformatted(session_title(v).c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s", v.profile_id.c_str());
    ImGui::Dummy(ImVec2(0, px(2)));

    auto kv = [&](const char* k, const std::string& value, ImU32 color = pal::kText) {
        ImGui::TextDisabled("%s", k);
        ImGui::SameLine(px(84));
        ImGui::PushStyleColor(ImGuiCol_Text, to_vec4(color));
        ImGui::TextWrapped("%s", value.c_str());
        ImGui::PopStyleColor();
    };
    kv("Agent", agent_label(v.agent_state), agent_color(v.agent_state));
    if (v.agent_state == "Unknown" && v.last_agent_state != "Unknown")
        kv("Last known", agent_label(v.last_agent_state), pal::kDim);
    kv("Source", v.agent_source.empty() ? std::string("none") : v.agent_source, pal::kDim);
    if (!v.agent_message.empty())
        kv("Message", v.agent_message);
    ImGui::Separator();
    std::string proc = v.lost ? std::string("Lost") : v.process_state;
    if (v.pid != 0)
        proc += "  \xC2\xB7  pid " + std::to_string(v.pid);
    kv("Process", proc);
    if (v.exit_code) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%u (0x%08X)", *v.exit_code, *v.exit_code);
        kv("Exit code", buf);
    }
    if (v.remaining_processes > 0)
        kv("Cleaning", std::to_string(v.remaining_processes) + " child processes", pal::kAmber);
    kv("Directory", display_path(v.cwd), pal::kDim);
    const auto age = elapsed_since(v.created_at);
    if (!age.empty())
        kv("Started", age + " ago", pal::kDim);
    kv("Input", v.is_controller ? std::string("this window") : std::string("read-only"), pal::kDim);
    ImGui::Separator();
    const bool running = v.process_state == "Running";
    ImGui::BeginDisabled(!connected || v.lost);
    if (running && ImGui::Button("Stop (interrupt)"))
        selected->stop(false);
    if (v.process_state == "Stopping") {
        ImGui::TextDisabled(v.still_running_after_interrupt ? "Still running after interrupt"
                                                            : "Stopping...");
        ImGui::PushStyleColor(ImGuiCol_Button, to_vec4(IM_COL32(170, 60, 52, 255)));
        if (ImGui::Button("Force stop"))
            selected->stop(true);
        ImGui::PopStyleColor();
    }
    if (running && v.is_controller) {
        ImGui::SameLine();
        if (ImGui::Button("Send Ctrl+C"))
            selected->send(agentdesk::host::RawInput{std::string("\x03", 1)});
    }
    if (!v.is_controller && running) {
        if (ImGui::Button("Take control"))
            selected->take_control();
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
}

// ---------------------------------------------------------------- status bar
void draw_status_bar(App& app, const std::shared_ptr<ui::RemoteSession>& selected, int session_count,
                     ImVec2 pos, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), pal::kSidebar);
    dl->AddLine(pos, ImVec2(pos.x + width, pos.y), pal::kBorder);
    const float fs = ImGui::GetFontSize();
    const float ty = pos.y + (height - fs) * 0.5f;
    float x = pos.x + px(12);
    auto item = [&](const std::string& text, ImU32 color) {
        dl->AddText(app.fonts.ui, fs, ImVec2(x, ty), color, text.c_str());
        x += ImGui::CalcTextSize(text.c_str()).x + px(18);
    };
    const bool connected = app.host->connected();
    dl->AddCircleFilled(ImVec2(x + px(4), pos.y + height * 0.5f), px(3.5f),
                        connected ? pal::kGreen : pal::kRed);
    x += px(14);
    item(connected ? "agent-host" : "agent-host disconnected: " + app.host->last_error(),
         connected ? pal::kDim : pal::kRed);
    if (!connected) {
        ImGui::SetCursorScreenPos(ImVec2(x, pos.y + px(2)));
        if (ImGui::SmallButton("Start host"))
            app.host->request_start_host();
        x += px(90);
    }
    if (app.host->host_dies_with_launcher())
        item("host ends when the launching terminal closes", pal::kAmber);
    item(std::to_string(session_count) + (session_count == 1 ? " session" : " sessions"), pal::kDim);
    if (selected) {
        const auto v = selected->view();
        const auto snap = selected->snapshot();
        item(display_path(v.cwd), pal::kDim);
        item(std::to_string(snap->cols) + "\xC3\x97" + std::to_string(snap->rows), pal::kDim);
        item(v.is_controller ? "input: this window" : "input: read-only",
             v.is_controller ? pal::kDim : pal::kBlue);
        if (v.history_truncated)
            item("history truncated", pal::kAmber);
        if (v.backpressure_waits > 0)
            item("output throttled", pal::kAmber);
    }
    const char* keys = "Ctrl+Shift+T new   Ctrl+Tab switch   Ctrl+Shift+W close pane";
    const float kw = ImGui::CalcTextSize(keys).x;
    if (pos.x + width - kw - px(12) > x)
        dl->AddText(app.fonts.ui, fs, ImVec2(pos.x + width - kw - px(12), ty), pal::kFaint, keys);
}

// ---------------------------------------------------------------- frame
void draw_ui(App& app) {
    const auto sessions = app.host->sessions();
    const bool connected = app.host->connected();
    sync_tabs(app, sessions);

    // Shortcuts while no terminal has the keyboard (a focused terminal reports them itself).
    if (!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_T) && connected)
            open_new_session(app, false);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Tab))
            cycle_tab(app, 1);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Tab))
            cycle_tab(app, -1);
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
    const float sidebar_w = px(232);
    const float status_h = px(28);
    const float tabbar_h = px(46);
    const float gap = px(10);

    draw_sidebar(app, sessions, origin, ImVec2(sidebar_w, full.y - status_h));

    const ImVec2 main_pos(origin.x + sidebar_w, origin.y);
    const float main_w = full.x - sidebar_w;
    draw_tab_bar(app, sessions, main_pos, main_w, tabbar_h);

    std::shared_ptr<ui::RemoteSession> selected;
    for (const auto& s : sessions) {
        if (s->id() == app.selected)
            selected = s;
    }
    const float details_w = app.show_details ? std::min(px(320), main_w * 0.32f) : 0.0f;
    const ImVec2 content_pos(main_pos.x + gap, main_pos.y + tabbar_h + gap);
    const ImVec2 content_size(main_w - gap * 2 - (details_w > 0 ? details_w + gap : 0.0f),
                              full.y - status_h - tabbar_h - gap * 2);

    std::string close_request;
    if (app.tabs.empty()) {
        draw_empty_state(app, content_pos, content_size);
    } else {
        Tab& tab = app.tabs[static_cast<std::size_t>(app.active_tab)];
        const int n = static_cast<int>(tab.panes.size());
        const float pane_w = (content_size.x - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
        for (int i = 0; i < n; ++i) {
            const auto& id = tab.panes[static_cast<std::size_t>(i)];
            const auto it =
                std::find_if(sessions.begin(), sessions.end(), [&](const auto& s) { return s->id() == id; });
            if (it == sessions.end())
                continue;
            draw_pane(
                app, *it, tab, i,
                ImVec2(std::round(content_pos.x + static_cast<float>(i) * (pane_w + gap)), content_pos.y),
                ImVec2(std::floor(pane_w), content_size.y), connected, &close_request);
        }
    }
    if (details_w > 0) {
        draw_details(app, selected, connected, ImVec2(content_pos.x + content_size.x + gap, content_pos.y),
                     ImVec2(details_w, content_size.y));
    }
    draw_status_bar(app, selected, static_cast<int>(sessions.size()),
                    ImVec2(origin.x, origin.y + full.y - status_h), full.x, status_h);
    ImGui::End();

    if (!close_request.empty())
        close_pane(app, close_request);
    draw_new_session(app);
    draw_exit_dialog(app, sessions);
    draw_close_notice(app);
}

// Developer options (automated checks): --start <profile id> [--exit-after-ms N] [--report file]
// [--pipe NAME]
struct DevOptions {
    std::string start_profile;
    long long exit_after_ms = 0;
    std::filesystem::path report;
    std::wstring pipe;
};

DevOptions parse_dev_options() {
    DevOptions o;
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    for (int i = 1; argv != nullptr && i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--start" && i + 1 < argc)
            o.start_profile = win::to_utf8(argv[++i]);
        else if (a == L"--exit-after-ms" && i + 1 < argc)
            o.exit_after_ms = std::stoll(argv[++i]);
        else if (a == L"--report" && i + 1 < argc)
            o.report = argv[++i];
        else if (a == L"--pipe" && i + 1 < argc)
            o.pipe = argv[++i];
    }
    ::LocalFree(argv);
    return o;
}

void write_report(const App& app, const std::filesystem::path& path) {
    Json out = {{"connected", app.host->connected()}, {"host_instance_id", app.host->host_instance_id()}};
    Json list = Json::array();
    for (const auto& s : app.host->sessions()) {
        const auto v = s->view();
        const auto snap = s->snapshot();
        Json screen = Json::array();
        for (int r = 0; r < snap->rows; ++r)
            screen.push_back(snap->row_text(r));
        list.push_back({{"id", v.id},
                        {"name", v.name},
                        {"process_state", v.process_state},
                        {"pid", v.pid},
                        {"agent_state", v.agent_state},
                        {"agent_source", v.agent_source},
                        {"is_controller", v.is_controller},
                        {"lost", v.lost},
                        {"cols", snap->cols},
                        {"rows", snap->rows},
                        {"screen", screen}});
    }
    out["sessions"] = list;
    out["tabs"] = app.tabs.size();
    out["selected"] = app.selected;
    std::ofstream(path, std::ios::binary) << out.dump(2) << '\n';
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const DevOptions dev = parse_dev_options();
    g_wake = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, wnd_proc,       0,      0, instance, nullptr, nullptr,
                   nullptr,    nullptr,    L"AgentDeskUi", nullptr};
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"AgentDesk", WS_OVERLAPPEDWINDOW, 100, 100, 1400, 900,
                                nullptr, nullptr, instance, nullptr);
    if (!create_device(hwnd)) {
        destroy_device();
        ::UnregisterClassW(wc.lpszClassName, instance);
        ::MessageBoxW(nullptr, L"Direct3D 11 is not available", L"AgentDesk", MB_ICONERROR);
        return 1;
    }
    {
        const BOOL dark = TRUE;  // dark title bar to match the chrome (DWMWA_USE_IMMERSIVE_DARK_MODE)
        ::DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    }
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // fixed layout; tabs and panes are rebuilt from the host's sessions
    g_dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    apply_style(g_dpi);
    ui::adapter::disable_nav_windowing();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_gfx.device, g_gfx.context);

    App app;
    app.settings = load_ui_settings();
    app.fonts = ui::load_fonts(14.0f, 13.5f);
    io.FontDefault = app.fonts.ui;
    {
        std::string cwd = app.settings.last_cwd;
        if (cwd.empty()) {
            const auto u8 = std::filesystem::current_path().u8string();
            cwd.assign(u8.begin(), u8.end());
        }
        std::snprintf(app.cwd_buf, sizeof(app.cwd_buf), "%s", cwd.c_str());
    }
    ui::HostClientOptions hopts;
    hopts.host_exe = ipc::sibling_executable(L"agent-host.exe");
    hopts.pipe_name = dev.pipe.empty() ? ipc::default_pipe_name() : dev.pipe;
    hopts.wake = [] { ::SetEvent(g_wake); };
    app.host = std::make_unique<ui::HostClient>(hopts);

    bool started_dev_session = dev.start_profile.empty();
    const auto launched = Clock::now();
    bool running = true;
    int frames_to_draw = 3;
    while (running) {
        if (!started_dev_session && app.host->connected()) {
            started_dev_session = true;
            app.host->create_session(dev.start_profile, app.cwd_buf, "", nullptr);
        }
        if (dev.exit_after_ms > 0 && Clock::now() - launched > std::chrono::milliseconds(dev.exit_after_ms)) {
            app.exit_now = true;
        }
        if (g_close_requested.exchange(false)) {
            if (app.settings.close_notice_shown || dev.exit_after_ms > 0)
                app.exit_now = true;
            else
                app.show_close_notice = true;
        }
        if (app.exit_now)
            break;

        // Event-driven: sleep until input, an IPC wake-up or the next view deadline.
        DWORD timeout = frames_to_draw > 0 ? 0 : 1000;  // 1 s heartbeat for connection state
        for (const auto& [id, vs] : app.views) {
            if (const auto d = vs.view.next_deadline()) {
                const auto ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(*d - Clock::now()).count();
                timeout = std::min<DWORD>(timeout, static_cast<DWORD>(std::max<long long>(ms, 0) + 1));
            }
        }
        if (dev.exit_after_ms > 0)
            timeout = std::min<DWORD>(timeout, 100);
        const DWORD r = ::MsgWaitForMultipleObjectsEx(1, &g_wake, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (r == WAIT_OBJECT_0 || r == WAIT_TIMEOUT)
            frames_to_draw = std::max(frames_to_draw, 2);
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                running = false;
            frames_to_draw = 3;
        }
        if (!running)
            break;
        if (frames_to_draw <= 0)
            continue;

        if (g_gfx.occluded && g_gfx.swap->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            frames_to_draw = 0;
            continue;
        }
        g_gfx.occluded = false;
        if (g_gfx.pending_w != 0 && g_gfx.pending_h != 0) {
            release_rtv();
            g_gfx.swap->ResizeBuffers(0, g_gfx.pending_w, g_gfx.pending_h, DXGI_FORMAT_UNKNOWN, 0);
            g_gfx.pending_w = g_gfx.pending_h = 0;
            create_rtv();
        }
        if (g_pending_dpi > 0.0f) {
            g_dpi = g_pending_dpi;
            apply_style(g_dpi);
            g_pending_dpi = 0.0f;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        draw_ui(app);
        ImGui::Render();
        const float clear[4] = {0.06f, 0.06f, 0.07f, 1.0f};
        g_gfx.context->OMSetRenderTargets(1, &g_gfx.rtv, nullptr);
        g_gfx.context->ClearRenderTargetView(g_gfx.rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const HRESULT hr = g_gfx.swap->Present(1, 0);
        g_gfx.occluded = hr == DXGI_STATUS_OCCLUDED;
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            ImGui_ImplDX11_Shutdown();
            destroy_device();
            if (!create_device(hwnd))
                break;
            ImGui_ImplDX11_Init(g_gfx.device, g_gfx.context);
        }
        --frames_to_draw;
    }

    if (!dev.report.empty())
        write_report(app, dev.report);
    app.host.reset();  // detach: sessions keep running in agent-host (design doc 4.3)
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroy_device();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, instance);
    ::CloseHandle(g_wake);
    return 0;
}
