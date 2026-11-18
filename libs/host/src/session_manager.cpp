#include "agentdesk/host/session_manager.hpp"

#include <algorithm>
#include <cwctype>

#include "agentdesk/host/git_tools.hpp"
#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::host {

namespace win = platform::win;
using protocol::ErrorCode;
using protocol::Json;
using protocol::Request;
using protocol::Response;

namespace {

constexpr std::size_t kMaxHistoryLines = 200;  // design doc 3.7 history.read
constexpr std::size_t kMaxSnapshotChunkBytes = 900 * 1024;
constexpr std::size_t kMaxRecords = 200;
// Exited sessions keep their Session (threads, engine, history) so the final screen can still be
// read; only the newest few do, otherwise create/stop churn grows threads and memory forever
// (design doc 1.5 stability target). Older ones keep metadata only.
constexpr std::size_t kMaxExitedLive = 8;

Response bad(const Request& r, std::string msg) {
    return protocol::fail(r.id, ErrorCode::kBadRequest, std::move(msg));
}

// PowerShell's cd (Set-Location) does not change the process's Windows current folder, which is
// what the host reads to follow the shell. Like herdr, wrap the prompt: after the person's own
// prompt runs, copy the location into [Environment]::CurrentDirectory. Only for an interactive
// PowerShell the profile starts without its own -Command/-File; cmd needs nothing.
void add_folder_hook(platform::win::PtySpawnSpec& spec) {
    std::wstring exe = spec.executable.filename().native();
    for (auto& c : exe)
        c = static_cast<wchar_t>(std::towlower(c));
    if (exe != L"pwsh.exe" && exe != L"powershell.exe")
        return;
    for (const auto& a : spec.args) {
        std::wstring l = a;
        for (auto& c : l)
            c = static_cast<wchar_t>(std::towlower(c));
        if (l == L"-command" || l == L"-c" || l == L"-file" || l == L"-f" || l == L"-encodedcommand" ||
            l == L"-ec" || l == L"-noninteractive")
            return;
    }
    spec.args.push_back(L"-NoExit");
    spec.args.push_back(L"-Command");
    spec.args.push_back(
        L"$global:__agentdesk_prompt = $function:prompt; "
        L"function global:prompt { $r = & $global:__agentdesk_prompt; "
        L"try { $l = Get-Location; if ($l.Provider.Name -eq 'FileSystem') "
        L"{ [Environment]::CurrentDirectory = $l.ProviderPath } } catch {}; $r }");
}

std::optional<terminal::Key> key_from_name(std::string_view n) {
    static const std::pair<std::string_view, terminal::Key> kKeys[] = {
        {"enter", terminal::Key::kEnter},
        {"tab", terminal::Key::kTab},
        {"backspace", terminal::Key::kBackspace},
        {"escape", terminal::Key::kEscape},
        {"up", terminal::Key::kUp},
        {"down", terminal::Key::kDown},
        {"left", terminal::Key::kLeft},
        {"right", terminal::Key::kRight},
        {"insert", terminal::Key::kInsert},
        {"delete", terminal::Key::kDelete},
        {"home", terminal::Key::kHome},
        {"end", terminal::Key::kEnd},
        {"pageup", terminal::Key::kPageUp},
        {"pagedown", terminal::Key::kPageDown},
    };
    for (const auto& [name, key] : kKeys) {
        if (name == n)
            return key;
    }
    if (n.size() >= 2 && n[0] == 'f') {
        const int f = std::atoi(std::string(n.substr(1)).c_str());
        if (f >= 1 && f <= 12)
            return static_cast<terminal::Key>(static_cast<int>(terminal::Key::kF1) + f - 1);
    }
    return std::nullopt;
}

std::uint8_t mods_from_json(const Json& j) {
    std::uint8_t m = terminal::kModNone;
    if (!j.is_object())
        return m;
    if (j.value("ctrl", false))
        m |= terminal::kModCtrl;
    if (j.value("shift", false))
        m |= terminal::kModShift;
    if (j.value("alt", false))
        m |= terminal::kModAlt;
    return m;
}

std::u32string utf8_to_u32(const std::string& s) {
    std::u32string out;
    const std::wstring w = win::to_wide(s);
    for (std::size_t i = 0; i < w.size(); ++i) {
        char32_t c = w[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size()) {
            c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<char32_t>(w[++i]) - 0xDC00);
        }
        out.push_back(c);
    }
    return out;
}

bool has_control_chars(const std::string& s) {
    return std::any_of(s.begin(), s.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u < 0x20 || u == 0x7F;
    });
}

std::string line_text(const terminal::HistoryLine& line) {
    std::string text;
    for (const auto& cell : line.cells) {
        if (cell.width == 0)
            continue;
        if (cell.chars[0] == 0) {
            text.push_back(' ');
            continue;
        }
        for (const char32_t cp : cell.chars) {
            if (cp == 0)
                break;
            terminal::append_utf8(text, cp);
        }
    }
    while (!text.empty() && text.back() == ' ')
        text.pop_back();
    return text;
}

}  // namespace

