// The handover channel on Windows: two anonymous pipes, and every terminal's handles duplicated INTO
// the new process. The new host never takes a process by a PID: only handles the old host
// duplicated into it, which PtyProcess::adopt checks one by one.
#include <windows.h>

#include <algorithm>
#include <array>
#include <vector>

#include "agentdesk/host/handover.hpp"
#include "agentdesk/platform/system.hpp"
#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/unique_handle.hpp"
#include "handover_os.hpp"

namespace agentdesk::host {

namespace win = platform::win;
using platform::PtyHandles;
using win::UniqueHandle;

namespace {

bool pipe_write_all(HANDLE h, std::string_view bytes) {
    while (!bytes.empty()) {
        DWORD n = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), 1u << 20));
        if (!::WriteFile(h, bytes.data(), chunk, &n, nullptr))
            return false;
        bytes.remove_prefix(n);
    }
    return true;
}

bool pipe_read_exact(HANDLE h, char* out, std::size_t len) {
    while (len > 0) {
        DWORD n = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(len, 1u << 20));
        if (!::ReadFile(h, out, chunk, &n, nullptr) || n == 0)
            return false;
        out += n;
        len -= n;
    }
    return true;
}

std::optional<std::string> pipe_read_line(HANDLE h) {
    std::string line;
    for (;;) {
        char c = 0;
        if (!pipe_read_exact(h, &c, 1))
            return std::nullopt;
        if (c == '\n')
            return line;
        if (line.size() < 4096)
            line.push_back(c);
    }
}

std::uint64_t handle_value(HANDLE h) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(h));
}

HANDLE handle_from(std::uint64_t v) {
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(v));
}

// A live terminal's handles, in the order the handover format stores them.
std::array<void**, 7> handle_slots(PtyHandles& h) {
    return {&h.signal, &h.reference, &h.conhost, &h.input_write, &h.output_read, &h.process, &h.job};
}

// One anonymous pipe, both ends inheritable.
void make_pipe(UniqueHandle& read_end, UniqueHandle& write_end) {
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
    if (!::CreatePipe(&rd, &wr, &inheritable, 0))
        win::throw_last_error("cannot create the handover pipe");
    read_end.reset(rd);
    write_end.reset(wr);
}

// The new agent-host, with three handles it inherits and nothing else: the two ends of the handover
// pipes and SYNCHRONIZE on this process.
UniqueHandle spawn_successor(const std::filesystem::path& exe, const std::string& pipe_name,
                             const std::filesystem::path& data_dir, HANDLE child_in, HANDLE child_out,
                             HANDLE self_for_child) {
    const std::wstring takeover = std::to_wstring(handle_value(child_in)) + L"," +
                                  std::to_wstring(handle_value(child_out)) + L"," +
                                  std::to_wstring(handle_value(self_for_child));
    const std::vector<std::wstring> args = {L"--takeover",           takeover,      L"--pipe",
                                            win::to_wide(pipe_name), L"--data-dir", data_dir.native()};
    std::wstring cmdline = win::build_command_line(exe.native(), args);
    HANDLE inherit[3] = {child_in, child_out, self_for_child};
    win::AttributeList attrs(1);
    attrs.update(PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit));
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = attrs.get();
    // Like spawn_detached (detached.cpp): not in the program's folder, so updates can rename it.
    std::filesystem::path cwd = exe.parent_path();
    std::error_code ec;
    if (const auto home = platform::home_dir(); !home.empty() && std::filesystem::is_directory(home, ec))
        cwd = home;
    PROCESS_INFORMATION pi{};
    const auto start = [&](DWORD flags) {
        return ::CreateProcessW(
                   exe.c_str(), cmdline.data(), nullptr, nullptr, TRUE,
                   flags | EXTENDED_STARTUPINFO_PRESENT | DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT,
                   nullptr, cwd.c_str(), &si.StartupInfo, &pi) != FALSE;
    };
    if (!start(CREATE_BREAKAWAY_FROM_JOB) && !start(0))
        win::throw_last_error("CreateProcessW(new agent-host)");
    ::CloseHandle(pi.hThread);
    return UniqueHandle(pi.hProcess);
}

}  // namespace

// ---------------------------------------------------------------- the old host's side

struct NewHost::Impl {
    UniqueHandle process;
    UniqueHandle to_new;    // we write the state here
    UniqueHandle from_new;  // and read the answers here
};

