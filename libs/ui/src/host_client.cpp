// HostClient: the UI's connection to agent-host (connect or start, reconnect, session list,
// per-session screen state) on a worker thread.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "agentdesk/ui/host_client.hpp"

#include <algorithm>

#include "agentdesk/ipc/host_launcher.hpp"
#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::ui {

namespace win = platform::win;
using protocol::Json;
using namespace std::chrono_literals;

namespace {

constexpr auto kListRefresh = 2s;
constexpr auto kReconnectDelay = 1s;

const char* key_name(terminal::Key k) {
    switch (k) {
        case terminal::Key::kEnter:
            return "enter";
        case terminal::Key::kTab:
            return "tab";
        case terminal::Key::kBackspace:
            return "backspace";
        case terminal::Key::kEscape:
            return "escape";
        case terminal::Key::kUp:
            return "up";
        case terminal::Key::kDown:
            return "down";
        case terminal::Key::kLeft:
            return "left";
        case terminal::Key::kRight:
            return "right";
        case terminal::Key::kInsert:
            return "insert";
        case terminal::Key::kDelete:
            return "delete";
        case terminal::Key::kHome:
            return "home";
        case terminal::Key::kEnd:
            return "end";
        case terminal::Key::kPageUp:
            return "pageup";
        case terminal::Key::kPageDown:
            return "pagedown";
        case terminal::Key::kF1:
            return "f1";
        case terminal::Key::kF2:
            return "f2";
        case terminal::Key::kF3:
            return "f3";
        case terminal::Key::kF4:
            return "f4";
        case terminal::Key::kF5:
            return "f5";
        case terminal::Key::kF6:
            return "f6";
        case terminal::Key::kF7:
            return "f7";
        case terminal::Key::kF8:
            return "f8";
        case terminal::Key::kF9:
            return "f9";
        case terminal::Key::kF10:
            return "f10";
        case terminal::Key::kF11:
            return "f11";
        case terminal::Key::kF12:
            return "f12";
    }
    return "enter";
}

Json mods_json(std::uint8_t m) {
    return {{"ctrl", (m & terminal::kModCtrl) != 0},
            {"shift", (m & terminal::kModShift) != 0},
            {"alt", (m & terminal::kModAlt) != 0}};
}

void update_view_from_json(SessionView& v, const Json& j) {
    v.name = j.value("name", v.name);
    v.start_cwd = j.value("cwd", v.start_cwd);
    // The shell's folder after cd (host reads it from the process); else where it started.
    const std::string live = j.value("live_cwd", "");
    if (!live.empty())
        v.cwd = live;
    else if (v.cwd.empty() || j.value("process_state", "") != "Running")
        v.cwd = v.start_cwd;
    v.created_at = j.value("created_at", v.created_at);
    if (const auto p = j.find("program"); p != j.end() && p->is_string())
        v.program = p->get<std::string>();
    if (const auto p = j.find("program_argv"); p != j.end() && p->is_array()) {
        v.program_argv.clear();
        for (const auto& w : *p)
            if (w.is_string())
                v.program_argv.push_back(w.get<std::string>());
    }
    if (const auto p = j.find("agent_app"); p != j.end() && p->is_string())
        v.agent_app = p->get<std::string>();
    if (const auto p = j.find("parent"); p != j.end() && p->is_string())
        v.parent = p->get<std::string>();
    if (const auto l = j.find("labels"); l != j.end() && l->is_object()) {
        v.labels.clear();
        for (const auto& [k, val] : l->items())
            if (val.is_string())
                v.labels[k] = val.get<std::string>();
    }
    v.profile_id = j.value("profile_id", v.profile_id);
    v.process_state = j.value("process_state", v.process_state);
    if (j.contains("exit_code")) {
        v.exit_code = j["exit_code"].is_number_unsigned()
                          ? std::optional<std::uint32_t>(j["exit_code"].get<std::uint32_t>())
                          : std::nullopt;
    }
    v.pid = j.value("pid", v.pid);
    v.agent_state = j.value("agent_state", v.agent_state);
    v.agent_source = j.value("agent_source", v.agent_source);
    v.agent_kind = j.value("agent_kind", v.agent_kind);
    v.agent_message = j.value("agent_message", v.agent_message);
    v.last_agent_state = j.value("last_agent_state", v.last_agent_state);
    if (const auto at = j.find("at"); at != j.end() && at->is_number_integer())
        v.state_at_ms = at->get<long long>();
    v.still_running_after_interrupt =
        j.value("still_running_after_interrupt", v.still_running_after_interrupt);
    v.remaining_processes = j.value("remaining_processes", v.remaining_processes);
    v.history_truncated = j.value("history_truncated", v.history_truncated);
    v.backpressure_waits = j.value("backpressure_waits", v.backpressure_waits);
    v.lost = v.lost || v.process_state == "Lost";
    if (v.lost)
        v.is_controller = false;  // nothing to control in a Lost session
}

// Fields of host replies, type-checked (json::value throws on a wrong type).
std::string str_field(const Json& j, const char* key) {
    if (!j.is_object())
        return {};
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}
bool bool_field(const Json& j, const char* key) {
    if (!j.is_object())
        return false;
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() && it->get<bool>();
}
// Another client holds input control: "controller" is present and not null.
bool controlled_by_other(const Json& j) {
    if (!j.is_object())
        return false;
    const auto it = j.find("controller");
    return it != j.end() && !it->is_null();
}

std::shared_ptr<const terminal::ScreenSnapshot> placeholder_screen() {
    static const auto blank = [] {
        auto s = std::make_shared<terminal::ScreenSnapshot>();
        s->rows = 1;
        s->cols = 2;
        s->cells.resize(2);
        return std::shared_ptr<const terminal::ScreenSnapshot>(s);
    }();
    return blank;
}

}  // namespace

