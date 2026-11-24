#pragma once

// Process, environment and file helpers shared by updates.cpp, telemetry.cpp and links.cpp: runs
// the system's own tools (curl, tar) without a console and without a shell. system_process.cpp is
// the Windows implementation, system_process_posix.cpp the macOS one (ADR-30).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace agentdesk::ui::detail {

struct ProcessResult {
    bool started = false;
    unsigned long exit_code = 0;
    std::string out;  // stdout
    std::string err;  // stderr
    [[nodiscard]] bool success() const { return started && exit_code == 0; }
};

// Starts `exe` with `args`, writes `input` to its stdin (then closes it), and collects both
// streams. While it runs, `keep_going` is asked every 50 ms; false kills it (started stays true,
// exit_code is 1). Never goes through a shell.
ProcessResult run_process(const std::filesystem::path& exe, const std::vector<std::wstring>& args,
                          const std::string& input = {}, const std::function<bool()>& keep_going = {});

// Starts `exe` detached (no console, no handles kept); `env_name`/`env_value` set in its
// environment when given. False when it could not start.
bool spawn_detached(const std::filesystem::path& exe, const std::vector<std::wstring>& args,
                    const std::wstring& env_name = {}, const std::wstring& env_value = {});

// <SystemRoot>\System32\<name>.exe, or a bare "<name>.exe" when SystemRoot is unset or too long.
std::filesystem::path system32_exe(std::wstring_view name);

std::string to_utf8(const std::wstring& w);
std::wstring to_wide(const std::string& s);

// An environment variable's value; nullopt when it is unset.
std::optional<std::wstring> env_var(const wchar_t* name);
void unset_env_var(const wchar_t* name);
// The person's home folder (USERPROFILE, HOME); nullopt when unknown.
std::optional<std::wstring> home_folder();
// `s` without leading and trailing spaces, tabs and line ends.
std::string trim(std::string_view s);
// ASCII lowercase.
std::string lower(std::string_view s);
// The whole file as bytes; `ok` (when given) says whether it could be opened.
std::string read_file(const std::filesystem::path& p, bool* ok = nullptr);
// Seconds since 1970-01-01 UTC.
std::uint64_t unix_now();

}  // namespace agentdesk::ui::detail
