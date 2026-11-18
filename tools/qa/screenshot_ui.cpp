// screenshot_ui: captures agent-ui.exe running against an isolated host, with one session that
// prints mixed Chinese/ASCII text. Used to check rendering (fonts, CJK widths) without a human.
//   screenshot_ui --out <png> [--build-dir <dir>] [--profile chinese|claude] [--wait-seconds 6]
//                 [--run <command>] [--child profile:name] [--remote <machine>] [--mods a,b]
//                 [--input "<step>;<step>..."]
// --input is passed to agent-ui, which feeds the steps to ImGui as a person's mouse and keys
// (nothing outside its window is touched): click:x,y rclick:x,y mclick:x,y dblclick:x,y
// press:x,y release:x,y (a left-button drag, with move steps between) move:x,y key:<enter|esc|tab|up|down|left|right|home|end|pgup|pgdn|back|del|space|f2|a-z|0-9>
// type:<text> wait:<ms> shot:<png> (a capture, relative to --out's folder). The final capture
// to --out is taken after the last step.
// --child starts profile:name as a sub-agent of the first session; --remote adds a second host
// shown as that machine (pipe: target); --mods enables mods (example mods are copied on first run).
// The PowerShell spellings (-Out, -WaitSeconds ...) are accepted too. agent-ui writes its
// --report next to the PNG (same name, .json). Environment: SHOT_CTL_HOST (start the host with
// agentctl first), SHOT_EXTRA ("profile:name,..." sessions created before the UI starts),
// SHOT_DEBUG (also save the first session's last lines and host.log next to the PNG).
#include <windows.h>
// wincodec.h needs windows.h first.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <wincodec.h>

#include "devkit.hpp"

namespace dk = agentdesk::devkit;
namespace fs = dk::fs;
using dk::Json;

namespace {

std::vector<std::wstring> split(const std::wstring& text, wchar_t sep) {
    std::vector<std::wstring> out;
    std::wstringstream in(text);
    std::wstring item;
    while (std::getline(in, item, sep))
        out.push_back(item);
    return out;
}

std::optional<std::wstring> env(const wchar_t* name) {
    const DWORD n = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (n <= 1)
        return std::nullopt;
    std::wstring v(n, L'\0');
    v.resize(::GetEnvironmentVariableW(name, v.data(), n));
    return v;
}

struct FindWindow {
    DWORD pid;
    HWND found;
};

// Process.MainWindowHandle: the first visible, unowned top-level window of the process.
HWND main_window(DWORD pid) {
    FindWindow f{pid, nullptr};
    ::EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto* f = reinterpret_cast<FindWindow*>(lp);
            DWORD owner = 0;
            ::GetWindowThreadProcessId(h, &owner);
            if (owner == f->pid && ::IsWindowVisible(h) && ::GetWindow(h, GW_OWNER) == nullptr) {
                f->found = h;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&f));
    return f.found;
}

template <typename T>
struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() {
        if (p)
            p->Release();
    }
    T** put() { return &p; }
    T* operator->() const { return p; }
};

void check(HRESULT hr, const char* what) {
    if (FAILED(hr))
        throw std::runtime_error(std::string(what) + " failed");
}

