// Child processes for the updater and telemetry: pipes, a handle list so concurrent children
// never inherit each other's pipe ends, and a detached start for relaunching the app.
#include "system_process.hpp"

#include <windows.h>

#include <cctype>
#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/unique_handle.hpp"

namespace agentdesk::ui::detail {

namespace win = agentdesk::platform::win;

namespace {

// One anonymous pipe; neither end is inheritable yet.
bool make_pipe(win::UniqueHandle& read_end, win::UniqueHandle& write_end) {
    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
    if (!::CreatePipe(&rd, &wr, nullptr, 0))
        return false;
    read_end.reset(rd);
    write_end.reset(wr);
    return true;
}

}  // namespace

std::string to_utf8(const std::wstring& w) {
    if (w.empty())
        return {};
    const int n =
        ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring to_wide(const std::string& s) {
    if (s.empty())
        return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

ProcessResult run_process(const std::filesystem::path& exe, const std::vector<std::wstring>& args,
                          const std::string& input, const std::function<bool()>& keep_going) {
    ProcessResult r;
    // Created not inheritable; only the child's three ends are made inheritable, and the child
    // inherits exactly those (handle list below). Children started at the same time from other
    // threads must not get each other's pipe ends: a write end held by another child keeps a
    // drain below from ever seeing EOF.
    win::UniqueHandle out_read;
    win::UniqueHandle out_write;
    win::UniqueHandle err_read;
    win::UniqueHandle err_write;
    win::UniqueHandle in_read;
    win::UniqueHandle in_write;
    if (!make_pipe(out_read, out_write) || !make_pipe(err_read, err_write) || !make_pipe(in_read, in_write))
        return r;
    HANDLE inherit[3] = {out_write.get(), err_write.get(), in_read.get()};
    for (HANDLE h : inherit)
        if (!::SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
            return r;

    std::wstring cmdline = win::build_command_line(exe.native(), args);
    std::optional<win::AttributeList> attrs;
    try {
        attrs.emplace(1);
        attrs->update(PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit));
    } catch (const std::exception&) {
        return r;
    }
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = out_write.get();
    si.StartupInfo.hStdError = err_write.get();
    si.StartupInfo.hStdInput = in_read.get();
    si.lpAttributeList = attrs->get();
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(exe.c_str(), cmdline.data(), nullptr, nullptr, TRUE,
                          CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo,
                          &pi))
        return r;
    r.started = true;
    win::UniqueHandle process(pi.hProcess);
    ::CloseHandle(pi.hThread);
    out_write.reset();
    err_write.reset();
    in_read.reset();
    auto drain = [](HANDLE pipe, std::string& into) {
        char buf[16384];
        DWORD n = 0;
        while (::ReadFile(pipe, buf, sizeof(buf), &n, nullptr) && n > 0)
            into.append(buf, n);
    };
    std::thread reader([&] { drain(out_read.get(), r.out); });
    std::thread err_reader([&] { drain(err_read.get(), r.err); });
    {
        std::size_t at = 0;
        while (at < input.size()) {
            DWORD n = 0;
            if (!::WriteFile(in_write.get(), input.data() + at, static_cast<DWORD>(input.size() - at), &n,
                             nullptr))
                break;
            at += n;
        }
        in_write.reset();
    }
    bool killed = false;
    for (;;) {
        if (::WaitForSingleObject(process.get(), keep_going ? 50 : INFINITE) == WAIT_OBJECT_0)
            break;
        if (keep_going && !keep_going()) {
            ::TerminateProcess(process.get(), 1);
            ::WaitForSingleObject(process.get(), 2000);
            killed = true;
            break;
        }
    }
    reader.join();
    err_reader.join();
    DWORD code = 1;
    if (!killed)
        ::GetExitCodeProcess(process.get(), &code);
    r.exit_code = killed ? 1 : code;
    return r;
}

bool spawn_detached(const std::filesystem::path& exe, const std::vector<std::wstring>& args,
                    const std::wstring& env_name, const std::wstring& env_value) {
    // The child inherits this process's environment: set the one variable around the start.
    std::wstring previous;
    bool had_previous = false;
    if (!env_name.empty()) {
        const DWORD n = ::GetEnvironmentVariableW(env_name.c_str(), nullptr, 0);
        if (n > 0) {
            previous.resize(n);
            previous.resize(::GetEnvironmentVariableW(env_name.c_str(), previous.data(), n));
            had_previous = true;
        }
        ::SetEnvironmentVariableW(env_name.c_str(), env_value.c_str());
    }
    std::wstring cmdline = win::build_command_line(exe.native(), args);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL ok = ::CreateProcessW(exe.c_str(), cmdline.data(), nullptr, nullptr, FALSE,
                                     CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi);
    if (!env_name.empty())
        ::SetEnvironmentVariableW(env_name.c_str(), had_previous ? previous.c_str() : nullptr);
    if (!ok)
        return false;
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return true;
}

std::filesystem::path system32_exe(std::wstring_view name) {
    wchar_t root[MAX_PATH]{};
    const DWORD n = ::GetEnvironmentVariableW(L"SystemRoot", root, MAX_PATH);
    const std::wstring file = std::wstring(name) + L".exe";
    if (n == 0 || n >= MAX_PATH)
        return std::filesystem::path(file);
    return std::filesystem::path(std::wstring(root, n)) / L"System32" / file;
}

std::optional<std::wstring> env_var(const wchar_t* name) {
    const DWORD n = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0)
        return std::nullopt;
    std::wstring v(n, L'\0');
    v.resize(::GetEnvironmentVariableW(name, v.data(), n));
    return v;
}

void unset_env_var(const wchar_t* name) {
    ::SetEnvironmentVariableW(name, nullptr);
}

std::optional<std::wstring> home_folder() {
    return env_var(L"USERPROFILE");
}

std::string trim(std::string_view s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos)
        return {};
    return std::string(s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1));
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string read_file(const std::filesystem::path& p, bool* ok) {
    std::ifstream in(p, std::ios::binary);
    if (ok != nullptr)
        *ok = static_cast<bool>(in);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::uint64_t unix_now() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}

}  // namespace agentdesk::ui::detail
