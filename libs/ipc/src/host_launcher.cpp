// Connects to agent-host (starting it when allowed) or to a remote host over ssh-like stdio.
#include <memory>
#include <mutex>
#include <thread>

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

std::filesystem::path sibling_executable(std::string_view name) {
    return platform::executable_path().parent_path() / platform::program_file(name);
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
            const auto spawned = platform::spawn_detached(host_exe, args);
            out.started_host = true;
            out.host_dies_with_launcher = spawned.dies_with_launcher;
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
    std::shared_ptr<platform::Process> child;  // also held by the connection's stream
    ~CommandTransport() {
        if (child)
            child->kill();
        if (err_reader.joinable())
            err_reader.join();
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
    auto t = std::make_shared<CommandTransport>();

    // stdin/stdout carry the protocol; the child ends with the connection (its own process group).
    platform::ProcessSpec spec;
    spec.executable = *exe;
    for (std::size_t i = 1; i < argv.size(); ++i)
        spec.args.push_back(platform::to_utf8(argv[i]));
    spec.pipe_stdin = true;
    spec.pipe_stdout = true;
    spec.pipe_stderr = true;
    try {
        t->child = platform::Process::spawn(spec);
    } catch (const std::exception& e) {
        out.error = "cannot start " + name + ": " + e.what();
        return out;
    }
    t->err_reader = std::thread([raw = t.get()] {
        while (auto chunk = raw->child->read_some_stderr(1024)) {
            std::lock_guard lock(raw->mu);
            if (raw->err_text.size() < 4096)
                raw->err_text += *chunk;
        }
    });

    out.client = PipeClient::adopt(platform::process_stream(t->child));
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
