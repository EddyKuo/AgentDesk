// vt_probe: runs inside a pseudoconsole and plays the role of an agent CLI.
//   vt_probe query   -> sends "OSC 7501 ; ?" then "CSI c", records every byte that comes back on
//                       stdin for up to 3 s and prints "PROBE-REPLY:<hex>" on one line.
//   vt_probe report  -> prints an OSC 7501 working report, then "PROBE-DONE".
//   vt_probe echo    -> echoes stdin lines as "PROBE-ECHO:<hex>" until "quit".
//   vt_probe mouse   -> turns on SGR click reporting, then behaves like echo.
//   vt_probe size    -> keeps printing "SIZE <cols> <rows>" of the console it sees.
//   vt_probe crash   -> exits with 0xC0000005 (access violation) after 300 ms.
//   vt_probe counter -> prints "COUNT n" every 200 ms until killed.
//   vt_probe flood [seconds=5] [bytes_per_sec=1048576] -> numbered lines, then "FLOOD-DONE <n>".
// Used by S1-5 (inbox vs bundled ConPTY), the integration tests and the QA tools.
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>

namespace {

std::string to_hex(std::string_view bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (const char c : bytes) {
        const auto u = static_cast<unsigned char>(c);
        out.push_back(kDigits[u >> 4]);
        out.push_back(kDigits[u & 0xF]);
    }
    return out;
}

void write_out(std::string_view s) {
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), s.data(), static_cast<DWORD>(s.size()), &written, nullptr);
}

void enable_vt_io() {
    HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (::GetConsoleMode(in, &mode)) {
        mode &= ~static_cast<DWORD>(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
        mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        ::SetConsoleMode(in, mode);
    }
    HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (::GetConsoleMode(out, &mode)) {
        ::SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);
    }
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
}

// Reads whatever arrives on stdin until `deadline`, without blocking past it.
std::string read_until(std::chrono::steady_clock::time_point deadline, std::string_view stop = {}) {
    HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
    std::string got;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (::WaitForSingleObject(in, static_cast<DWORD>(std::max<long long>(left.count(), 0))) !=
            WAIT_OBJECT_0) {
            break;
        }
        char buf[256];
        DWORD n = 0;
        if (!::ReadFile(in, buf, sizeof(buf), &n, nullptr) || n == 0) {
            // Console input may signal for non-key events; ReadFile then returns 0 bytes.
            continue;
        }
        got.append(buf, n);
        if (!stop.empty() && got.find(stop) != std::string::npos) {
            break;
        }
    }
    return got;
}

int run_query() {
    write_out("\x1b]7501;?\x1b\\\x1b[c");
    // The DA1 reply ends with 'c'; keep reading a little longer to catch late replies.
    const std::string got = read_until(std::chrono::steady_clock::now() + std::chrono::seconds(3));
    write_out("\r\nPROBE-REPLY:" + to_hex(got) + "\r\n");
    return 0;
}

int run_report() {
    write_out("\x1b]7501;state=working:app=vtprobe:progress=10\x1b\\");
    write_out("\x1b]7501;state=blocked:kind=question\x07");
    write_out("PROBE-DONE\r\n");
    ::Sleep(300);  // give the host a moment to read before the pseudoconsole closes
    return 0;
}

int run_echo() {
    for (;;) {
        const std::string got = read_until(std::chrono::steady_clock::now() + std::chrono::seconds(10), "\r");
        if (got.empty()) {
            return 1;
        }
        write_out("PROBE-ECHO:" + to_hex(got) + "\r\n");
        if (got.find("quit") != std::string::npos) {
            return 0;
        }
    }
}

// Writes numbered 64-byte lines at `bytes_per_sec` for `seconds`, then "FLOOD-DONE <lines>".
int run_flood(int seconds, long long bytes_per_sec) {
    constexpr int kLine = 64;
    const long long lines_per_tick = std::max<long long>(1, bytes_per_sec / kLine / 20);  // 50 ms ticks
    const auto start = std::chrono::steady_clock::now();
    long long n = 0;
    std::string block;
    char line[kLine + 1];
    for (int tick = 0; tick < seconds * 20; ++tick) {
        block.clear();
        for (long long i = 0; i < lines_per_tick; ++i, ++n) {
            std::snprintf(line, sizeof(line), "L%010lld %-49s\r\n", n,
                          "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLM");
            block.append(line, kLine);
        }
        write_out(block);
        const auto next = start + std::chrono::milliseconds(50 * (tick + 1));
        const auto now = std::chrono::steady_clock::now();
        if (next > now)
            ::Sleep(static_cast<DWORD>(
                std::chrono::duration_cast<std::chrono::milliseconds>(next - now).count()));
    }
    write_out("FLOOD-DONE " + std::to_string(n) + "\r\n");
    ::Sleep(300);
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    enable_vt_io();
    const std::wstring_view mode = argc > 1 ? argv[1] : L"query";
    if (mode == L"query")
        return run_query();
    if (mode == L"report")
        return run_report();
    if (mode == L"echo")
        return run_echo();
    if (mode == L"mouse") {  // asks for click reporting (SGR), then echoes input like "echo"
        write_out("\x1b[?1000h\x1b[?1006h");
        return run_echo();
    }
    if (mode == L"size") {  // QA-04: report the console size the child actually sees
        for (;;) {
            CONSOLE_SCREEN_BUFFER_INFO info{};
            ::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &info);
            const int cols = info.srWindow.Right - info.srWindow.Left + 1;
            const int rows = info.srWindow.Bottom - info.srWindow.Top + 1;
            write_out("\r\x1b[2KSIZE " + std::to_string(cols) + " " + std::to_string(rows));
            ::Sleep(100);
        }
    }
    if (mode == L"crash") {  // fault case: dies like an access violation after a short delay
        write_out("CRASHING\r\n");
        ::Sleep(300);
        ::ExitProcess(0xC0000005u);
    }
    if (mode == L"counter") {  // QA-05: "COUNT n" every 200 ms until killed
        for (long long n = 1;; ++n) {
            write_out("COUNT " + std::to_string(n) + "\r\n");
            ::Sleep(200);
        }
    }
    if (mode == L"flood") {
        const int seconds = argc > 2 ? _wtoi(argv[2]) : 5;
        const long long rate = argc > 3 ? _wtoi64(argv[3]) : 1024 * 1024;
        return run_flood(seconds, rate);
    }
    write_out("usage: vt_probe query|report|echo\r\n");
    return 2;
}
