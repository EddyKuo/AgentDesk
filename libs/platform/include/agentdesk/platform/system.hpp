#pragma once

// Folders, identifiers, hashes and shells: the platform-neutral interface
// (docs/design/platform-abstraction.md 5.4).

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace agentdesk::platform {

// AGENTDESK_DATA_DIR when set, else the per-user application data folder
// (macOS: ~/Library/Application Support/AgentDesk). Created when missing.
std::filesystem::path data_dir();
std::filesystem::path executable_path();  // the running executable, absolute
std::filesystem::path home_dir();
// The per-user folder for endpoints and locks (macOS: <per-user temp dir>/AgentDesk, mode 0700).
// Created when missing.
std::filesystem::path runtime_dir();

// Puts `temp` in place of `target` in one step; on failure `target` is unchanged.
void replace_file_atomically(const std::filesystem::path& temp, const std::filesystem::path& target);

std::string computer_name();  // UTF-8
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

}  // namespace agentdesk::platform