// ------------------------------------------------------------------ RemoteSession

RemoteSession::RemoteSession(HostClient& owner, std::string id) : owner_(owner), id_(std::move(id)) {
    view_.id = id_;
}

std::shared_ptr<const terminal::ScreenSnapshot> RemoteSession::snapshot() const {
    std::lock_guard lock(mutex_);
    auto s = screen_.current();
    return s ? s : placeholder_screen();
}

void RemoteSession::send(host::InputEvent input) {
    Json params{{"session", id_}};
    std::visit(
        [&](auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, host::KeyInput>) {
                params["kind"] = "key";
                params["key"] = key_name(e.key);
                params["mods"] = mods_json(e.mods);
            } else if constexpr (std::is_same_v<T, host::TextInput>) {
                std::string utf8;
                for (const char32_t cp : e.text)
                    terminal::append_utf8(utf8, cp);
                params["kind"] = "text";
                params["text"] = utf8;
                params["mods"] = mods_json(e.mods);
            } else if constexpr (std::is_same_v<T, host::PasteInput>) {
                params["kind"] = "paste";
                params["text"] = e.utf8;
            } else if constexpr (std::is_same_v<T, host::MouseInput>) {
                params["kind"] = "mouse";
                params["row"] = e.row;
                params["col"] = e.col;
                params["button"] = e.button;
                params["action"] = e.action == terminal::MouseAction::kPress     ? "press"
                                   : e.action == terminal::MouseAction::kRelease ? "release"
                                                                                 : "move";
                params["mods"] = mods_json(e.mods);
            } else {
                params["kind"] = "raw";
                params["bytes"] = e.bytes;
            }
        },
        input);
    owner_.post("session.input", std::move(params));
}

void RemoteSession::resize(int rows, int cols) {
    {
        std::lock_guard lock(mutex_);
        if (!view_.is_controller)
            return;  // read-only views never change the ConPTY size (3.7)
    }
    owner_.post("session.resize", {{"session", id_}, {"rows", rows}, {"cols", cols}});
}

bool RemoteSession::controls_size() const {
    std::lock_guard lock(mutex_);
    return view_.is_controller;
}

bool RemoteSession::can_input() const {
    std::lock_guard lock(mutex_);
    return owner_.connected() && !view_.lost && view_.is_controller &&
           (view_.process_state == "Running" || view_.process_state == "Stopping");
}

