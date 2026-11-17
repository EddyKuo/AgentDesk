#pragma once

// Host-owned settings files (design doc 3.9): %LOCALAPPDATA%\AgentDesk\{profiles,sessions}.json,
// schema_version 1, written to a temp file and atomically replaced, keeping one backup.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace agentdesk::host {

inline constexpr int kSchemaVersion = 1;

std::filesystem::path default_data_dir();  // %LOCALAPPDATA%\AgentDesk (created on demand)

// Atomic write: <file>.tmp -> ReplaceFile/MoveFileEx; previous content kept as <file>.bak.
void write_json_atomic(const std::filesystem::path& file, const nlohmann::json& doc);
// nullopt when missing. Corrupt content falls back to <file>.bak; when that is unusable too the
// damaged file is renamed to <file>.corrupt-<UTC time> and nullopt is returned (start from defaults).
std::optional<nlohmann::json> read_json_with_backup(const std::filesystem::path& file);

struct Profile {
    std::string id;
    std::string name;
    std::filesystem::path executable;
    std::vector<std::wstring> args;
};

nlohmann::json to_json(const Profile& p);
Profile profile_from_json(const nlohmann::json& j);

// Profiles found on PATH (claude, codex, pwsh, powershell, cmd), used when profiles.json is missing.
std::vector<Profile> detect_default_profiles();

// Validates a profile before spawning (design doc 1.4): executable exists, not a batch file.
std::optional<std::string> validate_profile(const Profile& p);

struct SessionRecord {
    std::string id;
    std::string name;
    std::string cwd;  // UTF-8
    std::string profile_id;
    std::string created_at;  // ISO 8601 UTC
    std::string process_state;
    std::optional<std::uint32_t> exit_code;
};

nlohmann::json to_json(const SessionRecord& r);
SessionRecord session_record_from_json(const nlohmann::json& j);

// On host start: records left Running/Starting/Stopping by a previous host become Lost
// (never adopt by PID; design doc 2.2 step 6).
std::vector<SessionRecord> mark_orphans_lost(std::vector<SessionRecord> records);

std::string iso8601_now();
std::string new_uuid();

}  // namespace agentdesk::host
