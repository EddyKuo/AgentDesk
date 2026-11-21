// screenshot_ui: captures agent-ui.exe running against an isolated host, with one session that
// prints mixed Chinese/ASCII text. Used to check rendering (fonts, CJK widths) without a human.
//   screenshot_ui --out <png> [--build-dir <dir>] [--profile chinese|claude] [--wait-seconds 6]
//                 [--run <command>] [--child profile:name] [--remote <machine>] [--mods a,b]
//                 [--input "<step>;<step>..."] [--layout <file.json>]
// --layout copies a layout file into the isolated data folder's layouts\ (show it with
// --run layout:<name>). --cwd <dir> starts the sessions (the first, SHOT_EXTRA's, --child's) in that
// folder instead of %TEMP% (a test repository for the Review layout). --appearance light|dark
// fixes Settings > Look > Appearance (default: follow Windows).
// --input is passed to agent-ui, which feeds the steps to ImGui as a person's mouse and keys
// (nothing outside its window is touched): click:x,y rclick:x,y mclick:x,y dblclick:x,y
// press:x,y release:x,y (a left-button drag, with move steps between) move:x,y
// key:[ctrl+][shift+][alt+]<enter|esc|tab|up|down|left|right|home|end|pgup|pgdn|back|del|space|
// period|.|f1-f12|a-z|0-9> (key:ctrl+shift+m, key:shift+f8) type:<text> wait:<ms>
// wheel:<notches> (at the last mouse position; negative scrolls down)
// shot:<png> (a capture, relative to --out's folder). The final capture to --out is taken after the last
// step.
// --child starts profile:name as a sub-agent of the first session; --remote adds a second host
// shown as that machine (pipe: target); --mods enables mods (example mods are copied on first run).
// The PowerShell spellings (-Out, -WaitSeconds ...) are accepted too. agent-ui writes its
// --report next to the PNG (same name, .json). Environment: SHOT_CTL_HOST (start the host with
// agentctl first), SHOT_EXTRA ("profile:name,..." sessions created before the UI starts),
// SHOT_DEBUG (also save the first session's last lines and host.log next to the PNG),
// SHOT_APPEARANCE ("light" or "dark" instead of the system's).
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
#include <tuple>
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

// A top-down 32-bit DIB selected into a memory DC; released in reverse order of creation.
class MemoryBitmap {
public:
    MemoryBitmap(int width, int height) : screen_(::GetDC(nullptr)), mem_(::CreateCompatibleDC(screen_)) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = -height;  // negative: rows run top to bottom
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        bmp_ = ::CreateDIBSection(screen_, &bi, DIB_RGB_COLORS, &bits_, nullptr, 0);
        old_ = ::SelectObject(mem_, bmp_);
    }
    MemoryBitmap(const MemoryBitmap&) = delete;
    MemoryBitmap& operator=(const MemoryBitmap&) = delete;
    ~MemoryBitmap() {
        ::SelectObject(mem_, old_);
        ::DeleteObject(bmp_);
        ::DeleteDC(mem_);
        ::ReleaseDC(nullptr, screen_);
    }
    [[nodiscard]] HDC dc() const { return mem_; }
    [[nodiscard]] BYTE* bits() const { return static_cast<BYTE*>(bits_); }

private:
    HDC screen_;
    HDC mem_;
    HBITMAP bmp_ = nullptr;
    HGDIOBJ old_ = nullptr;
    void* bits_ = nullptr;
};

// Encodes 32-bit BGR pixels as a 24-bit PNG through WIC.
void write_png(BYTE* pixels, int width, int height, const fs::path& out) {
    ComPtr<IWICImagingFactory> factory;
    check(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(factory.put())),
          "WIC factory");
    const UINT stride = static_cast<UINT>(width) * 4;
    ComPtr<IWICBitmap> source;
    check(factory->CreateBitmapFromMemory(static_cast<UINT>(width), static_cast<UINT>(height),
                                          GUID_WICPixelFormat32bppBGR, stride,
                                          stride * static_cast<UINT>(height), pixels, source.put()),
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
}

