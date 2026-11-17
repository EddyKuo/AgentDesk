#include "agentdesk/host/session.hpp"

#include <algorithm>
#include <stdexcept>

#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::host {

namespace win = platform::win;
using Clock = std::chrono::steady_clock;

namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

DWORD millis_until(Clock::time_point t) {
    const auto now = Clock::now();
    if (t <= now)
        return 0;
    return static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(t - now).count() + 1);
}

}  // namespace

Session::Session(SessionConfig config, SessionListener listener)
    : config_(std::move(config)),
      listener_(std::move(listener)),
      queue_event_(::CreateEventW(nullptr, FALSE, FALSE, nullptr)) {
    if (!queue_event_) {
        win::throw_last_error("CreateEventW(session queue)");
    }
}

std::unique_ptr<Session> Session::start(SessionConfig config, SessionListener listener) {
    std::unique_ptr<Session> self(new Session(std::move(config), std::move(listener)));
    terminal::EngineEvents events;
    events.on_status = [raw = self.get()](const terminal::StatusReport& r) { raw->on_engine_status(r); };
    self->engine_ = std::make_unique<terminal::VtEngine>(self->config_.engine, std::move(events));
    self->snapshot_ = std::make_shared<const terminal::ScreenSnapshot>(self->engine_->snapshot());

    self->writer_ = std::thread([raw = self.get()] { raw->writer_loop(); });
    try {
        auto process = win::PtyProcess::spawn(self->config_.api, self->config_.spec,
                                              [raw = self.get()](std::span<const char> b) {
                                                  raw->post(OutputChunk{std::string(b.data(), b.size())});
                                              });
        std::lock_guard lock(self->write_mutex_);  // the writer reads process_ under this lock
        self->process_ = std::move(process);
    } catch (...) {
        {
            std::lock_guard lock(self->state_mutex_);
            self->info_.process_state = ProcessState::kFailed;
        }
        {
            std::lock_guard lock(self->write_mutex_);
            self->writer_stop_ = true;
        }
        self->write_cv_.notify_all();
        self->writer_.join();
        throw;
    }
    {
        std::lock_guard lock(self->state_mutex_);
        self->info_.process_state = ProcessState::kRunning;
        self->info_.pid = self->process_->pid();
    }
    self->write_cv_.notify_all();  // replies queued before the process pointer existed
    self->actor_ = std::thread([raw = self.get()] { raw->actor_loop(); });
    self->publish_info();
    return self;
}

Session::~Session() {
    stopping_ = true;
    ::SetEvent(queue_event_.get());
    if (actor_.joinable())
        actor_.join();
    if (process_)
        process_->close();  // kills anything left in the job
    {
        std::lock_guard lock(write_mutex_);
        writer_stop_ = true;
    }
    write_cv_.notify_all();
    if (writer_.joinable())
        writer_.join();
}

void Session::post(Command cmd) {
    {
        std::lock_guard lock(queue_mutex_);
        queue_.push_back(std::move(cmd));
    }
    ::SetEvent(queue_event_.get());
}

void Session::send(InputEvent input) {
    post(std::move(input));
}

void Session::resize(int rows, int cols) {
    post(ResizeCmd{rows, cols});
}

void Session::interrupt() {
    post(InterruptCmd{});
}

void Session::force_stop() {
    {
        std::lock_guard lock(state_mutex_);
        if (info_.process_state == ProcessState::kExited)
            return;
        info_.process_state = ProcessState::kStopping;
        info_.stop_requested = true;
    }
    process_->terminate(1);
    ::SetEvent(queue_event_.get());
}

void Session::report_state(std::string source, std::uint64_t seq, AgentState state, terminal::StatusKind kind,
                           std::string message) {
    post(ReportCmd{std::move(source), seq, state, kind, std::move(message)});
}

std::shared_ptr<const terminal::ScreenSnapshot> Session::snapshot() const {
    std::lock_guard lock(state_mutex_);
    return snapshot_;
}

SessionInfo Session::info() const {
    std::lock_guard lock(state_mutex_);
    return info_;
}

std::vector<terminal::HistoryLine> Session::history(std::uint64_t from, std::size_t max_lines) const {
    auto promise = std::make_shared<std::promise<std::vector<terminal::HistoryLine>>>();
    auto future = promise->get_future();
    const_cast<Session*>(this)->post(HistoryCmd{from, max_lines, promise});
    return future.get();
}

