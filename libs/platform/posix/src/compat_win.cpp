// The macOS stand-ins for the Windows helper headers (libs/platform/posix/compat, see its README):
// text conversion and win::PtyProcess on top of platform::Pty.
#include <cerrno>
#include <stdexcept>
#include <system_error>

#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/text.hpp"
#include "agentdesk/platform/win/process_tree.hpp"
#include "agentdesk/platform/win/pty_process.hpp"

namespace agentdesk::platform::win {

bool is_shell_name(const std::wstring& name) {
    return platform::is_shell_name(to_utf8(name));
}

bool is_launchable_file(const std::wstring& path) {
    return platform::is_launchable_file(to_utf8(path));
}

namespace {

platform::PtySpawnSpec neutral_spec(const PtySpawnSpec& spec) {
    platform::PtySpawnSpec s;
    s.executable = spec.executable;
    s.args = spec.args;
    s.cwd = spec.cwd;
    s.env_overrides = spec.env_overrides;
    s.size = {static_cast<std::uint16_t>(spec.size.cols > 0 ? spec.size.cols : 1),
              static_cast<std::uint16_t>(spec.size.rows > 0 ? spec.size.rows : 1)};
    return s;
}

}  // namespace

PtyProcess::PtyProcess(std::unique_ptr<platform::Pty> pty) : pty_(std::move(pty)) {}

PtyProcess::~PtyProcess() = default;

std::unique_ptr<PtyProcess> PtyProcess::spawn(const ConPtyApi& /*api*/, const PtySpawnSpec& spec,
                                              PtyOutputSink on_output, std::function<void()> on_exit) {
    // As on Windows, a program that cannot be started is a system_error (CreateProcess's).
    try {
        return std::unique_ptr<PtyProcess>(
            new PtyProcess(platform::Pty::spawn(neutral_spec(spec), std::move(on_output), std::move(on_exit))));
    } catch (const std::invalid_argument& e) {
        throw std::system_error(ENOENT, std::generic_category(), e.what());
    }
}

void PtyProcess::write(std::string_view bytes) {
    pty_->write(bytes);
}

void PtyProcess::resize(PtySize size) {
    pty_->resize({static_cast<std::uint16_t>(size.cols > 0 ? size.cols : 1),
                  static_cast<std::uint16_t>(size.rows > 0 ? size.rows : 1)});
}

void PtyProcess::interrupt() {
    pty_->interrupt();
}

void PtyProcess::terminate(unsigned /*exit_code*/) {
    pty_->terminate();
}

std::optional<std::uint32_t> PtyProcess::wait_exit(std::chrono::milliseconds timeout) const {
    const auto code = pty_->wait_exit(timeout);
    if (!code)
        return std::nullopt;
    return static_cast<std::uint32_t>(*code);
}

std::uint32_t PtyProcess::pid() const noexcept {
    return pty_->pid();
}

// Like a Windows job's count, the root is included while it is known to have run: callers
// subtract it after the root exited.
std::uint32_t PtyProcess::job_active_processes() const {
    return static_cast<std::uint32_t>(pty_->live_processes() + (pty_->exit_code() ? 1 : 0));
}

std::string PtyProcess::foreground_program() const {
    return pty_->foreground().program;
}

Foreground PtyProcess::foreground() const {
    auto f = pty_->foreground();
    return {std::move(f.program), std::move(f.cwd), std::move(f.argv)};
}

void PtyProcess::close() {
    pty_->close();
}

bool PtyProcess::closed() const {
    return pty_->closed();
}

bool PtyProcess::pause_reader(std::chrono::milliseconds timeout) {
    return pty_->pause_reader(timeout);
}

void PtyProcess::resume_reader() {
    pty_->resume_reader();
}

std::string PtyProcess::read_available(std::size_t max) {
    return pty_->read_available(max);
}

std::optional<PtyHandles> PtyProcess::handover_handles(std::string* why) const {
    if (pty_->closed()) {
        if (why != nullptr)
            *why = "terminal already closed";
        return std::nullopt;
    }
    return PtyHandles{pty_->handover_fd(), pty_->pid()};
}

void PtyProcess::release_after_handover() {
    pty_->release_after_handover();
}

std::unique_ptr<PtyProcess> PtyProcess::adopt(const ConPtyApi& /*api*/, const PtyHandles& handles,
                                              PtyOutputSink on_output, std::function<void()> on_exit) {
    return std::unique_ptr<PtyProcess>(new PtyProcess(
        platform::Pty::adopt(handles.master_fd, handles.pid, std::move(on_output), std::move(on_exit))));
}

}  // namespace agentdesk::platform::win