// PrintWindow with PW_RENDERFULLCONTENT works when the window is covered, and for DX content.
void capture_png(HWND window, const fs::path& out, int& width, int& height) {
    RECT r{};
    ::GetWindowRect(window, &r);
    width = r.right - r.left;
    height = r.bottom - r.top;
    if (width <= 0 || height <= 0)
        throw std::runtime_error("window has no size");
    const MemoryBitmap bitmap(width, height);
    constexpr UINT kPwRenderFullContent = 2;
    ::PrintWindow(window, bitmap.dc(), kPwRenderFullContent);
    ::GdiFlush();
    write_png(bitmap.bits(), width, height, out);
}

std::wstring search_path(const wchar_t* exe) {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::SearchPathW(nullptr, exe, nullptr, MAX_PATH, buf, nullptr);
    return n > 0 && n < MAX_PATH ? std::wstring(buf, n) : std::wstring();
}

struct Options {
    fs::path out;
    fs::path build_dir;
    std::wstring profile;
    int wait_seconds = 6;
    std::wstring run_cmd;
    std::wstring child;
    std::wstring remote;
    std::wstring mods;
    std::wstring input;
    std::wstring layout_file;
    std::wstring cwd;
};

// The isolated host's profiles: the Chinese text shell (the default --profile), Claude Code when
// it is on PATH, a symbols/box-drawing sample, plain shells, and one shell per agent state.
Json shot_profiles() {
    const auto cmd = dk::u8(dk::system_dir() / L"cmd.exe");
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
    // Command Prompt, for the shell picker and cmd's prompt hook.
    profiles.push_back(
        {{"id", "cmd"}, {"name", "Command Prompt"}, {"executable", cmd}, {"args", Json::array()}});
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
    // "state-later": working at first, blocked 10 s later (an agent that needs you while the UI is
    // up: Focus's edge glow, Mission Control's ordering).
    profiles.push_back({{"id", "state-later"},
                        {"name", "state later"},
                        {"executable", ps},
                        {"args",
                         {"-NoLogo", "-NoProfile", "-NoExit", "-Command",
                          "[Console]::Write([char]27 + ']7501;state=working' + [char]7); Start-Sleep -Seconds 10; "
                          "[Console]::Write([char]27 + ']7501;state=blocked' + [char]7)"}}});
    // Shells that keep changing state (Timeline checks): "state-cycle-a" works, needs the person,
    // works again and finishes, over and over; "state-cycle-b" the same at another pace.
    for (const auto& [id, steps] :
         {std::pair<const char*, const char*>{"state-cycle-a", "working 18 blocked 10 working 14 done 20"},
          std::pair<const char*, const char*>{"state-cycle-b", "idle 6 working 30 done 8 blocked 6"}}) {
        std::string script = "$s = '" + std::string(steps) +
                             "'.Split(' '); while ($true) { for ($i = 0; $i -lt $s.Length; $i += 2) { "
                             "[Console]::Write([char]27 + ']7501;state=' + $s[$i] + [char]7); "
                             "Start-Sleep -Seconds ([int]$s[$i + 1]) } }";
        profiles.push_back({{"id", id},
                            {"name", id},
                            {"executable", ps},
                            {"args", {"-NoLogo", "-NoProfile", "-Command", script}}});
    }
    // Stand-in agents for the Pair layout: each waits for a line, reports working, prints a few
    // steps, edits README.md and writes a note of its own in the folder, then reports done.
    // "pair-quick" takes about 6 s, "pair-steady" about 11 s.
    for (const auto& [id, name, pause, note] :
         {std::tuple<const char*, const char*, int, const char*>{"pair-quick", "Quick agent", 3, "notes-quick.md"},
          std::tuple<const char*, const char*, int, const char*>{"pair-steady", "Steady agent", 8,
                                                                 "notes-steady.md"}}) {
        const std::string script =
            std::string("$n = 0; while ($true) { $t = Read-Host 'task'; $n++; ") +
            "[Console]::Write([char]27 + ']7501;state=working' + [char]7); " +
            "'Working on: ' + $t; Start-Sleep -Milliseconds 900; '  reading README.md'; " +
            "Start-Sleep -Milliseconds 900; '  editing README.md'; " +
            "Add-Content -Path 'README.md' -Value ('', '## Usage', ('Task ' + $n + ': ' + $t), 'Run the tool " +
            "with --help to see every option.'); " +
            "'  writing " + note + "'; Set-Content -Path '" + note + "' -Value ('# Notes', '', $t, '', '- step " +
            "one', '- step two', '- step three'); Start-Sleep -Seconds " + std::to_string(pause) + "; " +            "'  done: 2 files changed'; [Console]::Write([char]27 + ']7501;state=done' + [char]7) }";
        profiles.push_back({{"id", id},
                            {"name", name},
                            {"executable", ps},
                            {"args", {"-NoLogo", "-NoProfile", "-Command", script}}});
    }
    return profiles;
}