SessionManager::SessionManager(HostConfig config) : config_(std::move(config)) {
    load_profiles();
    // Previous host's sessions: history only, never adopted by PID (design doc 2.2 step 6).
    if (const auto doc = read_json_with_backup(config_.data_dir / L"sessions.json")) {
        std::vector<SessionRecord> records;
        for (const auto& j : doc->value("sessions", Json::array()))
            records.push_back(session_record_from_json(j));
        for (auto& r : mark_orphans_lost(std::move(records))) {
            auto m = std::make_shared<Managed>();
            m->last_process_state = r.process_state;
            m->record = std::move(r);
            sessions_.emplace(m->record.id, m);
        }
        std::lock_guard lock(mutex_);
        persist_locked();
    }
    maintenance_ = std::thread([this] {
        while (!stopping_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            try {
                deliver_pending();
            } catch (const std::exception&) {
            }
        }
    });
}

SessionManager::~SessionManager() {
    stopping_ = true;
    if (maintenance_.joinable())
        maintenance_.join();
    stop_all();
    std::map<std::string, ManagedPtr> doomed;
    {
        std::lock_guard lock(mutex_);
        doomed.swap(sessions_);
    }
    doomed.clear();  // Session destructors join their actors outside mutex_
}

void SessionManager::load_profiles() {
    const auto file = config_.data_dir / L"profiles.json";
    if (const auto doc = read_json_with_backup(file)) {
        for (const auto& j : doc->value("profiles", Json::array()))
            profiles_.push_back(profile_from_json(j));
        return;
    }
    profiles_ = detect_default_profiles();
    Json list = Json::array();
    for (const auto& p : profiles_)
        list.push_back(to_json(p));
    write_json_atomic(file, {{"schema_version", kSchemaVersion}, {"profiles", list}});
}

void SessionManager::persist_locked() {
    std::vector<SessionRecord> records;
    for (const auto& [id, m] : sessions_)
        records.push_back(m->record);
    std::sort(records.begin(), records.end(),
              [](const auto& a, const auto& b) { return a.created_at < b.created_at; });
    if (records.size() > kMaxRecords)
        records.erase(records.begin(), records.end() - kMaxRecords);
    Json list = Json::array();
    for (const auto& r : records)
        list.push_back(to_json(r));
    try {
        write_json_atomic(config_.data_dir / L"sessions.json",
                          {{"schema_version", kSchemaVersion}, {"sessions", list}});
    } catch (const std::exception&) {
        // Disk full or locked: metadata is history only; running sessions are unaffected.
    }
}

void SessionManager::on_open(const ipc::ConnectionPtr&) {}

void SessionManager::on_close(const ipc::ConnectionPtr& conn) {
    std::lock_guard lock(mutex_);
    ui_watchers_.erase(conn->id());
    for (auto& [id, m] : sessions_) {
        m->subscribers.erase(conn->id());
        if (m->controller == conn->id())
            m->controller.reset();  // holder disconnected: released
    }
}

void SessionManager::on_message(const ipc::ConnectionPtr& conn, std::string payload) {
    auto parsed = protocol::parse_message(payload);
    if (!parsed.message) {
        // Unparseable or wrong version: reply when possible, then drop the client.
        protocol::Response r;
        r.id = "unknown";
        r.error = parsed.version_mismatch ? ErrorCode::kVersionMismatch : ErrorCode::kBadRequest;
        r.error_message = parsed.error;
        conn->send(protocol::serialize(r));
        if (parsed.version_mismatch)
            conn->close();
        return;
    }
    const auto* req = std::get_if<Request>(&*parsed.message);
    if (req == nullptr)
        return;  // clients do not send responses or events
    Response resp;
    try {
        resp = dispatch(conn, *req);
    } catch (const std::exception& e) {
        resp = protocol::fail(req->id, ErrorCode::kInternal, e.what());
    }
    conn->send(protocol::serialize(resp));
}

Response SessionManager::dispatch(const ipc::ConnectionPtr& conn, const Request& req) {
    const auto& m = req.method;
    if (m == "hello")
        return do_hello(req);
    if (m == "profile.list")
        return do_profile_list(req);
    if (m == "session.create")
        return do_create(req);
    if (m == "session.list")
        return do_list(req);
    if (m == "session.get")
        return do_get(req);
    if (m == "session.attach")
        return do_attach(conn, req);
    if (m == "session.detach")
        return do_detach(conn, req);
    if (m == "session.input")
        return do_input(conn, req);
    if (m == "session.resize")
        return do_resize(conn, req);
    if (m == "session.stop")
        return do_stop(req);
    if (m == "screen.snapshot")
        return do_snapshot(conn, req);
    if (m == "history.read")
        return do_history(req);
    if (m == "state.report")
        return do_state_report(req);
    if (m == "host.shutdown")
        return do_shutdown(req);
    if (m == "ui.state.get")
        return do_ui_state_get(conn, req);
    if (m == "ui.state.set")
        return do_ui_state_set(conn, req);
    if (m == "worktree.add")
        return do_worktree_add(req);
    if (m == "session.wait")
        return do_wait(req);
    if (m == "session.label")
        return do_label(req);
    if (m == "session.report")
        return do_report(req);
    return bad(req, "unknown method: " + m);
}

