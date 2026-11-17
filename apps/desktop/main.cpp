// agent-ui.exe: Win32 + DX11 + Dear ImGui docking front end of agent-host.exe (design doc 4).
// The UI holds no process or ConPTY: every session lives in the host. Closing the window only
// detaches; "Exit and stop all sessions" is the explicit way to end the host (design doc 4.3).
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
// shellapi.h (CommandLineToArgvW) needs windows.h first.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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

// ---------------------------------------------------------------- app state
struct ViewState {
    ui::TerminalView view;
    bool open = true;
    bool focus_next = true;
};

struct App {
    std::unique_ptr<ui::HostClient> host;
    UiSettings settings;
    std::map<std::string, ViewState> views;  // by session id
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

const char* agent_label(const std::string& s) {
    if (s == "Idle")
        return "- Idle";
    if (s == "Working")
        return "* Working";
    if (s == "Blocked")
        return "! Blocked";
    if (s == "Done")
        return "v Done";
    if (s == "Error")
        return "x Error";
    return "? Unknown";  // neutral; never shown as done (design doc 4.1)
}

ImVec4 agent_color(const std::string& s) {
    if (s == "Blocked")
        return ImVec4(1.0f, 0.65f, 0.2f, 1.0f);
    if (s == "Working")
        return ImVec4(0.45f, 0.75f, 1.0f, 1.0f);
    if (s == "Done")
        return ImVec4(0.45f, 0.9f, 0.5f, 1.0f);
    if (s == "Error")
        return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
    return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
}

void draw_new_session(App& app) {
    if (app.show_new_dialog) {
        ImGui::OpenPopup("New session");
        app.show_new_dialog = false;
    }
    if (!ImGui::BeginPopupModal("New session", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    const auto profiles = app.host->profiles();
    app.profile_index = std::clamp(app.profile_index, 0, std::max(0, static_cast<int>(profiles.size()) - 1));
    if (ImGui::BeginCombo("Profile",
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
    ImGui::InputText("Working directory", app.cwd_buf, sizeof(app.cwd_buf));
    ImGui::InputText("Name", app.name_buf, sizeof(app.name_buf));
    if (!app.create_error.empty())
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", app.create_error.c_str());
    const bool can_start = app.host->connected() && !profiles.empty();
    ImGui::BeginDisabled(!can_start);
    if (ImGui::Button("Start")) {
        app.create_error = "Starting...";
        app.settings.last_cwd = app.cwd_buf;  // reused next time; never silently replaced (4.2)
        save_ui_settings(app.settings);
        auto* error = &app.create_error;
        app.host->create_session(profiles[static_cast<std::size_t>(app.profile_index)].first, app.cwd_buf,
                                 app.name_buf, [error](std::string e) { *error = e.empty() ? "" : e; });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();
    if (app.create_error.empty() && ImGui::IsWindowAppearing() == false) {
        // keep the dialog open; the user closes it explicitly so errors stay visible
    }
    ImGui::EndPopup();
}

void draw_exit_dialog(App& app, const std::vector<std::shared_ptr<ui::RemoteSession>>& sessions) {
    if (app.show_exit_dialog) {
        ImGui::OpenPopup("Exit and stop all sessions");
        app.show_exit_dialog = false;
        app.exit_error.clear();
    }
    if (!ImGui::BeginPopupModal("Exit and stop all sessions", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted("These sessions will be force-stopped and agent-host will exit:");
    int running = 0;
    for (const auto& s : sessions) {
        const auto v = s->view();
        if (v.process_state != "Running" && v.process_state != "Stopping" && v.process_state != "Starting")
            continue;
        ++running;
        ImGui::BulletText("%s  (%s)  %s", v.name.c_str(), v.process_state.c_str(), v.cwd.c_str());
    }
    if (running == 0)
        ImGui::TextDisabled("(no running sessions)");
    if (!app.exit_error.empty())
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", app.exit_error.c_str());
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
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void draw_close_notice(App& app) {
    if (app.show_close_notice) {
        ImGui::OpenPopup("Sessions keep running");
        app.show_close_notice = false;
    }
    if (!ImGui::BeginPopupModal("Sessions keep running", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted(
        "Closing this window only detaches. Your sessions keep running in agent-host,\n"
        "and reopening AgentDesk reconnects to them.\n"
        "Use File > Exit and stop all sessions to end everything.");
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

void draw_ui(App& app) {
    ImGui::DockSpaceOverViewport();
    const auto sessions = app.host->sessions();
    const bool connected = app.host->connected();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New session", nullptr, false, connected))
                app.show_new_dialog = true;
            if (ImGui::MenuItem("Exit and stop all sessions...", nullptr, false, connected))
                app.show_exit_dialog = true;
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("New session", nullptr, false, connected))
            app.show_new_dialog = true;
        ImGui::EndMainMenuBar();
    }
    draw_new_session(app);
    draw_exit_dialog(app, sessions);
    draw_close_notice(app);

    ImGui::Begin("Sessions");
    for (const auto& s : sessions) {
        const auto v = s->view();
        ImGui::PushID(v.id.c_str());
        if (ImGui::Selectable(v.name.c_str(), app.selected == v.id)) {
            app.selected = v.id;
            auto& vs = app.views[v.id];
            vs.open = true;  // reopen a closed panel; the session never stopped (4.3)
            vs.focus_next = true;
        }
        ImGui::SameLine(170);
        ImGui::TextDisabled("%s", v.process_state.c_str());
        ImGui::SameLine(250);
        ImGui::TextColored(agent_color(v.agent_state), "%s", agent_label(v.agent_state));
        ImGui::TextDisabled("  %s  |  %s", v.profile_id.c_str(), v.cwd.c_str());
        ImGui::PopID();
    }
    ImGui::End();

    std::shared_ptr<ui::RemoteSession> selected;
    for (const auto& s : sessions) {
        const auto v = s->view();
        auto [it, inserted] = app.views.try_emplace(v.id);
        if (inserted && app.selected.empty())
            app.selected = v.id;
        auto& vs = it->second;
        if (!vs.open)
            continue;
        const std::string title = v.name + "###term" + v.id;
        ImGui::SetNextWindowSize(ImVec2(900, 560), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title.c_str(), &vs.open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav)) {
            if (!connected || v.lost) {
                // Disconnected overlay: input disabled, never presented as Done (design doc 4.3).
                ImGui::TextColored(
                    ImVec4(1, 0.6f, 0.3f, 1), "%s",
                    v.lost ? "Lost (owned by a previous agent-host)" : "Disconnected from agent-host");
            } else if (!v.is_controller) {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1),
                                   "Read-only: another window controls this session");
                ImGui::SameLine();
                if (ImGui::SmallButton("Take control"))
                    s->take_control();
            }
            const bool focused = vs.view.draw("grid", *s, app.fonts, vs.focus_next);
            vs.focus_next = false;
            if (focused)
                app.selected = v.id;
        }
        ImGui::End();
        if (app.selected == v.id)
            selected = s;
    }

    ImGui::Begin("Details");
    if (selected) {
        const auto v = selected->view();
        ImGui::Text("Process: %s  pid %u", v.process_state.c_str(), v.pid);
        if (v.exit_code)
            ImGui::Text("Exit code: %u", *v.exit_code);
        if (v.remaining_processes > 0)
            ImGui::Text("Cleaning up %u child processes", v.remaining_processes);
        ImGui::TextColored(agent_color(v.agent_state), "Agent: %s", agent_label(v.agent_state));
        ImGui::Text("Source: %s", v.agent_source.empty() ? "(none)" : v.agent_source.c_str());
        if (v.agent_state == "Unknown" && v.last_agent_state != "Unknown")
            ImGui::TextDisabled("Last known: %s", agent_label(v.last_agent_state));
        if (!v.agent_message.empty())
            ImGui::TextWrapped("Message: %s", v.agent_message.c_str());
        ImGui::Text("Profile: %s", v.profile_id.c_str());
        ImGui::TextWrapped("Directory: %s", v.cwd.c_str());
        ImGui::Separator();
        const bool running = v.process_state == "Running";
        ImGui::BeginDisabled(!connected || v.lost);
        if (running && ImGui::Button("Stop (interrupt)"))
            selected->stop(false);
        if (v.process_state == "Stopping") {
            ImGui::TextDisabled(v.still_running_after_interrupt ? "Still running after interrupt"
                                                                : "Stopping...");
            if (ImGui::Button("Force stop"))
                selected->stop(true);
        }
        if (running && v.is_controller) {
            ImGui::SameLine();
            if (ImGui::Button("Send Ctrl+C"))
                selected->send(agentdesk::host::RawInput{std::string("\x03", 1)});
        }
        ImGui::EndDisabled();
    } else {
        ImGui::TextDisabled("No session selected");
    }
    ImGui::End();

    // Status bar: host connection, control, size, truncation / overload (design doc 4.1).
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, h));
    ImGui::Begin("##status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav);
    if (connected) {
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "host: connected");
    } else {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "host: disconnected (%s)",
                           app.host->last_error().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Start host"))
            app.host->request_start_host();
    }
    if (app.host->host_dies_with_launcher()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1),
                           "| agent-host will end when the launching terminal closes");
    }
    if (selected) {
        const auto v = selected->view();
        const auto snap = selected->snapshot();
        ImGui::SameLine();
        ImGui::TextDisabled("| %s | %dx%d%s%s", v.is_controller ? "input: this window" : "input: read-only",
                            snap->cols, snap->rows, v.history_truncated ? " | history truncated" : "",
                            v.backpressure_waits > 0 ? " | output throttled" : "");
    }
    ImGui::End();
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
                        {"cols", snap->cols},
                        {"rows", snap->rows},
                        {"screen", screen}});
    }
    out["sessions"] = list;
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
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;  // multi-viewports stay off in v1 (3.5)
    static std::string ini_path;
    {
        const auto u8 = ui_settings_path().parent_path().append(L"imgui.ini").u8string();
        ini_path.assign(u8.begin(), u8.end());
        io.IniFilename = ini_path.c_str();
    }
    ImGui::StyleColorsDark();
    const float dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    ImGui::GetStyle().ScaleAllSizes(dpi);
    ImGui::GetStyle().FontScaleDpi = dpi;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_gfx.device, g_gfx.context);

    App app;
    app.settings = load_ui_settings();
    app.fonts = ui::load_fonts(16.0f);
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
            ImGuiStyle& s = ImGui::GetStyle();
            s = ImGuiStyle();
            ImGui::StyleColorsDark(&s);
            s.ScaleAllSizes(g_pending_dpi);
            s.FontScaleDpi = g_pending_dpi;
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