void RemoteSession::request_history(std::size_t lines) {
    lines = std::min<std::size_t>(lines, 5000);
    {
        std::lock_guard lock(mutex_);
        // Refetch when more lines are needed or the cached copy is stale (new output keeps
        // pushing lines into history while the user is scrolled up or searching).
        const auto now = std::chrono::steady_clock::now();
        const auto max_age = lines > 400 ? 3s : 1s;
        if (lines <= history_requested_ && now - history_fetched_at_ < max_age)
            return;
        history_requested_ = lines;
        history_fetched_at_ = now;
    }
    owner_.enqueue([self = this, lines](ipc::PipeClient& c) {
        // history.read returns at most 200 lines (design doc 3.7): newest page by tail, then
        // older pages by line id until enough lines or the oldest kept line.
        const auto r = c.call("history.read", {{"session", self->id_}, {"tail", true}, {"max_lines", 200}});
        if (!r || !r->result)
            return;
        std::vector<HistoryLineText> lines_out;
        for (const auto& l : r->result->value("lines", Json::array()))
            lines_out.push_back(
                {l.value("line_id", static_cast<std::uint64_t>(0)), l.value("text", ""), l.value("wrapped", false)});
        const auto first_available =
            r->result->value("first_available_line_id", static_cast<std::uint64_t>(0));
        const bool truncated = r->result->value("truncated", false);
        while (lines_out.size() < lines && !lines_out.empty() &&
               lines_out.front().line_id > first_available) {
            const std::uint64_t first = lines_out.front().line_id;
            const std::uint64_t from = first > first_available + 200 ? first - 200 : first_available;
            const auto page =
                c.call("history.read", {{"session", self->id_},
                                        {"from_line_id", from},
                                        {"max_lines", static_cast<std::uint64_t>(first - from)}});
            if (!page || !page->result)
                break;
            std::vector<HistoryLineText> older;
            for (const auto& l : page->result->value("lines", Json::array())) {
                const auto id = l.value("line_id", static_cast<std::uint64_t>(0));
                if (id < first)
                    older.push_back({id, l.value("text", ""), l.value("wrapped", false)});
            }
            if (older.empty())
                break;
            lines_out.insert(lines_out.begin(), older.begin(), older.end());
        }
        auto out = std::make_shared<std::vector<HistoryLineText>>(std::move(lines_out));
        std::lock_guard lock(self->mutex_);
        self->history_ = std::move(out);
        self->view_.history_truncated = truncated;
    });
}

std::shared_ptr<const std::vector<HistoryLineText>> RemoteSession::history() const {
    std::lock_guard lock(mutex_);
    return history_;
}

SessionView RemoteSession::view() const {
    std::lock_guard lock(mutex_);
    return view_;
}

std::string RemoteSession::cwd() const {
    if (owner_.options_.connector)
        return {};  // another machine's folder is not a path here
    std::lock_guard lock(mutex_);
    return view_.cwd;
}

void RemoteSession::take_control() {
    owner_.enqueue([self = this](ipc::PipeClient& c) {
        const auto r = c.call("session.attach", {{"session", self->id_}, {"take_control", true}});
        if (!r || !r->result)
            return;
        {
            std::lock_guard lock(self->mutex_);
            self->view_.is_controller = bool_field(*r->result, "is_controller");
            self->view_.controlled_elsewhere = !self->view_.is_controller;
        }
        self->owner_.notify();  // redraw: the view now sends its size (TerminalView::handle_resize)
    });
}

void RemoteSession::stop(bool force) {
    owner_.post("session.stop", {{"session", id_}, {"mode", force ? "force" : "interrupt"}});
}

// ------------------------------------------------------------------ HostClient

HostClient::HostClient(HostClientOptions options) : options_(std::move(options)) {
    worker_ = std::thread([this] { worker_loop(); });
}

HostClient::~HostClient() {
    stop_ = true;
    cv_.notify_all();
    if (worker_.joinable())
        worker_.join();
    if (client_)
        client_->close();  // detach only: sessions keep running in the host (4.3)
}

std::string HostClient::host_instance_id() const {
    std::lock_guard lock(mutex_);
    return instance_id_;
}