Response SessionManager::do_ui_state_get(const ipc::ConnectionPtr& conn, const Request& req) {
    std::lock_guard lock(mutex_);
    if (!ui_state_loaded_) {
        ui_state_loaded_ = true;
        if (const auto j = read_json_with_backup(config_.data_dir / L"ui-state.json")) {
            ui_state_ = j->value("doc", Json::object());
            ui_state_rev_ = j->value("rev", static_cast<std::uint64_t>(0));
        }
    }
    if (req.params.value("watch", false))
        ui_watchers_[conn->id()] = conn;
    return protocol::ok(req.id, {{"rev", ui_state_rev_}, {"doc", ui_state_}});
}

Response SessionManager::do_ui_state_set(const ipc::ConnectionPtr& conn, const Request& req) {
    const auto it = req.params.find("doc");
    if (it == req.params.end() || !it->is_object())
        return bad(req, "doc must be an object");
    if (it->dump().size() > 1024 * 1024)
        return bad(req, "doc larger than 1 MiB");
    std::lock_guard lock(mutex_);
    ui_state_loaded_ = true;
    ui_state_ = *it;
    ++ui_state_rev_;
    try {
        write_json_atomic(config_.data_dir / L"ui-state.json",
                          {{"schema_version", 1}, {"rev", ui_state_rev_}, {"doc", ui_state_}});
    } catch (const std::exception&) {
        // Like sessions.json: a write failure must not stop the host (kept in memory).
    }
    protocol::Event e;
    e.event = "ui.state";
    e.body = {{"rev", ui_state_rev_}, {"doc", ui_state_}, {"from", conn->id()}};
    const auto payload = protocol::serialize(e);
    for (auto w = ui_watchers_.begin(); w != ui_watchers_.end();) {
        auto c = w->second.lock();
        if (!c) {
            w = ui_watchers_.erase(w);
            continue;
        }
        if (c->id() != conn->id())
            c->send(payload);
        ++w;
    }
    return protocol::ok(req.id, {{"rev", ui_state_rev_}});
}

Response SessionManager::do_worktree_add(const Request& req) {
    const std::string repo = req.params.value("repo", "");
    const std::string branch = req.params.value("branch", "");
    std::string path = req.params.value("path", "");
    if (repo.empty() || branch.empty() || has_control_chars(branch) || branch.starts_with("-"))
        return bad(req, "repo and branch are required; branch may not start with '-'");
    const std::filesystem::path repo_path(win::to_wide(repo));
    std::error_code ec;
    if (!std::filesystem::is_directory(repo_path, ec))
        return protocol::fail(req.id, ErrorCode::kBadRequest, "repo folder does not exist: " + repo);
    if (path.empty()) {
        wchar_t home[MAX_PATH]{};
        const DWORD n = ::GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        const auto base = std::filesystem::path(std::wstring(home, n)) / L".agentdesk" / L"worktrees";
        const auto p = default_worktree_path(base, repo_path, branch).u8string();
        path.assign(p.begin(), p.end());
    }
    const std::wstring wbranch = win::to_wide(branch);
    const std::wstring wpath = win::to_wide(path);
    // New branch first; if it already exists, check the existing branch out.
    auto r = run_tool({L"git", L"-C", repo_path.native(), L"worktree", L"add", L"-b", wbranch, wpath},
                      repo_path, std::chrono::seconds(60));
    if (r.started && !r.timed_out && r.exit_code != 0 &&
        r.output.find("already exists") != std::string::npos &&
        r.output.find("branch") != std::string::npos) {
        r = run_tool({L"git", L"-C", repo_path.native(), L"worktree", L"add", wpath, wbranch}, repo_path,
                     std::chrono::seconds(60));
    }
    if (!r.started)
        return protocol::fail(req.id, ErrorCode::kSpawnFailed, "git was not found on PATH");
    if (r.timed_out)
        return protocol::fail(req.id, ErrorCode::kInternal, "git worktree add timed out");
    if (r.exit_code != 0)
        return protocol::fail(req.id, ErrorCode::kBadRequest, "git worktree add failed: " + r.output);
    return protocol::ok(req.id, {{"path", path}, {"branch", branch}, {"output", r.output}});
}

namespace {
constexpr auto kTaskFallback = std::chrono::seconds(8);

bool busy(AgentState s) {
    return s == AgentState::kWorking || s == AgentState::kBlocked;
}
}  // namespace

void SessionManager::deliver_pending() {
    struct Delivery {
        Session* session;
        std::string text;
    };
    std::vector<Delivery> out;
    {
        std::lock_guard lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        for (auto& [id, m] : sessions_) {
            if (!m->session)
                continue;
            const auto info = m->session->info();
            if (info.process_state != ProcessState::kRunning)
                continue;
            const bool has_source = !info.agent.source.empty();
            if (!m->pending_task.empty()) {
                // Wait for the agent to say it is ready (Idle); without any status source, give
                // it kTaskFallback to start.
                if ((has_source && info.agent.state == AgentState::kIdle) ||
                    (!has_source && now >= m->task_after) || now >= m->task_after + kTaskFallback * 2) {
                    out.push_back({m->session.get(), std::exchange(m->pending_task, {})});
                }
            }
            if (!m->pending_reports.empty() && !busy(info.agent.state) && m->pending_task.empty()) {
                for (auto& r : m->pending_reports)
                    out.push_back({m->session.get(), std::move(r)});
                m->pending_reports.clear();
            }
        }
        // Sessions are only destroyed outside mutex_ by prune_exited on request threads; the
        // send calls below are queued writes and do not block.
        for (const auto& d : out) {
            d.session->send(PasteInput{d.text});
            d.session->send(RawInput{"\r"});
        }
    }
}

