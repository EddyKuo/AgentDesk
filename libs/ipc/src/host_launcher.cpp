#include "agentdesk/ipc/host_launcher.hpp"

#include "agentdesk/platform/win/detached.hpp"

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

}  // namespace agentdesk::ipc
