#pragma once

// Runs Windows' own helpers (curl.exe, tar.exe from System32; herdr update.rs 353-360) without a
// console, for updates.cpp and telemetry.cpp.

#include <filesystem>
#include <functional>
#include <string>
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

std::string to_utf8(const std::wstring& w);
std::wstring to_wide(const std::string& s);

}  // namespace agentdesk::ui::detail