Response SessionManager::do_wait(const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    const std::string until = req.params.value("until", "idle");
    if (until != "idle" && until != "blocked" && until != "exited" && until != "change")
        return bad(req, "until must be idle, blocked, exited or change");
    const auto timeout = std::chrono::milliseconds(
        std::clamp<long long>(req.params.value("timeout_ms", 600000LL), 0, 3600000LL));
    if (!m->session)
        return protocol::ok(
            req.id, {{"reached", until == "exited"}, {"process_state", "Lost"}, {"agent_state", "Unknown"}});
    const auto first = m->session->info();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool seen_working = first.agent.state == AgentState::kWorking;
    for (;;) {
        const auto info = m->session->info();
        seen_working = seen_working || info.agent.state == AgentState::kWorking;
        bool reached = false;
        if (info.process_state == ProcessState::kExited || info.process_state == ProcessState::kFailed)
            reached = true;  // nothing more will happen
        else if (until == "idle")
            // Finished: idle/done after it has worked (or right away if it never reports working
            // but is already idle).
            reached = (info.agent.state == AgentState::kIdle || info.agent.state == AgentState::kDone) &&
                      (seen_working || first.agent.state != AgentState::kUnknown);
        else if (until == "blocked")
            reached = info.agent.state == AgentState::kBlocked;
        else if (until == "change")
            reached = info.agent.state != first.agent.state || info.process_state != first.process_state;
        if (reached || std::chrono::steady_clock::now() >= deadline || stopping_) {
            return protocol::ok(req.id, {{"reached", reached},
                                         {"process_state", to_string(info.process_state)},
                                         {"agent_state", to_string(info.agent.state)},
                                         {"agent_message", info.agent.message}});
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

Response SessionManager::do_label(const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    const auto it = req.params.find("labels");
    if (it == req.params.end() || !it->is_object())
        return bad(req, "labels must be an object");
    std::lock_guard lock(mutex_);
    for (const auto& [k, v] : it->items()) {
        if (!v.is_string() || k.empty() || k.size() > 64 || has_control_chars(k) ||
            has_control_chars(v.get<std::string>()))
            return bad(req, "labels must be printable strings");
        if (v.get<std::string>().empty())
            m->record.labels.erase(k);
        else
            m->record.labels[k] = v.get<std::string>();
    }
    persist_locked();
    return protocol::ok(req.id, {{"labels", m->record.labels}});
}

// A sub-agent reports to the session that started it (or `to`): the text arrives in that
// agent's input, tagged with the sender, once it is not working or waiting for the user.
Response SessionManager::do_report(const Request& req) {
    std::optional<Response> err;
    const auto from = resolve(req, err);
    if (!from)
        return *err;
    const std::string text = req.params.value("text", "");
    if (text.empty() || text.size() > 64 * 1024 || text.find('\0') != std::string::npos)
        return bad(req, "text must be 1..64 KiB without NUL");
    std::lock_guard lock(mutex_);
    std::string to = req.params.value("to", "");
    if (to.empty())
        to = from->record.parent;
    if (to.empty())
        return protocol::fail(req.id, ErrorCode::kNotFound, "this session has no parent; pass \"to\"");
    const auto it = sessions_.find(to);
    if (it == sessions_.end() || !it->second->session)
        return protocol::fail(req.id, ErrorCode::kNotFound, "target session not found or not running: " + to);
    it->second->pending_reports.push_back("[from " + from->record.name + " " + from->record.id.substr(0, 8) +
                                          "] " + text);
    return protocol::ok(req.id, {{"queued", true}, {"to", to}});
}

void SessionManager::prune_exited() {
    std::vector<std::unique_ptr<Session>> doomed;
    {
        std::lock_guard lock(mutex_);
        std::vector<ManagedPtr> exited;
        for (const auto& [id, m] : sessions_) {
            if (m->session && m->session->info().process_state == ProcessState::kExited)
                exited.push_back(m);
        }
        if (exited.size() > kMaxExitedLive) {
            std::sort(exited.begin(), exited.end(), [](const auto& a, const auto& b) {
                return a->record.created_at < b->record.created_at;
            });
            for (std::size_t i = 0; i + kMaxExitedLive < exited.size(); ++i) {
                exited[i]->subscribers.clear();
                exited[i]->controller.reset();
                doomed.push_back(std::move(exited[i]->session));  // metadata stays in the list
            }
        }
        // Metadata-only records are bounded too (same limit as sessions.json).
        std::vector<ManagedPtr> history_only;
        for (const auto& [id, m] : sessions_) {
            if (!m->session)
                history_only.push_back(m);
        }
        if (history_only.size() > kMaxRecords) {
            std::sort(history_only.begin(), history_only.end(), [](const auto& a, const auto& b) {
                return a->record.created_at < b->record.created_at;
            });
            for (std::size_t i = 0; i + kMaxRecords < history_only.size(); ++i)
                sessions_.erase(history_only[i]->record.id);
        }
    }
    doomed.clear();  // joins actor/writer threads outside mutex_ (see header locking rule)
}

Response SessionManager::do_hello(const Request& req) {
    const int version = req.params.value("version", 1);
    if (version != protocol::kProtocolVersion) {
        return protocol::fail(req.id, ErrorCode::kVersionMismatch, "host speaks protocol v1");
    }
    return protocol::ok(
        req.id, {{"version", protocol::kProtocolVersion},
                 {"host_instance_id", config_.host_instance_id},
                 {"host_pid", ::GetCurrentProcessId()},
                 {"capabilities",
                  {{"osc7501", config_.conpty.bundled}, {"bundled_conpty", config_.conpty.bundled}}}});
}

Response SessionManager::do_profile_list(const Request& req) {
    std::lock_guard lock(mutex_);
    Json list = Json::array();
    for (const auto& p : profiles_)
        list.push_back(to_json(p));
    return protocol::ok(req.id, {{"profiles", list}});
}

Response SessionManager::do_create(const Request& req) {
    prune_exited();
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex_);
        std::erase_if(dedupe_,
                      [&](const auto& kv) { return now - kv.second.at > config_.request_dedupe_window; });
        if (const auto it = dedupe_.find(req.id); it != dedupe_.end())
            return it->second.response;  // retried request
    }
    const auto& p = req.params;
    const std::string profile_id = p.value("profile_id", "");
    const std::string cwd = p.value("cwd", "");
    if (profile_id.empty() || cwd.empty())
        return bad(req, "profile_id and cwd are required");
    std::optional<Profile> profile;
    {
        std::lock_guard lock(mutex_);
        for (const auto& pr : profiles_) {
            if (pr.id == profile_id)
                profile = pr;
        }
    }
    if (!profile)
        return protocol::fail(req.id, ErrorCode::kProfileInvalid, "unknown profile: " + profile_id);
    if (const auto problem = validate_profile(*profile))
        return protocol::fail(req.id, ErrorCode::kProfileInvalid, *problem);

    SessionRecord rec;
    rec.id = new_uuid();
    rec.name = p.value("name", profile->name);
    rec.cwd = cwd;
    rec.profile_id = profile_id;
    rec.created_at = iso8601_now();
    rec.process_state = "Starting";
    rec.parent = p.value("parent", "");
    if (!rec.parent.empty()) {
        std::lock_guard lock(mutex_);
        if (!sessions_.contains(rec.parent))
            return protocol::fail(req.id, ErrorCode::kNotFound, "parent session not found: " + rec.parent);
    }
    if (const auto it = p.find("labels"); it != p.end() && it->is_object()) {
        for (const auto& [k, v] : it->items()) {
            if (!v.is_string() || k.empty() || k.size() > 64 || has_control_chars(k) ||
                has_control_chars(v.get<std::string>()))
                return bad(req, "labels must be printable strings");
            rec.labels[k] = v.get<std::string>();
        }
    }
    const std::string task = p.value("task", "");
    if (task.size() > 64 * 1024)
        return bad(req, "task larger than 64 KiB");

    SessionConfig sc;
    sc.api = config_.conpty;
    sc.spec.executable = profile->executable;
    sc.spec.args = profile->args;
    for (const auto& a : p.value("args", Json::array()))
        sc.spec.args.push_back(win::to_wide(a.get<std::string>()));
    add_folder_hook(sc.spec);
    sc.spec.cwd = win::to_wide(cwd);  // never silently replaced (design doc 4.2)
    const int cols = std::clamp(p.value("cols", 120), 2, 1000);
    const int rows = std::clamp(p.value("rows", 40), 1, 500);
    sc.spec.size = {static_cast<short>(cols), static_cast<short>(rows)};
    sc.engine.cols = cols;
    sc.engine.rows = rows;
    sc.spec.env_overrides[L"AGENTDESK_SESSION_ID"] = win::to_wide(rec.id);
    sc.spec.env_overrides[L"AGENTDESK_CTL"] = config_.ctl_path.native();
    sc.spec.env_overrides[L"AGENTDESK_PIPE"] = config_.pipe_name;

    const std::string id = rec.id;
    SessionListener listener;
    listener.on_snapshot = [this, id](std::shared_ptr<const terminal::ScreenSnapshot> s) {
        fanout(id, std::move(s));
    };
    listener.on_info = [this, id](const SessionInfo& info) { on_session_info(id, info); };

    std::unique_ptr<Session> session;
    try {
        session = Session::start(std::move(sc), std::move(listener));  // outside mutex_ (listeners lock it)
    } catch (const std::exception& e) {
        auto r = protocol::fail(req.id, ErrorCode::kSpawnFailed, e.what());
        std::lock_guard lock(mutex_);
        dedupe_[req.id] = {now, r};
        return r;
    }
    auto m = std::make_shared<Managed>();
    m->record = rec;
    m->record.process_state = std::string(to_string(session->info().process_state));
    m->last_process_state = m->record.process_state;
    m->session = std::move(session);
    if (!task.empty()) {
        m->pending_task = task;
        m->task_after = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    }
    Response r = protocol::ok(req.id, {{"session_id", id}, {"process_state", m->record.process_state}});
    std::lock_guard lock(mutex_);
    sessions_.emplace(id, m);
    dedupe_[req.id] = {now, r};
    persist_locked();
    return r;
}

SessionManager::ManagedPtr SessionManager::resolve(const Request& req, std::optional<Response>& error) {
    const std::string key = req.params.value("session", req.params.value("session_id", ""));
    if (key.empty()) {
        error = bad(req, "session is required");
        return nullptr;
    }
    std::lock_guard lock(mutex_);
    if (const auto it = sessions_.find(key); it != sessions_.end())
        return it->second;
    ManagedPtr found;
    int matches = 0;
    for (const auto& [id, m] : sessions_) {
        if (m->record.name == key) {
            found = m;
            ++matches;
        }
    }
    if (matches == 1)
        return found;
    error =
        matches == 0
            ? protocol::fail(req.id, ErrorCode::kNotFound, "no session named or with id " + key)
            : protocol::fail(req.id, ErrorCode::kAmbiguous,
                             std::to_string(matches) + " sessions are named " + key + "; use the session id");
    return nullptr;
}

Json SessionManager::describe(const Managed& m) const {
    Json j = to_json(m.record);
    j["controller"] = m.controller ? Json(*m.controller) : Json(nullptr);
    j["subscribers"] = m.subscribers.size();
    if (m.session) {
        const auto info = m.session->info();
        const auto snap = m.session->snapshot();
        j["process_state"] = to_string(info.process_state);
        j["pid"] = info.pid;
        j["exit_code"] = info.exit_code ? Json(*info.exit_code) : Json(nullptr);
        j["agent_state"] = to_string(info.agent.state);
        j["agent_source"] = info.agent.source;
        j["agent_message"] = info.agent.message;
        j["agent_kind"] = terminal::to_string(info.agent.kind);
        j["last_agent_state"] = to_string(info.agent.last_state);
        j["still_running_after_interrupt"] = info.still_running_after_interrupt;
        j["remaining_processes"] = info.remaining_processes;
        j["cols"] = snap->cols;
        j["rows"] = snap->rows;
        j["history_truncated"] = info.history_truncated;
        j["backpressure_waits"] = info.backpressure_waits;
        const auto fg = m.session->foreground();
        j["program"] = fg.program;
        j["live_cwd"] = fg.cwd;  // the shell's folder after cd (empty: unknown, use cwd)
    } else {
        j["agent_state"] = "Unknown";
        j["history_only"] = true;  // Lost, or Exited and released to bound resources
    }
    return j;
}

Response SessionManager::do_list(const Request& req) {
    prune_exited();
    std::lock_guard lock(mutex_);
    Json list = Json::array();
    std::vector<ManagedPtr> ordered;
    for (const auto& [id, m] : sessions_)
        ordered.push_back(m);
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a->record.created_at < b->record.created_at; });
    for (const auto& m : ordered)
        list.push_back(describe(*m));
    return protocol::ok(req.id, {{"sessions", list}, {"host_instance_id", config_.host_instance_id}});
}

