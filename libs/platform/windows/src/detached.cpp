// spawn_detached: agent-host outlives the program that started it.
#include "agentdesk/platform/win/detached.hpp"

#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/unique_handle.hpp"

namespace agentdesk::platform::win {

bool current_job_kills_on_close() {
    BOOL in_job = FALSE;
    if (!::IsProcessInJob(::GetCurrentProcess(), nullptr, &in_job) || !in_job)
        return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    if (!::QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &info, sizeof(info),
                                     nullptr)) {
        return false;
    }
    return (info.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) != 0;
}

DetachedResult spawn_detached(const std::filesystem::path& exe, const std::vector<std::wstring>& args,
                              std::wstring environment) {
    std::wstring cmdline = build_command_line(exe.native(), args);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    constexpr DWORD kBase = DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT;
    // Not in the program's own folder: Windows can't rename a folder that is a process's working
    // directory, so an update could never be put in place. The home folder, else the program's
    // folder.
    std::filesystem::path cwd = exe.parent_path();
    {
        wchar_t home[MAX_PATH]{};
        const DWORD n = ::GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        std::error_code ec;
        if (n > 0 && n < MAX_PATH && std::filesystem::is_directory(std::wstring(home, n), ec))
            cwd = std::wstring(home, n);
    }
    DetachedResult result;
    auto start = [&](DWORD flags) {
        return ::CreateProcessW(exe.c_str(), cmdline.data(), nullptr, nullptr, FALSE, flags,
                                environment.empty() ? nullptr : environment.data(), cwd.c_str(), &si, &pi) != FALSE;
    };
    if (start(kBase | CREATE_BREAKAWAY_FROM_JOB)) {
        result.broke_away = true;
    } else {
        if (::GetLastError() != ERROR_ACCESS_DENIED)
            throw_last_error("CreateProcessW(host, breakaway)");
        // The parent job does not allow breakaway (some launchers, IDE terminals, ssh).
        result.parent_job_kills_on_close = current_job_kills_on_close();
        if (!start(kBase))
            throw_last_error("CreateProcessW(host)");
    }
    UniqueHandle process(pi.hProcess);
    UniqueHandle thread(pi.hThread);
    result.pid = pi.dwProcessId;
    return result;
}

}  // namespace agentdesk::platform::win
