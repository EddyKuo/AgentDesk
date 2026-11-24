// Session: the actor that owns one terminal (VtEngine + PtyProcess), its writer thread,
// backpressure, snapshot publishing and the handover state format.
#include "agentdesk/host/session.hpp"

#include <algorithm>
#include <stdexcept>

#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/terminal/bytes.hpp"
#include "agentdesk/terminal/cwd_uri.hpp"

namespace agentdesk::host {

namespace win = platform::win;
using Clock = std::chrono::steady_clock;

namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

std::int64_t wall_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// A new agent state; the one it replaces becomes last_state when it differs.
void set_agent_state(AgentStatus& a, AgentState next) {
    if (next != a.state)
        a.last_state = a.state;
    a.state = next;
}

}  // namespace

terminal::EngineEvents Session::engine_events() {
    terminal::EngineEvents events;
    events.on_status = [this](const terminal::StatusReport& r) { on_engine_status(r); };
    events.on_shell_prompt = [this] { refresh_agent_from_engine(); };
    return events;
}

platform::win::PtyOutputSink Session::output_sink() {
    return [this](std::span<const char> b) { post_output(std::string(b.data(), b.size())); };
}

std::unique_ptr<Session> Session::start(SessionConfig config, SessionListener listener) {
    std::unique_ptr<Session> self(new Session(std::move(config), std::move(listener)));
    self->engine_ = std::make_unique<terminal::VtEngine>(self->config_.engine, self->engine_events());
    self->snapshot_ = std::make_shared<const terminal::ScreenSnapshot>(self->engine_->snapshot());

    self->writer_ = std::thread([raw = self.get()] { raw->writer_loop(); });
    try {
        auto process = self->spawn_process();
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
        self->note_state_locked();
    }
    self->write_cv_.notify_all();  // replies queued before the process pointer existed
    self->actor_ = std::thread([raw = self.get()] { raw->actor_loop(); });
    self->publish_info();
    return self;
}

Session::~Session() {
    stopping_ = true;
    release_backpressure();
    wake_actor();
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
    wake_actor();
}

void Session::post_output(std::string bytes) {
    {
        std::unique_lock lock(queue_mutex_);
        const std::size_t limit = config_.output_queue_limit;
        const auto has_room = [&] {
            return no_backpressure_ || queued_output_bytes_ == 0 ||
                   queued_output_bytes_ + bytes.size() <= limit;
        };
        if (!has_room()) {
            // Backpressure (OQ-06): stop reading the PTY until the actor catches up. The child
            // blocks on its own writes; no terminal byte is ever dropped (design doc 3.3).
            ++backpressure_waits_;
            queue_space_cv_.wait(lock, has_room);
        }
        queued_output_bytes_ += bytes.size();
        queue_.push_back(OutputChunk{std::move(bytes)});
    }
    wake_actor();
}

void Session::release_backpressure() {
    {
        std::lock_guard lock(queue_mutex_);
        no_backpressure_ = true;
    }
    queue_space_cv_.notify_all();
}

void Session::drain_queue() {
    std::deque<Command> batch;
    {
        std::lock_guard lock(queue_mutex_);
        batch.swap(queue_);
    }
    std::size_t output_bytes = 0;
    for (auto& cmd : batch) {
        if (const auto* chunk = std::get_if<OutputChunk>(&cmd))
            output_bytes += chunk->bytes.size();
        std::visit([this](auto& c) { handle(c); }, cmd);
    }
    if (output_bytes > 0) {
        {
            std::lock_guard lock(queue_mutex_);
            queued_output_bytes_ -= output_bytes;
            std::lock_guard state(state_mutex_);
            info_.backpressure_waits = backpressure_waits_;
        }
        queue_space_cv_.notify_all();
    }
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
        note_state_locked();
    }
    if (process_)
        process_->terminate(1);
    wake_actor();
}

void Session::report_state(std::string source, std::uint64_t seq, AgentState state, terminal::StatusKind kind,
                           std::string message) {
    post(ReportCmd{std::move(source), seq, state, kind, std::move(message)});
}

