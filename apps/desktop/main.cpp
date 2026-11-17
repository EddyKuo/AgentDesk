// agent-ui.exe: Win32 + DX11 + Dear ImGui front end of agent-host.exe (design doc 4).
// The UI holds no process or ConPTY: every session lives in the host. Closing the window only
// detaches; "Exit and stop all sessions" is the explicit way to end the host (design doc 4.3).
// This file is the platform shell; the window contents are in app.cpp.
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
// dwmapi/shellapi need windows.h first.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <dbghelp.h>
#include <dwmapi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <nlohmann/json.hpp>
#include <shellapi.h>

#include "agentdesk/ipc/host_launcher.hpp"
#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/ui/host_client.hpp"
#include "agentdesk/ui/imgui_adapter.hpp"
#include "app.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace {

namespace win = agentdesk::platform::win;
namespace ui = agentdesk::ui;
namespace ipc = agentdesk::ipc;
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

HANDLE g_wake = nullptr;  // set by IPC threads; the UI loop wakes and redraws
float g_pending_dpi = 0.0f;
std::atomic<bool> g_close_requested{false};
std::atomic<int> g_focus_change{-1};  // -1 none, 0 lost, 1 gained

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
        case WM_ACTIVATEAPP:
            g_focus_change = wparam != FALSE ? 1 : 0;
            break;
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

// A crash leaves ui-crash.txt (data folder) with the exception and the call stack, using the
// .pdb next to the executable. Sessions keep running in the host either way.
LONG WINAPI write_crash_report(EXCEPTION_POINTERS* ep) {
    wchar_t buf[MAX_PATH]{};
    DWORD n = ::GetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", buf, MAX_PATH);
    std::filesystem::path dir;
    if (n > 0 && n < MAX_PATH) {
        dir = std::wstring(buf, n);
    } else {
        n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
        dir = std::filesystem::path(std::wstring(buf, n)) / L"AgentDesk";
    }
    std::ofstream out(dir / L"ui-crash.txt", std::ios::binary | std::ios::trunc);
    char head[96];
    std::snprintf(head, sizeof head, "exception 0x%08lX at %p\n", ep->ExceptionRecord->ExceptionCode,
                  ep->ExceptionRecord->ExceptionAddress);
    out << head;
    HANDLE process = ::GetCurrentProcess();
    ::SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    ::SymInitialize(process, nullptr, TRUE);
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    for (int i = 0;
         i < 48 && ::StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, ::GetCurrentThread(), &frame, &ctx,
                                 nullptr, ::SymFunctionTableAccess64, ::SymGetModuleBase64, nullptr);
         ++i) {
        alignas(SYMBOL_INFO) char sym_buf[sizeof(SYMBOL_INFO) + 256]{};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(sym_buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD line_disp = 0;
        const bool has_sym = ::SymFromAddr(process, frame.AddrPC.Offset, &disp, sym) != FALSE;
        const bool has_line =
            ::SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_disp, &line) != FALSE;
        out << "  " << (has_sym ? sym->Name : "?");
        if (has_line)
            out << "  " << line.FileName << ":" << line.LineNumber;
        out << "\n";
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Developer options (automated checks): --start <profile id> [--exit-after-ms N] [--report file]
// [--pipe NAME]
struct DevOptions {
    std::string start_profile;
    long long exit_after_ms = 0;
    std::filesystem::path report;
    std::wstring pipe;
    std::vector<std::string> run;
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
        else if (a == L"--run" && i + 1 < argc) {
            std::string all = win::to_utf8(argv[++i]);
            for (std::size_t p = 0; p <= all.size();) {
                const auto q = all.find(';', p);
                const std::string c = all.substr(p, q == std::string::npos ? std::string::npos : q - p);
                if (!c.empty())
                    o.run.push_back(c);
                if (q == std::string::npos)
                    break;
                p = q + 1;
            }
        }
    }
    ::LocalFree(argv);
    return o;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    ::SetUnhandledExceptionFilter(write_crash_report);
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
    io.IniFilename = nullptr;  // layout lives in agent-host (ui.state), settings in ui.json
    ui::adapter::disable_nav_windowing();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_gfx.device, g_gfx.context);

    ui::HostClientOptions hopts;
    hopts.host_exe = ipc::sibling_executable(L"agent-host.exe");
    hopts.pipe_name = dev.pipe.empty() ? ipc::default_pipe_name() : dev.pipe;
    hopts.wake = [] { ::SetEvent(g_wake); };
    agentdesk::desktop::AppConfig cfg;
    cfg.hwnd = hwnd;
    cfg.wake = [] { ::SetEvent(g_wake); };
    cfg.start_profile = dev.start_profile;
    cfg.run_commands = dev.run;
    cfg.automated = dev.exit_after_ms > 0;
    agentdesk::desktop::App app(std::move(cfg), std::make_unique<ui::HostClient>(hopts));
    app.load_fonts();
    app.apply_scale(ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd));

    const auto launched = Clock::now();
    bool running = true;
    bool exit_now = false;
    int frames_to_draw = 3;
    while (running) {
        if (dev.exit_after_ms > 0 && Clock::now() - launched > std::chrono::milliseconds(dev.exit_after_ms))
            exit_now = true;
        if (g_close_requested.exchange(false))
            app.on_close_requested();
        if (const int f = g_focus_change.exchange(-1); f >= 0)
            app.on_focus_changed(f == 1);
        if (exit_now || app.exit_requested())
            break;

        // Event-driven: sleep until input, an IPC wake-up or the app's next deadline.
        DWORD timeout =
            frames_to_draw > 0 ? 0 : static_cast<DWORD>(std::clamp<long long>(app.next_wakeup_ms(), 0, 1000));
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
            app.apply_scale(g_pending_dpi);
            g_pending_dpi = 0.0f;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.frame();
        ImGui::Render();
        const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        const float clear[4] = {bg.x, bg.y, bg.z, 1.0f};
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
        std::ofstream(dev.report, std::ios::binary) << app.report().dump(2) << '\n';
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroy_device();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, instance);
    ::CloseHandle(g_wake);
    return 0;
}
