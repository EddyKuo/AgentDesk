// The Windows half of session.cpp: how the actor waits (an event plus the root process handle),
// starting or adopting the terminal, and the computer name OSC 7 folders are checked against.
#include <windows.h>

#include <algorithm>

#include "agentdesk/host/session.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/unique_handle.hpp"
#include "agentdesk/terminal/cwd_uri.hpp"

namespace agentdesk::host {

namespace win = platform::win;
using Clock = std::chrono::steady_clock;

namespace {

DWORD millis_until(Clock::time_point t) {
    const auto now = Clock::now();
    if (t <= now)
        return 0;
    return static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(t - now).count() + 1);
}

}  // namespace

struct Session::ActorWake {
    win::UniqueHandle event;
};

void Session::ActorWakeDeleter::operator()(ActorWake* p) const noexcept {
    delete p;
}

Session::Session(SessionConfig config, SessionListener listener)
    : config_(std::move(config)),
      listener_(std::move(listener)),
      wake_(new ActorWake{win::UniqueHandle(::CreateEventW(nullptr, FALSE, FALSE, nullptr))}) {
    if (!wake_->event) {
        win::throw_last_error("CreateEventW(session queue)");
    }
}

void Session::wake_actor() {
    ::SetEvent(wake_->event.get());
}

std::unique_ptr<win::PtyProcess> Session::spawn_process() {
    return win::PtyProcess::spawn(config_.api, config_.spec, output_sink());
}

std::unique_ptr<win::PtyProcess> Session::adopt_process(const win::PtyHandles& handles) {
    return win::PtyProcess::adopt(config_.api, handles, output_sink());
}

void Session::wait_for_work(bool& root_exited) {
    DWORD timeout = INFINITE;
    if (engine_->seq() != published_seq_) {
        timeout = millis_until(last_publish_ + config_.snapshot_interval);
    }
    if (interrupt_deadline_) {
        timeout = std::min(timeout, millis_until(*interrupt_deadline_));
    }
    // Frozen for a handover: the program's exit is the new host's to see (design doc 7.4 OQ-09).
    const bool watch = !root_exited && !frozen_;
    HANDLE handles[2] = {wake_->event.get(), watch ? process_->process_handle() : nullptr};
    const DWORD count = watch ? 2 : 1;
    const DWORD r = ::WaitForMultipleObjects(count, handles, FALSE, timeout);

    drain_queue();

    if (!root_exited && !frozen_ &&
        ((watch && r == WAIT_OBJECT_0 + 1) ||
         ::WaitForSingleObject(process_->process_handle(), 0) == WAIT_OBJECT_0)) {
        root_exited = true;
        on_root_exit();
    }
}

std::optional<std::uint32_t> Session::recorded_exit_code(std::optional<std::uint32_t> waited) {
    return waited.value_or(0);
}

std::string Session::local_path_from_cwd_uri(const std::string& uri) {
    static const std::string local = [] {
        char buf[MAX_COMPUTERNAME_LENGTH + 1]{};
        DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
        return ::GetComputerNameA(buf, &n) ? std::string(buf, n) : std::string();
    }();
    return terminal::windows_path_from_cwd_uri(uri, local);
}

}  // namespace agentdesk::host