std::shared_ptr<const terminal::ScreenSnapshot> Session::snapshot() const {
    std::lock_guard lock(state_mutex_);
    return snapshot_;
}

platform::win::Foreground Session::foreground() const {
    const SessionInfo i = info();
    if (!process_ || i.process_state != ProcessState::kRunning)
        return {};
    platform::win::Foreground fg;
    try {
        fg = process_->foreground();
    } catch (const std::exception&) {
        return {};
    }
    // A folder the shell reported itself (OSC 7) beats the Windows process's current directory:
    // WSL and MSYS shells cd without changing it.
    if (!i.cwd_uri.empty()) {
        if (auto p = local_path_from_cwd_uri(i.cwd_uri); !p.empty())
            fg.cwd = std::move(p);
    }
    return fg;
}

std::string Session::foreground_program() const {
    // process_ is set before start() or adopt() returns and destroyed only with *this.
    if (!process_ || info().process_state != ProcessState::kRunning)
        return {};
    try {
        return process_->foreground_program();
    } catch (const std::exception&) {
        return {};
    }
}

SessionInfo Session::info() const {
    std::lock_guard lock(state_mutex_);
    return info_;
}

std::vector<terminal::HistoryLine> Session::history(std::uint64_t from, std::size_t max_lines) const {
    return ask_history(from, max_lines, false);
}

std::vector<terminal::HistoryLine> Session::history_tail(std::size_t max_lines) const {
    return ask_history(0, max_lines, true);
}

// History lives in the engine, so the actor copies it out; the caller waits for the copy.
std::vector<terminal::HistoryLine> Session::ask_history(std::uint64_t from, std::size_t max_lines,
                                                        bool tail) const {
    auto promise = std::make_shared<std::promise<std::vector<terminal::HistoryLine>>>();
    auto future = promise->get_future();
    const_cast<Session*>(this)->post(HistoryCmd{from, max_lines, promise, tail});
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
        writer_busy_ = true;
        lock.unlock();
        try {
            process_->write(chunk);
        } catch (const std::exception&) {
            // The pseudoconsole is closed or the child exited; input has nowhere to go.
        }
        lock.lock();
        writer_busy_ = false;
        if (write_queue_.empty())
            writer_idle_cv_.notify_all();
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
    // An adopted session whose program had already exited has no process (handover).
    bool root_exited = process_ == nullptr;
    for (;;) {
        // Wait for commands, root process exit, the next publish slot or the interrupt deadline.
        wait_for_work(root_exited);

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
    std::uint32_t remaining = 0;
    try {
        remaining = process_->job_active_processes();
    } catch (const std::exception&) {
    }
    const auto code = process_->wait_exit(std::chrono::milliseconds(0));
    // Close the pseudoconsole: the reader drains the final output (posted to our queue) and the
    // job's remaining descendants are terminated. The reader must not wait for queue space while
    // we join it, so backpressure is lifted first.
    release_backpressure();
    process_->close();
    drain_queue();
    engine_->on_process_exit();
    refresh_agent_from_engine();
    {
        std::lock_guard lock(state_mutex_);
        info_.process_state = ProcessState::kExited;
        info_.exit_code = recorded_exit_code(code);
        info_.remaining_processes = remaining > 0 ? remaining - 1 : 0;  // excluding the root
        info_.still_running_after_interrupt = false;
        note_state_locked();
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
    if (engine_->cwd_uri() != info_.cwd_uri)
        info_.cwd_uri = engine_->cwd_uri();
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
                   [&](MouseInput& m) { engine_->mouse(m.row, m.col, m.button, m.action, m.mods); },
               },
               c);
    queue_pty_bytes(engine_->take_pty_input());
}

