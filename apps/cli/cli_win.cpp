// cli_os.hpp on Windows, with the wmain entry point.
#include <windows.h>

#include <array>
#include <chrono>
#include <thread>

#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/ipc_transport.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "cli_os.hpp"

int wmain(int argc, wchar_t** argv) {
    ::SetConsoleOutputCP(CP_UTF8);
    return agentdesk::cli::agentctl_main(argc, argv);
}

namespace agentdesk::cli::os {

namespace win = platform::win;
using namespace std::chrono_literals;

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

bool bridge_stdio(const std::string& pipe) {
    const std::unique_ptr<platform::ByteStream> p = ipc::open_raw_pipe(pipe, 10s);
    if (!p)
        return false;
    HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
    HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    std::thread up([&] {
        std::array<char, 16384> buf{};
        DWORD n = 0;
        while (::ReadFile(in, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) && n > 0)
            if (!p->write_all(std::string_view(buf.data(), n)))
                break;
        p->cancel();
    });
    std::array<char, 16384> buf{};
    while (const std::size_t n = p->read_some(buf)) {
        DWORD w = 0;
        if (!::WriteFile(out, buf.data(), static_cast<DWORD>(n), &w, nullptr) || w != n)
            break;
    }
    up.detach();  // may be blocked reading stdin; the process exit ends it
    ::ExitProcess(0);
}

}  // namespace agentdesk::cli::os
