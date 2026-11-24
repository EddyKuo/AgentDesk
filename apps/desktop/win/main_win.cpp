// agent-ui.exe: Win32 + DX11 + Dear ImGui front end of agent-host.exe (design doc 4).
// The UI holds no process or ConPTY: every session lives in the host. Closing the window only
// detaches; "Exit and stop all sessions" is the explicit way to end the host (design doc 4.3).
// This file is the platform shell; the window contents are in app.cpp and the files that share
// app_internal.hpp.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

#include <functional>
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
#include "agentdesk/ui/telemetry.hpp"
#include "agentdesk/ui/updates.hpp"
#include "agentdesk/ui/window_place.hpp"
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
    IDXGISwapChain1* swap = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    UINT pending_w = 0;
    UINT pending_h = 0;
    bool occluded = false;
    DXGI_RGBA fill{};
    bool fill_set = false;
} g_gfx;

HANDLE g_wake = nullptr;  // set by IPC threads; the UI loop wakes and redraws
// Dragging a window edge runs Windows' own modal size loop, where the main loop does not turn:
// without a frame at each new size, DWM stretches the last one (text and pictures warp until the
// mouse is released). WM_SIZE draws one through g_draw_now while that loop runs.
std::function<void()> g_draw_now;
bool g_sizing = false;   // between WM_ENTERSIZEMOVE and WM_EXITSIZEMOVE
bool g_drawing = false;  // a frame is being built (WM_SIZE can arrive from inside one)
float g_pending_dpi = 0.0f;
std::atomic<bool> g_close_requested{false};
std::atomic<int> g_focus_change{-1};  // -1 none, 0 lost, 1 gained

// Windows takes Ctrl+Esc (Start) and Ctrl+Shift+Esc (Task Manager) before any window sees them.
// While AgentDesk is in front and keys.json binds the chord (its layout section or global), a low-level
// keyboard hook keeps it from the shell and hands it to the app. Anywhere else Windows keeps them.
HWND g_main_hwnd = nullptr;
// The window's last rect while neither maximized nor minimized: what window.json keeps.
ui::window_place::Rect g_normal_rect;
std::atomic<bool> g_take_ctrl_esc{false};
std::atomic<bool> g_take_ctrl_shift_esc{false};
std::atomic<int> g_reserved_chord{0};  // 1 Ctrl+Esc, 2 Ctrl+Shift+Esc, waiting for the next frame
bool g_swallow_esc_up = false;

LRESULT CALLBACK reserved_keys_hook(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);  // Win32 contract
        if (k->vkCode == VK_ESCAPE) {
            const bool down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
            if (!down && g_swallow_esc_up) {
                g_swallow_esc_up = false;
                return 1;
            }
            const bool ctrl = (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            if (down && ctrl && g_main_hwnd != nullptr && ::GetForegroundWindow() == g_main_hwnd &&
                (shift ? g_take_ctrl_shift_esc.load() : g_take_ctrl_esc.load())) {
                g_reserved_chord = shift ? 2 : 1;
                g_swallow_esc_up = true;
                ::PostMessageW(g_main_hwnd, WM_NULL, 0, 0);  // wake the loop
                return 1;
            }
        }
    }
    return ::CallNextHookEx(nullptr, code, wparam, lparam);
}
std::vector<std::string> g_dropped;  // files dropped on the window (UTF-8), for the app

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

void destroy_device();

