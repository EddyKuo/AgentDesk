#include "agentdesk/platform/win/pty_process.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <system_error>

#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/process_tree.hpp"

namespace agentdesk::platform::win {

namespace {

constexpr DWORD kReadChunk = 64 * 1024;

std::wstring lower(std::wstring_view s) {
    std::wstring out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return out;
}

COORD to_coord(PtySize size) {
    // ConPTY sizes are in character cells; clamp to a sane range before handing them over.
    const auto clamp = [](short v, short lo, short hi) { return std::clamp(v, lo, hi); };
    return COORD{clamp(size.cols, 2, 1000), clamp(size.rows, 1, 500)};
}

}  // namespace

std::wstring make_environment_block(const std::map<std::wstring, std::wstring>& overrides) {
    // Case-insensitive keys, as Windows treats environment variable names.
    std::map<std::wstring, std::pair<std::wstring, std::wstring>> vars;  // lower(name) -> (name, value)

    LPWCH current = ::GetEnvironmentStringsW();
    if (current == nullptr) {
        throw_last_error("GetEnvironmentStringsW");
    }
    for (const wchar_t* p = current; *p != L'\0'; p += std::wcslen(p) + 1) {
        const std::wstring_view entry(p);
        // Entries such as "=C:=C:\\dir" start with '=': keep them verbatim, keyed by the whole entry.
        const auto eq = entry.find(L'=', 1);
        if (eq == std::wstring_view::npos) {
            continue;
        }
        const std::wstring name(entry.substr(0, eq));
        vars[lower(name)] = {name, std::wstring(entry.substr(eq + 1))};
    }
    ::FreeEnvironmentStringsW(current);

    for (const auto& [name, value] : overrides) {
        if (value.empty()) {
            vars.erase(lower(name));
        } else {
            vars[lower(name)] = {name, value};
        }
    }

    std::wstring block;
    for (const auto& [key, entry] : vars) {
        block.append(entry.first).append(L"=").append(entry.second).push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

PtyProcess::PtyProcess(const ConPtyApi& api, PtyOutputSink on_output)
    : api_(api), on_output_(std::move(on_output)) {}

std::unique_ptr<PtyProcess> PtyProcess::spawn(const ConPtyApi& api, const PtySpawnSpec& spec,
                                              PtyOutputSink on_output) {
    if (!on_output) {
        throw std::invalid_argument("PtyProcess::spawn: output sink is required");
    }
    if (is_batch_file(spec.executable.native())) {
        throw std::invalid_argument("PtyProcess::spawn: batch files need the shell launcher adapter");
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(spec.executable, ec)) {
        throw std::system_error(std::make_error_code(std::errc::no_such_file_or_directory),
                                "executable not found");
    }
    if (!std::filesystem::is_directory(spec.cwd, ec)) {
        throw std::system_error(std::make_error_code(std::errc::not_a_directory), "cwd not found");
    }

    std::unique_ptr<PtyProcess> self(new PtyProcess(api, std::move(on_output)));

    HANDLE in_read = nullptr;
    HANDLE in_write = nullptr;
    if (!::CreatePipe(&in_read, &in_write, nullptr, 0)) {
        throw_last_error("CreatePipe(input)");
    }
    UniqueHandle pty_input(in_read);
    self->input_write_.reset(in_write);

    HANDLE out_read = nullptr;
    HANDLE out_write = nullptr;
    if (!::CreatePipe(&out_read, &out_write, nullptr, 0)) {
        throw_last_error("CreatePipe(output)");
    }
    self->output_read_.reset(out_read);
    UniqueHandle pty_output(out_write);

    HPCON pcon = nullptr;
    if (const HRESULT hr = api.create(to_coord(spec.size), pty_input.get(), pty_output.get(), 0, &pcon);
        FAILED(hr)) {
        throw_hresult(hr, "CreatePseudoConsole");
    }
    self->pcon_ = pcon;  // from here on the destructor closes it

    AttributeList attrs(1);
    attrs.update(PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pcon, sizeof(pcon));

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = attrs.get();
    // Keep the child off the host's own (possibly redirected) stdio; it must use the ConPTY.
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = INVALID_HANDLE_VALUE;
    si.StartupInfo.hStdOutput = INVALID_HANDLE_VALUE;
    si.StartupInfo.hStdError = INVALID_HANDLE_VALUE;

    std::wstring command_line = build_command_line(spec.executable.native(), spec.args);
    std::wstring environment = make_environment_block(spec.env_overrides);
    const std::wstring cwd = spec.cwd.native();

    PROCESS_INFORMATION pi{};
    constexpr DWORD kFlags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;
    if (!::CreateProcessW(spec.executable.c_str(), command_line.data(), nullptr, nullptr, FALSE, kFlags,
                          environment.data(), cwd.c_str(), &si.StartupInfo, &pi)) {
        throw_last_error("CreateProcessW");
    }
    self->process_.reset(pi.hProcess);
    UniqueHandle thread(pi.hThread);
    self->pid_ = pi.dwProcessId;

    try {
        self->job_.assign(pi.hProcess);
    } catch (...) {
        // Still suspended: nothing ran yet, so no half-managed tree is left behind.
        ::TerminateProcess(pi.hProcess, 1);
        throw;
    }
    if (::ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        const DWORD err = ::GetLastError();
        self->job_.terminate(1);
        throw std::system_error(static_cast<int>(err), std::system_category(), "ResumeThread");
    }

    // The pseudoconsole holds its own copies. Keeping ours would prevent EOF on the reader.
    pty_input.reset();
    pty_output.reset();

    self->reader_ = std::thread([raw = self.get()] { raw->reader_loop(); });
    return self;
}

PtyProcess::~PtyProcess() {
    close();
}

void PtyProcess::reader_loop() noexcept {
    std::array<char, kReadChunk> buffer{};
    for (;;) {
        DWORD read = 0;
        if (!::ReadFile(output_read_.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                        nullptr)) {
            // ERROR_BROKEN_PIPE is the normal end once the pseudoconsole is closed.
            return;
        }
        if (read == 0) {
            return;
        }
        // The sink must not throw: dropping terminal bytes is not allowed (design 3.3), so a
        // throwing sink is a programming error and terminates (fail fast, IL-4).
        on_output_(std::span<const char>(buffer.data(), read));
    }
}

void PtyProcess::write(std::string_view bytes) {
    // write_mutex_ keeps close() from releasing the handle while WriteFile uses it. A write
    // blocked on a full pipe is released by close(): closing the pseudoconsole breaks the pipe.
    std::lock_guard lock(write_mutex_);
    if (!input_write_) {
        throw std::logic_error("PtyProcess::write after close");
    }
    while (!bytes.empty()) {
        const DWORD chunk =
            static_cast<DWORD>(std::min<size_t>(bytes.size(), std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!::WriteFile(input_write_.get(), bytes.data(), chunk, &written, nullptr)) {
            throw_last_error("WriteFile(pty input)");
        }
        bytes.remove_prefix(written);
    }
}

void PtyProcess::resize(PtySize size) {
    std::lock_guard lock(close_mutex_);
    if (closed_ || pcon_ == nullptr) {
        throw std::logic_error("PtyProcess::resize after close");
    }
    if (const HRESULT hr = api_.resize(pcon_, to_coord(size)); FAILED(hr)) {
        throw_hresult(hr, "ResizePseudoConsole");
    }
}

void PtyProcess::interrupt() {
    write(std::string_view("\x03", 1));
}

void PtyProcess::terminate(UINT exit_code) {
    job_.terminate(exit_code);
}

std::optional<DWORD> PtyProcess::wait_exit(std::chrono::milliseconds timeout) const {
    const auto ms = static_cast<DWORD>(std::clamp<long long>(timeout.count(), 0, INFINITE - 1));
    if (::WaitForSingleObject(process_.get(), ms) != WAIT_OBJECT_0) {
        return std::nullopt;
    }
    DWORD code = 0;
    if (!::GetExitCodeProcess(process_.get(), &code)) {
        throw_last_error("GetExitCodeProcess");
    }
    return code;
}

std::string PtyProcess::foreground_program() const {
    return agentdesk::platform::win::foreground_program(job_.get(), pid_);
}

void PtyProcess::close() {
    std::lock_guard lock(close_mutex_);
    if (closed_) {
        return;
    }
    closed_ = true;
    // 1) Close the pseudoconsole while the reader is still draining: on older Windows builds
    //    ClosePseudoConsole waits until its output has been consumed.
    if (pcon_ != nullptr) {
        api_.close(pcon_);
        pcon_ = nullptr;
    }
    // 2) The output pipe now reaches EOF; let the reader deliver the final bytes and exit.
    if (reader_.joinable()) {
        reader_.join();
    }
    // 3) Release pipes (input only once no write is in flight). 4) Kill whatever is left in
    //    the job; the job handle itself closes with *this.
    {
        std::lock_guard write_lock(write_mutex_);
        input_write_.reset();
    }
    output_read_.reset();
    try {
        job_.terminate(1);
    } catch (const std::system_error&) {
        // The job may already be empty; KILL_ON_JOB_CLOSE still applies when the handle closes.
    }
}

}  // namespace agentdesk::platform::win
