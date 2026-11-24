// agent-host.exe: per-user background process that owns every session (design doc 2.1, 2.2).
// Not a Windows Service; runs as the logged-in user. Exits on host.shutdown, or after handing
// every session to a newer agent-host (host.update, design doc 7.4 OQ-09).
//   agent-host.exe [--pipe NAME] [--data-dir DIR]
//   agent-host.exe --version                          "agent-host <version>" (agentctl update)
//   agent-host.exe --takeover IN,OUT,OLD [--pipe NAME] [--data-dir DIR]
//       started by a running host's host.update: adopts its sessions (handover.cpp)
#include <windows.h>
// shellapi.h needs windows.h first.
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>

#include <shellapi.h>

#include "agentdesk/host/handover.hpp"
#include "agentdesk/host/session_manager.hpp"
#include "agentdesk/host/store.hpp"
#include "agentdesk/ipc/host_launcher.hpp"
#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/ipc_transport.hpp"
#include "agentdesk/platform/pty.hpp"
#include "agentdesk/platform/system.hpp"
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

// The old host's pipe instances close just before it says "go"; give the name a moment.
std::unique_ptr<ipc::PipeServer> serve(const std::string& name, const ipc::ServerHandlers& handlers,
                                       bool after_takeover) {
    for (int attempt = 0;; ++attempt) {
        try {
            return std::make_unique<ipc::PipeServer>(name, handlers);
        } catch (const std::system_error& e) {
            if (!after_takeover || e.code().value() != ERROR_ACCESS_DENIED || attempt >= 100)
                throw;
            ::Sleep(100);
        }
    }
}

struct HostArgs {
    std::string pipe_name;
    std::filesystem::path data_dir;
    std::wstring takeover;  // "IN,OUT,OLD" handle values from the old host, or empty
    bool version = false;
};

HostArgs parse_command_line() {
    HostArgs a;
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    for (int i = 1; argv != nullptr && i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--pipe" && i + 1 < argc)
            a.pipe_name = win::to_utf8(argv[++i]);
        else if (arg == L"--data-dir" && i + 1 < argc)
            a.data_dir = argv[++i];
        else if (arg == L"--takeover" && i + 1 < argc)
            a.takeover = argv[++i];
        else if (arg == L"--version")
            a.version = true;
    }
    ::LocalFree(argv);
    return a;
}

// Like agent-ui --version: written to the stdout handle it was given (no console of its own).
int print_version() {
    const std::string line = std::string("agent-host ") + AGENTDESK_VERSION_STRING + "\n";
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), line.data(), static_cast<DWORD>(line.size()), &written,
                nullptr);
    return 0;
}

// --data-dir, else AGENTDESK_DATA_DIR (tests, portable use), else %LOCALAPPDATA%\AgentDesk.
std::filesystem::path resolve_data_dir(std::filesystem::path given) {
    if (!given.empty())
        return given;
    return agentdesk::platform::data_dir();
}

// The new host's side of host.update: adopts the old host's sessions and waits for its "go".
// nullptr when the takeover did not go through (logged); the old host then keeps everything.
std::shared_ptr<host::SessionManager> take_over(const host::HostConfig& cfg, host::OldHost& old_host,
                                                const std::filesystem::path& data_dir) {
    std::shared_ptr<host::SessionManager> manager;
    try {
        manager = host::receive_handover(cfg, old_host);
    } catch (const std::exception& e) {
        log_line(data_dir, std::string("takeover refused: ") + e.what());
        return nullptr;
    }
    if (!host::await_handover_go(old_host)) {
        manager->abandon_adopted();
        log_line(data_dir, "takeover abandoned: the old host kept its sessions");
        return nullptr;
    }
    manager->start_adopted();
    return manager;
}

ipc::ServerHandlers handlers_for(const std::shared_ptr<host::SessionManager>& manager) {
    ipc::ServerHandlers handlers;
    handlers.on_open = [manager](const ipc::ConnectionPtr& c) { manager->on_open(c); };
    handlers.on_message = [manager](const ipc::ConnectionPtr& c, std::string p) {
        manager->on_message(c, std::move(p));
    };
    handlers.on_close = [manager](const ipc::ConnectionPtr& c) { manager->on_close(c); };
    return handlers;
}

// The program file the old host ran from may have been moved aside as agent-host.exe.old
// (agentctl update); it can go once that host has exited.
void remove_old_exe_after(std::shared_ptr<host::OldHost> old_host) {
    std::thread([old = std::move(old_host)] {
        old->wait_exit(std::chrono::seconds(60));
        std::error_code ec;
        std::filesystem::remove(ipc::sibling_executable("agent-host").concat(".old"), ec);
    }).detach();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const HostArgs args = parse_command_line();
    if (args.version)
        return print_version();
    const bool custom_pipe = !args.pipe_name.empty();
    const std::string pipe_name = custom_pipe ? args.pipe_name : ipc::default_pipe_name();
    const std::filesystem::path data_dir = resolve_data_dir(args.data_dir);
    std::filesystem::create_directories(data_dir);
    const bool taking_over = !args.takeover.empty();

    // One host per user and logon session (design doc 2.2 step 2). A second one just exits;
    // its launcher then connects to the existing host. A host taking over from the running one
    // shares the name until that one exits.
    const std::string lock_name =
        custom_pipe ? "host-test-" + std::to_string(std::hash<std::string>{}(pipe_name)) : "host";
    const auto instance = agentdesk::platform::InstanceLock::try_acquire(lock_name, taking_over);
    if (!instance)
        return 0;

    win::UniqueHandle shutdown(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    host::HostConfig cfg;
    const bool bundled_conpty = agentdesk::platform::pty_capabilities().answers_osc7501_query;
    cfg.data_dir = data_dir;
    cfg.ctl_path = ipc::sibling_executable("agentctl");
    cfg.pipe_name = pipe_name;
    cfg.host_instance_id = host::new_uuid();
    cfg.host_version = AGENTDESK_VERSION_STRING;
    cfg.on_shutdown = [ev = shutdown.get()] { ::SetEvent(ev); };
    cfg.on_handed_over = [ev = shutdown.get()] { ::SetEvent(ev); };

    try {
        std::shared_ptr<host::SessionManager> manager;
        std::shared_ptr<host::OldHost> old_host;
        if (taking_over) {
            old_host = host::OldHost::from_takeover(win::to_utf8(args.takeover));
            manager = take_over(cfg, *old_host, data_dir);
            if (!manager)
                return 1;
        } else {
            manager = std::make_shared<host::SessionManager>(cfg);
        }
        auto server = serve(pipe_name, handlers_for(manager), taking_over);
        log_line(data_dir, std::string(taking_over ? "took over" : "started") +
                               " instance=" + cfg.host_instance_id +
                               " conpty=" + (bundled_conpty ? "bundled" : "inbox"));
        if (old_host)
            remove_old_exe_after(std::move(old_host));
        ::WaitForSingleObject(shutdown.get(), INFINITE);
        // Let the shutdown (or host.update) response reach the client before tearing down the pipe.
        ::Sleep(200);
        server->stop();
        if (manager->handed_over()) {
            // Every session belongs to the new host: let go without stopping anything.
            manager->finish_handover();
            log_line(data_dir, "handed over instance=" + cfg.host_instance_id);
            return 0;
        }
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