Response SessionManager::do_get(const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    std::lock_guard lock(mutex_);
    return protocol::ok(req.id, describe(*m));
}

bool SessionManager::send_snapshot(ipc::Connection& conn, const std::string& session_id,
                                   const terminal::ScreenSnapshot& snap, bool droppable,
                                   std::string& snapshot_id) {
    snapshot_id = "snap-" + std::to_string(next_snapshot_id_++);
    const std::size_t row_bytes = static_cast<std::size_t>(std::max(snap.cols, 1)) * 32;
    const int rows_per_chunk = static_cast<int>(std::max<std::size_t>(1, kMaxSnapshotChunkBytes / row_bytes));
    const int chunk_count = std::max(1, (snap.rows + rows_per_chunk - 1) / rows_per_chunk);
    for (int i = 0; i < chunk_count; ++i) {
        protocol::Event e;
        e.event = "screen.snapshot";
        e.body = {{"session_id", session_id},
                  {"snapshot_id", snapshot_id},
                  {"chunk_index", i},
                  {"chunk_count", chunk_count},
                  {"meta", protocol::encode_snapshot_meta(snap)},
                  {"rows", protocol::encode_rows(snap, i * rows_per_chunk, (i + 1) * rows_per_chunk)}};
        if (!conn.send(protocol::serialize(e), droppable))
            return false;
    }
    return true;
}

