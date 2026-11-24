// system.hpp on Windows.
#include <windows.h>

#include "agentdesk/platform/system.hpp"
#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::platform {

std::filesystem::path executable_path() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0)
            win::throw_last_error("GetModuleFileNameW");
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::uint32_t current_pid() {
    return ::GetCurrentProcessId();
}

std::filesystem::path program_file(std::string_view name) {
    return std::filesystem::path(to_wide(name) + L".exe");
}

}  // namespace agentdesk::platform
