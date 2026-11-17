#include "agentdesk/host/git_tools.hpp"

#include <windows.h>

#include <thread>

#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/unique_handle.hpp"

namespace agentdesk::host {

namespace win = agentdesk::platform::win;

ToolResult run_tool(const std::vector<std::wstring>& argv, const std::filesystem::path& cwd,
                    std::chrono::milliseconds timeout) {
    ToolResult r;
    if (argv.empty())
        return r;
    wchar_t found[MAX_PATH]{};
    if (::SearchPathW(nullptr, argv[0].c_str(), L".exe", MAX_PATH, found, nullptr) == 0)
        return r;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read_raw = nullptr;
    HANDLE write_raw = nullptr;
    if (!::CreatePipe(&read_raw, &write_raw, &sa, 0))
        return r;
    win::UniqueHandle out_read(read_raw);
    win::UniqueHandle out_write(write_raw);
    ::SetHandleInformation(out_read.get(), HANDLE_FLAG_INHERIT, 0);

    std::vector<std::wstring> args(argv.begin() + 1, argv.end());
    std::wstring cmdline = win::build_command_line(found, args);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_write.get();
    si.hStdError = out_write.get();
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(found, cmdline.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                          cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
        return r;
    }
    r.started = true;
    win::UniqueHandle process(pi.hProcess);
    ::CloseHandle(pi.hThread);
    out_write.reset();  // EOF once the child exits
    std::thread reader([&] {
        char buf[4096];
        DWORD n = 0;
        while (::ReadFile(out_read.get(), buf, sizeof(buf), &n, nullptr) && n > 0) {
            if (r.output.size() < 64 * 1024)
                r.output.append(buf, n);
        }
    });
    if (::WaitForSingleObject(process.get(), static_cast<DWORD>(timeout.count())) == WAIT_TIMEOUT) {
        r.timed_out = true;
        ::TerminateProcess(process.get(), 1);
        ::WaitForSingleObject(process.get(), 2000);
    }
    reader.join();
    DWORD code = 0;
    ::GetExitCodeProcess(process.get(), &code);
    r.exit_code = code;
    return r;
}

std::filesystem::path default_worktree_path(const std::filesystem::path& base,
                                            const std::filesystem::path& repo, const std::string& branch) {
    std::string safe = branch;
    for (char& c : safe) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|')
            c = '-';
    }
    auto name = repo.filename();
    if (name.empty())
        name = repo.parent_path().filename();
    return base / name / std::filesystem::path(std::u8string(safe.begin(), safe.end()));
}

}  // namespace agentdesk::host