Response SessionManager::do_attach(const ipc::ConnectionPtr& conn, const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    std::lock_guard lock(mutex_);
    if (!m->session)
        return protocol::fail(req.id, ErrorCode::kNotFound, "session is Lost (from a previous host)");
    // Snapshot and subscription under the same lock as fan-out: deltas start exactly after seq S.
    auto snap = m->session->snapshot();
    const bool want = req.params.value("want_control", false);
    const bool take = req.params.value("take_control", false);
    if (take || (want && !m->controller))
        m->controller = conn->id();
    m->subscribers[conn->id()] = Subscriber{conn, snap, false};
    std::string snapshot_id;
    if (!send_snapshot(*conn, m->record.id, *snap, false, snapshot_id)) {
        m->subscribers[conn->id()].needs_snapshot = true;
    }
    Json result = describe(*m);
    result["is_controller"] = m->controller == conn->id();
    result["snapshot_id"] = snapshot_id;
    result["snapshot_meta"] = protocol::encode_snapshot_meta(*snap);
    return protocol::ok(req.id, result);
}

Response SessionManager::do_detach(const ipc::ConnectionPtr& conn, const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    std::lock_guard lock(mutex_);
    m->subscribers.erase(conn->id());
    if (m->controller == conn->id())
        m->controller.reset();
    return protocol::ok(req.id, {{"detached", true}});
}

