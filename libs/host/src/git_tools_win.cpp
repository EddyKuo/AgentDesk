// The Windows half of git_tools.cpp: WSL paths, and running a tool without a console window.
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <string>
#include <thread>
#include <vector>

#include "agentdesk/host/git_tools.hpp"
#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/unique_handle.hpp"

namespace agentdesk::host {

namespace win = agentdesk::platform::win;

bool is_wsl_path(const std::wstring& path) {
    std::wstring p = path.substr(0, 16);
    for (auto& c : p)
        c = static_cast<wchar_t>(std::towlower(c == L'/' ? L'\\' : c));
    return p.starts_with(L"\\\\wsl.localhost\\") || p.starts_with(L"\\\\wsl$\\");
}

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
    if (!::CreatePipe(&read_raw, &write_raw, &sa, 0))
        return r;
    win::UniqueHandle err_read(read_raw);
    win::UniqueHandle err_write(write_raw);
    ::SetHandleInformation(err_read.get(), HANDLE_FLAG_INHERIT, 0);

    std::vector<std::wstring> args(argv.begin() + 1, argv.end());
    // Files inside WSL belong to the Linux user, so Git for Windows refuses the repository as
    // "dubious ownership". They are the person's own distribution: trust it for this one call.
    if (is_wsl_path(cwd.native())) {
        std::wstring tool = std::filesystem::path(argv[0]).stem().native();
        for (auto& c : tool)
            c = static_cast<wchar_t>(std::towlower(c));
        if (tool == L"git")
            args.insert(args.begin(), {L"-c", L"safe.directory=*"});
    }
    std::wstring cmdline = win::build_command_line(found, args);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_write.get();
    si.hStdError = err_write.get();
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
    err_write.reset();
    // Both pipes drain at once: a child blocked writing one would never finish the other.
    auto drain = [](HANDLE pipe, std::string& into) {
        char buf[16384];
        DWORD n = 0;
        while (::ReadFile(pipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
            if (into.size() < kMaxToolOutput)
                into.append(buf, std::min<std::size_t>(n, kMaxToolOutput - into.size()));
        }
    };
    std::thread reader([&] { drain(out_read.get(), r.output); });
    std::thread err_reader([&] { drain(err_read.get(), r.error); });
    if (::WaitForSingleObject(process.get(), static_cast<DWORD>(timeout.count())) == WAIT_TIMEOUT) {
        r.timed_out = true;
        ::TerminateProcess(process.get(), 1);
        ::WaitForSingleObject(process.get(), 2000);
    }
    reader.join();
    err_reader.join();
    DWORD code = 0;
    ::GetExitCodeProcess(process.get(), &code);
    r.exit_code = code;
    return r;
}

}  // namespace agentdesk::host