std::string HostClient::host_version() const {
    std::lock_guard lock(mutex_);
    return host_version_;
}

std::string HostClient::last_error() const {
    std::lock_guard lock(mutex_);
    return last_error_;
}

std::vector<std::shared_ptr<RemoteSession>> HostClient::sessions() const {
    std::lock_guard lock(mutex_);
    std::vector<std::shared_ptr<RemoteSession>> out;
    for (const auto& [id, s] : sessions_)
        out.push_back(s);
    return out;
}

std::vector<std::pair<std::string, std::string>> HostClient::profiles() const {
    std::lock_guard lock(mutex_);
    return profiles_;
}

std::string HostClient::profile_executable(const std::string& id) const {
    std::lock_guard lock(mutex_);
    const auto it = profile_exes_.find(id);
    return it == profile_exes_.end() ? std::string() : it->second;
}

void HostClient::notify() const {
    if (options_.wake)
        options_.wake();
}

void HostClient::enqueue(std::function<void(ipc::PipeClient&)> run,
                         std::function<void(const std::string& error)> fail) {
    {
        std::lock_guard lock(mutex_);
        tasks_.push_back({std::move(run), std::move(fail)});
    }
    cv_.notify_all();
}

void HostClient::post(std::string method, Json params) {
    enqueue([method = std::move(method), params = std::move(params)](ipc::PipeClient& c) {
        c.call(method, params);  // errors are visible through the next state/list refresh
    });
}

void HostClient::call(std::string method, Json params, std::function<void(Json, std::string)> done,
                      std::chrono::milliseconds timeout) {
    // Shared by the request and its not-connected answer (only one of them runs).
    auto cb = std::make_shared<std::function<void(Json, std::string)>>(std::move(done));
    enqueue(
        [this, method = std::move(method), params = std::move(params), cb, timeout](ipc::PipeClient& c) {
            const auto r = c.call(method, params, timeout);
            if (!*cb)
                return;
            if (!r)
                (*cb)(Json(), "no response from agent-host");
            else if (r->error)
                (*cb)(Json(), std::string(protocol::to_string(*r->error)) + ": " + r->error_message);
            else
                (*cb)(r->result.value_or(Json::object()), "");
            notify();
        },
        [this, cb](const std::string& error) {
            if (*cb)
                (*cb)(Json(), error);
            notify();
        });
}

std::optional<Json> HostClient::take_ui_state() {
    std::lock_guard lock(mutex_);
    return std::exchange(pending_ui_state_, std::nullopt);
}

void HostClient::set_ui_state(Json doc) {
    post("ui.state.set", {{"doc", std::move(doc)}});
}

void HostClient::request_start_host() {
    start_requested_ = true;
    cv_.notify_all();
}

void HostClient::create_session(std::string profile_id, std::string cwd, std::string name,
                                std::function<void(std::string)> done) {
    auto cb = std::make_shared<std::function<void(std::string)>>(std::move(done));
    enqueue(
        [this, profile_id = std::move(profile_id), cwd = std::move(cwd), name = std::move(name),
         cb](ipc::PipeClient& c) {
            Json params{{"profile_id", profile_id}, {"cwd", cwd}};
            if (!name.empty())
                params["name"] = name;
            const auto r = c.call("session.create", params, 15s);
            std::string error;
            if (!r)
                error = "no response from agent-host";
            else if (r->error)
                error = std::string(protocol::to_string(*r->error)) + ": " + r->error_message;
            else
                refresh_sessions(c);
            if (*cb)
                (*cb)(error);
            notify();
        },
        [this, cb](const std::string& error) {
            if (*cb)
                (*cb)(error);
            notify();
        });
}

void HostClient::shutdown_host(bool stop_sessions, std::function<void(std::string)> done) {
    auto cb = std::make_shared<std::function<void(std::string)>>(std::move(done));
    enqueue(
        [stop_sessions, cb](ipc::PipeClient& c) {
            const auto r = c.call("host.shutdown", {{"stop_sessions", stop_sessions}});
            std::string error;
            if (!r)
                error = "no response from agent-host";
            else if (r->error)
                error = r->error_message;
            if (*cb)
                (*cb)(error);
        },
        [cb](const std::string& error) {
            if (*cb)
                (*cb)(error);
        });
}

