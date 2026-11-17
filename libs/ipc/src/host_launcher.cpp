#include "agentdesk/ipc/host_launcher.hpp"

#include <atomic>
#include <mutex>
#include <thread>

#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/detached.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/job.hpp"

namespace agentdesk::ipc {

std::filesystem::path sibling_executable(const wchar_t* file_name) {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path() / file_name;
}

HostConnection connect_or_start_host(const std::filesystem::path& host_exe, const std::wstring& pipe_name,
                                     std::chrono::milliseconds timeout, bool allow_start) {
    HostConnection out;
    out.client = PipeClient::connect(pipe_name, std::chrono::milliseconds(300));
    if (!out.client && allow_start) {
        std::vector<std::wstring> args;
        if (pipe_name != default_pipe_name())
            args = {L"--pipe", pipe_name};
        const auto spawned = platform::win::spawn_detached(host_exe, args);
        out.started_host = true;
        out.host_dies_with_launcher = !spawned.broke_away && spawned.parent_job_kills_on_close;
        out.client = PipeClient::connect(pipe_name, timeout);
    }
    if (!out.client)
        return out;
    const auto hello =
        out.client->call("hello", {{"version", protocol::kProtocolVersion}, {"client", "agentdesk"}});
    if (!hello || hello->error) {
        out.client.reset();
        return out;
    }
    out.host_instance_id = hello->result->value("host_instance_id", "");
    return out;
}

namespace {

using platform::win::UniqueHandle;

// The ssh process, its job (kills it with the connection) and its stderr reader.
struct CommandTransport {
    platform::win::KillOnCloseJob job;
    UniqueHandle process;
    std::thread err_reader;
    std::string err_text;
    std::mutex mu;
    ~CommandTransport() {
        if (process)
            ::TerminateProcess(process.get(), 1);
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
    wchar_t exe[MAX_PATH]{};
    if (::SearchPathW(nullptr, argv[0].c_str(), L".exe", MAX_PATH, exe, nullptr) == 0) {
        out.error = platform::win::to_utf8(argv[0]) + " was not found";
        return out;
    }
    // Two private pipes, like a real ssh: the child's stdin and stdout are separate streams
    // (one synchronous handle for both would serialize its read and write). Our ends are
    // overlapped for PipeClient.
    static std::atomic<unsigned> counter{0};
    const std::wstring base = L"\\\\.\\pipe\\AgentDeskTransport-" + std::to_wstring(::GetCurrentProcessId()) +
                              L"-" + std::to_wstring(++counter);
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    auto make = [&](const std::wstring& name, DWORD our_access, DWORD their_access, UniqueHandle& ours,
                    UniqueHandle& theirs) {
        ours.reset(::CreateNamedPipeW(
            name.c_str(), our_access | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 64 * 1024,
            64 * 1024, 0, nullptr));
        if (!ours || ours.get() == INVALID_HANDLE_VALUE)
            return false;
        theirs.reset(::CreateFileW(name.c_str(), their_access, 0, &sa, OPEN_EXISTING, 0, nullptr));
        return theirs && theirs.get() != INVALID_HANDLE_VALUE;
    };
    UniqueHandle to_child_ours, to_child_theirs, from_child_ours, from_child_theirs;
    if (!make(base + L"-in", PIPE_ACCESS_OUTBOUND, GENERIC_READ, to_child_ours, to_child_theirs) ||
        !make(base + L"-out", PIPE_ACCESS_INBOUND, GENERIC_WRITE, from_child_ours, from_child_theirs)) {
        out.error = "cannot create transport pipes";
        return out;
    }
    HANDLE err_read_raw = nullptr;
    HANDLE err_write_raw = nullptr;
    ::CreatePipe(&err_read_raw, &err_write_raw, &sa, 0);
    UniqueHandle err_read(err_read_raw);
    UniqueHandle err_write(err_write_raw);
    ::SetHandleInformation(err_read.get(), HANDLE_FLAG_INHERIT, 0);

    auto t = std::make_shared<CommandTransport>();
    std::vector<std::wstring> args(argv.begin() + 1, argv.end());
    std::wstring cmdline = platform::win::build_command_line(exe, args);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = to_child_theirs.get();
    si.hStdOutput = from_child_theirs.get();
    si.hStdError = err_write.get();
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(exe, cmdline.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                          nullptr, nullptr, &si, &pi)) {
        out.error = "cannot start " + platform::win::to_utf8(exe);
        return out;
    }
    t->process.reset(pi.hProcess);
    try {
        t->job.assign(pi.hProcess);
    } catch (const std::exception&) {
    }
    ::ResumeThread(pi.hThread);
    ::CloseHandle(pi.hThread);
    to_child_theirs.reset();
    from_child_theirs.reset();
    err_write.reset();
    t->err_reader = std::thread([raw = t.get(), h = std::make_shared<UniqueHandle>(std::move(err_read))] {
        char buf[1024];
        DWORD n = 0;
        while (::ReadFile(h->get(), buf, sizeof buf, &n, nullptr) && n > 0) {
            std::lock_guard lock(raw->mu);
            if (raw->err_text.size() < 4096)
                raw->err_text.append(buf, n);
        }
    });

    out.client = PipeClient::adopt(std::move(from_child_ours), std::move(to_child_ours));
    const auto hello = out.client->call(
        "hello", {{"version", protocol::kProtocolVersion}, {"client", "agentdesk"}}, timeout);
    if (!hello || hello->error) {
        out.client->close();
        out.client.reset();
        // Give ssh a moment to explain itself on stderr.
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
    out.host_instance_id = hello->result->value("host_instance_id", "");
    out.transport = t;
    return out;
}

std::vector<std::wstring> ssh_bridge_argv(const std::string& target, const std::string& remote_ctl) {
    return {L"ssh",
            L"-T",
            L"-o",
            L"BatchMode=yes",  // never prompt in a window nobody sees; say what is missing instead
            L"-o",
            L"ServerAliveInterval=15",
            L"-o",
            L"ConnectTimeout=10",
            platform::win::to_wide(target),
            platform::win::to_wide(remote_ctl.empty() ? std::string("agentctl") : remote_ctl),
            L"bridge"};
}

}  // namespace agentdesk::ipc
