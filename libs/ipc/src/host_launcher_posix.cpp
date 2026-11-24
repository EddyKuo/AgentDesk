// Connects to agent-host on macOS (starting it when allowed) or to a remote host over ssh-like
// stdio. The interface is host_launcher.hpp; executable names given with ".exe" (shared callers)
// name the same program without the extension here.
#include <array>
#include <mutex>
#include <thread>

#include <unistd.h>

#include "agentdesk/ipc/host_launcher.hpp"
#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/system.hpp"
#include "agentdesk/platform/text.hpp"

namespace agentdesk::ipc {


namespace {

protocol::Json hello_params() {
    return {{"version", protocol::kProtocolVersion}, {"client", "agentdesk"}};
}

void take_hello(HostConnection& out, const protocol::Json& result) {
    out.host_instance_id = result.value("host_instance_id", "");
    out.host_version = result.value("host_version", "");
    for (const auto& s : result.value("succeeds", protocol::Json::array()))
        if (s.is_string())
            out.host_succeeds.push_back(s.get<std::string>());
}

}  // namespace

std::filesystem::path sibling_executable(const wchar_t* file_name) {
    std::string name = platform::to_utf8(file_name);
    if (name.size() > 4 && name.ends_with(".exe"))
        name.resize(name.size() - 4);
    return platform::executable_path().parent_path() / name;
}

HostConnection connect_or_start_host(const std::filesystem::path& host_exe, const std::string& pipe_name,
                                     std::chrono::milliseconds timeout, bool allow_start) {
    HostConnection out;
    out.client = PipeClient::connect(pipe_name, std::chrono::milliseconds(300));
    if (!out.client && allow_start) {
        std::vector<std::string> args;
        if (pipe_name != default_pipe_name())
            args = {"--pipe", pipe_name};
        try {
            platform::spawn_detached(host_exe, args);
            out.started_host = true;
        } catch (const std::exception& e) {
            out.error = std::string("cannot start agent-host: ") + e.what();
            return out;
        }
        out.client = PipeClient::connect(pipe_name, timeout);
    }
    if (!out.client)
        return out;
    const auto hello = out.client->call("hello", hello_params());
    if (!hello || hello->error) {
        out.client.reset();
        return out;
    }
    take_hello(out, *hello->result);
    return out;
}

namespace {

// The command's stderr reader (ssh explains failures there).
struct CommandTransport {
    std::thread err_reader;
    std::string err_text;
    std::mutex mu;
    int err_fd = -1;
    ~CommandTransport() {
        if (err_reader.joinable())
            err_reader.join();
        if (err_fd >= 0)
            ::close(err_fd);
    }
    std::string stderr_text() {
        std::lock_guard lock(mu);
        return err_text;
    }
};

}  // namespace

HostConnection connect_over_command(const std::vector<std::wstring>& argv,
                                    std::chrono::milliseconds timeout) {
    HostConnection out;
    if (argv.empty()) {
        out.error = "no command";
        return out;
    }
    const std::string name = platform::to_utf8(argv[0]);
    const auto exe = platform::find_executable(name);
    if (!exe) {
        out.error = name + " was not found";
        return out;
    }
    std::array<int, 2> err_pipe{-1, -1};
    if (::pipe(err_pipe.data()) != 0) {
        out.error = "cannot create a pipe";
        return out;
    }
    auto t = std::make_shared<CommandTransport>();
    t->err_fd = err_pipe[0];

    // stdin/stdout carry the protocol; the child ends with the connection (its own process group).
    platform::ProcessSpec spec;
    spec.executable = *exe;
    for (std::size_t i = 1; i < argv.size(); ++i)
        spec.args.push_back(platform::to_utf8(argv[i]));
    spec.pipe_stdin = true;
    spec.pipe_stdout = true;
    spec.stderr_fd = err_pipe[1];
    std::unique_ptr<platform::Process> child;
    try {
        child = platform::Process::spawn(spec);
    } catch (const std::exception& e) {
        ::close(err_pipe[1]);
        out.error = "cannot start " + name + ": " + e.what();
        return out;
    }
    ::close(err_pipe[1]);
    t->err_reader = std::thread([raw = t.get()] {
        std::array<char, 1024> buf{};
        for (;;) {
            const ssize_t n = ::read(raw->err_fd, buf.data(), buf.size());
            if (n <= 0)
                break;
            std::lock_guard lock(raw->mu);
            if (raw->err_text.size() < 4096)
                raw->err_text.append(buf.data(), static_cast<std::size_t>(n));
        }
    });

    out.client = PipeClient::adopt(platform::process_stream(std::move(child)));
    const auto hello = out.client->call("hello", hello_params(), timeout);
    if (!hello || hello->error) {
        out.client->close();
        out.client.reset();
        for (int i = 0; i < 20 && t->stderr_text().empty(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::string why = t->stderr_text();
        while (!why.empty() && (why.back() == '\n' || why.back() == '\r'))
            why.pop_back();
        out.error = why.empty() ? (hello && hello->error ? hello->error_message
                                                         : std::string("no answer from the remote host"))
                                : why;
        return out;
    }
    take_hello(out, *hello->result);
    out.transport = t;
    return out;
}

std::vector<std::wstring> ssh_bridge_argv(const std::string& target, const std::string& remote_ctl) {
    return {L"ssh",
            L"-T",
            L"-o",
            L"BatchMode=yes",  // never prompt where nobody sees it; say what is missing instead
            L"-o",
            L"ServerAliveInterval=15",
            L"-o",
            L"ConnectTimeout=10",
            platform::to_wide(target),
            platform::to_wide(remote_ctl.empty() ? std::string("agentctl") : remote_ctl),
            L"bridge"};
}

}  // namespace agentdesk::ipc