std::optional<std::uint32_t> Session::wait_exit(std::chrono::milliseconds timeout) const {
    std::unique_lock lock(state_mutex_);
    exit_cv_.wait_for(lock, timeout, [&] { return info_.exit_code.has_value(); });
    return info_.exit_code;
}

void Session::writer_loop() {
    std::unique_lock lock(write_mutex_);
    for (;;) {
        write_cv_.wait(lock, [&] { return writer_stop_ || (!write_queue_.empty() && process_); });
        if (writer_stop_)
            return;
        std::string chunk = std::move(write_queue_.front());
        write_queue_.pop_front();
        lock.unlock();
        try {
            process_->write(chunk);
        } catch (const std::exception&) {
            // The pseudoconsole is closed or the child exited; input has nowhere to go.
        }
        lock.lock();
    }
}

void Session::queue_pty_bytes(std::string bytes) {
    if (bytes.empty())
        return;
    {
        std::lock_guard lock(write_mutex_);
        write_queue_.push_back(std::move(bytes));
    }
    write_cv_.notify_all();
}

void Session::actor_loop() {
    bool root_exited = false;
    for (;;) {
        // Wait for commands, root process exit, the next publish slot or the interrupt deadline.
        DWORD timeout = INFINITE;
        if (engine_->seq() != published_seq_) {
            timeout = millis_until(last_publish_ + config_.snapshot_interval);
        }
        if (interrupt_deadline_) {
            timeout = std::min(timeout, millis_until(*interrupt_deadline_));
        }
        HANDLE handles[2] = {queue_event_.get(), process_->process_handle()};
        const DWORD count = root_exited ? 1 : 2;
        const DWORD r = ::WaitForMultipleObjects(count, handles, FALSE, timeout);

        std::deque<Command> batch;
        {
            std::lock_guard lock(queue_mutex_);
            batch.swap(queue_);
        }
        for (auto& cmd : batch) {
            std::visit([this](auto& c) { handle(c); }, cmd);
        }

        if (!root_exited && (r == WAIT_OBJECT_0 + 1 ||
                             ::WaitForSingleObject(process_->process_handle(), 0) == WAIT_OBJECT_0)) {
            root_exited = true;
            on_root_exit();
        }

        if (interrupt_deadline_ && Clock::now() >= *interrupt_deadline_) {
            interrupt_deadline_.reset();
            bool changed = false;
            {
                std::lock_guard lock(state_mutex_);
                if (info_.process_state == ProcessState::kStopping && !info_.exit_code) {
                    info_.still_running_after_interrupt = true;  // UI now offers force (4.3)
                    changed = true;
                }
            }
            if (changed)
                publish_info();
        }

        publish(false);

        if (stopping_) {
            std::lock_guard lock(queue_mutex_);
            if (queue_.empty())
                return;
        }
    }
}

void Session::on_root_exit() {
    DWORD remaining = 0;
    try {
        remaining = process_->job_active_processes();
    } catch (const std::exception&) {
    }
    const auto code = process_->wait_exit(std::chrono::milliseconds(0));
    // Close the pseudoconsole: the reader drains the final output (posted to our queue) and the
    // job's remaining descendants are terminated.
    process_->close();
    {
        std::deque<Command> batch;
        {
            std::lock_guard lock(queue_mutex_);
            batch.swap(queue_);
        }
        for (auto& cmd : batch) {
            std::visit([this](auto& c) { handle(c); }, cmd);
        }
    }
    engine_->on_process_exit();
    refresh_agent_from_engine();
    {
        std::lock_guard lock(state_mutex_);
        info_.process_state = ProcessState::kExited;
        info_.exit_code = code.value_or(0);
        info_.remaining_processes = remaining > 0 ? remaining - 1 : 0;  // excluding the root
        info_.still_running_after_interrupt = false;
    }
    interrupt_deadline_.reset();
    exit_cv_.notify_all();
    publish(true);
    publish_info();
}

void Session::handle(OutputChunk& c) {
    engine_->feed(c.bytes);
    queue_pty_bytes(engine_->take_pty_input());  // query replies, in parse order
    std::lock_guard lock(state_mutex_);
    info_.bytes_from_program += c.bytes.size();
}

