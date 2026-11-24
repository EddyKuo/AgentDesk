// process.hpp on Windows: child processes with pipes, background processes, PATH lookup.
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/detached.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/job.hpp"
#include "agentdesk/platform/win/pty_process.hpp"
#include "agentdesk/platform/win/unique_handle.hpp"

namespace agentdesk::platform {

using win::UniqueHandle;

struct Process::Impl {
    std::optional<win::KillOnCloseJob> job;  // end_with_parent
    UniqueHandle process;
    UniqueHandle in_write;  // child's stdin
    UniqueHandle out_read;  // child's stdout (and stderr when merged)
    UniqueHandle err_read;  // child's stderr, with pipe_stderr
    DWORD pid = 0;
};

namespace {

// One anonymous pipe; neither end is inheritable yet.
void make_pipe(UniqueHandle& read_end, UniqueHandle& write_end) {
    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
    if (!::CreatePipe(&rd, &wr, nullptr, 0))
        win::throw_last_error("CreatePipe");
    read_end.reset(rd);
    write_end.reset(wr);
}

UniqueHandle null_device(DWORD access) {
    UniqueHandle h(::CreateFileW(L"NUL", access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                 0, nullptr));
    if (!win::is_valid(h))
        win::throw_last_error("CreateFileW(NUL)");
    return h;
}

std::vector<std::wstring> wide_args(const std::vector<std::string>& args) {
    std::vector<std::wstring> out;
    out.reserve(args.size());
    for (const auto& a : args)
        out.push_back(to_wide(a));
    return out;
}

std::wstring environment_block(const std::map<std::string, std::string>& overrides) {
    std::map<std::wstring, std::wstring> wide;
    for (const auto& [name, value] : overrides)
        wide[to_wide(name)] = to_wide(value);
    return win::make_environment_block(wide);
}

std::optional<std::string> read_pipe(HANDLE pipe, std::size_t max) {
    if (pipe == nullptr || max == 0)
        return std::nullopt;
    std::string buf(std::min<std::size_t>(max, 1u << 20), '\0');
    DWORD n = 0;
    if (!::ReadFile(pipe, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) || n == 0)
        return std::nullopt;
    buf.resize(n);
    return buf;
}

// The child's three standard handles, the ones it inherits.
struct ChildStdio {
    UniqueHandle in;
    UniqueHandle out;
    UniqueHandle err;  // empty: the same as out

    [[nodiscard]] std::vector<HANDLE> handles() const {
        std::vector<HANDLE> h = {in.get(), out.get()};
        if (err)
            h.push_back(err.get());
        return h;
    }
};

ChildStdio make_stdio(const ProcessSpec& spec, UniqueHandle& in_write, UniqueHandle& out_read,
                      UniqueHandle& err_read) {
    ChildStdio s;
    if (spec.pipe_stdin)
        make_pipe(s.in, in_write);
    else
        s.in = null_device(GENERIC_READ);
    if (spec.pipe_stdout)
        make_pipe(out_read, s.out);
    else
        s.out = null_device(GENERIC_WRITE);
    if (!spec.merge_stderr || !spec.pipe_stdout) {
        if (spec.pipe_stderr)
            make_pipe(err_read, s.err);
        else
            s.err = null_device(GENERIC_WRITE);
    }
    for (HANDLE h : s.handles())
        if (!::SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
            win::throw_last_error("SetHandleInformation(pipe)");
    return s;
}

}  // namespace

Process::Process(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Process::~Process() {
    // Our pipe ends close first; closing the job (end_with_parent) then ends what is left.
    impl_->in_write.reset();
    impl_->out_read.reset();
    impl_->err_read.reset();
}

std::unique_ptr<Process> Process::spawn(const ProcessSpec& spec) {
    if (win::is_batch_file(spec.executable.native()))
        throw std::invalid_argument("batch files need the shell launcher adapter");
    auto impl = std::make_unique<Impl>();
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(STARTUPINFOW);
    DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
    // Without any pipe the child keeps its own (hidden) console's handles.
    ChildStdio stdio;
    std::vector<HANDLE> inherit;
    win::AttributeList attrs(1);
    if (spec.pipe_stdin || spec.pipe_stdout || spec.pipe_stderr) {
        stdio = make_stdio(spec, impl->in_write, impl->out_read, impl->err_read);
        // The child inherits exactly these. Children started at the same time from other threads
        // must not get each other's: a write end held by another child keeps a reader from ever
        // seeing the end of the stream.
        inherit = stdio.handles();
        attrs.update(PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit.data(), inherit.size() * sizeof(HANDLE));
        si.lpAttributeList = attrs.get();
        si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = stdio.in.get();
        si.StartupInfo.hStdOutput = stdio.out.get();
        si.StartupInfo.hStdError = stdio.err ? stdio.err.get() : stdio.out.get();
        flags |= EXTENDED_STARTUPINFO_PRESENT;
    }
    if (spec.end_with_parent) {
        impl->job.emplace();
        flags |= CREATE_SUSPENDED;  // nothing runs outside the job
    }
    std::wstring cmdline = win::build_command_line(spec.executable.native(), wide_args(spec.args));
    std::wstring env;
    if (!spec.env_overrides.empty())
        env = environment_block(spec.env_overrides);
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(spec.executable.c_str(), cmdline.data(), nullptr, nullptr, !inherit.empty(), flags,
                          env.empty() ? nullptr : env.data(), spec.cwd.empty() ? nullptr : spec.cwd.c_str(),
                          &si.StartupInfo, &pi))
        win::throw_last_error("CreateProcessW");
    impl->process.reset(pi.hProcess);
    impl->pid = pi.dwProcessId;
    const UniqueHandle thread(pi.hThread);
    if (impl->job) {
        try {
            impl->job->assign(pi.hProcess);
            if (::ResumeThread(thread.get()) == static_cast<DWORD>(-1))
                win::throw_last_error("ResumeThread");
        } catch (...) {
            ::TerminateProcess(pi.hProcess, 1);  // still suspended: nothing ran
            throw;
        }
    }
    return std::unique_ptr<Process>(new Process(std::move(impl)));
}

bool Process::write(std::string_view bytes) {
    if (!impl_->in_write)
        return false;
    while (!bytes.empty()) {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), 1u << 20));
        DWORD n = 0;
        if (!::WriteFile(impl_->in_write.get(), bytes.data(), chunk, &n, nullptr))
            return false;
        bytes.remove_prefix(n);
    }
    return true;
}

