#include "agentdesk/host/git_tools.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <thread>

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

std::string ToolResult::message() const {
    const std::string& m = error.find_first_not_of(" \t\r\n") != std::string::npos ? error : output;
    const auto a = m.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    return m.substr(a, m.find_last_not_of(" \t\r\n") - a + 1);
}

std::string worktree_slug(const std::string& branch) {
    // Port of herdr defaults/mods/worktree/main.js slug(): /[^\p{L}\p{M}\p{N}._-]+/ -> "-".
    // Non-ASCII code points count as letters (no Unicode tables here); NFC is not applied.
    std::string out;
    bool dash = false;
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < branch.size();) {
        const auto b0 = static_cast<unsigned char>(branch[i]);
        const std::size_t len = b0 < 0x80 ? 1 : (b0 & 0xE0) == 0xC0 ? 2 : (b0 & 0xF0) == 0xE0 ? 3 : 4;
        std::uint32_t cp = len == 1 ? b0 : len == 2 ? (b0 & 0x1Fu) : len == 3 ? (b0 & 0x0Fu) : (b0 & 0x07u);
        for (std::size_t m = 1; m < len && i + m < branch.size(); ++m)
            cp = (cp << 6) | (static_cast<unsigned char>(branch[i + m]) & 0x3Fu);
        hash = (hash ^ cp) * 16777619u;
        const bool keep = cp >= 0x80 ? (cp != 0x3000 && !(cp >= 0x2000 && cp <= 0x206F))
                                     : (std::isalnum(static_cast<int>(cp)) != 0 || cp == '.' || cp == '_' || cp == '-');
        if (keep) {
            out.append(branch, i, len);
            dash = false;
        } else if (!dash) {
            out += '-';
            dash = true;
        }
        i += len;
    }
    const auto a = out.find_first_not_of('-');
    if (a == std::string::npos) {
        char buf[24];
        std::snprintf(buf, sizeof buf, "branch-%08x", hash);
        return buf;
    }
    return out.substr(a, out.find_last_not_of('-') - a + 1);
}

std::filesystem::path default_worktree_path(const std::filesystem::path& base,
                                            const std::filesystem::path& repo, const std::string& branch) {
    const std::string safe = worktree_slug(branch);
    auto name = repo.filename();
    if (name.empty())
        name = repo.parent_path().filename();
    return base / name / std::filesystem::path(std::u8string(safe.begin(), safe.end()));
}

}  // namespace agentdesk::host