bool HostClient::try_connect(bool allow_start) {
    auto conn = options_.connector
                    ? options_.connector()
                    : ipc::connect_or_start_host(options_.host_exe, options_.pipe_name, 10s, allow_start);
    if (!conn.client) {
        std::lock_guard lock(mutex_);
        last_error_ = conn.error.empty() ? std::string("agent-host is not reachable") : conn.error;
        return false;
    }
    transport_ = conn.transport;
    if (conn.host_dies_with_launcher)
        host_dies_with_launcher_ = true;
    conn.client->set_event_handler([this](protocol::Event e) { on_event(std::move(e)); });
    conn.client->set_disconnect_handler([this] {
        connected_ = false;
        notify();
    });
    {
        std::lock_guard lock(mutex_);
        // New host instance: sessions we knew were owned by the old one and are Lost now.
        // Never re-run anything automatically (design doc 4.3). Unless the new host took them
        // over in a self-update (design doc 7.4 OQ-09): the host restarts but its programs keep
        // running, so the sessions are the same ones and are reattached.
        const bool took_over = std::find(conn.host_succeeds.begin(), conn.host_succeeds.end(),
                                         instance_id_) != conn.host_succeeds.end();
        if (!instance_id_.empty() && instance_id_ != conn.host_instance_id && !took_over) {
            for (auto& [id, s] : sessions_) {
                std::lock_guard sl(s->mutex_);
                s->view_.lost = true;
                s->view_.process_state = "Lost";
                s->view_.is_controller = false;
                s->attached_ = false;
            }
        }
        for (auto& [id, s] : sessions_) {
            std::lock_guard sl(s->mutex_);
            s->attached_ = false;  // subscriptions die with the old connection
            s->screen_.mark_needs_snapshot();
        }
        instance_id_ = conn.host_instance_id;
        host_version_ = conn.host_version;
        last_error_.clear();
    }
    client_ = std::move(conn.client);
    connected_ = true;
    // Host replies are checked field by field: a malformed one is skipped, not fatal.
    if (const auto r = client_->call("profile.list", Json::object());
        r && r->result && r->result->is_object()) {
        std::vector<std::pair<std::string, std::string>> list;
        std::map<std::string, std::string> exes;
        if (const auto ps = r->result->find("profiles"); ps != r->result->end() && ps->is_array()) {
            for (const auto& p : *ps) {
                const std::string id = str_field(p, "id");
                list.emplace_back(id, str_field(p, "name"));
                if (const std::string exe = str_field(p, "executable"); !exe.empty())
                    exes[id] = exe;
            }
        }
        std::lock_guard lock(mutex_);
        profiles_ = std::move(list);
        profile_exes_ = std::move(exes);
    }
    if (const auto r = client_->call("ui.state.get", {{"watch", true}});
        r && r->result && r->result->is_object()) {
        std::lock_guard lock(mutex_);
        const auto doc = r->result->find("doc");
        pending_ui_state_ = doc != r->result->end() ? *doc : Json::object();
    }
    try {
        refresh_sessions(*client_);
    } catch (const std::exception&) {
        // the next periodic refresh tries again
    }
    notify();
    return true;
}

void HostClient::refresh_sessions(ipc::PipeClient& c) {
    const auto r = c.call("session.list", Json::object());
    if (!r || !r->result)
        return;
    std::vector<std::shared_ptr<RemoteSession>> to_attach;
    {
        std::lock_guard lock(mutex_);
        const auto list = r->result->is_object() ? r->result->find("sessions") : r->result->end();
        if (list == r->result->end() || !list->is_array())
            return;
        for (const auto& j : *list) {
            const std::string id = str_field(j, "id");
            if (id.empty())
                continue;
            auto& s = sessions_[id];
            if (!s)
                s = std::make_shared<RemoteSession>(*this, id);
            std::lock_guard sl(s->mutex_);
            try {
                update_view_from_json(s->view_, j);
            } catch (const nlohmann::json::exception&) {
                continue;  // this session's malformed entry; the others still update
            }
            if (!s->attached_ && !s->view_.lost)
                to_attach.push_back(s);
        }
    }
    for (const auto& s : to_attach) {
        // UI clients ask for control; a second UI becomes read-only (design doc 3.7).
        const auto a = c.call("session.attach", {{"session", s->id_}, {"want_control", true}});
        std::lock_guard sl(s->mutex_);
        if (a && a->result && a->result->is_object()) {
            s->attached_ = true;
            s->view_.is_controller = bool_field(*a->result, "is_controller");
            s->view_.controlled_elsewhere = !s->view_.is_controller && controlled_by_other(*a->result);
            try {
                update_view_from_json(s->view_, *a->result);
            } catch (const nlohmann::json::exception&) {
                // a malformed field keeps its last value; the next list refresh updates it
            }
        }
    }
    notify();
}

