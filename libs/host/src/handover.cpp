// agent-host's self-update: the running host hands every session, live, to a new agent-host.
//
// The terminals keep running and the clients reconnect to the new host (design doc 7.4 OQ-09,
// ADR-30). The channel and how a terminal is passed are the system's (handover_os.hpp):
//
//   old host (host.update {exe})                     new host (agent-host --takeover)
//   1. starts the new exe with a private channel ->  waits for the state
//   2. freezes every session (reader paused at a
//      parser ground state, input flushed, exit
//      not watched), passes each terminal, sends
//      the state                                 ->  3. adopts: checks every terminal, restores
//                                                       engines; nothing runs yet
//                                                <-  "ready <instance> <version>"
//   4. answers the caller, stops serving, closes
//      its own side of each terminal without
//      ending anything, "go", exits              ->  5. starts every session, serves the endpoint
//
// Until 4 the old host owns everything: any failure (or a timeout) ends the new process, what was
// passed to it closes with it, and the old host resumes as if nothing happened.
#include "agentdesk/host/handover.hpp"

#include <algorithm>
#include <future>
#include <stdexcept>

#include "agentdesk/host/session_manager.hpp"
#include "agentdesk/platform/text.hpp"
#include "agentdesk/terminal/bytes.hpp"
#include "handover_os.hpp"

namespace agentdesk::host {

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

std::string one_line(std::string s) {
    std::replace(s.begin(), s.end(), '\n', ' ');
    std::replace(s.begin(), s.end(), '\r', ' ');
    return s;
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
    std::unique_ptr<NewHost> host;
    std::vector<ManagedPtr> sessions;  // frozen, handed over
};

void SessionManager::SuccessorDeleter::operator()(Successor* p) const noexcept { delete p; }

// ---------------------------------------------------------------- old host

Response SessionManager::do_update(const Request& req) {
    std::unique_lock update_lock(update_mutex_, std::try_to_lock);
    if (!update_lock.owns_lock() || handed_over_)
        return protocol::fail(req.id, ErrorCode::kOverloaded, "agent-host is updating", true);
    const std::string exe_param = req.params.value("exe", "");
    const std::filesystem::path exe(platform::to_wide(exe_param));
    std::error_code ec;
    if (exe_param.empty() || !exe.is_absolute() || !std::filesystem::is_regular_file(exe, ec))
        return protocol::fail(req.id, ErrorCode::kBadRequest,
                              "exe must be an agent-host program on this computer");

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

    std::unique_ptr<NewHost> successor;
    try {
        successor = NewHost::start(exe, config_.pipe_name, config_.data_dir);
    } catch (const std::exception& e) {
        return protocol::fail(req.id, ErrorCode::kSpawnFailed, e.what());
    }

    // From here on nothing changes under the handover.
    updating_ = true;
    std::vector<ManagedPtr> frozen;
    std::vector<std::optional<Session::Frozen>> states;
    const auto give_up = [&](std::string why) {
        successor->end();
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
                successor->add_terminal(*f->handles);
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
                NewHost::write_handles(w, *h);
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
    auto answer = std::async(std::launch::async, [&] { return successor->read_line(); });
    if (!successor->write_all(len.data()) || !successor->write_all(blob) || !successor->send_terminals()) {
        successor->kill();  // ends the read too
        answer.wait();
        return give_up("the new agent-host stopped before it got the terminals");
    }
    if (answer.wait_for(kReadyTimeout) != std::future_status::ready) {
        successor->kill();
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
    successor_.reset(new Successor{std::move(successor), std::move(frozen)});
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
    successor_->host->write_all("go\n");
    successor_->host->close_channel();
}

// ---------------------------------------------------------------- new host

SessionManager::SessionManager(HostConfig config, Adopting) : config_(std::move(config)) {
    load_profiles();
}

// receive_handover builds the manager from private parts; SessionManager befriends this.
struct HandoverAccess {
    static std::shared_ptr<SessionManager> receive(HostConfig config, OldHost& old_host);
};

std::shared_ptr<SessionManager> receive_handover(HostConfig config, OldHost& old_host) {
    return HandoverAccess::receive(std::move(config), old_host);
}

std::shared_ptr<SessionManager> HandoverAccess::receive(HostConfig config, OldHost& old_host) {
    const auto refuse = [&](const std::string& why) {
        old_host.write_all("error " + one_line(why) + "\n");
        throw std::runtime_error(why);
    };
    char len_bytes[8];
    if (!old_host.read_exact(len_bytes, sizeof len_bytes))
        throw std::runtime_error("the old agent-host sent nothing");
    const std::uint64_t len = ByteReader(std::string_view(len_bytes, 8)).u64();
    if (len > (1ull << 34))
        refuse("handover too large");
    std::string blob(static_cast<std::size_t>(len), '\0');
    if (!old_host.read_exact(blob.data(), blob.size()))
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
                std::optional<platform::PtyHandles> handles;
                if (r.boolean()) {
                    handles = old_host.read_handles(r);
                    if (!handles)
                        refuse("a terminal did not arrive");
                }
                if (!r.ok())
                    refuse("the handover is damaged");
                const std::string id = m->record.id;
                SessionConfig sc;
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
    if (!old_host.write_all("ready " + self.config_.host_instance_id + " " + self.config_.host_version + "\n")) {
        manager->abandon_adopted();
        throw std::runtime_error("the old agent-host went away");
    }
    return manager;
}

bool await_handover_go(OldHost& old_host) {
    const auto line = old_host.read_line();
    if (line && *line == "go")
        return true;
    // The channel ended without "go": only a finished old host has let go of everything. One still
    // alive gave up and owns the sessions again.
    return old_host.wait_exit(std::chrono::seconds(10));
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