bool create_swap_chain(HWND hwnd) {
    // DXGI_SCALING_NONE: while a window edge is dragged, DWM shows the last frame at its own size
    // (top-left aligned) and fills the uncovered strip with the background colour, instead of
    // stretching it to the new size until the next frame lands. Flip model only (DXGI_SCALING docs).
    IDXGIDevice* dxgi_device = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    HRESULT hr = g_gfx.device->QueryInterface(IID_PPV_ARGS(&dxgi_device));
    if (SUCCEEDED(hr))
        hr = dxgi_device->GetAdapter(&adapter);
    if (SUCCEEDED(hr))
        hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) {
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.Scaling = DXGI_SCALING_NONE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        hr = factory->CreateSwapChainForHwnd(g_gfx.device, hwnd, &sd, nullptr, nullptr, &g_gfx.swap);
        if (FAILED(hr)) {  // some drivers refuse NONE: fall back to the stretching default
            sd.Scaling = DXGI_SCALING_STRETCH;
            hr = factory->CreateSwapChainForHwnd(g_gfx.device, hwnd, &sd, nullptr, nullptr, &g_gfx.swap);
        }
        if (SUCCEEDED(hr))
            factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    }
    if (factory)
        factory->Release();
    if (adapter)
        adapter->Release();
    if (dxgi_device)
        dxgi_device->Release();
    return SUCCEEDED(hr);
}

bool create_device(HWND hwnd) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                   D3D11_SDK_VERSION, &g_gfx.device, &got, &g_gfx.context);
    if (hr == DXGI_ERROR_UNSUPPORTED) {  // no hardware device: WARP software rasterizer
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                               &g_gfx.device, &got, &g_gfx.context);
    }
    if (FAILED(hr) || !create_swap_chain(hwnd)) {
        destroy_device();
        return false;
    }
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

void note_normal_rect(HWND hwnd) {
    RECT r{};
    if (::IsZoomed(hwnd) == FALSE && ::IsIconic(hwnd) == FALSE && ::GetWindowRect(hwnd, &r) != FALSE)
        g_normal_rect = {r.left, r.top, r.right - r.left, r.bottom - r.top};
}

LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
        return 1;
    switch (msg) {
        case WM_NCCALCSIZE:
            // No system title bar: the layout draws its own (window buttons in the tab bar).
            // The whole window is client area; when maximized, keep it inside the
            // monitor (a maximized window extends past the screen by the frame thickness).
            if (wparam == TRUE) {
                auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);  // Win32 contract
                if (::IsZoomed(hwnd)) {
                    const UINT dpi = ::GetDpiForWindow(hwnd);
                    const int f = ::GetSystemMetricsForDpi(SM_CXFRAME, dpi) +
                                  ::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    p->rgrc[0].left += f;
                    p->rgrc[0].top += f;
                    p->rgrc[0].right -= f;
                    p->rgrc[0].bottom -= f;
                }
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            // Resize edges stay native (snap, Aero shake); everything else is ours.
            if (::IsZoomed(hwnd))
                return HTCLIENT;
            POINT pt{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
            RECT wr{};
            ::GetWindowRect(hwnd, &wr);
            const int e = ::MulDiv(6, static_cast<int>(::GetDpiForWindow(hwnd)), 96);
            const bool l = pt.x < wr.left + e, r = pt.x >= wr.right - e;
            const bool t = pt.y < wr.top + e, b = pt.y >= wr.bottom - e;
            if (t && l)
                return HTTOPLEFT;
            if (t && r)
                return HTTOPRIGHT;
            if (b && l)
                return HTBOTTOMLEFT;
            if (b && r)
                return HTBOTTOMRIGHT;
            if (l)
                return HTLEFT;
            if (r)
                return HTRIGHT;
            if (t)
                return HTTOP;
            if (b)
                return HTBOTTOM;
            return HTCLIENT;
        }
        case WM_MOVE:
            note_normal_rect(hwnd);
            return 0;
        case WM_SIZE:
            note_normal_rect(hwnd);
            if (wparam != SIZE_MINIMIZED) {
                g_gfx.pending_w = LOWORD(lparam);
                g_gfx.pending_h = HIWORD(lparam);
                if (g_sizing && g_draw_now && !g_drawing)
                    g_draw_now();  // a frame at the new size now, not a stretched old one
            }
            return 0;
        case WM_ENTERSIZEMOVE:
            g_sizing = true;
            return 0;
        case WM_EXITSIZEMOVE:
            g_sizing = false;
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
        case WM_DROPFILES: {
            // Files dropped from Explorer: the terminal under the pointer types their paths in.
            auto* drop = reinterpret_cast<HDROP>(wparam);  // Win32 contract for this message
            const UINT n = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; ++i) {
                std::wstring path(::DragQueryFileW(drop, i, nullptr, 0), L'\0');
                ::DragQueryFileW(drop, i, path.data(), static_cast<UINT>(path.size() + 1));
                try {
                    if (!path.empty())
                        g_dropped.push_back(agentdesk::platform::win::to_utf8(path));
                } catch (const std::exception&) {
                }
            }
            ::DragFinish(drop);
            return 0;
        }
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
    std::string pipe;
    std::vector<std::string> run;
    std::string input;
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
            o.pipe = win::to_utf8(argv[++i]);
        else if (a == L"--input" && i + 1 < argc)
            o.input = win::to_utf8(argv[++i]);
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

