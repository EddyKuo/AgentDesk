// macOS (ADR-30): the helpers of system_process.hpp. The system's own tools live in /usr/bin
// (curl, tar), and programs start through posix_spawn, never through a shell.
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

#include <unistd.h>

#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "system_process.hpp"

namespace agentdesk::ui::detail {

namespace win = agentdesk::platform::win;

std::string to_utf8(const std::wstring& w) {
    return win::to_utf8(w);
}

std::wstring to_wide(const std::string& s) {
    return win::to_wide(s);
}

ProcessResult run_process(const std::filesystem::path& exe, const std::vector<std::wstring>& args,
                          const std::string& input, const std::function<bool()>& keep_going) {
    ProcessResult r;
    std::array<int, 2> err_pipe{-1, -1};
    if (::pipe(err_pipe.data()) != 0)
        return r;
    platform::ProcessSpec spec;
    spec.executable = exe;
    for (const auto& a : args)
        spec.args.push_back(win::to_utf8(a));
    spec.pipe_stdin = true;
    spec.pipe_stdout = true;
    spec.stderr_fd = err_pipe[1];
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
    std::thread writer([&] {
        if (!input.empty())
            child->write(input);
        child->close_stdin();
    });
    std::thread err_reader([fd = err_pipe[0], &r] {
        std::array<char, 16 * 1024> buf{};
        for (;;) {
            const ssize_t n = ::read(fd, buf.data(), buf.size());
            if (n <= 0)
                break;
            r.err.append(buf.data(), static_cast<std::size_t>(n));
        }
        ::close(fd);
    });
    std::thread out_reader([&] {
        while (auto chunk = child->read_some(64 * 1024))
            r.out += *chunk;
    });
    bool killed = false;
    std::optional<int> code;
    while (!(code = child->wait(std::chrono::milliseconds(50)))) {
        if (keep_going && !keep_going()) {
            killed = true;
            child->kill();
            code = child->wait(std::chrono::seconds(5));
            break;
        }
    }
    writer.join();
    out_reader.join();
    err_reader.join();
    r.exit_code = killed ? 1ul : static_cast<unsigned long>(code.value_or(1));
    return r;
}

// The variable goes into the child's environment only: changing ours would race with other
// threads reading it.
bool spawn_detached(const std::filesystem::path& exe, const std::vector<std::wstring>& args,
                    const std::wstring& env_name, const std::wstring& env_value) {
    std::map<std::string, std::string> env;
    std::vector<std::string> a;
    try {
        if (!env_name.empty())
            env[win::to_utf8(env_name)] = win::to_utf8(env_value);
        for (const auto& w : args)
            a.push_back(win::to_utf8(w));
        platform::spawn_detached(exe, a, env);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

// The system's own copy of a tool: /usr/bin/<name> (curl, tar ship with macOS).
std::filesystem::path system32_exe(std::wstring_view name) {
    return std::filesystem::path("/usr/bin") / win::to_utf8(name);
}

std::optional<std::wstring> env_var(const wchar_t* name) {
    const char* v = std::getenv(win::to_utf8(name).c_str());
    if (v == nullptr)
        return std::nullopt;
    return win::to_wide(v);
}

void unset_env_var(const wchar_t* name) {
    ::unsetenv(win::to_utf8(name).c_str());
}

const char* os_name() {
    return "macos";
}

const char* cpu_arch() {
    return "aarch64";
}

std::optional<std::wstring> home_folder() {
    return env_var(L"HOME");
}

std::string trim(std::string_view s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos)
        return {};
    return std::string(s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1));
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string read_file(const std::filesystem::path& p, bool* ok) {
    std::ifstream in(p, std::ios::binary);
    if (ok != nullptr)
        *ok = static_cast<bool>(in);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::uint64_t unix_now() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}

}  // namespace agentdesk::ui::detail