void Session::handle(ResizeCmd& c) {
    engine_->resize(c.rows, c.cols);
    const auto snap = engine_->snapshot();
    if (!process_)
        return;  // adopted after its program exited: nothing to resize
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
        note_state_locked();
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
        set_agent_state(a, c.state);
        a.source = "report:" + c.source;
        a.kind = c.state == AgentState::kBlocked ? c.kind : terminal::StatusKind::kNone;
        a.message = c.message;
        a.updated_at = std::chrono::system_clock::now();
        note_state_locked();
    }
    publish_info();
}

void Session::handle(HistoryCmd& c) {
    std::vector<terminal::HistoryLine> out;
    const auto& hist = engine_->history();
    if (c.tail) {
        const std::size_t n = std::min(c.max_lines, hist.size());
        out.assign(hist.end() - static_cast<std::ptrdiff_t>(n), hist.end());
        c.result->set_value(std::move(out));
        return;
    }
    for (const auto& line : hist) {
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
            const std::string app = engine_->status().effective_app(std::string());
            if (next != a.state || a.source != "osc7501" || a.message != root->report.msg || a.app != app) {
                a.app = app;
                set_agent_state(a, next);
                a.source = "osc7501";
                a.kind = root->report.kind;
                a.message = root->report.msg;
                a.updated_at = std::chrono::system_clock::now();
                changed = true;
            }
        } else if (a.source == "osc7501" && a.state != AgentState::kUnknown) {
            // Record dropped (clear, process exit, shell prompt): never guess Idle or Done.
            set_agent_state(a, AgentState::kUnknown);
            a.kind = terminal::StatusKind::kNone;
            a.message.clear();
            a.app.clear();
            a.updated_at = std::chrono::system_clock::now();
            changed = true;
        }
        if (changed)
            note_state_locked();
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

void Session::note_state_locked() {
    if (timeline_.add(wall_ms(), info_.agent.state, info_.process_state))
        info_.state_changed_ms = timeline_.marks().back().at_ms;
}

Session::Timeline Session::timeline(std::int64_t since_ms) const {
    std::lock_guard lock(state_mutex_);
    return {timeline_.since(since_ms), timeline_.truncated(), adopted_};
}

void Session::publish_info() {
    if (!listener_.on_info)
        return;
    listener_.on_info(info());
}

// ---- agent-host's self-update (design doc 7.4 OQ-09)

namespace {

constexpr std::uint32_t kSessionMagic = 0x53534441u;  // "ADSS"
constexpr std::uint32_t kSessionFormat = 1;

std::int64_t to_ticks(std::chrono::system_clock::time_point t) {
    return static_cast<std::int64_t>(t.time_since_epoch().count());
}
std::chrono::system_clock::time_point from_ticks(std::int64_t v) {
    return std::chrono::system_clock::time_point(std::chrono::system_clock::duration(v));
}

// SessionInfo in the handover format (the history fields are rebuilt from the engine).
void write_info(terminal::ByteWriter& w, const SessionInfo& i) {
    w.u8(static_cast<std::uint8_t>(i.process_state));
    w.boolean(i.exit_code.has_value());
    w.u32(i.exit_code.value_or(0));
    w.u32(i.pid);
    w.boolean(i.stop_requested);
    w.boolean(i.still_running_after_interrupt);
    w.u32(i.remaining_processes);
    w.u8(static_cast<std::uint8_t>(i.agent.state));
    w.str(i.agent.source);
    w.i64(to_ticks(i.agent.updated_at));
    w.u8(static_cast<std::uint8_t>(i.agent.kind));
    w.str(i.agent.message);
    w.u8(static_cast<std::uint8_t>(i.agent.last_state));
    w.str(i.agent.app);
    w.u64(i.bytes_from_program);
    w.u64(i.backpressure_waits);
    w.str(i.cwd_uri);
}

// The reverse of write_info; false when a state or kind is out of range.
bool read_info(terminal::ByteReader& r, SessionInfo& i) {
    const auto ps = r.u8();
    if (ps > static_cast<std::uint8_t>(ProcessState::kLost))
        return false;
    i.process_state = static_cast<ProcessState>(ps);
    const bool has_code = r.boolean();
    const auto code = r.u32();
    if (has_code)
        i.exit_code = code;
    i.pid = r.u32();
    i.stop_requested = r.boolean();
    i.still_running_after_interrupt = r.boolean();
    i.remaining_processes = r.u32();
    const auto as = r.u8();
    i.agent.source = r.str();
    i.agent.updated_at = from_ticks(r.i64());
    const auto kind = r.u8();
    i.agent.message = r.str();
    const auto last = r.u8();
    i.agent.app = r.str();
    if (as > static_cast<std::uint8_t>(AgentState::kError) ||
        last > static_cast<std::uint8_t>(AgentState::kError) ||
        kind > static_cast<std::uint8_t>(terminal::StatusKind::kAuth))
        return false;
    i.agent.state = static_cast<AgentState>(as);
    i.agent.kind = static_cast<terminal::StatusKind>(kind);
    i.agent.last_state = static_cast<AgentState>(last);
    i.bytes_from_program = r.u64();
    i.backpressure_waits = r.u64();
    i.cwd_uri = r.str();
    return true;
}

}  // namespace

bool Session::can_hand_over(std::string* why) const {
    if (!process_ || process_->closed())
        return true;  // the program exited: only its screen and history move
    return process_->handover_handles(why).has_value();
}

std::optional<Session::Frozen> Session::freeze(std::chrono::milliseconds timeout, std::string* why) {
    const auto deadline = Clock::now() + timeout;
    if (process_) {
        // The reader must not wait for queue space while it is being stopped.
        {
            std::lock_guard lock(queue_mutex_);
            no_backpressure_ = true;
        }
        queue_space_cv_.notify_all();
        const bool paused = process_->pause_reader(timeout);
        {
            std::lock_guard lock(queue_mutex_);
            no_backpressure_ = stopping_.load();
        }
        if (!paused) {
            *why = "the terminal's output did not pause";
            return std::nullopt;
        }
    }
    // Every chunk the reader delivered is queued before this command: FIFO keeps byte order.
    FreezeCmd cmd{deadline, std::make_shared<std::promise<std::string>>(), std::make_shared<std::string>()};
    auto future = cmd.result->get_future();
    auto error = cmd.error;
    post(std::move(cmd));
    if (future.wait_until(deadline + std::chrono::seconds(2)) != std::future_status::ready) {
        *why = "the terminal did not answer";
        return std::nullopt;
    }
    Frozen frozen;
    frozen.state = future.get();
    if (frozen.state.empty()) {
        if (error->empty())
            return frozen;  // exited mid-sequence: the record only (handle(FreezeCmd))
        *why = *error;
        return std::nullopt;
    }
    if (process_ && !process_->closed()) {
        frozen.handles = process_->handover_handles(why);
        if (!frozen.handles)
            return std::nullopt;
    }
    return frozen;
}

void Session::handle(FreezeCmd& c) {
    frozen_ = true;
    // Finish an escape sequence the program is in the middle of: the reader is paused, so the
    // actor reads on, byte by byte, until the parser is back in its ground state.
    while (!engine_->at_handover_point()) {
        const bool live = process_ && !process_->closed();
        if (!live) {
            // Exited mid-sequence: no more bytes will come. Its record moves, not its screen.
            c.error->clear();
            c.result->set_value({});
            return;
        }
        // Checked on every byte: a program that keeps writing one endless sequence must not hold
        // the actor (and the handover) past the deadline.
        if (Clock::now() >= c.deadline) {
            *c.error = "a program stopped in the middle of an escape sequence";
            c.result->set_value({});
            return;
        }
        std::string byte = process_->read_available(1);
        if (byte.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        OutputChunk chunk{std::move(byte)};
        handle(chunk);
    }
    queue_pty_bytes(engine_->take_pty_input());
    // Every input byte reaches the program before the new host owns the pipe.
    {
        std::unique_lock lock(write_mutex_);
        const bool idle = writer_idle_cv_.wait_until(lock, c.deadline, [&] {
            return (write_queue_.empty() && !writer_busy_) || !process_ || process_->closed();
        });
        if (!idle) {
            *c.error = "a program is not reading its input";
            c.result->set_value({});
            return;
        }
    }
    publish(true);
    c.result->set_value(save_session_state());
}

void Session::handle(UnfreezeCmd& c) {
    frozen_ = false;
    c.done->set_value();
}

void Session::resume() {
    // freeze() may have given up while the actor still reads the program's bytes itself
    // (handle(FreezeCmd)). The reader may only start again once the actor has stopped: the queue
    // runs in order, so the actor reaching this command means any FreezeCmd before it finished.
    // That is bounded by the freeze deadline; two readers would mix up the bytes' order.
    UnfreezeCmd cmd{std::make_shared<std::promise<void>>()};
    auto done = cmd.done->get_future();
    post(std::move(cmd));
    done.wait();
    if (process_)
        process_->resume_reader();  // no-op once closed
}

void Session::release_after_handover() {
    if (process_)
        process_->release_after_handover();
}

std::string Session::save_session_state() const {
    terminal::ByteWriter w;
    w.u32(kSessionMagic);
    w.u32(kSessionFormat);
    w.str(engine_->save_state());
    write_info(w, info());
    w.u64(report_seq_.size());
    for (const auto& [source, seq] : report_seq_) {
        w.str(source);
        w.u64(seq);
    }
    w.boolean(interrupt_deadline_.has_value());
    w.i64(interrupt_deadline_
              ? std::chrono::duration_cast<std::chrono::milliseconds>(*interrupt_deadline_ - Clock::now())
                    .count()
              : 0);
    w.u32(kSessionMagic);
    return w.take();
}

std::unique_ptr<Session> Session::adopt(SessionConfig config, SessionListener listener,
                                        std::string_view state,
                                        const std::optional<platform::win::PtyHandles>& handles) {
    std::unique_ptr<Session> self(new Session(std::move(config), std::move(listener)));
    terminal::ByteReader r(state);
    const auto bad = [] { return std::runtime_error("not a session state this agent-host reads"); };
    if (r.u32() != kSessionMagic || r.u32() != kSessionFormat)
        throw bad();
    const std::string engine_state = r.str();
    if (!r.ok())
        throw bad();
    self->engine_ = terminal::VtEngine::restore(self->config_.engine, self->engine_events(), engine_state);
    SessionInfo& i = self->info_;
    if (!read_info(r, i))
        throw bad();
    const auto sources = r.count(16);
    for (std::uint64_t k = 0; k < sources && r.ok(); ++k) {
        std::string source = r.str();
        self->report_seq_[std::move(source)] = r.u64();
    }
    const bool has_deadline = r.boolean();
    const auto remaining = r.i64();
    if (has_deadline)
        self->interrupt_deadline_ =
            Clock::now() + std::chrono::milliseconds(std::max<std::int64_t>(remaining, 0));
    if (r.u32() != kSessionMagic || !r.ok() || !r.at_end())
        throw bad();
    self->backpressure_waits_ = i.backpressure_waits;
    if (handles) {
        if (handles->pid != i.pid)
            throw std::runtime_error("handed over process is not this session's");
        // Last step that can throw: once adopted, the process must be released, never closed.
        self->process_ = self->adopt_process(*handles);
    } else if (i.process_state != ProcessState::kExited && i.process_state != ProcessState::kFailed) {
        throw std::runtime_error("a running session came without its terminal");
    }
    self->snapshot_ = std::make_shared<const terminal::ScreenSnapshot>(self->engine_->snapshot());
    self->published_seq_ = self->engine_->seq();
    return self;
}

void Session::start_adopted() {
    {
        // The history does not move with a handover (it starts at the takeover).
        std::lock_guard lock(state_mutex_);
        adopted_ = true;
        note_state_locked();
    }
    writer_ = std::thread([this] { writer_loop(); });
    actor_ = std::thread([this] { actor_loop(); });
    if (process_)
        process_->resume_reader();
    publish_info();
}

}  // namespace agentdesk::host
