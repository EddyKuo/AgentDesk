// agent-ui.exe (M0 prototype): Win32 + DX11 + Dear ImGui docking.
// M0 runs sessions in-process through host::Session. From M2 the same views talk to
// agent-host.exe over the named pipe instead (TerminalSource is the seam).
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
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <nlohmann/json.hpp>
#include <shellapi.h>

#include "agentdesk/host/session.hpp"
#include "agentdesk/platform/win/conpty_api.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/ui/fonts.hpp"
#include "agentdesk/ui/terminal_view.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace {

namespace win = agentdesk::platform::win;
namespace host = agentdesk::host;
namespace ui = agentdesk::ui;
using Clock = std::chrono::steady_clock;

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

HANDLE g_wake = nullptr;  // set by session threads; the UI loop wakes and redraws
float g_pending_dpi = 0.0f;

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
    UINT flags = 0;
#ifndef NDEBUG
    // D3D11_CREATE_DEVICE_DEBUG needs the Graphics Tools optional feature; not required.
#endif
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_gfx.swap, &g_gfx.device, &got,
                                               &g_gfx.context);
    if (hr == DXGI_ERROR_UNSUPPORTED) {  // no hardware device: WARP software rasterizer
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2,
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
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

// ---------------------------------------------------------------- sessions (M0: in-process)
struct Profile {
    std::string name;
    std::filesystem::path executable;
    std::vector<std::wstring> args;
};

std::vector<Profile> detect_profiles() {
    std::vector<Profile> out;
    auto add = [&](const char* name, const wchar_t* exe, std::vector<std::wstring> args = {}) {
        wchar_t buf[MAX_PATH]{};
        if (::SearchPathW(nullptr, exe, L".exe", MAX_PATH, buf, nullptr) > 0) {
            out.push_back({name, buf, std::move(args)});
        }
    };
    add("Claude Code", L"claude.exe");
    add("Codex CLI", L"codex.exe");
    add("PowerShell 7", L"pwsh.exe", {L"-NoLogo"});
    add("Windows PowerShell", L"powershell.exe", {L"-NoLogo"});
    add("Command Prompt", L"cmd.exe");
    return out;
}

class LocalSource final : public ui::TerminalSource {
public:
    explicit LocalSource(host::Session& s) : session_(s) {}
    std::shared_ptr<const agentdesk::terminal::ScreenSnapshot> snapshot() const override {
        return session_.snapshot();
    }
    void send(host::InputEvent input) override { session_.send(std::move(input)); }
    void resize(int rows, int cols) override { session_.resize(rows, cols); }
    bool can_input() const override {
        const auto s = session_.info().process_state;
        return s == host::ProcessState::kRunning || s == host::ProcessState::kStopping;
    }

private:
    host::Session& session_;
};

struct SessionItem {
    int id = 0;
    std::string name;
    std::string profile;
    std::string cwd;
    std::unique_ptr<host::Session> session;
    std::unique_ptr<LocalSource> source;
    ui::TerminalView view;
    bool view_open = true;
    bool focus_next = true;
};

struct App {
    win::ConPtyApi conpty;
    std::vector<Profile> profiles;
    std::vector<std::unique_ptr<SessionItem>> sessions;
    int next_id = 1;
    int selected = 0;
    ui::FontSet fonts;
    char cwd_buf[512] = {};
    char name_buf[128] = {};
    int profile_index = 0;
    std::string error;
    bool show_new_dialog = false;
};

std::filesystem::path exe_dir() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

const char* agent_label(host::AgentState s) {
    switch (s) {
        case host::AgentState::kUnknown:
            return "? Unknown";
        case host::AgentState::kIdle:
            return "- Idle";
        case host::AgentState::kWorking:
            return "* Working";
        case host::AgentState::kBlocked:
            return "! Blocked";
        case host::AgentState::kDone:
            return "v Done";
        case host::AgentState::kError:
            return "x Error";
    }
    return "?";
}

ImVec4 agent_color(host::AgentState s) {
    switch (s) {
        case host::AgentState::kBlocked:
            return ImVec4(1.0f, 0.65f, 0.2f, 1.0f);
        case host::AgentState::kWorking:
            return ImVec4(0.45f, 0.75f, 1.0f, 1.0f);
        case host::AgentState::kDone:
            return ImVec4(0.45f, 0.9f, 0.5f, 1.0f);
        case host::AgentState::kError:
            return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
        default:
            return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    }
}

void start_session(App& app) {
    app.error.clear();
    if (app.profiles.empty()) {
        app.error = "No profiles found";
        return;
    }
    const Profile& p = app.profiles[static_cast<std::size_t>(app.profile_index)];
    auto item = std::make_unique<SessionItem>();
    item->id = app.next_id++;
    item->name = app.name_buf[0] ? app.name_buf : p.name;
    item->profile = p.name;
    item->cwd = app.cwd_buf;
    host::SessionConfig cfg;
    cfg.api = app.conpty;
    cfg.spec.executable = p.executable;
    cfg.spec.args = p.args;
    try {
        cfg.spec.cwd = win::to_wide(item->cwd);
    } catch (const std::exception&) {
        app.error = "cwd is not valid UTF-8";
        return;
    }
    cfg.spec.size = {120, 40};
    cfg.engine.rows = 40;
    cfg.engine.cols = 120;
    host::SessionListener listener;
    listener.on_snapshot = [](auto) { ::SetEvent(g_wake); };
    listener.on_info = [](const host::SessionInfo&) { ::SetEvent(g_wake); };
    try {
        item->session = host::Session::start(std::move(cfg), std::move(listener));
    } catch (const std::exception& e) {
        app.error = std::string("Start failed: ") + e.what();  // the form keeps its input (4.2)
        return;
    }
    item->source = std::make_unique<LocalSource>(*item->session);
    app.selected = item->id;
    app.sessions.push_back(std::move(item));
    app.show_new_dialog = false;
}

void draw_new_session(App& app) {
    if (app.show_new_dialog) {
        ImGui::OpenPopup("New session");
        app.show_new_dialog = false;
    }
    if (!ImGui::BeginPopupModal("New session", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    if (ImGui::BeginCombo("Profile",
                          app.profiles.empty() ? "(none)" : app.profiles[app.profile_index].name.c_str())) {
        for (int i = 0; i < static_cast<int>(app.profiles.size()); ++i) {
            if (ImGui::Selectable(app.profiles[i].name.c_str(), i == app.profile_index))
                app.profile_index = i;
        }
        ImGui::EndCombo();
    }
    if (!app.profiles.empty()) {
        const auto exe = app.profiles[app.profile_index].executable.u8string();
        ImGui::TextDisabled("%s", reinterpret_cast<const char*>(exe.c_str()));
    }
    ImGui::InputText("Working directory", app.cwd_buf, sizeof(app.cwd_buf));
    ImGui::InputText("Name", app.name_buf, sizeof(app.name_buf));
    if (!app.error.empty())
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", app.error.c_str());
    if (ImGui::Button("Start")) {
        start_session(app);
        if (app.error.empty())
            ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void draw_ui(App& app) {
    ImGui::DockSpaceOverViewport();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::MenuItem("New session"))
            app.show_new_dialog = true;
        ImGui::TextDisabled("ConPTY: %s",
                            app.conpty.bundled ? "bundled" : "inbox (status reports unavailable)");
        ImGui::EndMainMenuBar();
    }
    draw_new_session(app);

    ImGui::Begin("Sessions");
    for (auto& item : app.sessions) {
        const auto info = item->session->info();
        ImGui::PushID(item->id);
        if (ImGui::Selectable(item->name.c_str(), app.selected == item->id)) {
            app.selected = item->id;
            item->view_open = true;
            item->focus_next = true;
        }
        ImGui::SameLine(180);
        ImGui::TextDisabled("%s", std::string(host::to_string(info.process_state)).c_str());
        ImGui::SameLine(260);
        ImGui::TextColored(agent_color(info.agent.state), "%s", agent_label(info.agent.state));
        ImGui::TextDisabled("  %s  |  %s", item->profile.c_str(), item->cwd.c_str());
        ImGui::PopID();
    }
    ImGui::End();

    SessionItem* selected = nullptr;
    for (auto& item : app.sessions) {
        if (!item->view_open)
            continue;
        const std::string title = item->name + "###term" + std::to_string(item->id);
        ImGui::SetNextWindowSize(ImVec2(900, 560), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title.c_str(), &item->view_open,
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav)) {
            const bool focused = item->view.draw("grid", *item->source, app.fonts, item->focus_next);
            item->focus_next = false;
            if (focused)
                app.selected = item->id;
        }
        ImGui::End();
        if (app.selected == item->id)
            selected = item.get();
    }

    ImGui::Begin("Details");
    if (selected != nullptr) {
        const auto info = selected->session->info();
        ImGui::Text("Process: %s  pid %u", std::string(host::to_string(info.process_state)).c_str(),
                    info.pid);
        if (info.exit_code)
            ImGui::Text("Exit code: %u", *info.exit_code);
        if (info.remaining_processes > 0)
            ImGui::Text("Cleaning up %u child processes", info.remaining_processes);
        ImGui::TextColored(agent_color(info.agent.state), "Agent: %s", agent_label(info.agent.state));
        ImGui::Text("Source: %s", info.agent.source.empty() ? "(none)" : info.agent.source.c_str());
        if (info.agent.state == host::AgentState::kUnknown &&
            info.agent.last_state != host::AgentState::kUnknown) {
            ImGui::TextDisabled("Last known: %s", agent_label(info.agent.last_state));
        }
        if (!info.agent.message.empty())
            ImGui::TextWrapped("Message: %s", info.agent.message.c_str());
        const auto snap = selected->session->snapshot();
        ImGui::Text("Size: %d x %d   history truncated: %s", snap->cols, snap->rows,
                    info.history_truncated ? "yes" : "no");
        ImGui::Separator();
        const bool running = info.process_state == host::ProcessState::kRunning;
        if (running && ImGui::Button("Stop (interrupt)"))
            selected->session->interrupt();
        if (info.process_state == host::ProcessState::kStopping) {
            ImGui::TextDisabled(info.still_running_after_interrupt ? "Still running after interrupt"
                                                                   : "Stopping...");
            if (ImGui::Button("Force stop"))
                selected->session->force_stop();
        }
        if (running) {
            ImGui::SameLine();
            if (ImGui::Button("Send Ctrl+C"))
                selected->session->send(host::RawInput{std::string("\x03", 1)});
        }
    } else {
        ImGui::TextDisabled("No session selected");
    }
    ImGui::End();
}

}  // namespace

namespace {

// Developer options (M0 evidence runs): --start <profile name> [--exit-after-ms N] [--report file]
struct DevOptions {
    std::string start_profile;
    long long exit_after_ms = 0;
    std::filesystem::path report;
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
    }
    ::LocalFree(argv);
    return o;
}

void write_report(const App& app, const std::filesystem::path& path) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& item : app.sessions) {
        const auto info = item->session->info();
        const auto snap = item->session->snapshot();
        nlohmann::json screen = nlohmann::json::array();
        for (int r = 0; r < snap->rows; ++r)
            screen.push_back(snap->row_text(r));
        out.push_back({{"name", item->name},
                       {"process_state", host::to_string(info.process_state)},
                       {"agent_state", host::to_string(info.agent.state)},
                       {"agent_source", info.agent.source},
                       {"last_agent_state", host::to_string(info.agent.last_state)},
                       {"cols", snap->cols},
                       {"rows", snap->rows},
                       {"bytes_from_program", info.bytes_from_program},
                       {"screen", screen}});
    }
    std::ofstream(path, std::ios::binary) << out.dump(2) << '\n';
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const DevOptions dev = parse_dev_options();
    g_wake = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, wnd_proc,       0,      0, instance, nullptr, nullptr,
                   nullptr,    nullptr,    L"AgentDeskUi", nullptr};
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"AgentDesk (M0 prototype)", WS_OVERLAPPEDWINDOW, 100, 100,
                                1400, 900, nullptr, nullptr, instance, nullptr);
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
        wchar_t buf[MAX_PATH]{};
        if (::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH) > 0) {
            const auto dir = std::filesystem::path(buf) / L"AgentDesk";
            std::filesystem::create_directories(dir);
            const auto u8 = (dir / L"imgui.ini").u8string();
            ini_path.assign(u8.begin(), u8.end());
            io.IniFilename = ini_path.c_str();
        }
    }
    ImGui::StyleColorsDark();
    const float dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(dpi);
    style.FontScaleDpi = dpi;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_gfx.device, g_gfx.context);

    App app;
    app.fonts = ui::load_fonts(16.0f);
    app.conpty = win::load_conpty(exe_dir());
    app.profiles = detect_profiles();
    {
        const auto cwd = std::filesystem::current_path().u8string();
        std::snprintf(app.cwd_buf, sizeof(app.cwd_buf), "%s", reinterpret_cast<const char*>(cwd.c_str()));
    }

    if (!dev.start_profile.empty()) {
        for (int i = 0; i < static_cast<int>(app.profiles.size()); ++i) {
            if (app.profiles[i].name == dev.start_profile)
                app.profile_index = i;
        }
        start_session(app);
    }
    const auto launched = Clock::now();

    bool running = true;
    int frames_to_draw = 3;
    while (running) {
        if (dev.exit_after_ms > 0 && Clock::now() - launched > std::chrono::milliseconds(dev.exit_after_ms)) {
            ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        // Event-driven: sleep until input, a session wake-up or the next view deadline (skill §2).
        DWORD timeout = frames_to_draw > 0 ? 0 : (dev.exit_after_ms > 0 ? 200 : INFINITE);
        for (const auto& item : app.sessions) {
            if (const auto d = item->view.next_deadline()) {
                const auto ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(*d - Clock::now()).count();
                timeout = std::min<DWORD>(timeout, static_cast<DWORD>(std::max<long long>(ms, 0) + 1));
            }
        }
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
    app.sessions.clear();  // M0: sessions end with the window (host process arrives in M1)
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroy_device();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, instance);
    ::CloseHandle(g_wake);
    return 0;
}