// A second host on its own pipe and data folder, with one session, listed in ui.json as the
// machine `remote` (a "pipe:" target stands in for ssh). Returns the ui.json document.
Json start_remote_host(const fs::path& ctl, const fs::path& data, const fs::path& remote_data,
                       const std::wstring& remote_pipe, const std::wstring& remote) {
    fs::create_directories(remote_data);
    fs::copy_file(data / L"profiles.json", remote_data / L"profiles.json",
                  fs::copy_options::overwrite_existing);
    // agentctl starts the host it needs, which takes its data folder from this variable.
    ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", remote_data.c_str());
    dk::run(ctl, {L"--pipe", remote_pipe, L"run", L"--profile", L"chinese", L"--name", L"build-agent",
                  L"--cwd", remote_data.native()});
    ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", data.c_str());
    return Json{
        {"schema_version", 1},
        {"machines",
         Json::array({{{"name", dk::u8(remote)}, {"target", "pipe:" + dk::u8(remote_pipe)}, {"ctl", ""}}})}};
}

// SHOT_CTL_HOST and SHOT_EXTRA: start the host, and sessions, before agent-ui does.
void prepare_host_from_env(const fs::path& ctl, const std::wstring& pipe, const std::wstring& cwd) {
    if (env(L"SHOT_CTL_HOST"))
        dk::run(ctl, {L"--pipe", pipe, L"list"});
    if (const auto extra = env(L"SHOT_EXTRA")) {  // "profile:name,profile:name"
        for (const auto& e : split(*extra, L',')) {
            const auto pn = split(e, L':');
            if (pn.size() >= 2)
                dk::run(ctl, {L"--pipe", pipe, L"run", L"--profile", pn[0], L"--name", pn[1], L"--cwd", cwd});
        }
    }
}

std::vector<std::wstring> ui_arguments(const Options& o, const std::wstring& pipe, const fs::path& report) {
    // With --input the steps time themselves: agent-ui stays open long enough (60 s at most) and
    // the last step is the final capture.
    const int exit_after_seconds = o.input.empty() ? o.wait_seconds + 1 : o.wait_seconds + 60;
    std::vector<std::wstring> ui_args = {L"--pipe",          pipe,
                                         L"--start",         o.profile,
                                         L"--exit-after-ms", std::to_wstring(exit_after_seconds * 1000),
                                         L"--report",        report.native()};
    // --run "a|b": several commands, in order.
    for (const auto& c : split(o.run_cmd, L'|')) {
        if (!c.empty()) {
            ui_args.push_back(L"--run");
            ui_args.push_back(c);
        }
    }
    if (!o.input.empty()) {
        ui_args.push_back(L"--input");
        ui_args.push_back(o.input + L";wait:300;shot:" + o.out.native() + L";wait:200");
    }
    return ui_args;
}

// --child: once the UI has created its first session, adds profile:name as its sub-agent.
void start_child_session(const fs::path& ctl, const std::wstring& pipe, const std::wstring& child,
                         const std::wstring& cwd) {
    const Json list = Json::parse(dk::run(ctl, {L"--pipe", pipe, L"list", L"--json"}).out);
    const auto first = list.at("sessions").at(0).at("id").get<std::string>();
    const auto pn = split(child, L':');
    if (pn.size() < 2)
        throw std::invalid_argument("--child expects profile:name");
    dk::run(ctl, {L"--pipe", pipe, L"run", L"--profile", pn[0], L"--name", pn[1], L"--cwd", cwd, L"--parent",
                  dk::wide(first)});
}

