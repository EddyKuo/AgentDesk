// agent-host.exe: per-user background process that owns every session (design doc 2.1, 2.2).
// Not a Windows Service; runs as the logged-in user. Exits only on host.shutdown.
//   agent-host.exe [--pipe NAME] [--data-dir DIR]
#include <windows.h>
// shellapi.h needs windows.h first.
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>

#include <shellapi.h>

#include "agentdesk/host/session_manager.hpp"
#include "agentdesk/host/store.hpp"
#include "agentdesk/ipc/host_launcher.hpp"
#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/win/conpty_api.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/unique_handle.hpp"

namespace {

namespace win = agentdesk::platform::win;
namespace host = agentdesk::host;
namespace ipc = agentdesk::ipc;

// Diagnostics only: IDs, event types, error codes. Never input text or environment (3.9).
void log_line(const std::filesystem::path& dir, const std::string& line) {
    std::ofstream(dir / L"host.log", std::ios::app)
        << host::iso8601_now() << " pid=" << ::GetCurrentProcessId() << ' ' << line << '\n';
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    std::wstring pipe_name;
    std::filesystem::path data_dir;
    {
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        for (int i = 1; argv != nullptr && i < argc; ++i) {
            const std::wstring a = argv[i];
            if (a == L"--pipe" && i + 1 < argc)
                pipe_name = argv[++i];
            else if (a == L"--data-dir" && i + 1 < argc)
                data_dir = argv[++i];
        }
        ::LocalFree(argv);
    }
    const bool custom_pipe = !pipe_name.empty();
    if (!custom_pipe)
        pipe_name = ipc::default_pipe_name();
    if (data_dir.empty()) {
        wchar_t buf[MAX_PATH]{};
        const DWORD n =
            ::GetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", buf, MAX_PATH);  // tests, portable use
        data_dir =
            n > 0 && n < MAX_PATH ? std::filesystem::path(std::wstring(buf, n)) : host::default_data_dir();
    }
    std::filesystem::create_directories(data_dir);

    // One host per user and logon session (design doc 2.2 step 2). A second one just exits;
    // its launcher then connects to the existing host.
    const std::wstring mutex_name =
        custom_pipe ? L"Local\\AgentDesk-host-test-" + std::to_wstring(std::hash<std::wstring>{}(pipe_name))
                    : ipc::default_mutex_name();
    win::UniqueHandle instance(::CreateMutexW(nullptr, FALSE, mutex_name.c_str()));
    if (!instance || ::GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    win::UniqueHandle shutdown(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    host::HostConfig cfg;
    cfg.conpty = win::load_conpty(ipc::sibling_executable(L"agent-host.exe").parent_path());
    cfg.data_dir = data_dir;
    cfg.ctl_path = ipc::sibling_executable(L"agentctl.exe");
    cfg.pipe_name = pipe_name;
    cfg.host_instance_id = host::new_uuid();
    cfg.host_version = AGENTDESK_VERSION_STRING;
    cfg.on_shutdown = [ev = shutdown.get()] { ::SetEvent(ev); };

    try {
        auto manager = std::make_shared<host::SessionManager>(cfg);
        ipc::ServerHandlers handlers;
        handlers.on_open = [manager](const ipc::ConnectionPtr& c) { manager->on_open(c); };
        handlers.on_message = [manager](const ipc::ConnectionPtr& c, std::string p) {
            manager->on_message(c, std::move(p));
        };
        handlers.on_close = [manager](const ipc::ConnectionPtr& c) { manager->on_close(c); };
        ipc::PipeServer server(pipe_name, handlers);
        log_line(data_dir, "started instance=" + cfg.host_instance_id +
                               " conpty=" + (cfg.conpty.bundled ? "bundled" : "inbox"));
        ::WaitForSingleObject(shutdown.get(), INFINITE);
        // Let the shutdown response reach the client before tearing down the pipe.
        ::Sleep(200);
        server.stop();
        manager->stop_all();
        log_line(data_dir, "stopped instance=" + cfg.host_instance_id);
    } catch (const std::system_error& e) {
        log_line(data_dir, std::string("fatal: ") + e.what());
        return e.code().value() == ERROR_ACCESS_DENIED ? 3 : 1;  // pipe name owned by someone else
    } catch (const std::exception& e) {
        log_line(data_dir, std::string("fatal: ") + e.what());
        return 1;
    }
    return 0;
}