Response SessionManager::do_input(const ipc::ConnectionPtr& conn, const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    Session* session = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (!m->session)
            return protocol::fail(req.id, ErrorCode::kNotFound, "session is Lost");
        // One input controller per session; others are read-only (design doc 3.7, QA-14).
        if (m->controller && *m->controller != conn->id()) {
            return protocol::fail(req.id, ErrorCode::kNotController, "another client controls this session");
        }
        session = m->session.get();
    }
    const auto& p = req.params;
    const std::string kind = p.value("kind", "text");
    if (kind == "text") {
        session->send(
            TextInput{utf8_to_u32(p.value("text", "")), mods_from_json(p.value("mods", Json::object()))});
    } else if (kind == "key") {
        const auto key = key_from_name(p.value("key", ""));
        if (!key)
            return bad(req, "unknown key");
        session->send(KeyInput{*key, mods_from_json(p.value("mods", Json::object()))});
    } else if (kind == "paste") {
        const std::string text = p.value("text", "");
        if (text.size() > 1024 * 1024 || text.find('\0') != std::string::npos)
            return bad(req, "paste rejected (NUL or > 1 MiB)");
        session->send(PasteInput{text});
    } else if (kind == "raw") {
        session->send(RawInput{p.value("bytes", "")});
    } else {
        return bad(req, "unknown input kind");
    }
    return protocol::ok(req.id, {{"accepted", true}});
}

Response SessionManager::do_resize(const ipc::ConnectionPtr& conn, const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    std::lock_guard lock(mutex_);
    if (!m->session)
        return protocol::fail(req.id, ErrorCode::kNotFound, "session is Lost");
    if (m->controller && *m->controller != conn->id()) {
        return protocol::fail(req.id, ErrorCode::kNotController, "only the controller sets the size");
    }
    m->session->resize(req.params.value("rows", 40), req.params.value("cols", 120));
    return protocol::ok(req.id, {{"accepted", true}});
}

Response SessionManager::do_stop(const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    const std::string mode = req.params.value("mode", "interrupt");
    std::lock_guard lock(mutex_);
    if (!m->session)
        return protocol::fail(req.id, ErrorCode::kNotFound, "session is Lost");
    if (mode == "interrupt")
        m->session->interrupt();
    else if (mode == "force")
        m->session->force_stop();
    else
        return bad(req, "mode must be interrupt or force");
    return protocol::ok(req.id, {{"mode", mode}, {"session_id", m->record.id}});
}

Response SessionManager::do_snapshot(const ipc::ConnectionPtr& conn, const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    std::lock_guard lock(mutex_);
    if (!m->session)
        return protocol::fail(req.id, ErrorCode::kNotFound, "session is Lost");
    auto snap = m->session->snapshot();
    std::string snapshot_id;
    if (!send_snapshot(*conn, m->record.id, *snap, false, snapshot_id)) {
        return protocol::fail(req.id, ErrorCode::kOverloaded, "client send queue is full", true);
    }
    if (auto it = m->subscribers.find(conn->id()); it != m->subscribers.end()) {
        it->second.last = snap;  // resync point for later deltas
        it->second.needs_snapshot = false;
    }
    Json meta = protocol::encode_snapshot_meta(*snap);
    meta["snapshot_id"] = snapshot_id;
    return protocol::ok(req.id, meta);
}

Response SessionManager::do_history(const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    if (!m->session)
        return protocol::fail(req.id, ErrorCode::kNotFound, "session is Lost");
    const auto from = req.params.value("from_line_id", static_cast<std::uint64_t>(0));
    const auto max = std::min<std::size_t>(req.params.value("max_lines", kMaxHistoryLines), kMaxHistoryLines);
    // Blocking call into the actor: must not hold mutex_ (see header).
    const bool tail = req.params.value("tail", false);
    const auto lines = tail ? m->session->history_tail(max) : m->session->history(from, max);
    const auto info = m->session->info();
    Json out = Json::array();
    for (const auto& line : lines)
        out.push_back({{"line_id", line.line_id}, {"text", line_text(line)}});
    return protocol::ok(req.id, {{"lines", out},
                                 {"first_available_line_id", info.first_history_line_id},
                                 {"truncated", info.history_truncated}});
}

Response SessionManager::do_state_report(const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    const auto& p = req.params;
    const std::string source = p.value("source", "");
    if (source.empty() || source.size() > 64 || has_control_chars(source))
        return bad(req, "source must be 1..64 printable characters");
    if (source.rfind("agentdesk:", 0) == 0)
        return bad(req, "the agentdesk: prefix is reserved");
    const auto state = parse_agent_state(p.value("state", ""));
    if (!state)
        return bad(req, "state must be idle|working|done|blocked|error|unknown");
    if (!p.contains("seq") || !p["seq"].is_number_unsigned())
        return bad(req, "seq must be an unsigned integer");
    const std::string message = p.value("message", "");
    if (message.size() > 2048 || has_control_chars(message))
        return bad(req, "message must be one line of at most 2048 bytes");
    terminal::StatusKind kind = terminal::StatusKind::kNone;
    const std::string k = p.value("kind", "");
    if (k == "permission")
        kind = terminal::StatusKind::kPermission;
    else if (k == "question")
        kind = terminal::StatusKind::kQuestion;
    else if (k == "auth")
        kind = terminal::StatusKind::kAuth;
    else if (!k.empty())
        return bad(req, "kind must be permission|question|auth");
    std::lock_guard lock(mutex_);
    if (!m->session)
        return protocol::fail(req.id, ErrorCode::kNotFound, "session is Lost");
    m->session->report_state(source, p["seq"].get<std::uint64_t>(), *state, kind, message);
    return protocol::ok(req.id, {{"accepted", true}});
}

