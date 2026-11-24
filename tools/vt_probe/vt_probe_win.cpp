// vt_probe_os.hpp on Windows: the console handles, and wmain.
#include <windows.h>

#include <algorithm>
#include <string>

#include "vt_probe_os.hpp"

namespace vt_probe {

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
std::string read_until(std::chrono::steady_clock::time_point deadline, std::string_view stop) {
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

void sleep_ms(unsigned ms) {
    ::Sleep(ms);
}

void console_size(int& cols, int& rows) {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    ::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &info);
    cols = info.srWindow.Right - info.srWindow.Left + 1;
    rows = info.srWindow.Bottom - info.srWindow.Top + 1;
}

void crash() {
    ::ExitProcess(0xC0000005u);
}

}  // namespace vt_probe

int wmain(int argc, wchar_t** argv) {
    vt_probe::enable_vt_io();
    // The arguments are ASCII (mode names, numbers).
    const auto narrow = [](const wchar_t* w) {
        std::string s;
        for (; *w != L'\0'; ++w)
            s.push_back(static_cast<char>(*w));
        return s;
    };
    return vt_probe::run(argc > 1 ? narrow(argv[1]) : std::string("query"),
                         argc > 2 ? narrow(argv[2]) : std::string(),
                         argc > 3 ? narrow(argv[3]) : std::string());
}
