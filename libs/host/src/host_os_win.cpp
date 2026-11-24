// host_os.hpp on Windows: system error text, the shells' folder hooks (PowerShell, cmd, WSL).
#include <windows.h>

#include <cwctype>
#include <memory>
#include <system_error>

#include "agentdesk/platform/win/error.hpp"
#include "host_os.hpp"

namespace agentdesk::host::detail {

namespace win = platform::win;

std::string os_error_text(const std::exception& e, std::string what) {
    if (const auto* se = dynamic_cast<const std::system_error*>(&e);
        se != nullptr && se->code().category() == std::system_category()) {
        const std::string ansi = se->code().message();
        std::string prefix = what;
        if (what.size() >= ansi.size() + 2 && what.ends_with(ansi))
            prefix = what.substr(0, what.size() - ansi.size() - 2);
        wchar_t* text = nullptr;
        const DWORD n = ::FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, static_cast<DWORD>(se->code().value()), 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
        const std::unique_ptr<wchar_t, decltype(&::LocalFree)> owned(text, &::LocalFree);
        std::string message;
        if (n > 0 && text != nullptr) {
            std::wstring_view w(text, n);
            while (!w.empty() &&
                   (w.back() == L'\r' || w.back() == L'\n' || w.back() == L' ' || w.back() == L'.'))
                w.remove_suffix(1);
            try {
                message = win::to_utf8(w);
            } catch (const std::exception&) {
            }
        }
        if (message.empty())
            message = "Windows error " + std::to_string(se->code().value());
        what = prefix.empty() ? message : prefix + ": " + message;
    }
    return what;
}

namespace {

// WSL: a Linux cd never reaches wsl.exe's Windows current folder. bash runs PROMPT_COMMAND
// before each prompt; set it (passed into Linux through WSLENV) to send OSC 7 with the distro
// as the host, which the session maps to \\wsl.localhost\<distro>\... or C:\... for /mnt/c.
// Only for an interactive shell: a profile that runs a command (-e, --exec, --) is left alone.
// A ~/.bashrc that assigns PROMPT_COMMAND, or zsh/fish, replaces it; then the folder is unknown.
void add_wsl_folder_hook(platform::win::PtySpawnSpec& spec) {
    for (const auto& a : spec.args)
        if (a == L"-e" || a == L"--exec" || a == L"--")
            return;
    // OSC 133 A too: the prompt is back, so the program that ran before it (an agent) has ended.
    spec.env_overrides[L"PROMPT_COMMAND"] =
        L"printf '\\033]133;A\\007\\033]7;file://%s%s\\007' \"$WSL_DISTRO_NAME\" \"$PWD\"";
    std::wstring wslenv;
    if (const auto it = spec.env_overrides.find(L"WSLENV"); it != spec.env_overrides.end()) {
        wslenv = it->second;
    } else {
        wchar_t buf[4096]{};
        const DWORD n = ::GetEnvironmentVariableW(L"WSLENV", buf, 4096);
        if (n > 0 && n < 4096)
            wslenv.assign(buf, n);
    }
    if (wslenv.find(L"PROMPT_COMMAND") == std::wstring::npos)
        wslenv += (wslenv.empty() ? L"" : L":") + std::wstring(L"PROMPT_COMMAND/u");
    spec.env_overrides[L"WSLENV"] = wslenv;
}

std::wstring to_lower(std::wstring s) {
    for (auto& c : s)
        c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

}  // namespace

// PowerShell's cd (Set-Location) does not change the process's Windows current folder, which is
// what the host reads to follow the shell. So the prompt is wrapped: after the person's own
// prompt runs, the location is copied into [Environment]::CurrentDirectory. Only for an interactive
// PowerShell the profile starts without its own -Command/-File; cmd needs nothing.
void add_folder_hook(platform::win::PtySpawnSpec& spec) {
    const std::wstring exe = to_lower(spec.executable.filename().native());
    if (exe == L"wsl.exe") {
        add_wsl_folder_hook(spec);
        return;
    }
    if (exe == L"cmd.exe") {
        // cmd's PROMPT can send escapes ($E): OSC 133 A before the person's own prompt (or the
        // default $P$G), so the host knows a program that ran before it (an agent) has ended.
        wchar_t buf[1024]{};
        const DWORD n = ::GetEnvironmentVariableW(L"PROMPT", buf, 1024);
        const std::wstring own = n > 0 && n < 1024 ? std::wstring(buf, n) : std::wstring(L"$P$G");
        if (own.find(L"]133;A") == std::wstring::npos)
            spec.env_overrides[L"PROMPT"] = L"$E]133;A$E\\" + own;
        return;
    }
    if (exe != L"pwsh.exe" && exe != L"powershell.exe")
        return;
    for (const auto& a : spec.args) {
        const std::wstring l = to_lower(a);
        if (l == L"-command" || l == L"-c" || l == L"-file" || l == L"-f" || l == L"-encodedcommand" ||
            l == L"-ec" || l == L"-noninteractive")
            return;
    }
    spec.args.push_back(L"-NoExit");
    spec.args.push_back(L"-Command");
    spec.args.push_back(
        L"$global:__agentdesk_prompt = $function:prompt; "
        L"function global:prompt { [Console]::Write([char]27 + ']133;A' + [char]7); "
        L"$r = & $global:__agentdesk_prompt; "
        L"try { $l = Get-Location; if ($l.Provider.Name -eq 'FileSystem') "
        L"{ [Environment]::CurrentDirectory = $l.ProviderPath } } catch {}; $r }");
}

std::uint32_t current_pid() {
    return ::GetCurrentProcessId();
}

std::filesystem::path home_dir() {
    wchar_t home[MAX_PATH]{};
    const DWORD n = ::GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
    return std::filesystem::path(std::wstring(home, n));
}

}  // namespace agentdesk::host::detail
