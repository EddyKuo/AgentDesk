// platform::Pty on Windows: PtyProcess (ConPTY and a kill-on-close job), with the root's exit
// reported through a thread pool wait instead of a handle the caller would have to wait on.
#include <windows.h>

#include <mutex>
#include <stdexcept>

#include "agentdesk/platform/pty.hpp"
#include "agentdesk/platform/pty_handles.hpp"
#include "agentdesk/platform/system.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/pty_conpty.hpp"
#include "agentdesk/platform/win/pty_process.hpp"

namespace agentdesk::platform {

namespace win {

namespace {

struct ConPtyChoice {
    std::mutex mutex;
    std::optional<ConPtyApi> api;
};
ConPtyChoice& conpty_choice() {
    static ConPtyChoice c;
    return c;
}

}  // namespace

ConPtyApi pty_conpty() {
    auto& c = conpty_choice();
    std::lock_guard lock(c.mutex);
    if (!c.api)
        c.api = load_conpty(executable_path().parent_path());
    return *c.api;
}

void use_conpty(ConPtyApi api) {
    auto& c = conpty_choice();
    std::lock_guard lock(c.mutex);
    c.api = std::move(api);
}

}  // namespace win

struct Pty::Impl {
    std::unique_ptr<win::PtyProcess> process;
    PtyExitSink on_exit;
    std::mutex wait_mutex;
    HANDLE wait = nullptr;

    static void CALLBACK exited(void* context, BOOLEAN /*timed_out*/) {
        const auto* self = static_cast<Impl*>(context);
        try {
            self->on_exit();
        } catch (...) {  // nothing may escape into the thread pool
        }
    }

    void watch() {
        if (!on_exit)
            return;
        if (!::RegisterWaitForSingleObject(&wait, process->process_handle(), &Impl::exited, this, INFINITE,
                                           WT_EXECUTEONLYONCE))
            win::throw_last_error("RegisterWaitForSingleObject(root process)");
    }

    // Afterwards the sink is not running and will not be called; must be done while the process
    // handle is still open.
    void unwatch() {
        std::lock_guard lock(wait_mutex);
        if (wait != nullptr)
            ::UnregisterWaitEx(std::exchange(wait, nullptr), INVALID_HANDLE_VALUE);
    }
};

Pty::Pty(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Pty::~Pty() {
    if (impl_)
        impl_->unwatch();  // ~PtyProcess closes the rest
}

std::unique_ptr<Pty> Pty::spawn(const PtySpawnSpec& spec, PtyOutputSink on_output, PtyExitSink on_exit) {
    win::PtySpawnSpec s;
    s.executable = spec.executable;
    s.args = spec.args;
    s.cwd = spec.cwd;
    s.env_overrides = spec.env_overrides;
    s.size = {static_cast<short>(spec.size.cols), static_cast<short>(spec.size.rows)};
    auto impl = std::make_unique<Impl>();
    impl->on_exit = std::move(on_exit);
    impl->process = win::PtyProcess::spawn(win::pty_conpty(), s, std::move(on_output));
    impl->watch();
    return std::unique_ptr<Pty>(new Pty(std::move(impl)));
}

std::unique_ptr<Pty> Pty::adopt(const PtyHandles& handles, PtyOutputSink on_output, PtyExitSink on_exit) {
    auto impl = std::make_unique<Impl>();
    impl->on_exit = std::move(on_exit);
    impl->process = win::PtyProcess::adopt(win::pty_conpty(), handles, std::move(on_output));
    try {
        impl->watch();
    } catch (...) {
        impl->process->release_after_handover();  // the old host still holds the terminal
        throw;
    }
    return std::unique_ptr<Pty>(new Pty(std::move(impl)));
}

void Pty::write(std::string_view bytes) {
    impl_->process->write(bytes);
}

void Pty::resize(PtySize size) {
    impl_->process->resize({static_cast<short>(size.cols), static_cast<short>(size.rows)});
}

void Pty::interrupt() {
    impl_->process->interrupt();
}

void Pty::terminate() {
    impl_->process->terminate(1);
}

std::optional<int> Pty::exit_code() const {
    return wait_exit(std::chrono::milliseconds(0));
}

bool Pty::exit_code_known() const {
    return true;
}

std::optional<int> Pty::wait_exit(std::chrono::milliseconds timeout) const {
    const auto code = impl_->process->wait_exit(timeout);
    if (!code)
        return std::nullopt;
    return static_cast<int>(*code);
}

std::uint32_t Pty::pid() const noexcept {
    return impl_->process->pid();
}

// The job still counts a root that has exited while its handle is open.
std::size_t Pty::live_processes() const {
    const std::size_t n = impl_->process->job_active_processes();
    return n > 0 && exit_code() ? n - 1 : n;
}

ForegroundInfo Pty::foreground() const {
    auto f = impl_->process->foreground();
    return {std::move(f.program), std::move(f.cwd), std::move(f.argv)};
}

std::string Pty::foreground_program() const {
    return impl_->process->foreground_program();
}

void Pty::close() {
    impl_->unwatch();
    impl_->process->close();
}

bool Pty::closed() const {
    return impl_->process->closed();
}

bool Pty::pause_reader(std::chrono::milliseconds timeout) {
    return impl_->process->pause_reader(timeout);
}

void Pty::resume_reader() {
    impl_->process->resume_reader();
}

std::string Pty::read_available(std::size_t max) {
    return impl_->process->read_available(max);
}

std::optional<PtyHandles> Pty::handover_handles(std::string* why) const {
    return impl_->process->handover_handles(why);
}

void Pty::release_after_handover() {
    impl_->unwatch();
    impl_->process->release_after_handover();
}

PtyCapabilities pty_capabilities() {
    const auto api = win::pty_conpty();
    // Only the bundled ConPTY passes the host's answers through (ADR-08).
    return {.answers_osc7501_query = api.bundled, .handover = api.pack != nullptr};
}

bool is_shell_name(std::string_view name) {
    return win::is_shell_name(win::to_wide(name));
}

}  // namespace agentdesk::platform