void Process::close_stdin() {
    impl_->in_write.reset();
}

std::optional<std::string> Process::read_some(std::size_t max) {
    return read_pipe(impl_->out_read.get(), max);
}

std::optional<std::string> Process::read_some_stderr(std::size_t max) {
    return read_pipe(impl_->err_read.get(), max);
}

std::optional<int> Process::wait(std::chrono::milliseconds timeout) {
    const auto ms = static_cast<DWORD>(std::clamp<long long>(timeout.count(), 0, INFINITE - 1));
    if (::WaitForSingleObject(impl_->process.get(), ms) != WAIT_OBJECT_0)
        return std::nullopt;
    DWORD code = 1;
    if (!::GetExitCodeProcess(impl_->process.get(), &code))
        win::throw_last_error("GetExitCodeProcess");
    return static_cast<int>(code);
}

void Process::kill() {
    if (impl_->job) {
        try {
            impl_->job->terminate(1);  // the whole tree
            return;
        } catch (const std::system_error&) {
        }
    }
    ::TerminateProcess(impl_->process.get(), 1);
}

std::uint32_t Process::pid() const noexcept {
    return impl_->pid;
}

RunResult run_capture(const ProcessSpec& spec, std::chrono::milliseconds timeout) {
    ProcessSpec s = spec;
    s.pipe_stdout = true;
    s.end_with_parent = true;
    auto child = Process::spawn(s);
    RunResult r;
    std::thread reader([&] {
        while (auto chunk = child->read_some(64 * 1024))
            r.out += *chunk;
    });
    if (const auto code = child->wait(timeout)) {
        r.exit_code = *code;
    } else {
        r.timed_out = true;
        child->kill();
        child->wait(std::chrono::seconds(5));
    }
    reader.join();
    return r;
}

DetachedResult spawn_detached(const std::filesystem::path& exe, const std::vector<std::string>& args,
                              const std::map<std::string, std::string>& env_overrides) {
    std::wstring env;
    if (!env_overrides.empty())
        env = environment_block(env_overrides);
    const auto r = win::spawn_detached(exe, wide_args(args), std::move(env));
    return {r.pid, !r.broke_away && r.parent_job_kills_on_close};
}

std::optional<std::filesystem::path> find_executable(std::string_view name) {
    if (name.empty())
        return std::nullopt;
    const std::wstring wide = to_wide(name);
    std::wstring found(MAX_PATH, L'\0');
    const auto search = [&] {
        return ::SearchPathW(nullptr, wide.c_str(), L".exe", static_cast<DWORD>(found.size()), found.data(),
                             nullptr);
    };
    DWORD n = search();
    if (n >= found.size()) {  // too small: n is the size it needs
        found.resize(n);
        n = search();
    }
    if (n == 0 || n >= found.size())
        return std::nullopt;
    found.resize(n);
    return std::filesystem::path(found);
}

bool is_launchable_file(const std::filesystem::path& path) {
    return win::is_launchable_file(path.native());
}

bool is_batch_file(const std::filesystem::path& path) {
    return win::is_batch_file(path.native());
}

}  // namespace agentdesk::platform
