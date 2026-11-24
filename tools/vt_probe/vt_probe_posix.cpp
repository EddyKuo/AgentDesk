// vt_probe_os.hpp on macOS (ADR-30): a Unix pty, and main.
#include <algorithm>
#include <csignal>
#include <string>

#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "vt_probe_os.hpp"

namespace vt_probe {

void write_out(std::string_view s) {
    while (!s.empty()) {
        const ssize_t n = ::write(1, s.data(), s.size());
        if (n <= 0)
            return;
        s.remove_prefix(static_cast<std::size_t>(n));
    }
}

// Raw input: every byte as it comes, no echo, no signals from Ctrl+C; output as written.
void enable_vt_io() {
    termios t{};
    if (::tcgetattr(0, &t) == 0) {
        t.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | ISIG | IEXTEN);
        t.c_iflag &= ~static_cast<tcflag_t>(ICRNL | IXON);
        t.c_oflag &= ~static_cast<tcflag_t>(OPOST);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        ::tcsetattr(0, TCSANOW, &t);
    }
}

std::string read_until(std::chrono::steady_clock::time_point deadline, std::string_view stop) {
    std::string got;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        pollfd p{0, POLLIN, 0};
        if (::poll(&p, 1, static_cast<int>(std::max<long long>(left.count(), 0))) != 1)
            break;
        char buf[256];
        const ssize_t n = ::read(0, buf, sizeof(buf));
        if (n <= 0)
            break;
        got.append(buf, static_cast<std::size_t>(n));
        if (!stop.empty() && got.find(stop) != std::string::npos)
            break;
    }
    return got;
}

void sleep_ms(unsigned ms) {
    ::usleep(static_cast<useconds_t>(ms) * 1000);
}

void console_size(int& cols, int& rows) {
    winsize ws{};
    ::ioctl(1, TIOCGWINSZ, &ws);
    cols = ws.ws_col;
    rows = ws.ws_row;
}

void crash() {
    std::raise(SIGSEGV);
    std::_Exit(1);
}

}  // namespace vt_probe

int main(int argc, char** argv) {
    vt_probe::enable_vt_io();
    return vt_probe::run(argc > 1 ? argv[1] : "query", argc > 2 ? argv[2] : "", argc > 3 ? argv[3] : "");
}
