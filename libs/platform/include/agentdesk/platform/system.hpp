#pragma once

// Folders, identifiers, hashes and shells: the platform-neutral interface
// (docs/design/platform-abstraction.md 5.4).

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <span>
#include <string>
#include <vector>

namespace agentdesk::platform {

// AGENTDESK_DATA_DIR when set, else the per-user application data folder
// (macOS: ~/Library/Application Support/AgentDesk). Created when missing.
std::filesystem::path data_dir();
std::filesystem::path executable_path();  // the running executable, absolute
// The file of a program of ours: "agent-host" is agent-host.exe on Windows.
std::filesystem::path program_file(std::string_view name);
std::filesystem::path home_dir();
// The per-user folder for endpoints and locks (macOS: <per-user temp dir>/AgentDesk, mode 0700).
// Created when missing.
std::filesystem::path runtime_dir();

// Puts `temp` in place of `target` in one step; on failure `target` is unchanged.
void replace_file_atomically(const std::filesystem::path& temp, const std::filesystem::path& target);

std::string computer_name();  // UTF-8
std::uint32_t current_pid();
std::string new_uuid();       // lower case with dashes
void random_bytes(std::span<std::byte> out);
// Lower-case hex SHA-256 of a file's contents; throws std::system_error when unreadable.
std::string sha256_file_hex(const std::filesystem::path& file);

// Shells to offer as profiles, the person's login shell first.
struct ShellCandidate {
    std::string id;    // "zsh", "bash", ...
    std::string name;  // shown in menus
    std::filesystem::path executable;
    std::vector<std::string> args;
};
std::vector<ShellCandidate> detect_shells();

// The PATH a login shell sets up (asked once, then cached; empty when it could not be read).
// Apps started from the Finder get a minimal PATH, so programs the person installed (claude,
// node, git from Homebrew) are found only with this one.
std::string login_path();
// Folders on `login_path()` (or PATH when that is empty), for looking up programs.
std::optional<std::filesystem::path> find_on_login_path(std::string_view name);

}  // namespace agentdesk::platform