Response SessionManager::do_shutdown(const Request& req) {
    const bool stop_sessions = req.params.value("stop_sessions", false);
    const auto running = running_session_count();
    if (running > 0 && !stop_sessions) {
        return protocol::fail(req.id, ErrorCode::kActiveSessions,
                              std::to_string(running) + " sessions are still running");
    }
    stop_all();
    if (config_.on_shutdown)
        config_.on_shutdown();
    return protocol::ok(req.id, {{"stopped_sessions", running}});
}

std::size_t SessionManager::running_session_count() const {
    std::lock_guard lock(mutex_);
    std::size_t n = 0;
    for (const auto& [id, m] : sessions_) {
        if (!m->session)
            continue;
        const auto s = m->session->info().process_state;
        if (s == ProcessState::kRunning || s == ProcessState::kStopping || s == ProcessState::kStarting)
            ++n;
    }
    return n;
}

void SessionManager::stop_all() {
    std::lock_guard lock(mutex_);
    for (auto& [id, m] : sessions_) {
        if (m->session && m->session->info().process_state != ProcessState::kExited)
            m->session->force_stop();
    }
}

void SessionManager::fanout(const std::string& id, std::shared_ptr<const terminal::ScreenSnapshot> snap) {
    std::lock_guard lock(mutex_);
    const auto it = sessions_.find(id);
    if (it == sessions_.end())
        return;
    auto& m = *it->second;
    for (auto sit = m.subscribers.begin(); sit != m.subscribers.end();) {
        auto conn = sit->second.conn.lock();
        if (!conn || conn->closed()) {
            sit = m.subscribers.erase(sit);
            continue;
        }
        auto& sub = sit->second;
        const bool full = sub.needs_snapshot || !sub.last || sub.last->generation != snap->generation ||
                          sub.last->rows != snap->rows || sub.last->cols != snap->cols;
        if (full) {
            std::string snapshot_id;
            if (send_snapshot(*conn, id, *snap, true, snapshot_id)) {
                sub.last = snap;
                sub.needs_snapshot = false;
            } else {
                sub.needs_snapshot = true;  // resent from the newest snapshot once the queue drains
            }
        } else if (snap->seq != sub.last->seq) {
            std::vector<int> changed;
            for (int r = 0; r < snap->rows; ++r) {
                const auto a = sub.last->cells.begin() + static_cast<std::ptrdiff_t>(r) * snap->cols;
                const auto b = snap->cells.begin() + static_cast<std::ptrdiff_t>(r) * snap->cols;
                if (!std::equal(a, a + snap->cols, b))
                    changed.push_back(r);
            }
            Json rows = Json::array();
            for (const int r : changed) {
                for (auto& row : protocol::encode_rows(*snap, r, r + 1))
                    rows.push_back(std::move(row));
            }
            protocol::Event e;
            e.event = "screen.delta";
            e.body = {{"session_id", id},
                      {"generation", snap->generation},
                      {"base_seq", sub.last->seq},
                      {"seq", snap->seq},
                      {"meta", protocol::encode_snapshot_meta(*snap)},
                      {"rows", std::move(rows)}};
            if (conn->send(protocol::serialize(e), true)) {
                sub.last = snap;
            } else {
                sub.needs_snapshot = true;  // design doc 3.3: drop deltas, resync with a snapshot
            }
        }
        ++sit;
    }
}

void SessionManager::on_session_info(const std::string& id, const SessionInfo& info) {
    std::lock_guard lock(mutex_);
    const auto it = sessions_.find(id);
    if (it == sessions_.end())
        return;
    auto& m = *it->second;
    const std::string state(to_string(info.process_state));
    if (state != m.last_process_state) {
        m.last_process_state = state;
        m.record.process_state = state;
        m.record.exit_code = info.exit_code;
        persist_locked();
    }
    protocol::Event e;
    e.event = "session.state";
    e.body = {{"session_id", id},
              {"process_state", state},
              {"exit_code", info.exit_code ? Json(*info.exit_code) : Json(nullptr)},
              {"agent_state", to_string(info.agent.state)},
              {"agent_source", info.agent.source},
              {"agent_kind", terminal::to_string(info.agent.kind)},
              {"agent_message", info.agent.message},
              {"last_agent_state", to_string(info.agent.last_state)},
              {"still_running_after_interrupt", info.still_running_after_interrupt},
              {"remaining_processes", info.remaining_processes}};
    const std::string payload = protocol::serialize(e);
    for (auto& [cid, sub] : m.subscribers) {
        if (auto conn = sub.conn.lock())
            conn->send(payload);  // state changes are reliable
    }
}

}  // namespace agentdesk::host