// --input: captures whenever agent-ui asks. It writes the target path to <report>.shot and waits
// for that file to go away; the capture to --out is the last one.
void capture_on_request(HWND window, const Options& o, const fs::path& report, int& width, int& height) {
    fs::path signal = report;
    signal += L".shot";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(o.wait_seconds + 60);
    bool final_done = false;
    while (!final_done && std::chrono::steady_clock::now() < deadline) {
        std::error_code ec;
        if (fs::exists(signal, ec)) {
            dk::sleep_ms(150);  // the frame after the step has been presented
            fs::path target = dk::wide(dk::read_file(signal));
            if (target.is_relative())
                target = o.out.parent_path() / target;
            capture_png(window, target, width, height);
            final_done = fs::equivalent(target, o.out, ec) || target == o.out;
            fs::remove(signal, ec);
        }
        dk::sleep_ms(50);
    }
    if (!final_done)
        throw std::runtime_error("agent-ui did not finish the --input steps");
}

// SHOT_DEBUG: the first session's last lines and the host log, next to the PNG.
void save_debug_files(const fs::path& ctl, const std::wstring& pipe, const fs::path& out,
                      const fs::path& data) {
    const Json list = Json::parse(dk::run(ctl, {L"--pipe", pipe, L"list", L"--json"}).out);
    const auto sid = dk::wide(list.at("sessions").at(0).at("id").get<std::string>());
    fs::path read_bin = out;
    read_bin.replace_extension(L".read.bin");
    dk::write_file(read_bin, dk::run(ctl, {L"--pipe", pipe, L"read", sid, L"--lines", L"3"}).out);
    fs::path host_log = out;
    host_log.replace_extension(L".host.log");
    fs::copy_file(data / L"host.log", host_log, fs::copy_options::overwrite_existing);
}

