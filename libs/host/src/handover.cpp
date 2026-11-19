// agent-host's self-update: the running host hands every session, live, to a new agent-host.
//
// The terminals keep running and the clients reconnect to the new host. This is the smallest
// mechanism Windows allows (design doc 7.4 OQ-09):
//
//   old host (host.update {exe})                     new host (agent-host --takeover)
//   1. starts the new exe with two private pipes ->  waits for the state
//   2. freezes every session (reader paused at a
//      parser ground state, input flushed, exit
//      not watched), duplicates each terminal's
//      handles INTO the new process, sends state ->  3. adopts: checks every handle (process id
//                                                       from the handle, job membership,
//                                                       kill-on-close, OpenConsole), restores
//                                                       engines; nothing runs yet
//                                                <-  "ready <instance> <version>"
//   4. answers the caller, stops serving, closes
//      its own handles (no ClosePseudoConsole
//      signal, no TerminateJobObject), "go", exits -> 5. starts every session, serves the pipe
//
// Until 4 the old host owns everything: any failure (or a timeout) ends the new process, whose
// duplicates then close, and the old host resumes as if nothing happened. The new host never
// takes a process by a PID: only handles the old host duplicated into it, checked one by one.
#include <windows.h>

#include <algorithm>
#include <future>
#include <set>
#include <stdexcept>
#include <thread>

#include "agentdesk/host/session_manager.hpp"
#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/terminal/bytes.hpp"

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

bool write_all(HANDLE h, std::string_view bytes) {
    while (!bytes.empty()) {
        DWORD n = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), 1u << 20));
        if (!::WriteFile(h, bytes.data(), chunk, &n, nullptr))
            return false;
        bytes.remove_prefix(n);
    }
    return true;
}

bool read_exact(HANDLE h, char* out, std::size_t len) {
    while (len > 0) {
        DWORD n = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(len, 1u << 20));
        if (!::ReadFile(h, out, chunk, &n, nullptr) || n == 0)
            return false;
        out += n;
        len -= n;
    }
    return true;
}

// One line ("ready ...", "error ...", "go"); nullopt when the pipe ended first.
std::optional<std::string> read_line(HANDLE h) {
    std::string line;
    for (;;) {
        char c = 0;
        if (!read_exact(h, &c, 1))
            return std::nullopt;
        if (c == '\n')
            return line;
        if (line.size() < 4096)
            line.push_back(c);
    }
}

std::string one_line(std::string s) {
    std::replace(s.begin(), s.end(), '\n', ' ');
    std::replace(s.begin(), s.end(), '\r', ' ');
    return s;
}

std::uint64_t handle_value(HANDLE h) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(h));
}
HANDLE handle_from(std::uint64_t v) {
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(v));
}

// The new agent-host, with three handles it inherits and nothing else: the two ends of the
// handover pipes and SYNCHRONIZE on this process.
win::UniqueHandle spawn_successor(const std::filesystem::path& exe, const std::wstring& pipe_name,
                                  const std::filesystem::path& data_dir, HANDLE child_in, HANDLE child_out,
                                  HANDLE self_for_child) {
    std::wstring takeover = std::to_wstring(handle_value(child_in)) + L"," +
                            std::to_wstring(handle_value(child_out)) + L"," +
                            std::to_wstring(handle_value(self_for_child));
    const std::vector<std::wstring> args = {L"--takeover", takeover, L"--pipe", pipe_name, L"--data-dir",
                                            data_dir.native()};
    std::wstring cmdline = win::build_command_line(exe.native(), args);
    HANDLE inherit[3] = {child_in, child_out, self_for_child};
    win::AttributeList attrs(1);
    attrs.update(PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit));
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = attrs.get();
    // Like spawn_detached (detached.cpp): not in the program's folder, so updates can rename it.
    std::filesystem::path cwd = exe.parent_path();
    {
        wchar_t home[MAX_PATH]{};
        const DWORD n = ::GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        std::error_code ec;
        if (n > 0 && n < MAX_PATH && std::filesystem::is_directory(std::wstring(home, n), ec))
            cwd = std::wstring(home, n);
    }
    PROCESS_INFORMATION pi{};
    const auto start = [&](DWORD flags) {
        return ::CreateProcessW(exe.c_str(), cmdline.data(), nullptr, nullptr, TRUE,
                                flags | EXTENDED_STARTUPINFO_PRESENT | DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT,
                                nullptr, cwd.c_str(), &si.StartupInfo, &pi) != FALSE;
    };
    if (!start(CREATE_BREAKAWAY_FROM_JOB) && !start(0))
        win::throw_last_error("CreateProcessW(new agent-host)");
    ::CloseHandle(pi.hThread);
    return win::UniqueHandle(pi.hProcess);
}

