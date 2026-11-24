// agent-host on macOS (ADR-30): the per-user background process that owns every session (design
// doc 2.1, 2.2). Runs as the logged-in user in its own session, detached from whoever started it.
// Exits on host.shutdown, or after handing every session to a newer agent-host (host.update).
//   agent-host [--pipe PATH] [--data-dir DIR]
//   agent-host --version                          "agent-host <version>" (agentctl update)
//   agent-host --takeover OLD_PID [--pipe PATH] [--data-dir DIR]
//       started by a running host's host.update with the handover channel as fd 3
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

#include <fcntl.h>
#include <unistd.h>

#include "agentdesk/host/handover.hpp"
#include "agentdesk/host/session_manager.hpp"
#include "agentdesk/host/store.hpp"
#include "agentdesk/ipc/host_launcher.hpp"
#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/ipc_transport.hpp"
#include "agentdesk/platform/win/conpty_api.hpp"
#include "agentdesk/platform/text.hpp"

namespace {

namespace win = agentdesk::platform::win;
namespace host = agentdesk::host;
namespace ipc = agentdesk::ipc;
namespace platform = agentdesk::platform;

constexpr int kHandoverFd = 3;  // the old host's end of the handover channel

// Diagnostics only: IDs, event types, error codes. Never input text or environment (3.9).
void log_line(const std::filesystem::path& dir, const std::string& line) {
    std::ofstream(dir / "host.log", std::ios::app)
        << host::iso8601_now() << " pid=" << ::getpid() << ' ' << line << '\n';
}

// The old host's socket closes just before it says "go"; give the endpoint a moment.
std::unique_ptr<ipc::PipeServer> serve(const std::string& name, const ipc::ServerHandlers& handlers,
                                       bool after_takeover) {
    for (int attempt = 0;; ++attempt) {
        try {
            return std::make_unique<ipc::PipeServer>(name, handlers);
        } catch (const std::system_error& e) {
            if (!after_takeover || e.code().value() != EADDRINUSE || attempt >= 100)
                throw;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

struct HostArgs {
    std::string pipe_name;
    std::filesystem::path data_dir;
    std::uint32_t takeover_from = 0;  // the old host's pid, or 0
    bool version = false;
};

HostArgs parse_command_line(int argc, char** argv) {
    HostArgs a;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--pipe" && i + 1 < argc)
            a.pipe_name = argv[++i];
        else if (arg == "--data-dir" && i + 1 < argc)
            a.data_dir = argv[++i];
        else if (arg == "--takeover" && i + 1 < argc)
            a.takeover_from = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (arg == "--version")
            a.version = true;
    }
    return a;
}

// --data-dir, else AGENTDESK_DATA_DIR (tests, portable use), else ~/Library/Application Support.
std::filesystem::path resolve_data_dir(std::filesystem::path given) {
    if (!given.empty())
        return given;
    const char* env = std::getenv("AGENTDESK_DATA_DIR");
    return env != nullptr && *env != '\0' ? std::filesystem::path(env) : host::default_data_dir();
}

// The new host's side of host.update: adopts the old host's sessions and waits for its "go".
// nullptr when the takeover did not go through (logged); the old host then keeps everything.
std::shared_ptr<host::SessionManager> take_over(const host::HostConfig& cfg, std::uint32_t old_pid,
                                                const std::filesystem::path& data_dir) {
    ::fcntl(kHandoverFd, F_SETFD, FD_CLOEXEC);
    std::shared_ptr<host::SessionManager> manager;
    try {
        manager = host::receive_handover(cfg, kHandoverFd);
    } catch (const std::exception& e) {
        log_line(data_dir, std::string("takeover refused: ") + e.what());
        ::close(kHandoverFd);
        return nullptr;
    }
    const bool go = host::await_handover_go(kHandoverFd, old_pid);
    ::close(kHandoverFd);
    if (!go) {
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

// Set by host.shutdown / host.update or a signal; main waits for it.
struct Shutdown {
    std::mutex mutex;
    std::condition_variable cv;
    bool requested = false;
    void request() {
        {
            std::lock_guard lock(mutex);
            requested = true;
        }
        cv.notify_all();
    }
    void wait() {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return requested; });
    }
};

}  // namespace

int main(int argc, char** argv) {
    const HostArgs args = parse_command_line(argc, argv);
    if (args.version) {
        std::printf("agent-host %s\n", AGENTDESK_VERSION_STRING);
        return 0;
    }
    // A client that goes away mid-write must not kill the host; sockets also use SO_NOSIGPIPE.
    std::signal(SIGPIPE, SIG_IGN);
    // Started from a terminal (developers): a hang-up there must not end the sessions.
    std::signal(SIGHUP, SIG_IGN);

    const bool custom_pipe = !args.pipe_name.empty();
    const std::string pipe_name = custom_pipe ? args.pipe_name : ipc::default_pipe_name();
    const std::filesystem::path data_dir = resolve_data_dir(args.data_dir);
    std::filesystem::create_directories(data_dir);
    const bool taking_over = args.takeover_from != 0;

    // One host per user (design doc 2.2 step 2). A second one just exits; its launcher then
    // connects to the existing host. A host taking over from the running one waits for its lock.
    const std::string lock_name =
        custom_pipe ? "host-test-" + std::to_string(std::hash<std::string>{}(pipe_name)) : "host";
    std::unique_ptr<platform::InstanceLock> instance = platform::InstanceLock::try_acquire(lock_name);
    if (!instance && !taking_over)
        return 0;

    Shutdown shutdown;
    host::HostConfig cfg;
    cfg.conpty = win::load_conpty({});
    cfg.data_dir = data_dir;
    cfg.ctl_path = ipc::sibling_executable(L"agentctl");
    cfg.pipe_name = pipe_name;
    cfg.host_instance_id = host::new_uuid();
    cfg.host_version = AGENTDESK_VERSION_STRING;
    cfg.on_shutdown = [&shutdown] { shutdown.request(); };
    cfg.on_handed_over = [&shutdown] { shutdown.request(); };

    try {
        std::shared_ptr<host::SessionManager> manager;
        if (taking_over) {
            manager = take_over(cfg, args.takeover_from, data_dir);
            if (!manager)
                return 1;
            // The old host has exited or is about to: its lock becomes ours.
            for (int i = 0; i < 100 && !instance; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                instance = platform::InstanceLock::try_acquire(lock_name);
            }
        } else {
            manager = std::make_shared<host::SessionManager>(cfg);
        }
        auto server = serve(pipe_name, handlers_for(manager), taking_over);
        log_line(data_dir, std::string(taking_over ? "took over" : "started") +
                               " instance=" + cfg.host_instance_id + " pty=posix");
        if (taking_over) {
            std::error_code ec;
            std::filesystem::remove(ipc::sibling_executable(L"agent-host.old"), ec);
        }
        shutdown.wait();
        // Let the shutdown (or host.update) response reach the client before tearing down the socket.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (manager->handed_over()) {
            // The new host listens on the same path once we let go: do not unlink it under it.
            server->stop();
            manager->finish_handover();
            log_line(data_dir, "handed over instance=" + cfg.host_instance_id);
            server.release();  // NOLINT: its destructor would remove the new host's socket file
            return 0;
        }
        server->stop();
        manager->stop_all();
        log_line(data_dir, "stopped instance=" + cfg.host_instance_id);
    } catch (const std::system_error& e) {
        log_line(data_dir, std::string("fatal: ") + e.what());
        return e.code().value() == EADDRINUSE ? 3 : 1;  // endpoint owned by a live server
    } catch (const std::exception& e) {
        log_line(data_dir, std::string("fatal: ") + e.what());
        return 1;
    }
    return 0;
}