// PrintWindow with PW_RENDERFULLCONTENT works when the window is covered, and for DX content.
void capture_png(HWND window, const fs::path& out, int& width, int& height) {
    RECT r{};
    ::GetWindowRect(window, &r);
    width = r.right - r.left;
    height = r.bottom - r.top;
    if (width <= 0 || height <= 0)
        throw std::runtime_error("window has no size");
    HDC screen = ::GetDC(nullptr);
    HDC mem = ::CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    constexpr UINT kPwRenderFullContent = 2;
    ::PrintWindow(window, mem, kPwRenderFullContent);
    ::GdiFlush();

    try {
        ComPtr<IWICImagingFactory> factory;
        check(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(factory.put())),
              "WIC factory");
        const UINT stride = static_cast<UINT>(width) * 4;
        ComPtr<IWICBitmap> source;
        check(factory->CreateBitmapFromMemory(
                  static_cast<UINT>(width), static_cast<UINT>(height), GUID_WICPixelFormat32bppBGR, stride,
                  stride * static_cast<UINT>(height), static_cast<BYTE*>(bits), source.put()),
              "CreateBitmapFromMemory");
        ComPtr<IWICStream> stream;
        check(factory->CreateStream(stream.put()), "CreateStream");
        check(stream->InitializeFromFilename(out.c_str(), GENERIC_WRITE), "open png");
        ComPtr<IWICBitmapEncoder> encoder;
        check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()), "PNG encoder");
        check(encoder->Initialize(stream.p, WICBitmapEncoderNoCache), "encoder init");
        ComPtr<IWICBitmapFrameEncode> frame;
        check(encoder->CreateNewFrame(frame.put(), nullptr), "CreateNewFrame");
        check(frame->Initialize(nullptr), "frame init");
        check(frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height)), "SetSize");
        WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
        check(frame->SetPixelFormat(&format), "SetPixelFormat");
        check(frame->WriteSource(source.p, nullptr), "WriteSource");
        check(frame->Commit(), "frame commit");
        check(encoder->Commit(), "encoder commit");
    } catch (...) {
        ::SelectObject(mem, old);
        ::DeleteObject(bmp);
        ::DeleteDC(mem);
        ::ReleaseDC(nullptr, screen);
        throw;
    }
    ::SelectObject(mem, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);
}