HANDLE duplicate_into(HANDLE target_process, HANDLE h) {
    HANDLE out = nullptr;
    if (!::DuplicateHandle(::GetCurrentProcess(), h, target_process, &out, 0, FALSE, DUPLICATE_SAME_ACCESS))
        win::throw_last_error("DuplicateHandle(into the new agent-host)");
    return out;
}

}  // namespace

// ---------------------------------------------------------------- old host

Response SessionManager::do_update(const Request& req) {
    std::unique_lock update_lock(update_mutex_, std::try_to_lock);
    if (!update_lock.owns_lock() || handed_over_)
        return protocol::fail(req.id, ErrorCode::kOverloaded, "agent-host is updating", true);
    const std::string exe_param = req.params.value("exe", "");
    const std::filesystem::path exe(win::to_wide(exe_param));
    std::error_code ec;
    if (exe_param.empty() || !exe.is_absolute() || !std::filesystem::is_regular_file(exe, ec))
        return protocol::fail(req.id, ErrorCode::kBadRequest, "exe must be an agent-host.exe on this computer");

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

    // The handover pipes: we write state on `to_new`, read answers on `from_new`.
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE child_in = nullptr;
    HANDLE to_new_raw = nullptr;
    HANDLE from_new_raw = nullptr;
    HANDLE child_out = nullptr;
    if (!::CreatePipe(&child_in, &to_new_raw, &inheritable, 0))
        return protocol::fail(req.id, ErrorCode::kInternal, "cannot create the handover pipe");
    win::UniqueHandle child_in_h(child_in);
    win::UniqueHandle to_new(to_new_raw);
    if (!::CreatePipe(&from_new_raw, &child_out, &inheritable, 0))
        return protocol::fail(req.id, ErrorCode::kInternal, "cannot create the handover pipe");
    win::UniqueHandle from_new(from_new_raw);
    win::UniqueHandle child_out_h(child_out);
    ::SetHandleInformation(to_new.get(), HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(from_new.get(), HANDLE_FLAG_INHERIT, 0);
    HANDLE self_raw = nullptr;
    if (!::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentProcess(), ::GetCurrentProcess(), &self_raw,
                           SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0))
        return protocol::fail(req.id, ErrorCode::kInternal, "cannot share this process's handle");
    win::UniqueHandle self_for_child(self_raw);

    win::UniqueHandle successor;
    try {
        successor = spawn_successor(exe, config_.pipe_name, config_.data_dir, child_in, child_out, self_raw);
    } catch (const std::exception& e) {
        return protocol::fail(req.id, ErrorCode::kSpawnFailed, e.what());
    }
    child_in_h.reset();
    child_out_h.reset();
    self_for_child.reset();

    // From here on nothing changes under the handover.
    updating_ = true;
    std::vector<ManagedPtr> frozen;
    std::vector<std::optional<Session::Frozen>> states;
    const auto give_up = [&](std::string why) {
        ::TerminateProcess(successor.get(), 1);  // its duplicates close with it
        ::WaitForSingleObject(successor.get(), 5000);
        for (const auto& m : frozen)
            m->session->resume();
        updating_ = false;
        return protocol::fail(req.id, ErrorCode::kInternal, std::move(why));
    };

    ByteWriter w;
    w.u32(kMagic);
    w.u32(kFormat);
    w.str(config_.host_instance_id);
    std::vector<std::string> succeeds = config_.succeeds;
    succeeds.push_back(config_.host_instance_id);
    if (succeeds.size() > kMaxSucceeds)
        succeeds.erase(succeeds.begin(), succeeds.end() - kMaxSucceeds);
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
            if (f->handles) {
                // Duplicated into the new process: they are its handles, valid there only.
                auto& h = *f->handles;
                h.signal = duplicate_into(successor.get(), h.signal);
                h.reference = duplicate_into(successor.get(), h.reference);
                h.conhost = duplicate_into(successor.get(), h.conhost);
                h.input_write = duplicate_into(successor.get(), h.input_write);
                h.output_read = duplicate_into(successor.get(), h.output_read);
                h.process = duplicate_into(successor.get(), h.process);
                h.job = duplicate_into(successor.get(), h.job);
            }
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
            if (h) {
                for (HANDLE x : {h->signal, h->reference, h->conhost, h->input_write, h->output_read, h->process,
                                 h->job})
                    w.u64(handle_value(x));
                w.u32(h->pid);
            }
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

    // Send, then wait for "ready" (or the new host's end).
    ByteWriter len;
    len.u64(blob.size());
    auto answer = std::async(std::launch::async, [from = from_new.get()] { return read_line(from); });
    if (!write_all(to_new.get(), len.data()) || !write_all(to_new.get(), blob)) {
        ::TerminateProcess(successor.get(), 1);
        answer.wait();
        return give_up("the new agent-host stopped before it got the terminals");
    }
    if (answer.wait_for(kReadyTimeout) != std::future_status::ready) {
        ::TerminateProcess(successor.get(), 1);  // ends the read too
        answer.wait();
        return give_up("the new agent-host did not answer");
    }
    const auto line = answer.get();
    if (!line || line->rfind("ready ", 0) != 0) {
        std::string why = line && line->rfind("error ", 0) == 0 ? line->substr(6)
                                                                : std::string("the new agent-host stopped");
        return give_up(why);
    }
    // "ready <instance id> <version>"
    const std::string rest = line->substr(6);
    const auto space = rest.find(' ');
    const std::string instance = rest.substr(0, space);
    const std::string version = space == std::string::npos ? std::string() : rest.substr(space + 1);

    handed_over_ = true;
    successor_ = Successor{std::move(successor), std::move(to_new), std::move(frozen)};
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
    write_all(successor_->to_new.get(), "go\n");
    successor_->to_new.reset();
}

