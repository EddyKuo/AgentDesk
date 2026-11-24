// agent-host's self-update on macOS (ADR-30, docs/design/platform-abstraction.md 5.5): the running
// host hands every session, live, to a new agent-host. Same steps and state format as on Windows
// (handover.cpp); only the transport differs:
//
//   - one private socketpair: the new host gets its end as fd 3 (`agent-host --takeover <old pid>`)
//   - the state blob goes over it, then each live terminal's pty master as SCM_RIGHTS, in order
//   - the new host checks each terminal (a pty master; the root is alive, ours, leads its own
//     session, and that session owns the terminal) before it says "ready"
//
// Until "go" the old host owns everything: any failure ends the new process, whose fd copies
// then close, and the old host resumes. The new host never takes a process by a PID alone.
#include <algorithm>
#include <array>
#include <cstring>
#include <future>
#include <set>
#include <stdexcept>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "agentdesk/host/handover.hpp"
#include "agentdesk/host/session_manager.hpp"
#include "agentdesk/platform/system.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/terminal/bytes.hpp"

extern char** environ;  // NOLINT(readability-redundant-declaration): not declared by unistd.h on macOS

namespace agentdesk::host {

namespace win = platform::win;
using protocol::ErrorCode;
using protocol::Json;
using protocol::Request;
using protocol::Response;
using terminal::ByteReader;
using terminal::ByteWriter;

namespace {

constexpr std::uint32_t kMagic = 0x4F484441u;  // "ADHO"
constexpr std::uint32_t kFormat = 1;
constexpr auto kFreezeTimeout = std::chrono::seconds(3);
constexpr auto kReadyTimeout = std::chrono::seconds(30);
constexpr std::size_t kMaxSucceeds = 16;

bool write_all(int fd, std::string_view bytes) {
    while (!bytes.empty()) {
        const ssize_t n = ::send(fd, bytes.data(), bytes.size(), 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        bytes.remove_prefix(static_cast<std::size_t>(n));
    }
    return true;
}

bool read_exact(int fd, char* out, std::size_t len) {
    while (len > 0) {
        const ssize_t n = ::read(fd, out, len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        out += n;
        len -= static_cast<std::size_t>(n);
    }
    return true;
}

// One line ("ready ...", "error ...", "go"); nullopt when the socket ended first.
std::optional<std::string> read_line(int fd) {
    std::string line;
    for (;;) {
        char c = 0;
        if (!read_exact(fd, &c, 1))
            return std::nullopt;
        if (c == '\n')
            return line;
        if (line.size() < 4096)
            line.push_back(c);
    }
}

// One descriptor with a one-byte payload.
bool send_fd(int channel, int fd) {
    char byte = 'F';
    iovec iov{&byte, 1};
    alignas(cmsghdr) std::array<char, CMSG_SPACE(sizeof(int))> control{};
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.data();
    msg.msg_controllen = control.size();
    cmsghdr* c = CMSG_FIRSTHDR(&msg);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(c), &fd, sizeof(int));
    for (;;) {
        const ssize_t n = ::sendmsg(channel, &msg, 0);
        if (n == 1)
            return true;
        if (n < 0 && errno == EINTR)
            continue;
        return false;
    }
}

// The descriptor sent with the next byte, or -1.
int receive_fd(int channel) {
    char byte = 0;
    iovec iov{&byte, 1};
    alignas(cmsghdr) std::array<char, CMSG_SPACE(sizeof(int))> control{};
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.data();
    msg.msg_controllen = control.size();
    ssize_t n = -1;
    do {
        n = ::recvmsg(channel, &msg, 0);
    } while (n < 0 && errno == EINTR);
    if (n != 1)
        return -1;
    for (cmsghdr* c = CMSG_FIRSTHDR(&msg); c != nullptr; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS &&
            c->cmsg_len == CMSG_LEN(sizeof(int))) {
            int fd = -1;
            std::memcpy(&fd, CMSG_DATA(c), sizeof(int));
            ::fcntl(fd, F_SETFD, FD_CLOEXEC);
            return fd;
        }
    }
    return -1;
}

std::string one_line(std::string s) {
    std::replace(s.begin(), s.end(), '\n', ' ');
    std::replace(s.begin(), s.end(), '\r', ' ');
    return s;
}

bool process_alive(pid_t pid) {
    return ::kill(pid, 0) == 0 || errno == EPERM;
}

// The new agent-host with its end of the channel as fd 3 and nothing else inherited, in its own
// session (it outlives us), started in the home folder so updates can replace the program.
pid_t spawn_successor(const std::filesystem::path& exe, const std::wstring& pipe_name,
                      const std::filesystem::path& data_dir, int child_end) {
    std::vector<std::string> argv_strings = {
        exe.string(), "--takeover",     std::to_string(::getpid()), "--pipe", win::to_utf8(pipe_name),
        "--data-dir", data_dir.string()};
    std::vector<char*> argv;
    for (auto& a : argv_strings)
        argv.push_back(a.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, child_end, 3);
    const std::string home = platform::home_dir().string();
    posix_spawn_file_actions_addchdir_np(&actions, home.c_str());
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t all;
    sigfillset(&all);
    posix_spawnattr_setsigdefault(&attr, &all);
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT | POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGDEF |
                                        POSIX_SPAWN_SETSIGMASK);
    pid_t pid = -1;
    const int err = ::posix_spawn(&pid, exe.c_str(), &actions, &attr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (err != 0)
        throw std::system_error(err, std::generic_category(), "posix_spawn(new agent-host)");
    return pid;
}

void end_successor(pid_t pid) {
    ::kill(pid, SIGKILL);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
}

void write_handles(ByteWriter& w, const win::PtyHandles& h) {
    w.u32(h.pid);
}

win::PtyHandles read_handles(ByteReader& r) {
    win::PtyHandles h;
    h.pid = r.u32();
    return h;
}
// The instance ids this host's successor will name in hello: ours appended, newest last, at
// most kMaxSucceeds.
std::vector<std::string> successor_lineage(const HostConfig& config) {
    std::vector<std::string> succeeds = config.succeeds;
    succeeds.push_back(config.host_instance_id);
    if (succeeds.size() > kMaxSucceeds)
        succeeds.erase(succeeds.begin(), succeeds.end() - kMaxSucceeds);
    return succeeds;
}

// The new host's "ready <instance id> <version>" line, split.
struct ReadyLine {
    std::string instance;
    std::string version;
};
ReadyLine parse_ready(const std::string& line) {
    const std::string rest = line.substr(6);
    const auto space = rest.find(' ');
    return {rest.substr(0, space), space == std::string::npos ? std::string() : rest.substr(space + 1)};
}

}  // namespace

struct SessionManager::Successor {
    std::uint32_t pid = 0;
    int channel = -1;  // closed by finish_handover()
    std::vector<ManagedPtr> sessions;  // frozen, handed over
};

void SessionManager::SuccessorDeleter::operator()(Successor* p) const noexcept { delete p; }

// ---------------------------------------------------------------- old host

Response SessionManager::do_update(const Request& req) {
    std::unique_lock update_lock(update_mutex_, std::try_to_lock);
    if (!update_lock.owns_lock() || handed_over_)
        return protocol::fail(req.id, ErrorCode::kOverloaded, "agent-host is updating", true);
    const std::string exe_param = req.params.value("exe", "");
    const std::filesystem::path exe(win::to_wide(exe_param));
    std::error_code ec;
    if (exe_param.empty() || !exe.is_absolute() || !std::filesystem::is_regular_file(exe, ec))
        return protocol::fail(req.id, ErrorCode::kBadRequest,
                              "exe must be an agent-host.exe on this computer");

    // Every live terminal must be movable before anything stops.
    std::vector<ManagedPtr> all;
    {
        std::lock_guard lock(mutex_);
        for (const auto& [id, m] : sessions_)
            all.push_back(m);
    }
    for (const auto& m : all) {
        std::string why;
        if (m->session && !m->session->can_hand_over(&why))
            return protocol::fail(req.id, ErrorCode::kInternal, why);
    }

    // The handover channel: our end, and the new host's end that becomes its fd 3.
    std::array<int, 2> pair{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair.data()) != 0)
        return protocol::fail(req.id, ErrorCode::kInternal, "cannot create the handover channel");
    ::fcntl(pair[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(pair[1], F_SETFD, FD_CLOEXEC);
    const int one = 1;
    ::setsockopt(pair[0], SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    pid_t successor = -1;
    try {
        successor = spawn_successor(exe, config_.pipe_name, config_.data_dir, pair[1]);
    } catch (const std::exception& e) {
        ::close(pair[0]);
        ::close(pair[1]);
        return protocol::fail(req.id, ErrorCode::kSpawnFailed, e.what());
    }
    ::close(pair[1]);  // the new host has its copy
    const int channel = pair[0];

    // From here on nothing changes under the handover.
    updating_ = true;
    std::vector<ManagedPtr> frozen;
    std::vector<std::optional<Session::Frozen>> states;
    std::vector<int> fds;  // pty masters, sent after the state in the order it lists them
    const auto give_up = [&](std::string why) {
        end_successor(successor);  // its fd copies close with it
        ::close(channel);
        for (const auto& m : frozen)
            m->session->resume();
        updating_ = false;
        return protocol::fail(req.id, ErrorCode::kInternal, std::move(why));
    };

    ByteWriter w;
    w.u32(kMagic);
    w.u32(kFormat);
    w.str(config_.host_instance_id);
    const auto succeeds = successor_lineage(config_);
    w.u64(succeeds.size());
    for (const auto& s : succeeds)
        w.str(s);

    try {
        for (const auto& m : all) {
            if (!m->session)
                continue;
            std::string why;
            auto f = m->session->freeze(kFreezeTimeout, &why);
            if (!f) {
                m->session->resume();
                return give_up("terminal " + m->record.id.substr(0, 8) + ": " + why);
            }
            frozen.push_back(m);
            if (f->handles)
                fds.push_back(f->handles->master_fd);
            states.push_back(std::move(f));
        }
    } catch (const std::exception& e) {
        return give_up(e.what());
    }

    {
        std::lock_guard lock(mutex_);
        w.u64(all.size());
        std::size_t next_state = 0;
        const auto now = std::chrono::steady_clock::now();
        for (const auto& m : all) {
            w.str(to_json(m->record).dump());
            w.str(m->pending_task);
            w.i64(std::chrono::duration_cast<std::chrono::milliseconds>(m->task_after - now).count());
            w.u64(m->pending_reports.size());
            for (const auto& r : m->pending_reports)
                w.str(r);
            const std::optional<Session::Frozen>* f = m->session ? &states[next_state++] : nullptr;
            const bool has_state = f != nullptr && !(*f)->state.empty();
            w.boolean(has_state);
            if (!has_state)
                continue;
            w.str((*f)->state);
            const auto& h = (*f)->handles;
            w.boolean(h.has_value());
            if (h)
                write_handles(w, *h);
        }
        w.boolean(ui_state_loaded_);
        w.str(ui_state_.dump());
        w.u64(ui_state_rev_);
        w.u64(dedupe_.size());
        for (const auto& [id, d] : dedupe_) {
            w.str(id);
            w.i64(std::chrono::duration_cast<std::chrono::milliseconds>(now - d.at).count());
            w.str(protocol::serialize(d.response));
        }
    }
    w.u32(kMagic);
    const std::string blob = w.take();

    // Send the state and the terminals, then wait for "ready" (or the new host's end).
    ByteWriter len;
    len.u64(blob.size());
    auto answer = std::async(std::launch::async, [channel] { return read_line(channel); });
    bool sent = write_all(channel, len.data()) && write_all(channel, blob);
    for (std::size_t i = 0; sent && i < fds.size(); ++i)
        sent = send_fd(channel, fds[i]);
    if (!sent) {
        ::kill(successor, SIGKILL);
        answer.wait();
        return give_up("the new agent-host stopped before it got the terminals");
    }
    if (answer.wait_for(kReadyTimeout) != std::future_status::ready) {
        ::kill(successor, SIGKILL);  // ends the read too
        answer.wait();
        return give_up("the new agent-host did not answer");
    }
    const auto line = answer.get();
    if (!line || line->rfind("ready ", 0) != 0) {
        std::string why = line && line->rfind("error ", 0) == 0 ? line->substr(6)
                                                                : std::string("the new agent-host stopped");
        return give_up(why);
    }
    const auto [instance, version] = parse_ready(*line);

    handed_over_ = true;
    successor_.reset(new Successor{static_cast<std::uint32_t>(successor), channel, std::move(frozen)});
    if (config_.on_handed_over)
        config_.on_handed_over();
    // `agentctl update --json` prints this "now" text as is.
    return protocol::ok(req.id, {{"now", "agent-host " + version}, {"host_instance_id", instance}});
}

void SessionManager::finish_handover() {
    if (!successor_)
        return;
    std::vector<std::shared_ptr<Session>> released;
    {
        std::lock_guard lock(mutex_);
        for (const auto& m : successor_->sessions) {
            m->session->release_after_handover();
            released.push_back(std::move(m->session));
        }
        sessions_.clear();
    }
    released.clear();  // actors and writers stop; the processes and their jobs carry on
    write_all(successor_->channel, "go\n");
    ::close(successor_->channel);
    successor_->channel = -1;
}

// ---------------------------------------------------------------- new host

SessionManager::SessionManager(HostConfig config, Adopting) : config_(std::move(config)) {
    load_profiles();
}

// receive_handover builds the manager from private parts; SessionManager befriends this.
struct HandoverAccess {
    static std::shared_ptr<SessionManager> receive(HostConfig config, int channel);
};

std::shared_ptr<SessionManager> receive_handover(HostConfig config, int channel) {
    return HandoverAccess::receive(std::move(config), channel);
}

std::shared_ptr<SessionManager> HandoverAccess::receive(HostConfig config, int channel) {
    const int from_old = channel;
    const int to_old = channel;
    const auto refuse = [&](const std::string& why) {
        write_all(to_old, "error " + one_line(why) + "\n");
        throw std::runtime_error(why);
    };
    char len_bytes[8];
    if (!read_exact(from_old, len_bytes, sizeof len_bytes))
        throw std::runtime_error("the old agent-host sent nothing");
    const std::uint64_t len = ByteReader(std::string_view(len_bytes, 8)).u64();
    if (len > (1ull << 34))
        refuse("handover too large");
    std::string blob(static_cast<std::size_t>(len), '\0');
    if (!read_exact(from_old, blob.data(), blob.size()))
        throw std::runtime_error("the old agent-host stopped sending");

    ByteReader r(blob);
    if (r.u32() != kMagic || r.u32() != kFormat)
        refuse("this agent-host does not read that agent-host's handover");
    (void)r.str();  // the old instance id (also the last of `succeeds`)
    const auto n_succeeds = r.count(8);
    for (std::uint64_t i = 0; i < n_succeeds && r.ok(); ++i)
        config.succeeds.push_back(r.str());

    std::shared_ptr<SessionManager> manager(
        new SessionManager(std::move(config), SessionManager::Adopting{}));
    auto& self = *manager;
    try {
        const auto n = r.count(8);
        const auto now = std::chrono::steady_clock::now();
        for (std::uint64_t i = 0; i < n && r.ok(); ++i) {
            auto m = std::make_shared<SessionManager::Managed>();
            const Json rec = Json::parse(r.str(), nullptr, false);
            if (rec.is_discarded() || !rec.is_object())
                refuse("a session record is damaged");
            m->record = session_record_from_json(rec);
            m->pending_task = r.str();
            m->task_after = now + std::chrono::milliseconds(r.i64());
            const auto reports = r.count(8);
            for (std::uint64_t k = 0; k < reports && r.ok(); ++k)
                m->pending_reports.push_back(r.str());
            if (r.boolean()) {
                const std::string state = r.str();
                std::optional<win::PtyHandles> handles;
                if (r.boolean()) {
                    handles = read_handles(r);
                    // The terminals' pty masters follow the state, in the order it lists them.
                    handles->master_fd = receive_fd(from_old);
                    if (handles->master_fd < 0)
                        refuse("a terminal did not arrive");
                }
                if (!r.ok())
                    refuse("the handover is damaged");
                const std::string id = m->record.id;
                SessionConfig sc;
                sc.api = self.config_.conpty;
                try {
                    m->session = Session::adopt(std::move(sc), self.listener_for(id), state, handles);
                } catch (const std::exception& e) {
                    refuse("terminal " + id.substr(0, 8) + ": " + e.what());
                }
            }
            m->last_process_state = m->session ? std::string(to_string(m->session->info().process_state))
                                               : m->record.process_state;
            m->record.process_state = m->last_process_state;
            self.sessions_.emplace(m->record.id, m);
        }
        self.ui_state_loaded_ = r.boolean();
        const Json doc = Json::parse(r.str(), nullptr, false);
        self.ui_state_ = doc.is_object() ? doc : Json::object();
        self.ui_state_rev_ = r.u64();
        const auto n_dedupe = r.count(16);
        for (std::uint64_t i = 0; i < n_dedupe && r.ok(); ++i) {
            std::string id = r.str();
            const auto age = r.i64();
            auto parsed = protocol::parse_message(r.str());
            if (parsed.message)
                if (const auto* resp = std::get_if<Response>(&*parsed.message))
                    self.dedupe_[id] = {now - std::chrono::milliseconds(age), *resp};
        }
        if (r.u32() != kMagic || !r.ok() || !r.at_end())
            refuse("the handover is damaged");
    } catch (...) {
        manager->abandon_adopted();
        throw;
    }
    if (!write_all(to_old,
                   "ready " + self.config_.host_instance_id + " " + self.config_.host_version + "\n")) {
        manager->abandon_adopted();
        throw std::runtime_error("the old agent-host went away");
    }
    return manager;
}

bool await_handover_go(int channel, std::uint32_t old_pid) {
    const auto line = read_line(channel);
    if (line && *line == "go")
        return true;
    // The channel ended without "go": only an old host that has exited let go of everything. One
    // still alive gave up and owns the sessions again.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!process_alive(static_cast<pid_t>(old_pid)))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

void SessionManager::start_adopted() {
    std::vector<Session*> adopted;
    {
        std::lock_guard lock(mutex_);
        for (const auto& [id, m] : sessions_)
            if (m->session)
                adopted.push_back(m->session.get());
        persist_locked();
    }
    for (Session* s : adopted)
        s->start_adopted();
    start_maintenance();
}

void SessionManager::abandon_adopted() {
    std::map<std::string, ManagedPtr> doomed;
    {
        std::lock_guard lock(mutex_);
        doomed.swap(sessions_);
    }
    for (auto& [id, m] : doomed)
        if (m->session)
            m->session->release_after_handover();
    doomed.clear();
}

}  // namespace agentdesk::host