NewHost::NewHost(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

NewHost::~NewHost() = default;

std::unique_ptr<NewHost> NewHost::start(const std::filesystem::path& exe, const std::string& pipe_name,
                                        const std::filesystem::path& data_dir) {
    auto impl = std::make_unique<Impl>();
    UniqueHandle child_in;
    UniqueHandle child_out;
    make_pipe(child_in, impl->to_new);
    make_pipe(impl->from_new, child_out);
    // Our own ends stay out of the new host.
    ::SetHandleInformation(impl->to_new.get(), HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(impl->from_new.get(), HANDLE_FLAG_INHERIT, 0);
    HANDLE self_raw = nullptr;  // the new host sees us exit
    if (!::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentProcess(), ::GetCurrentProcess(), &self_raw,
                           SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0))
        win::throw_last_error("cannot share this process's handle");
    const UniqueHandle self_for_child(self_raw);
    impl->process = spawn_successor(exe, pipe_name, data_dir, child_in.get(), child_out.get(), self_raw);
    // The new host has its copies; ours (closed here) would keep the pipes open after it ended.
    return std::unique_ptr<NewHost>(new NewHost(std::move(impl)));
}

bool NewHost::write_all(std::string_view bytes) {
    return pipe_write_all(impl_->to_new.get(), bytes);
}

std::optional<std::string> NewHost::read_line() {
    return pipe_read_line(impl_->from_new.get());
}

void NewHost::add_terminal(PtyHandles& h) {
    for (void** slot : handle_slots(h)) {
        HANDLE out = nullptr;
        if (!::DuplicateHandle(::GetCurrentProcess(), *slot, impl_->process.get(), &out, 0, FALSE,
                               DUPLICATE_SAME_ACCESS))
            win::throw_last_error("DuplicateHandle(into the new agent-host)");
        *slot = out;  // the new process's handle, valid there only
    }
}

void NewHost::write_handles(terminal::ByteWriter& w, const PtyHandles& h) {
    PtyHandles copy = h;
    for (void** slot : handle_slots(copy))
        w.u64(handle_value(*slot));
    w.u32(h.pid);
}

bool NewHost::send_terminals() {
    return true;  // add_terminal() already put them there
}

void NewHost::kill() {
    ::TerminateProcess(impl_->process.get(), 1);
}

void NewHost::end() {
    kill();
    ::WaitForSingleObject(impl_->process.get(), 5000);
}

void NewHost::close_channel() {
    impl_->to_new.reset();
}

// ---------------------------------------------------------------- the new host's side

struct OldHost::Impl {
    UniqueHandle from_old;
    UniqueHandle to_old;
    UniqueHandle process;
};

OldHost::OldHost(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

OldHost::~OldHost() = default;

std::unique_ptr<OldHost> OldHost::from_takeover(const std::string& argument) {
    std::size_t at = 0;
    const auto next = [&] {
        const auto comma = std::min(argument.find(',', at), argument.size());
        const std::string part = argument.substr(at, comma - at);
        at = std::min(comma + 1, argument.size());
        return handle_from(std::strtoull(part.c_str(), nullptr, 10));
    };
    auto impl = std::make_unique<Impl>();
    impl->from_old.reset(next());
    impl->to_old.reset(next());
    impl->process.reset(next());
    return std::unique_ptr<OldHost>(new OldHost(std::move(impl)));
}

bool OldHost::read_exact(char* out, std::size_t len) {
    return pipe_read_exact(impl_->from_old.get(), out, len);
}

bool OldHost::write_all(std::string_view bytes) {
    return pipe_write_all(impl_->to_old.get(), bytes);
}

std::optional<std::string> OldHost::read_line() {
    return pipe_read_line(impl_->from_old.get());
}

std::optional<PtyHandles> OldHost::read_handles(terminal::ByteReader& r) {
    PtyHandles h;
    for (void** slot : handle_slots(h))
        *slot = handle_from(r.u64());
    h.pid = r.u32();
    return h;
}

bool OldHost::wait_exit(std::chrono::milliseconds timeout) {
    const auto ms = static_cast<DWORD>(std::clamp<long long>(timeout.count(), 0, INFINITE - 1));
    return ::WaitForSingleObject(impl_->process.get(), ms) == WAIT_OBJECT_0;
}

}  // namespace agentdesk::host
