// The macOS stand-ins for the Windows helper headers (libs/platform/posix/compat, see its README):
// text conversion and win::PtyProcess on top of platform::Pty.
#include <stdexcept>

#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/process_tree.hpp"
#include "agentdesk/platform/win/pty_process.hpp"

namespace agentdesk::platform::win {

std::string to_utf8(std::wstring_view text) {
    std::string out;
    out.reserve(text.size());
    for (const wchar_t wc : text) {
        const auto c = static_cast<std::uint32_t>(wc);
        if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
            throw std::range_error("invalid code point");
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

std::wstring to_wide(std::string_view utf8) {
    std::wstring out;
    out.reserve(utf8.size());
    std::size_t i = 0;
    while (i < utf8.size()) {
        const auto b = static_cast<unsigned char>(utf8[i]);
        std::uint32_t c = 0;
        std::size_t len = 0;
        if (b < 0x80) {
            c = b;
            len = 1;
        } else if ((b & 0xE0) == 0xC0) {
            c = b & 0x1F;
            len = 2;
        } else if ((b & 0xF0) == 0xE0) {
            c = b & 0x0F;
            len = 3;
        } else if ((b & 0xF8) == 0xF0) {
            c = b & 0x07;
            len = 4;
        } else {
            throw std::range_error("invalid UTF-8");
        }
        if (i + len > utf8.size())
            throw std::range_error("truncated UTF-8");
        for (std::size_t k = 1; k < len; ++k) {
            const auto cb = static_cast<unsigned char>(utf8[i + k]);
            if ((cb & 0xC0) != 0x80)
                throw std::range_error("invalid UTF-8");
            c = (c << 6) | (cb & 0x3F);
        }
        static constexpr std::uint32_t kMin[] = {0, 0, 0x80, 0x800, 0x10000};
        if (c < kMin[len] || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
            throw std::range_error("invalid UTF-8");
        out.push_back(static_cast<wchar_t>(c));
        i += len;
    }
    return out;
}

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
    for (const auto& a : spec.args)
        s.args.push_back(to_utf8(a));
    s.cwd = spec.cwd;
    for (const auto& [k, v] : spec.env_overrides)
        s.env_overrides[to_utf8(k)] = to_utf8(v);
    s.size = {static_cast<std::uint16_t>(spec.size.cols > 0 ? spec.size.cols : 1),
              static_cast<std::uint16_t>(spec.size.rows > 0 ? spec.size.rows : 1)};
    return s;
}

}  // namespace

PtyProcess::PtyProcess(std::unique_ptr<platform::Pty> pty) : pty_(std::move(pty)) {}

PtyProcess::~PtyProcess() = default;

std::unique_ptr<PtyProcess> PtyProcess::spawn(const ConPtyApi& /*api*/, const PtySpawnSpec& spec,
                                              PtyOutputSink on_output, std::function<void()> on_exit) {
    return std::unique_ptr<PtyProcess>(
        new PtyProcess(platform::Pty::spawn(neutral_spec(spec), std::move(on_output), std::move(on_exit))));
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
