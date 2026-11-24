// cli_os.hpp on macOS (ADR-30), with the main entry point.
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <thread>
#include <vector>

#include <unistd.h>

#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/ipc_transport.hpp"
#include "agentdesk/platform/text.hpp"
#include "cli_os.hpp"

// The arguments arrive as UTF-8; the shared code takes them as wide strings, as on Windows.
int main(int argc, char** argv_utf8) {
    std::signal(SIGPIPE, SIG_IGN);  // the bridge's ssh peer may go away: EPIPE, not death
    std::vector<std::wstring> wide;
    try {
        for (int i = 0; i < argc; ++i)
            wide.push_back(agentdesk::platform::to_wide(argv_utf8[i]));
    } catch (const std::exception&) {
        std::fputs("agentctl: an argument is not valid UTF-8\n", stderr);
        return 2;  // bad arguments
    }
    std::vector<wchar_t*> ptrs;
    for (auto& w : wide)
        ptrs.push_back(w.data());
    ptrs.push_back(nullptr);
    return agentdesk::cli::agentctl_main(argc, ptrs.data());
}

namespace agentdesk::cli::os {

using namespace std::chrono_literals;

std::string env(const wchar_t* name) {
    const char* v = std::getenv(platform::to_utf8(name).c_str());
    return v != nullptr ? std::string(v) : std::string();
}

std::string read_stdin(std::size_t max) {
    std::string input;
    char buf[4096];
    ssize_t n = 0;
    while (input.size() < max && (n = ::read(0, buf, sizeof buf)) > 0)
        input.append(buf, static_cast<std::size_t>(n));
    return input;
}

bool bridge_stdio(const std::string& pipe) {
    std::unique_ptr<platform::ByteStream> p = ipc::open_raw_pipe(pipe, 10s);
    if (!p)
        return false;
    std::thread up([&] {
        char buf[16384];
        for (;;) {
            const ssize_t n = ::read(0, buf, sizeof buf);
            if (n <= 0 || !p->write_all(std::string_view(buf, static_cast<std::size_t>(n))))
                break;
        }
        p->cancel();
    });
    std::array<char, 16384> buf{};
    for (;;) {
        const std::size_t n = p->read_some(buf);
        if (n == 0)
            break;
        std::size_t done = 0;
        while (done < n) {
            const ssize_t w = ::write(1, buf.data() + done, n - done);
            if (w <= 0)
                break;
            done += static_cast<std::size_t>(w);
        }
        if (done < n)
            break;
    }
    up.detach();  // may be blocked reading stdin; the process exit ends it
    std::_Exit(0);
}

}  // namespace agentdesk::cli::os
