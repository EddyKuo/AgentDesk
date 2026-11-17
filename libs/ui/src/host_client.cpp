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
    v.cwd = j.value("cwd", v.cwd);
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
    v.still_running_after_interrupt =
        j.value("still_running_after_interrupt", v.still_running_after_interrupt);
    v.remaining_processes = j.value("remaining_processes", v.remaining_processes);
    v.history_truncated = j.value("history_truncated", v.history_truncated);
    v.backpressure_waits = j.value("backpressure_waits", v.backpressure_waits);
    v.lost = v.lost || v.process_state == "Lost";
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

bool RemoteSession::can_input() const {
    std::lock_guard lock(mutex_);
    return owner_.connected() && !view_.lost && view_.is_controller &&
           (view_.process_state == "Running" || view_.process_state == "Stopping");
}

void RemoteSession::request_history(std::size_t lines) {
    lines = std::min<std::size_t>(lines, 200);  // history.read returns at most 200 lines
    {
        std::lock_guard lock(mutex_);
        // Refetch when more lines are needed or the cached copy is over a second old (new output
        // keeps pushing lines into history while the user is scrolled up).
        const auto now = std::chrono::steady_clock::now();
        if (lines <= history_requested_ && now - history_fetched_at_ < 1s)
            return;
        history_requested_ = lines;
        history_fetched_at_ = now;
    }
    owner_.enqueue([self = this, lines](ipc::PipeClient& c) {
        const auto r = c.call("history.read", {{"session", self->id_}, {"tail", true}, {"max_lines", lines}});
        if (!r || !r->result)
            return;
        auto out = std::make_shared<std::vector<HistoryLineText>>();
        for (const auto& l : r->result->value("lines", Json::array())) {
            out->push_back({l.value("line_id", static_cast<std::uint64_t>(0)), l.value("text", "")});
        }
        std::lock_guard lock(self->mutex_);
        self->history_ = std::move(out);
        self->view_.history_truncated = r->result->value("truncated", false);
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

void RemoteSession::take_control() {
    owner_.enqueue([self = this](ipc::PipeClient& c) {
        const auto r = c.call("session.attach", {{"session", self->id_}, {"take_control", true}});
        if (!r || !r->result)
            return;
        std::lock_guard lock(self->mutex_);
        self->view_.is_controller = r->result->value("is_controller", false);
        self->view_.controlled_elsewhere = !self->view_.is_controller;
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

void HostClient::notify() const {
    if (options_.wake)
        options_.wake();
}

void HostClient::enqueue(Task task) {
    {
        std::lock_guard lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    cv_.notify_all();
}

void HostClient::post(std::string method, Json params) {
    enqueue([method = std::move(method), params = std::move(params)](ipc::PipeClient& c) {
        c.call(method, params);  // errors are visible through the next state/list refresh
    });
}

void HostClient::request_start_host() {
    start_requested_ = true;
    cv_.notify_all();
}

void HostClient::create_session(std::string profile_id, std::string cwd, std::string name,
                                std::function<void(std::string)> done) {
    enqueue([this, profile_id = std::move(profile_id), cwd = std::move(cwd), name = std::move(name),
             done = std::move(done)](ipc::PipeClient& c) {
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
        if (done)
            done(error);
        notify();
    });
}

void HostClient::shutdown_host(bool stop_sessions, std::function<void(std::string)> done) {
    enqueue([stop_sessions, done = std::move(done)](ipc::PipeClient& c) {
        const auto r = c.call("host.shutdown", {{"stop_sessions", stop_sessions}});
        std::string error;
        if (!r)
            error = "no response from agent-host";
        else if (r->error)
            error = r->error_message;
        if (done)
            done(error);
    });
}

bool HostClient::try_connect(bool allow_start) {
    auto hc = ipc::connect_or_start_host(options_.host_exe, options_.pipe_name, 10s, allow_start);
    if (!hc.client) {
        std::lock_guard lock(mutex_);
        last_error_ = "agent-host is not reachable";
        return false;
    }
    if (hc.host_dies_with_launcher)
        host_dies_with_launcher_ = true;
    hc.client->set_event_handler([this](protocol::Event e) { on_event(std::move(e)); });
    hc.client->set_disconnect_handler([this] {
        connected_ = false;
        notify();
    });
    {
        std::lock_guard lock(mutex_);
        // New host instance: sessions we knew were owned by the old one and are Lost now.
        // Never re-run anything automatically (design doc 4.3).
        if (!instance_id_.empty() && instance_id_ != hc.host_instance_id) {
            for (auto& [id, s] : sessions_) {
                std::lock_guard sl(s->mutex_);
                s->view_.lost = true;
                s->view_.process_state = "Lost";
                s->attached_ = false;
            }
        }
        for (auto& [id, s] : sessions_) {
            std::lock_guard sl(s->mutex_);
            s->attached_ = false;  // subscriptions die with the old connection
            s->screen_.mark_needs_snapshot();
        }
        instance_id_ = hc.host_instance_id;
        last_error_.clear();
    }
    client_ = std::move(hc.client);
    connected_ = true;
    if (const auto r = client_->call("profile.list", Json::object()); r && r->result) {
        std::vector<std::pair<std::string, std::string>> list;
        for (const auto& p : r->result->value("profiles", Json::array()))
            list.emplace_back(p.value("id", ""), p.value("name", ""));
        std::lock_guard lock(mutex_);
        profiles_ = std::move(list);
    }
    refresh_sessions(*client_);
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
        for (const auto& j : r->result->value("sessions", Json::array())) {
            const std::string id = j.value("id", "");
            auto& s = sessions_[id];
            if (!s)
                s = std::make_shared<RemoteSession>(*this, id);
            std::lock_guard sl(s->mutex_);
            update_view_from_json(s->view_, j);
            if (!s->attached_ && !s->view_.lost)
                to_attach.push_back(s);
        }
    }
    for (const auto& s : to_attach) {
        // UI clients ask for control; a second UI becomes read-only (design doc 3.7).
        const auto a = c.call("session.attach", {{"session", s->id_}, {"want_control", true}});
        std::lock_guard sl(s->mutex_);
        if (a && a->result) {
            s->attached_ = true;
            s->view_.is_controller = a->result->value("is_controller", false);
            s->view_.controlled_elsewhere = !s->view_.is_controller && !a->result->at("controller").is_null();
            update_view_from_json(s->view_, *a->result);
        }
    }
    notify();
}

void HostClient::on_event(protocol::Event e) {
    const std::string id = e.body.value("session_id", "");
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
                cv_.wait_for(lock, kReconnectDelay, [&] { return stop_.load() || start_requested_.load(); });
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
        if (task && client_ && connected_)
            task(*client_);
        if (std::chrono::steady_clock::now() >= next_refresh && client_ && connected_) {
            refresh_sessions(*client_);
            next_refresh = std::chrono::steady_clock::now() + kListRefresh;
        }
    }
}

}  // namespace agentdesk::ui
