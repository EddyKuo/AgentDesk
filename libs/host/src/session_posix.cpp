// The macOS half of session.cpp (ADR-30): how the actor waits (a condition variable the pty's
// exit callback also signals), starting or adopting the terminal, and the host name OSC 7
// folders are checked against.
#include <algorithm>

#include <unistd.h>

#include "agentdesk/host/session.hpp"
#include "agentdesk/terminal/cwd_uri.hpp"

namespace agentdesk::host {

namespace win = platform::win;
using Clock = std::chrono::steady_clock;

struct Session::ActorWake {
    std::condition_variable cv;
    bool signalled = false;  // guarded by queue_mutex_
};

void Session::ActorWakeDeleter::operator()(ActorWake* p) const noexcept {
    delete p;
}

Session::Session(SessionConfig config, SessionListener listener)
    : config_(std::move(config)), listener_(std::move(listener)), wake_(new ActorWake) {}

void Session::wake_actor() {
    {
        std::lock_guard lock(queue_mutex_);
        wake_->signalled = true;
    }
    wake_->cv.notify_all();
}

std::unique_ptr<win::PtyProcess> Session::spawn_process() {
    return win::PtyProcess::spawn(config_.api, config_.spec, output_sink(), [this] { wake_actor(); });
}

std::unique_ptr<win::PtyProcess> Session::adopt_process(const win::PtyHandles& handles) {
    return win::PtyProcess::adopt(config_.api, handles, output_sink(), [this] { wake_actor(); });
}

void Session::wait_for_work(bool& root_exited) {
    // The pty's exit callback wakes us; a deadline bounds the wait like the Windows timeout.
    std::optional<Clock::time_point> wake_at;
    if (engine_->seq() != published_seq_)
        wake_at = last_publish_ + config_.snapshot_interval;
    if (interrupt_deadline_)
        wake_at = wake_at ? std::min(*wake_at, *interrupt_deadline_) : *interrupt_deadline_;
    {
        std::unique_lock lock(queue_mutex_);
        const auto ready = [&] { return wake_->signalled || !queue_.empty() || stopping_.load(); };
        if (wake_at)
            wake_->cv.wait_until(lock, *wake_at, ready);
        else
            wake_->cv.wait(lock, ready);
        wake_->signalled = false;
    }

    drain_queue();

    if (!root_exited && !frozen_ && process_->wait_exit(std::chrono::milliseconds(0)).has_value()) {
        root_exited = true;
        on_root_exit();
    }
}

// The platform layer reports -1 for an exit it could not observe; real codes are 0..255.
std::optional<std::uint32_t> Session::recorded_exit_code(std::optional<std::uint32_t> waited) {
    if (waited == static_cast<std::uint32_t>(-1))
        return std::nullopt;
    return waited.value_or(0);
}

std::string Session::local_path_from_cwd_uri(const std::string& uri) {
    static const std::string local = [] {
        char buf[256]{};
        return ::gethostname(buf, sizeof(buf) - 1) == 0 ? std::string(buf) : std::string();
    }();
    return terminal::posix_path_from_cwd_uri(uri, local);
}

}  // namespace agentdesk::host