// `agent-ui --version` prints "agent-ui <version>" on the stdout it was given, before any window:
// the updater asks a downloaded copy this way. True when that was the request.
bool answered_version() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    const bool version = argv != nullptr && argc == 2 && std::wstring(argv[1]) == L"--version";
    ::LocalFree(argv);
    if (!version)
        return false;
    const std::string line = std::string("agent-ui ") + AGENTDESK_VERSION_STRING + "\n";
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), line.data(), static_cast<DWORD>(line.size()), &written,
                nullptr);
    return true;
}

// The updater works on this executable and the data folder; it wakes the loop when it has news.
void configure_updates() {
    wchar_t exe[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    ui::updates::configure({AGENTDESK_VERSION_STRING, std::filesystem::path(std::wstring(exe, n)),
                            agentdesk::desktop::data_folder(), [] {
                                if (g_wake != nullptr)
                                    ::SetEvent(g_wake);
                            }});
}

// No system caption (see WM_NCCALCSIZE): a 1 px DWM frame keeps the shadow and the rounded
// corners, and the title bar Windows would draw is dark to match the chrome.
void use_custom_frame(HWND hwnd) {
    const BOOL dark = TRUE;
    ::DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));  // DWMWA_USE_IMMERSIVE_DARK_MODE
    const MARGINS m{0, 0, 1, 0};
    ::DwmExtendFrameIntoClientArea(hwnd, &m);
    ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
}

// Where the window opens. Automated runs (--exit-after-ms) keep a fixed 1400x900 at (100, 100):
// the screenshot checks compare against that size, and they must not read or write window.json.
ui::window_place::Place start_place(bool automated) {
    namespace place = ui::window_place;
    if (automated)
        return {{100, 100, 1400, 900}, false};
    std::size_t under_cursor = 0;
    const auto monitors = place::monitors(&under_cursor);
    return place::restore(place::load(agentdesk::desktop::data_folder() / L"window.json"), monitors,
                          under_cursor, agentdesk::desktop::saved_zoom());
}

// Keeps the window's place for the next start.
void save_place(HWND hwnd) {
    namespace place = ui::window_place;
    if (g_normal_rect.w <= 0 || g_normal_rect.h <= 0)
        return;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    ::GetWindowPlacement(hwnd, &wp);
    // Minimized from maximized: it reopens maximized.
    const bool maximized =
        ::IsZoomed(hwnd) != FALSE || (::IsIconic(hwnd) != FALSE && (wp.flags & WPF_RESTORETOMAXIMIZED) != 0);
    const auto monitors = place::monitors();
    place::save(agentdesk::desktop::data_folder() / L"window.json",
                place::capture(g_normal_rect, maximized, place::monitor_for(g_normal_rect, monitors)));
}

agentdesk::desktop::AppConfig app_config(const DevOptions& dev, HWND hwnd) {
    agentdesk::desktop::AppConfig cfg;
    cfg.window = hwnd;
    cfg.wake = [] { ::SetEvent(g_wake); };
    cfg.start_profile = dev.start_profile;
    cfg.run_commands = dev.run;
    cfg.input_script = dev.input;
    if (!dev.report.empty())
        cfg.shot_signal = std::filesystem::path(dev.report).concat(L".shot");
    cfg.automated = dev.exit_after_ms > 0;
    return cfg;
}