std::wstring search_path(const wchar_t* exe) {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::SearchPathW(nullptr, exe, nullptr, MAX_PATH, buf, nullptr);
    return n > 0 && n < MAX_PATH ? std::wstring(buf, n) : std::wstring();
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const dk::Args args(argc, argv);
        std::wstring out_arg = args.get_or(L"out", args.positional().empty() ? L"" : args.positional()[0]);
        if (out_arg.empty()) {
            std::fprintf(stderr,
                         "usage: screenshot_ui --out <png> [--build-dir <dir>] [--profile chinese|claude] "
                         "[--wait-seconds 6] [--run <cmd>] [--child profile:name] [--remote <machine>] "
                         "[--mods a,b]\n");
            return 2;
        }
        const fs::path out = fs::absolute(out_arg);
        const fs::path root = dk::source_root();
        const fs::path build_dir = args.path_or(L"build-dir", (root / L"build" / L"msvc-x64-debug").native());
        const std::wstring profile = args.get_or(L"profile", L"chinese");
        const int wait_seconds = args.get_int(L"wait-seconds", 6);
        const std::wstring run_cmd = args.get_or(L"run", L"");
        const std::wstring child = args.get_or(L"child", L"");
        const std::wstring remote = args.get_or(L"remote", L"");
        const std::wstring mods = args.get_or(L"mods", L"");
        const std::wstring input = args.get_or(L"input", L"");

        ::SetProcessDPIAware();
        check(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "CoInitializeEx");
        const fs::path dir = build_dir / L"apps" / L"desktop";
        const fs::path ctl = dir / L"agentctl.exe";
        const auto cmd = dk::u8(dk::system_dir() / L"cmd.exe");
        const fs::path data = dk::temp_dir() / (L"agentdesk-shot-" + dk::pid_string());
        fs::create_directories(data);
        const std::wstring temp_cwd = dk::temp_dir().native() + L"\\";

        Json profiles = Json::array(
            {{{"id", "chinese"}, {"name", "中文測試"}, {"executable", cmd}, {"args", Json::array()}}});
        const auto claude = search_path(L"claude.exe");
        if (!claude.empty()) {
            profiles.push_back({{"id", "claude"},
                                {"name", "Claude Code"},
                                {"executable", dk::u8(claude)},
                                {"args", Json::array()}});
        }
        const auto ps = dk::u8(dk::system_dir() / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe");
        const std::string symbols =
            "[Console]::OutputEncoding = [Text.Encoding]::UTF8; "
            "'中文測試：寬字元 ABC 混排，全形標點。'; "
            "'symbols: ' + [char]0x26A0 + ' ' + [char]0x2387 + ' ' + [char]0x2713 + ' ' + [char]0x23F5 + "
            "[char]0x23F5 + ' ' + [char]0x25D0 + ' ' + [char]::ConvertFromUtf32(0x1F500) + ' ' + "
            "[char]::ConvertFromUtf32(0x1F9E0); "
            "'box: ' + [char]0x250C + [string][char]0x2500 * 6 + [char]0x2510 + ' ' + [char]0x2588 + "
            "[char]0x2593 + "
            "[char]0x2592 + [char]0x2591";
        profiles.push_back({{"id", "symbols"},
                            {"name", "symbols"},
                            {"executable", ps},
                            {"args", {"-NoLogo", "-NoProfile", "-NoExit", "-Command", symbols}}});
        profiles.push_back({{"id", "powershell"},
                            {"name", "Windows PowerShell"},
                            {"executable", ps},
                            {"args", {"-NoLogo", "-NoProfile"}}});
        // Shells that report an agent state over OSC 7501 and stay open: "state-working",
        // "state-blocked", "state-idle", "state-done" (sidebar agents list checks).
        for (const char* st : {"working", "blocked", "idle", "done"}) {
            profiles.push_back(
                {{"id", std::string("state-") + st},
                 {"name", std::string("state ") + st},
                 {"executable", ps},
                 {"args",
                  {"-NoLogo", "-NoProfile", "-NoExit", "-Command",
                   std::string("[Console]::Write([char]27 + ']7501;state=") + st + "' + [char]7)"}}});
        }
        dk::write_file(data / L"profiles.json", dk::profiles_json(profiles));
        ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", data.c_str());

        std::optional<Json> ui_doc;
        const fs::path data2 = data.native() + L"-remote";
        const std::wstring pipe2 = L"\\\\.\\pipe\\AgentDeskShotRemote-" + dk::pid_string();
        if (!remote.empty()) {
            fs::create_directories(data2);
            fs::copy_file(data / L"profiles.json", data2 / L"profiles.json",
                          fs::copy_options::overwrite_existing);
            ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", data2.c_str());
            dk::run(ctl, {L"--pipe", pipe2, L"run", L"--profile", L"chinese", L"--name", L"build-agent",
                          L"--cwd", data2.native()});
            ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", data.c_str());
            ui_doc =
                Json{{"schema_version", 1},
                     {"machines",
                      Json::array(
                          {{{"name", dk::u8(remote)}, {"target", "pipe:" + dk::u8(pipe2)}, {"ctl", ""}}})}};
        }
        if (!mods.empty()) {
            if (!ui_doc)
                ui_doc = Json{{"schema_version", 1}};
            Json enabled = Json::array();
            for (const auto& m : split(mods, L','))
                enabled.push_back(dk::u8(m));
            (*ui_doc)["mods_enabled"] = enabled;
        }
        if (ui_doc)
            dk::write_file(data / L"ui.json", ui_doc->dump(2));

        const std::wstring pipe = L"\\\\.\\pipe\\AgentDeskShot-" + dk::pid_string();
        const fs::path ui = dir / L"agent-ui.exe";
        fs::path report = out;
        report.replace_extension(L".json");
        if (env(L"SHOT_CTL_HOST"))
            dk::run(ctl, {L"--pipe", pipe, L"list"});
        if (const auto extra = env(L"SHOT_EXTRA")) {  // "profile:name,profile:name"
            for (const auto& e : split(*extra, L',')) {
                const auto pn = split(e, L':');
                if (pn.size() >= 2)
                    dk::run(ctl, {L"--pipe", pipe, L"run", L"--profile", pn[0], L"--name", pn[1], L"--cwd",
                                  temp_cwd});
            }
        }

        int width = 0;
        int height = 0;
        std::optional<dk::Started> p;
        int rc = 0;
        try {
            std::vector<std::wstring> ui_args = {
                L"--pipe",          pipe,
                L"--start",         profile,
                L"--exit-after-ms", std::to_wstring((wait_seconds + 1) * 1000),
                L"--report",        report.native()};
            if (!run_cmd.empty()) {
                ui_args.push_back(L"--run");
                ui_args.push_back(run_cmd);
            }
            if (!input.empty()) {
                // Steps time themselves; agent-ui stays open long enough (60 s at most) and the
                // last step is the final capture.
                ui_args[5] = std::to_wstring((wait_seconds + 60) * 1000);
                ui_args.push_back(L"--input");
                ui_args.push_back(input + L";wait:300;shot:" + out.native() + L";wait:200");
            }
            p = dk::start(ui, ui_args, dk::exe_dir());
            if (!child.empty()) {
                dk::sleep_ms(4000);
                const Json list = Json::parse(dk::run(ctl, {L"--pipe", pipe, L"list", L"--json"}).out);
                const auto first = list.at("sessions").at(0).at("id").get<std::string>();
                const auto pn = split(child, L':');
                if (pn.size() < 2)
                    throw std::invalid_argument("--child expects profile:name");
                dk::run(ctl, {L"--pipe", pipe, L"run", L"--profile", pn[0], L"--name", pn[1], L"--cwd",
                              temp_cwd, L"--parent", dk::wide(first)});
                dk::sleep_ms(std::max(1, wait_seconds - 4) * 1000);
            } else {
                dk::sleep_ms(wait_seconds * 1000);
            }
            const HWND h = main_window(p->pid);
            if (h == nullptr)
                throw std::runtime_error("agent-ui has no window");
            ::SetForegroundWindow(h);
            dk::sleep_ms(500);
            if (input.empty()) {
                capture_png(h, out, width, height);
            } else {
                // Capture whenever agent-ui asks (it writes the target path to <report>.shot and
                // waits for the file to go away); done after the final capture to --out.
                fs::path signal = report;
                signal += L".shot";
                const auto deadline =
                    std::chrono::steady_clock::now() + std::chrono::seconds(wait_seconds + 60);
                bool final_done = false;
                while (!final_done && std::chrono::steady_clock::now() < deadline) {
                    std::error_code ec;
                    if (fs::exists(signal, ec)) {
                        dk::sleep_ms(150);  // the frame after the step has been presented
                        fs::path target = dk::wide(dk::read_file(signal));
                        if (target.is_relative())
                            target = out.parent_path() / target;
                        capture_png(h, target, width, height);
                        final_done = fs::equivalent(target, out, ec) || target == out;
                        fs::remove(signal, ec);
                    }
                    dk::sleep_ms(50);
                }
                if (!final_done)
                    throw std::runtime_error("agent-ui did not finish the --input steps");
            }
            dk::wait_exit(*p, std::chrono::milliseconds(15000));
            if (env(L"SHOT_DEBUG")) {
                const Json list = Json::parse(dk::run(ctl, {L"--pipe", pipe, L"list", L"--json"}).out);
                const auto sid = dk::wide(list.at("sessions").at(0).at("id").get<std::string>());
                fs::path read_bin = out;
                read_bin.replace_extension(L".read.bin");
                dk::write_file(read_bin, dk::run(ctl, {L"--pipe", pipe, L"read", sid, L"--lines", L"3"}).out);
                fs::path host_log = out;
                host_log.replace_extension(L".host.log");
                fs::copy_file(data / L"host.log", host_log, fs::copy_options::overwrite_existing);
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "screenshot_ui: %s\n", e.what());
            rc = 1;
        }
        if (p)
            dk::kill(*p);
        dk::run(ctl, {L"--pipe", pipe, L"shutdown", L"--stop-sessions"});
        if (!remote.empty()) {
            dk::run(ctl, {L"--pipe", pipe2, L"shutdown", L"--stop-sessions"});
            dk::remove_tree(data2);
        }
        dk::sleep_ms(1000);
        dk::remove_tree(data);
        ::CoUninitialize();
        if (rc == 0)
            std::printf("saved %s (%d x %d)\n", dk::u8(out).c_str(), width, height);
        return rc;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "screenshot_ui: %s\n", e.what());
        return 1;
    }
}