void HostClient::on_event(protocol::Event e) {
    // Runs on the pipe reader thread: nothing may escape (std::terminate would end agent-ui.exe).
    if (e.event == "ui.state") {
        if (const auto doc = e.body.is_object() ? e.body.find("doc") : e.body.end(); doc != e.body.end()) {
            std::lock_guard lock(mutex_);
            pending_ui_state_ = *doc;
        }
        notify();
        return;
    }
    const std::string id = str_field(e.body, "session_id");
    std::shared_ptr<RemoteSession> s;
    {
        std::lock_guard lock(mutex_);
        const auto it = sessions_.find(id);
        if (it != sessions_.end())
            s = it->second;
    }
    if (!s)
        return;  // created elsewhere; the next list refresh picks it up
    bool want_snapshot = false;
    {
        std::lock_guard lock(s->mutex_);
        try {
            if (e.event == "screen.snapshot") {
                if (s->screen_.apply_snapshot_chunk(e.body))
                    s->resync_requested_ = false;
            } else if (e.event == "screen.delta") {
                if (s->screen_.apply_delta(e.body) == DeltaResult::kNeedSnapshot && !s->resync_requested_) {
                    s->resync_requested_ = true;
                    want_snapshot = true;
                }
            } else if (e.event == "session.state") {
                update_view_from_json(s->view_, e.body);
            }
        } catch (const std::exception&) {
            // Malformed host data (json at/get/value, or a size that cannot be allocated): the
            // screen keeps what it had and a fresh snapshot replaces it.
            s->screen_.mark_needs_snapshot();
            if (!s->resync_requested_) {
                s->resync_requested_ = true;
                want_snapshot = true;
            }
        }
    }
    if (want_snapshot)
        post("screen.snapshot", {{"session", id}});
    notify();
}

void HostClient::worker_loop() {
    auto next_refresh = std::chrono::steady_clock::now();
    while (!stop_) {
        if (!connected_) {
            if (client_) {
                client_->close();
                client_.reset();
            }
            const bool allow_start = start_requested_.exchange(false);
            if (!try_connect(allow_start)) {
                notify();
                std::unique_lock lock(mutex_);
                cv_.wait_for(lock, options_.connector ? std::chrono::seconds(10) : kReconnectDelay,
                             [&] { return stop_.load() || start_requested_.load(); });
                continue;
            }
        }
        Task task;
        {
            std::unique_lock lock(mutex_);
            cv_.wait_until(lock, next_refresh,
                           [&] { return stop_.load() || !tasks_.empty() || !connected_; });
            if (stop_)
                return;
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
        }
        // Replies are host data: a malformed one (json at/get/value throwing) costs that request,
        // never the worker thread (an escaping exception would end agent-ui.exe).
        if (task.run && client_ && connected_) {
            try {
                task.run(*client_);
            } catch (const std::exception&) {
                notify();
            }
        } else if (task.fail) {
            try {
                task.fail("not connected to agent-host");
            } catch (const std::exception&) {
            }
        }
        if (std::chrono::steady_clock::now() >= next_refresh && client_ && connected_) {
            try {
                refresh_sessions(*client_);
            } catch (const std::exception&) {
                notify();
            }
            next_refresh = std::chrono::steady_clock::now() + kListRefresh;
        }
    }
}

}  // namespace agentdesk::ui
