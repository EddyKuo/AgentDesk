// The macOS half of git_tools.cpp (ADR-30): running a tool found on the login shell's PATH.
#include <array>
#include <thread>

#include <poll.h>
#include <unistd.h>

#include "agentdesk/host/git_tools.hpp"
#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/system.hpp"
#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::host {

namespace win = agentdesk::platform::win;

// No WSL on macOS.
bool is_wsl_path(const std::wstring& /*path*/) {
    return false;
}

// macOS (ADR-30): the program is looked up on the login shell's PATH (git from Homebrew), run in
// its own process group with stdout and stderr on separate pipes, and killed after `timeout`.
ToolResult run_tool(const std::vector<std::wstring>& argv, const std::filesystem::path& cwd,
                    std::chrono::milliseconds timeout) {
    ToolResult r;
    if (argv.empty())
        return r;
    const std::string name = win::to_utf8(argv[0]);
    const auto exe = name.find('/') != std::string::npos ? platform::find_executable(name)
                                                         : platform::find_on_login_path(name);
    if (!exe)
        return r;
    std::array<int, 2> err_pipe{-1, -1};
    if (::pipe(err_pipe.data()) != 0)
        return r;
    platform::ProcessSpec spec;
    spec.executable = *exe;
    if (exe->filename() == "git")  // the same safe.directory override as on Windows
        spec.args = {"-c", "safe.directory=*"};
    for (std::size_t i = 1; i < argv.size(); ++i)
        spec.args.push_back(win::to_utf8(argv[i]));
    spec.cwd = cwd;
    spec.stderr_fd = err_pipe[1];
    if (const std::string path = platform::login_path(); !path.empty())
        spec.env_overrides["PATH"] = path;
    std::unique_ptr<platform::Process> child;
    try {
        child = platform::Process::spawn(spec);
    } catch (const std::exception&) {
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        return r;
    }
    ::close(err_pipe[1]);
    r.started = true;
    std::string err;
    std::thread err_reader([fd = err_pipe[0], &err] {
        std::array<char, 16 * 1024> buf{};
        for (;;) {
            const ssize_t n = ::read(fd, buf.data(), buf.size());
            if (n <= 0)
                break;
            if (err.size() < kMaxToolOutput)
                err.append(buf.data(), static_cast<std::size_t>(n));
        }
        ::close(fd);
    });
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::thread out_reader([&] {
        while (auto chunk = child->read_some(64 * 1024)) {
            if (r.output.size() < kMaxToolOutput)
                r.output += *chunk;
        }
    });
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    auto code = child->wait(std::max(left, std::chrono::milliseconds(0)));
    if (!code) {
        r.timed_out = true;
        child->kill();
        code = child->wait(std::chrono::seconds(2));
    }
    out_reader.join();
    err_reader.join();
    r.error = std::move(err);
    r.exit_code = code ? static_cast<unsigned long>(*code) : 1ul;
    return r;
}

}  // namespace agentdesk::host