void Session::handle(InputEvent& c) {
    std::visit(Overloaded{
                   [&](KeyInput& k) { engine_->key(k.key, k.mods); },
                   [&](TextInput& t) {
                       for (const char32_t cp : t.text)
                           engine_->text(cp, t.mods);
                   },
                   [&](PasteInput& p) {
                       try {
                           engine_->paste(p.utf8);
                       } catch (const std::invalid_argument&) {
                           // rejected by policy (NUL, > 1 MiB); the UI validates before sending
                       }
                   },
                   [&](RawInput& raw) { queue_pty_bytes(std::move(raw.bytes)); },
               },
               c);
    queue_pty_bytes(engine_->take_pty_input());
}

void Session::handle(ResizeCmd& c) {
    engine_->resize(c.rows, c.cols);
    const auto snap = engine_->snapshot();
    try {
        process_->resize({static_cast<short>(snap.cols), static_cast<short>(snap.rows)});
    } catch (const std::exception&) {
        // closed after exit; the engine size still changes for history viewing
    }
}

void Session::handle(InterruptCmd&) {
    {
        std::lock_guard lock(state_mutex_);
        if (info_.process_state == ProcessState::kExited)
            return;
        info_.process_state = ProcessState::kStopping;
        info_.stop_requested = true;
        info_.still_running_after_interrupt = false;
    }
    queue_pty_bytes(std::string("\x03", 1));
    interrupt_deadline_ = Clock::now() + config_.interrupt_grace;
    publish_info();
}

void Session::handle(ReportCmd& c) {
    auto& last = report_seq_[c.source];
    if (c.seq <= last)
        return;  // stale or replayed report from the same source
    last = c.seq;
    {
        std::lock_guard lock(state_mutex_);
        auto& a = info_.agent;
        if (a.state != c.state)
            a.last_state = a.state;
        a.state = c.state;
        a.source = "report:" + c.source;
        a.kind = c.state == AgentState::kBlocked ? c.kind : terminal::StatusKind::kNone;
        a.message = c.message;
        a.updated_at = std::chrono::system_clock::now();
    }
    publish_info();
}

void Session::handle(HistoryCmd& c) {
    std::vector<terminal::HistoryLine> out;
    for (const auto& line : engine_->history()) {
        if (line.line_id < c.from)
            continue;
        if (out.size() >= c.max_lines)
            break;
        out.push_back(line);
    }
    c.result->set_value(std::move(out));
}

void Session::on_engine_status(const terminal::StatusReport&) {
    refresh_agent_from_engine();
}

void Session::refresh_agent_from_engine() {
    const auto* root = engine_->status().root();
    bool changed = false;
    {
        std::lock_guard lock(state_mutex_);
        auto& a = info_.agent;
        if (root != nullptr) {
            const AgentState next = from_status(root->report.state);
            if (next != a.state || a.source != "osc7501" || a.message != root->report.msg) {
                if (next != a.state)
                    a.last_state = a.state;
                a.state = next;
                a.source = "osc7501";
                a.kind = root->report.kind;
                a.message = root->report.msg;
                a.updated_at = std::chrono::system_clock::now();
                changed = true;
            }
        } else if (a.source == "osc7501" && a.state != AgentState::kUnknown) {
            // Record dropped (clear, process exit, shell prompt): never guess Idle or Done.
            a.last_state = a.state;
            a.state = AgentState::kUnknown;
            a.kind = terminal::StatusKind::kNone;
            a.message.clear();
            a.updated_at = std::chrono::system_clock::now();
            changed = true;
        }
    }
    if (changed)
        publish_info();
}

void Session::publish(bool force) {
    if (engine_->seq() == published_seq_ && !force)
        return;
    const auto now = Clock::now();
    if (!force && now - last_publish_ < config_.snapshot_interval)
        return;
    auto snap = std::make_shared<const terminal::ScreenSnapshot>(engine_->snapshot());
    published_seq_ = engine_->seq();
    last_publish_ = now;
    {
        std::lock_guard lock(state_mutex_);
        snapshot_ = snap;
        info_.history_truncated = engine_->history_truncated();
        info_.first_history_line_id = engine_->first_history_line_id();
    }
    if (listener_.on_snapshot)
        listener_.on_snapshot(std::move(snap));
}

void Session::publish_info() {
    if (!listener_.on_info)
        return;
    listener_.on_info(info());
}

}  // namespace agentdesk::host
