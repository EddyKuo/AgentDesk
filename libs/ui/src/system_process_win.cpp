// system_process.hpp on Windows: lenient text conversion, the environment, the system's own tools.
#include "system_process.hpp"

#include <windows.h>

namespace agentdesk::ui::detail {

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

// What these tools start (curl, PowerShell relaunching the app) may outlive us: no job.
void prepare_tool(platform::ProcessSpec& spec) {
    spec.end_with_parent = false;
}

// A console without a window, not DETACHED_PROCESS: what PowerShell starts would open windows.
bool start_in_background(const std::filesystem::path& exe, const std::vector<std::string>& args,
                         const std::map<std::string, std::string>& env) {
    platform::ProcessSpec spec;
    spec.executable = exe;
    spec.args = args;
    spec.env_overrides = env;
    spec.pipe_stdout = false;
    spec.end_with_parent = false;
    try {
        (void)platform::Process::spawn(spec);
    } catch (const std::exception&) {
        return false;
    }
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


const char* os_name() {
    return "windows";
}

const char* cpu_arch() {
    return "x86_64";
}

}  // namespace agentdesk::ui::detail