// Runs agent-ui, waits, and takes the capture(s). The caller cleans up whatever this leaves.
void run_and_capture(const Options& o, const fs::path& ctl, const fs::path& ui, const std::wstring& pipe,
                     const fs::path& report, const fs::path& data, const std::wstring& cwd,
                     std::optional<dk::Started>& p, int& width, int& height) {
    p = dk::start(ui, ui_arguments(o, pipe, report), dk::exe_dir());
    if (!o.child.empty()) {
        dk::sleep_ms(4000);
        start_child_session(ctl, pipe, o.child, cwd);
        dk::sleep_ms(std::max(1, o.wait_seconds - 4) * 1000);
    } else {
        dk::sleep_ms(o.wait_seconds * 1000);
    }
    const HWND h = main_window(p->pid);
    if (h == nullptr)
        throw std::runtime_error("agent-ui has no window");
    ::SetForegroundWindow(h);
    dk::sleep_ms(500);
    if (o.input.empty())
        capture_png(h, o.out, width, height);
    else
        capture_on_request(h, o, report, width, height);
    dk::wait_exit(*p, std::chrono::milliseconds(15000));
    if (env(L"SHOT_DEBUG"))
        save_debug_files(ctl, pipe, o.out, data);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const dk::Args args(argc, argv);
        const std::wstring out_arg =
            args.get_or(L"out", args.positional().empty() ? L"" : args.positional()[0]);
        if (out_arg.empty()) {
            std::fprintf(stderr,
                         "usage: screenshot_ui --out <png> [--build-dir <dir>] [--profile chinese|claude] "
                         "[--wait-seconds 6] [--run <cmd>] [--child profile:name] [--remote <machine>] "
                         "[--mods a,b]\n");
            return 2;
        }
        Options o;
        o.out = fs::absolute(out_arg);
        o.build_dir = args.path_or(L"build-dir", (dk::source_root() / L"build" / L"msvc-x64-debug").native());
        o.profile = args.get_or(L"profile", L"chinese");
        o.wait_seconds = args.get_int(L"wait-seconds", 6);
        o.run_cmd = args.get_or(L"run", L"");
        o.child = args.get_or(L"child", L"");
        o.remote = args.get_or(L"remote", L"");
        o.mods = args.get_or(L"mods", L"");
        o.input = args.get_or(L"input", L"");
        o.layout_file = args.get_or(L"layout", L"");
        o.cwd = args.get_or(L"cwd", L"");

        ::SetProcessDPIAware();
        check(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "CoInitializeEx");
        const fs::path dir = o.build_dir / L"apps" / L"desktop";
        const fs::path ctl = dir / L"agentctl.exe";
        const fs::path data = dk::temp_dir() / (L"agentdesk-shot-" + dk::pid_string());
        fs::create_directories(data);
        const std::wstring temp_cwd =
            o.cwd.empty() ? dk::temp_dir().native() + L"\\" : fs::absolute(o.cwd).native();

        dk::write_file(data / L"profiles.json", dk::profiles_json(shot_profiles()));
        if (!o.layout_file.empty()) {  // a layout of the person's: <data>\layouts\<name>.json
            fs::create_directories(data / L"layouts");
            fs::copy_file(fs::path(o.layout_file), data / L"layouts" / fs::path(o.layout_file).filename(),
                          fs::copy_options::overwrite_existing);
        }
        ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", data.c_str());

        // ui.json is written only when --remote or --mods needs it.
        std::optional<Json> ui_doc;
        const fs::path remote_data = data.native() + L"-remote";
        const std::wstring remote_pipe = L"\\\\.\\pipe\\AgentDeskShotRemote-" + dk::pid_string();
        if (!o.remote.empty())
            ui_doc = start_remote_host(ctl, data, remote_data, remote_pipe, o.remote);
        if (!o.mods.empty()) {
            if (!ui_doc)
                ui_doc = Json{{"schema_version", 1}};
            Json enabled = Json::array();
            for (const auto& m : split(o.mods, L','))
                enabled.push_back(dk::u8(m));
            (*ui_doc)["mods_enabled"] = enabled;
        }
        if (!o.cwd.empty()) {  // agent-ui starts its first session in ui.json's last_cwd
            if (!ui_doc)
                ui_doc = Json{{"schema_version", 1}};
            (*ui_doc)["last_cwd"] = dk::u8(temp_cwd);
        }
        // --appearance light|dark (or SHOT_APPEARANCE): not the system's.
        const std::wstring look = args.get_or(L"appearance", env(L"SHOT_APPEARANCE").value_or(L""));
        if (!look.empty()) {
            if (!ui_doc)
                ui_doc = Json{{"schema_version", 1}};
            (*ui_doc)["appearance"] = dk::u8(look);
        }
        if (ui_doc)
            dk::write_file(data / L"ui.json", ui_doc->dump(2));

        const std::wstring pipe = L"\\\\.\\pipe\\AgentDeskShot-" + dk::pid_string();
        fs::path report = o.out;
        report.replace_extension(L".json");
        prepare_host_from_env(ctl, pipe, temp_cwd);

        int width = 0;
        int height = 0;
        std::optional<dk::Started> p;
        int rc = 0;
        try {
            run_and_capture(o, ctl, dir / L"agent-ui.exe", pipe, report, data, temp_cwd, p, width, height);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "screenshot_ui: %s\n", e.what());
            rc = 1;
        }
        if (p)
            dk::kill(*p);
        dk::run(ctl, {L"--pipe", pipe, L"shutdown", L"--stop-sessions"});
        if (!o.remote.empty()) {
            dk::run(ctl, {L"--pipe", remote_pipe, L"shutdown", L"--stop-sessions"});
            dk::remove_tree(remote_data);
        }
        dk::sleep_ms(1000);
        dk::remove_tree(data);
        ::CoUninitialize();
        if (rc == 0)
            std::printf("saved %s (%d x %d)\n", dk::u8(o.out).c_str(), width, height);
        return rc;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "screenshot_ui: %s\n", e.what());
        return 1;
    }
}