// Installs reserved_keys_hook while keys.json binds Ctrl+Esc or Ctrl+Shift+Esc, removes it
// otherwise. A low-level hook runs on this thread: while it is installed, every key anywhere in
// Windows waits for this loop, so a UI thread that is busy (a long git job) would hold up the
// keyboard; hence only while a binding wants it.
void sync_reserved_hook(const agentdesk::desktop::App& app, HINSTANCE instance, HHOOK& hook) {
    g_take_ctrl_esc = app.binds_chord(ImGuiMod_Ctrl | ImGuiKey_Escape);
    g_take_ctrl_shift_esc = app.binds_chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Escape);
    if (const bool want_hook = g_take_ctrl_esc || g_take_ctrl_shift_esc; want_hook && hook == nullptr) {
        hook = ::SetWindowsHookExW(WH_KEYBOARD_LL, reserved_keys_hook, instance, 0);
    } else if (!want_hook && hook != nullptr) {
        ::UnhookWindowsHookEx(hook);
        hook = nullptr;
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (answered_version())
        return 0;
    ::SetUnhandledExceptionFilter(write_crash_report);
    configure_updates();
    // Leave the install folder, put a waiting update in place (that starts the new copy and this
    // one ends), then start the clock for telemetry.
    ui::updates::leave_install_folder();
    if (ui::updates::apply_on_startup())
        return 0;
    ui::telemetry::clock();
    const DevOptions dev = parse_dev_options();
    g_wake = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, wnd_proc,       0,      0, instance, nullptr, nullptr,
                   nullptr,    nullptr,    L"AgentDeskUi", nullptr};
    ::RegisterClassExW(&wc);
    const bool automated = dev.exit_after_ms > 0;
    const ui::window_place::Place place = start_place(automated);
    const auto& pr = place.rect;
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"AgentDesk", WS_OVERLAPPEDWINDOW, pr.x, pr.y, pr.w, pr.h,
                                nullptr, nullptr, instance, nullptr);
    g_normal_rect = pr;
    g_main_hwnd = hwnd;
    HHOOK reserved_hook = nullptr;  // see sync_reserved_hook
    if (!create_device(hwnd)) {
        destroy_device();
        ::UnregisterClassW(wc.lpszClassName, instance);
        ::MessageBoxW(nullptr, L"Direct3D 11 is not available", L"AgentDesk", MB_ICONERROR);
        return 1;
    }
    use_custom_frame(hwnd);
    ::DragAcceptFiles(hwnd, TRUE);
    ::ShowWindow(hwnd, place.maximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // layout lives in agent-host (ui.state), settings in ui.json
    ui::adapter::disable_nav_windowing();
    ImGui_ImplWin32_Init(hwnd);
    bool dx11_backend = ImGui_ImplDX11_Init(g_gfx.device, g_gfx.context);

    ui::HostClientOptions hopts;
    hopts.host_exe = ipc::sibling_executable("agent-host");
    hopts.pipe_name = dev.pipe.empty() ? ipc::default_pipe_name() : dev.pipe;
    ui::updates::set_pipe(dev.pipe);  // agent-host's update goes to the host this window uses
    hopts.wake = [] { ::SetEvent(g_wake); };
    agentdesk::desktop::App app(app_config(dev, hwnd), std::make_unique<ui::HostClient>(hopts));
    app.load_fonts();
    app.apply_scale(ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd));

    const auto launched = Clock::now();
    bool running = true;
    bool exit_now = false;
    int frames_to_draw = 3;
    auto last_tick = Clock::time_point{};
    // One frame at the window's current size. A lost device whose re-creation fails ends the
    // loop (running = false).
    auto draw_frame = [&] {
        g_drawing = true;
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
        app.before_new_frame();  // --input keeps its mouse position over the backend's
        ImGui::NewFrame();
        app.frame();
        ImGui::Render();
        const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        const float clear[4] = {bg.x, bg.y, bg.z, 1.0f};
        // The colour DWM fills the uncovered strip with while the window grows (DXGI_SCALING_NONE).
        if (const DXGI_RGBA fill{bg.x, bg.y, bg.z, 1.0f};
            fill.r != g_gfx.fill.r || fill.g != g_gfx.fill.g || fill.b != g_gfx.fill.b || !g_gfx.fill_set) {
            g_gfx.swap->SetBackgroundColor(&fill);
            g_gfx.fill = fill;
            g_gfx.fill_set = true;
        }
        g_gfx.context->OMSetRenderTargets(1, &g_gfx.rtv, nullptr);
        g_gfx.context->ClearRenderTargetView(g_gfx.rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const HRESULT hr = g_gfx.swap->Present(1, 0);
        g_gfx.occluded = hr == DXGI_STATUS_OCCLUDED;
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            ImGui_ImplDX11_Shutdown();
            dx11_backend = false;
            destroy_device();
            if (!create_device(hwnd) || !(dx11_backend = ImGui_ImplDX11_Init(g_gfx.device, g_gfx.context)))
                running = false;
        }
        g_drawing = false;
    };
    g_draw_now = [&] {
        if (running && dx11_backend)
            draw_frame();
    };
    while (running) {
        if (dev.exit_after_ms > 0 && Clock::now() - launched > std::chrono::milliseconds(dev.exit_after_ms))
            exit_now = true;
        if (g_close_requested.exchange(false))
            app.on_close_requested();
        if (const int f = g_focus_change.exchange(-1); f >= 0)
            app.on_focus_changed(f == 1);
        sync_reserved_hook(app, instance, reserved_hook);
        if (const int r = g_reserved_chord.exchange(0); r != 0)  // the bound command, as its window chord
            app.run_chord(r == 2 ? (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Escape)
                                 : (ImGuiMod_Ctrl | ImGuiKey_Escape));
        if (!g_dropped.empty())
            app.on_files_dropped(std::exchange(g_dropped, {}));
        if (exit_now || app.exit_requested())
            break;

        // Event-driven: sleep until input, an IPC wake-up or the app's next deadline.
        DWORD timeout =
            frames_to_draw > 0 ? 0 : static_cast<DWORD>(std::clamp<long long>(app.next_wakeup_ms(), 0, 1000));
        if (g_gfx.occluded)  // nothing is drawn: the non-drawing work needs no tighter cadence
            timeout = std::max<DWORD>(timeout, 100);
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
        // Sessions, attention (sounds, toasts, taskbar flash), mods and persistence run on every
        // wake, drawn or not: minimized or locked is exactly when an alert matters. Occluded,
        // a burst of IPC wake-ups runs it at most every 100 ms.
        if (const auto now = Clock::now();
            !g_gfx.occluded || now - last_tick >= std::chrono::milliseconds(100)) {
            last_tick = now;
            app.tick();
        }
        if (frames_to_draw <= 0)
            continue;

        if (g_gfx.occluded && g_gfx.swap->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            frames_to_draw = 0;
            continue;
        }
        g_gfx.occluded = false;
        draw_frame();
        if (!running)
            break;
        --frames_to_draw;
    }
    g_draw_now = nullptr;
    if (!automated)
        save_place(hwnd);

    app.on_quit();
    if (!dev.report.empty())
        std::ofstream(dev.report, std::ios::binary) << app.report().dump(2) << '\n';
    if (dx11_backend)  // a lost device whose re-creation failed has shut it down already
        ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroy_device();
    if (reserved_hook != nullptr)
        ::UnhookWindowsHookEx(reserved_hook);
    g_main_hwnd = nullptr;
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, instance);
    // g_wake stays open: `app` (and its IPC threads, which may still signal it) is destroyed only
    // after this return; Windows closes the handle when the process ends.
    return 0;
}
