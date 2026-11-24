// system_process.hpp on macOS (ADR-30). The system's own tools live in /usr/bin (curl, tar).
#include "system_process.hpp"

#include <cstdlib>

#include "agentdesk/platform/text.hpp"

namespace agentdesk::ui::detail {

std::string to_utf8(const std::wstring& w) {
    return platform::to_utf8(w);
}

std::wstring to_wide(const std::string& s) {
    return platform::to_wide(s);
}

void prepare_tool(platform::ProcessSpec& /*spec*/) {}

bool start_in_background(const std::filesystem::path& exe, const std::vector<std::string>& args,
                         const std::map<std::string, std::string>& env) {
    try {
        platform::spawn_detached(exe, args, env);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

// The system's own copy of a tool: /usr/bin/<name> (curl, tar ship with macOS).
std::filesystem::path system32_exe(std::wstring_view name) {
    return std::filesystem::path("/usr/bin") / platform::to_utf8(name);
}

std::optional<std::wstring> env_var(const wchar_t* name) {
    const char* v = std::getenv(platform::to_utf8(name).c_str());
    if (v == nullptr)
        return std::nullopt;
    return platform::to_wide(v);
}

void unset_env_var(const wchar_t* name) {
    ::unsetenv(platform::to_utf8(name).c_str());
}

const char* os_name() {
    return "macos";
}

const char* cpu_arch() {
    return "aarch64";
}

std::optional<std::wstring> home_folder() {
    return env_var(L"HOME");
}

}  // namespace agentdesk::ui::detail
