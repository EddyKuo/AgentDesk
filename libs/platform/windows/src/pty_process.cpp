#include "agentdesk/platform/win/pty_process.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
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
    if (!is_launchable_file(spec.executable.native())) {
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
    struct Done {  // tells close() the reader has left, however it leaves
        PtyProcess* p;
        ~Done() {
            {
                std::lock_guard lock(p->reader_mutex_);
                p->reader_done_ = true;
            }
            p->reader_cv_.notify_all();
        }
    } done{this};
    std::array<char, kReadChunk> buffer{};
    for (;;) {
        if (pause_requested_) {
            return;  // handover: the rest stays in the pipe for the next reader
        }
        DWORD read = 0;
        if (!::ReadFile(output_read_.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                        nullptr)) {
            // ERROR_BROKEN_PIPE is the normal end once the pseudoconsole is closed;
            // ERROR_OPERATION_ABORTED is pause_reader() cancelling a read that got nothing.
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

// ---- agent-host's handover (design doc 7.4 OQ-09)

namespace {

// The bundled ConPTY's HPCON (microsoft/terminal v1.24.11911.0 src/winconpty/winconpty.h 11-41;
// fetch_conpty pins that build by hash). Only read after the checks in looks_like_conhost.
struct PseudoConsoleLayout {
    HANDLE signal;
    HANDLE reference;
    HANDLE conhost;
};

bool is_open_console(HANDLE process) {
    if (process == nullptr || ::GetProcessId(process) == 0)
        return false;
    wchar_t path[MAX_PATH * 2]{};
    DWORD n = MAX_PATH * 2;
    if (!::QueryFullProcessImageNameW(process, 0, path, &n))
        return false;
    const std::wstring name = lower(std::filesystem::path(std::wstring(path, n)).filename().native());
    return name == L"openconsole.exe";
}

bool is_pipe(HANDLE h) {
    return h != nullptr && h != INVALID_HANDLE_VALUE && ::GetFileType(h) == FILE_TYPE_PIPE;
}

}  // namespace

bool PtyProcess::pause_reader(std::chrono::milliseconds timeout) {
    // close() joins the reader too: one of them at a time. The reader never waits for this lock.
    std::lock_guard close_lock(close_mutex_);
    pause_requested_ = true;
    if (closed_ || !reader_.joinable())
        return true;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        {
            std::unique_lock lock(reader_mutex_);
            if (reader_cv_.wait_for(lock, std::chrono::milliseconds(20), [&] { return reader_done_; }))
                break;
        }
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        // A synchronous ReadFile on an anonymous pipe returns ERROR_OPERATION_ABORTED with
        // nothing read; a read that already got bytes completes normally and reaches the sink.
        ::CancelSynchronousIo(reader_.native_handle());
    }
    reader_.join();
    return true;
}

void PtyProcess::resume_reader() {
    std::lock_guard lock(close_mutex_);
    if (closed_)
        return;
    if (reader_.joinable())
        reader_.join();
    {
        std::lock_guard reader_lock(reader_mutex_);
        reader_done_ = false;
    }
    pause_requested_ = false;
    reader_ = std::thread([this] { reader_loop(); });
}

std::string PtyProcess::read_available(std::size_t max) {
    DWORD available = 0;
    if (!output_read_ || !::PeekNamedPipe(output_read_.get(), nullptr, 0, nullptr, &available, nullptr) ||
        available == 0)
        return {};
    std::string out(std::min<std::size_t>(available, max), '\0');
    DWORD read = 0;
    if (!::ReadFile(output_read_.get(), out.data(), static_cast<DWORD>(out.size()), &read, nullptr))
        return {};
    out.resize(read);
    return out;
}

std::optional<PtyHandles> PtyProcess::handover_handles(std::string* why) const {
    if (!api_.bundled || api_.pack == nullptr) {
        *why = "the system's ConPTY cannot hand terminals to another agent-host";
        return std::nullopt;
    }
    std::lock_guard close_lock(close_mutex_);
    if (closed_ || pcon_ == nullptr) {
        *why = "the terminal is closed";
        return std::nullopt;
    }
    const auto* layout = static_cast<const PseudoConsoleLayout*>(static_cast<const void*>(pcon_));
    if (!is_pipe(layout->signal) || layout->reference == nullptr ||
        layout->reference == INVALID_HANDLE_VALUE || !is_open_console(layout->conhost)) {
        *why = "this ConPTY build keeps its console differently; it cannot be handed over";
        return std::nullopt;
    }
    PtyHandles h;
    h.signal = layout->signal;
    h.reference = layout->reference;
    h.conhost = layout->conhost;
    h.input_write = input_write_.get();
    h.output_read = output_read_.get();
    h.process = process_.get();
    h.job = job_.get();
    h.pid = pid_;
    return h;
}

void PtyProcess::release_after_handover() {
    std::lock_guard lock(close_mutex_);
    if (closed_)
        return;
    closed_ = true;
    if (reader_.joinable())
        reader_.join();  // paused by the handover
    // The bundled ClosePseudoConsole only closes the three member handles and frees the struct
    // (winconpty.cpp 393-430). The new host holds duplicates of all three, so the signal pipe is
    // not broken and OpenConsole keeps running.
    if (pcon_ != nullptr) {
        api_.close(pcon_);
        pcon_ = nullptr;
    }
    {
        std::lock_guard write_lock(write_mutex_);
        input_write_.reset();
    }
    output_read_.reset();
    process_.reset();
    // job_ closes with *this: the new host's handle keeps the job (and KILL_ON_JOB_CLOSE) alive.
}

std::unique_ptr<PtyProcess> PtyProcess::adopt(const ConPtyApi& api, const PtyHandles& handles,
                                              PtyOutputSink on_output) {
    // Own every handle first, so any failure below closes all of them (the old host still holds
    // its own and keeps the terminal).
    UniqueHandle signal(handles.signal);
    UniqueHandle reference(handles.reference);
    UniqueHandle conhost(handles.conhost);
    UniqueHandle process(handles.process);
    UniqueHandle job(handles.job);
    std::unique_ptr<PtyProcess> self(new PtyProcess(api, std::move(on_output)));
    self->input_write_.reset(handles.input_write);
    self->output_read_.reset(handles.output_read);
    if (!self->on_output_)
        throw std::invalid_argument("PtyProcess::adopt: output sink is required");
    if (api.pack == nullptr)
        throw std::runtime_error("this agent-host's ConPTY cannot take over terminals");
    // The handle is the authority, not a PID: it must be the process the old host started.
    if (!is_valid(process) || handles.pid == 0 || ::GetProcessId(process.get()) != handles.pid)
        throw std::runtime_error("handed over process does not match its id");
    // Kept apart from *this until everything checked out: a refused adoption must only close
    // its duplicates, never terminate the job (close() would).
    KillOnCloseJob adopted_job(std::move(job));  // throws unless kill-on-close
    BOOL in_job = FALSE;
    if (!::IsProcessInJob(process.get(), adopted_job.get(), &in_job) || !in_job)
        throw std::runtime_error("handed over process is not in its job");
    if (!is_pipe(self->input_write_.get()) || !is_pipe(self->output_read_.get()) || !is_pipe(signal.get()))
        throw std::runtime_error("handed over terminal pipes are not pipes");
    if (!is_valid(reference) || !is_open_console(conhost.get()))
        throw std::runtime_error("handed over console host is not OpenConsole");
    HPCON pcon = nullptr;
    if (const HRESULT hr = api.pack(conhost.get(), reference.get(), signal.get(), &pcon); FAILED(hr))
        throw_hresult(hr, "ConptyPackPseudoConsole");
    // The packed HPCON owns these three now; ClosePseudoConsole closes them.
    (void)conhost.release();
    (void)reference.release();
    (void)signal.release();
    self->pcon_ = pcon;
    self->process_ = std::move(process);
    self->pid_ = handles.pid;
    self->job_ = std::move(adopted_job);
    {
        std::lock_guard lock(self->reader_mutex_);
        self->reader_done_ = true;  // no reader until resume_reader()
    }
    return self;
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

Foreground PtyProcess::foreground() const {
    return agentdesk::platform::win::foreground(job_.get(), pid_);
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
    //    A client that is still starting up can keep the console host, and so the pipe, alive
    //    after ClosePseudoConsole (seen under load with cmd.exe). Give the reader 3 s, then end
    //    the job's processes, which releases the pipe, and wait for it.
    if (reader_.joinable()) {
        bool drained = false;
        {
            std::unique_lock reader_lock(reader_mutex_);
            drained = reader_cv_.wait_for(reader_lock, std::chrono::seconds(3), [&] { return reader_done_; });
        }
        if (!drained) {
            try {
                job_.terminate(1);
            } catch (const std::system_error&) {
            }
        }
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