// ---------------------------------------------------------------- new host

SessionManager::SessionManager(HostConfig config, Adopting) : config_(std::move(config)) {
    load_profiles();
}

std::shared_ptr<SessionManager> receive_handover(HostConfig config, HANDLE from_old, HANDLE to_old) {
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

    std::shared_ptr<SessionManager> manager(new SessionManager(std::move(config), SessionManager::Adopting{}));
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
                    win::PtyHandles h;
                    for (HANDLE* x :
                         {&h.signal, &h.reference, &h.conhost, &h.input_write, &h.output_read, &h.process, &h.job})
                        *x = handle_from(r.u64());
                    h.pid = r.u32();
                    handles = h;
                }
                if (!r.ok())
                    refuse("the handover is damaged");
                const std::string id = m->record.id;
                SessionConfig sc;
                sc.api = self.config_.conpty;
                SessionListener listener;
                SessionManager* raw = manager.get();
                listener.on_snapshot = [raw, id](std::shared_ptr<const terminal::ScreenSnapshot> s) {
                    raw->fanout(id, std::move(s));
                };
                listener.on_info = [raw, id](const SessionInfo& info) { raw->on_session_info(id, info); };
                try {
                    m->session = Session::adopt(std::move(sc), std::move(listener), state, handles);
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
    if (!write_all(to_old, "ready " + self.config_.host_instance_id + " " + self.config_.host_version + "\n")) {
        manager->abandon_adopted();
        throw std::runtime_error("the old agent-host went away");
    }
    return manager;
}

bool await_handover_go(HANDLE from_old, HANDLE old_process) {
    const auto line = read_line(from_old);
    if (line && *line == "go")
        return true;
    // The pipe ended without "go": only a finished old host has let go of everything (its
    // handles closed with it). One still alive gave up and owns the sessions again.
    return ::WaitForSingleObject(old_process, 10000) == WAIT_OBJECT_0;
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
