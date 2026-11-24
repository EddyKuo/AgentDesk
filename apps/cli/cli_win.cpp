// cli_os.hpp on Windows, with the wmain entry point.
#include <windows.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/unique_handle.hpp"
#include "cli_os.hpp"

int wmain(int argc, wchar_t** argv) {
    ::SetConsoleOutputCP(CP_UTF8);
    return agentdesk::cli::agentctl_main(argc, argv);
}

namespace agentdesk::cli::os {

namespace win = platform::win;
using namespace std::chrono_literals;

std::string env(const wchar_t* name) {
    wchar_t buf[512]{};
    const DWORD n = ::GetEnvironmentVariableW(name, buf, 512);
    return n > 0 && n < 512 ? win::to_utf8(std::wstring(buf, n)) : std::string();
}

std::string read_stdin(std::size_t max) {
    std::string input;
    HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
    char buf[4096];
    DWORD n = 0;
    while (input.size() < max && in != nullptr && in != INVALID_HANDLE_VALUE &&
           ::ReadFile(in, buf, sizeof buf, &n, nullptr) && n > 0)
        input.append(buf, n);
    return input;
}

bool bridge_stdio(const std::wstring& pipe) {
    win::UniqueHandle p = ipc::open_raw_pipe(pipe, 10s);
    if (!p)
        return false;
    HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
    HANDLE out_h = ::GetStdHandle(STD_OUTPUT_HANDLE);
    std::atomic<bool> done{false};
    auto io = [&](bool write, char* buf, DWORD len, DWORD& n) {
        OVERLAPPED ov{};
        win::UniqueHandle ev(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        ov.hEvent = ev.get();
        const BOOL ok = write ? ::WriteFile(p.get(), buf, len, nullptr, &ov)
                              : ::ReadFile(p.get(), buf, len, nullptr, &ov);
        if (!ok && ::GetLastError() != ERROR_IO_PENDING)
            return false;
        return ::GetOverlappedResult(p.get(), &ov, &n, TRUE) != FALSE;
    };
    std::thread up([&] {
        char buf[16384];
        DWORD n = 0;
        while (!done && ::ReadFile(in, buf, sizeof buf, &n, nullptr) && n > 0) {
            DWORD w = 0;
            if (!io(true, buf, n, w) || w != n)
                break;
        }
        done = true;
        ::CancelIoEx(p.get(), nullptr);
    });
    char buf[16384];
    DWORD n = 0;
    while (!done && io(false, buf, sizeof buf, n) && n > 0) {
        DWORD w = 0;
        if (!::WriteFile(out_h, buf, n, &w, nullptr) || w != n)
            break;
    }
    done = true;
    up.detach();  // may be blocked reading stdin; the process exit ends it
    ::ExitProcess(0);
}

}  // namespace agentdesk::cli::os
