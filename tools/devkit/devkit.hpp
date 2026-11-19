#pragma once

// Shared helpers for the developer and QA tools (not shipped): argument parsing, child processes,
// files, hashes and the JSON layout the evidence files have used since the PowerShell versions.

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentdesk/platform/win/unique_handle.hpp"

namespace agentdesk::devkit {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

// Source checkout the tool was built from; the defaults of every tool are relative to it.
fs::path source_root();
fs::path exe_dir();
fs::path temp_dir();        // %TEMP% with its trailing separator removed
fs::path system_dir();      // C:\Windows\System32
std::wstring pid_string();  // this process' id, for unique pipe names and temp dirs

// Accepts the PowerShell spelling and the usual one: "-WaitSeconds 6", "--wait-seconds 6",
// "--wait-seconds=6". Names compare case-insensitively without dashes or underscores. Every
// option takes a value; anything else is positional.
class Args {
public:
    Args(int argc, wchar_t** argv);
    [[nodiscard]] std::optional<std::wstring> get(std::wstring_view name) const;
    [[nodiscard]] std::wstring get_or(std::wstring_view name, std::wstring fallback) const;
    // A path option made absolute against the current directory (relative paths are passed on to
    // child processes that run elsewhere).
    [[nodiscard]] fs::path path_or(std::wstring_view name, const fs::path& fallback) const;
    [[nodiscard]] int get_int(std::wstring_view name, int fallback) const;
    [[nodiscard]] const std::vector<std::wstring>& positional() const noexcept { return positional_; }

private:
    std::vector<std::pair<std::wstring, std::wstring>> named_;
    std::vector<std::wstring> positional_;
};

struct RunResult {
    DWORD exit_code = 1;
    std::string out;  // stdout; stderr goes to this tool's console
};

// Runs a console program to completion and captures its stdout. Throws when it cannot start.
RunResult run(const fs::path& exe, const std::vector<std::wstring>& args, const fs::path& cwd = {});

// Starts a program without waiting (GUI programs: agent-ui). Throws when it cannot start.
struct Started {
    platform::win::UniqueHandle process;
    DWORD pid = 0;
};
Started start(const fs::path& exe, const std::vector<std::wstring>& args, const fs::path& cwd = {});
bool wait_exit(const Started& p, std::chrono::milliseconds timeout);
DWORD exit_code(const Started& p);
void kill(const Started& p);
bool process_alive(DWORD pid);
std::wstring process_image_path(DWORD pid);  // empty when the process is gone

// Pid of the agent-host that owns `pipe` (asked through hello), 0 when none answers.
DWORD host_pid(const std::wstring& pipe);

void sleep_ms(int ms);

std::string read_file(const fs::path& path);  // binary; throws when missing
void write_file(const fs::path& path, std::string_view bytes, bool bom = false);
Json read_json(const fs::path& path);  // tolerates a UTF-8 BOM
// Recursive copy that replaces existing files (Copy-Item -Recurse semantics for a new target).
void copy_tree(const fs::path& from, const fs::path& to);
void remove_tree(const fs::path& path) noexcept;  // best effort, like -ErrorAction SilentlyContinue

std::string sha256_hex(const fs::path& path);
std::string utc_now_iso();  // round-trip format with 7 fractional digits, like (Get-Date).ToString('o')

// Windows PowerShell 5.1 ConvertTo-Json layout for a flat object (4-space indent, two spaces
// after the colon), which the committed evidence files use. Lines end with "\n".
std::string ps_json(const Json& object);
// profiles.json for an isolated host.
std::string profiles_json(const Json& profiles);
// object[key], or null when the key is missing (a missing value shows as null in evidence files).
Json field(const Json& object, const char* key);
// docs\poc\evidence\<name> in the source checkout, where the QA tools write their results.
fs::path evidence_path(std::wstring_view name);

std::string u8(const fs::path& path);
std::wstring wide(std::string_view text);

}  // namespace agentdesk::devkit
