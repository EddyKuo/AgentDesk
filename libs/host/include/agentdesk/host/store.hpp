#pragma once

// Host-owned settings files (design doc 3.9): profiles.json and sessions.json in the data folder,
// schema_version 1, written to a temp file and atomically replaced, keeping one backup.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace agentdesk::host {

inline constexpr int kSchemaVersion = 1;

// Atomic write: <file>.tmp replaces it in one step; previous content kept as <file>.bak.
void write_json_atomic(const std::filesystem::path& file, const nlohmann::json& doc);
// nullopt when missing. Corrupt content falls back to <file>.bak; when that is unusable too the
// damaged file is renamed to <file>.corrupt-<UTC time> and nullopt is returned (start from defaults).
std::optional<nlohmann::json> read_json_with_backup(const std::filesystem::path& file);

struct Profile {
    std::string id;
    std::string name;
    std::filesystem::path executable;
    std::vector<std::string> args;  // UTF-8
};

nlohmann::json to_json(const Profile& p);
Profile profile_from_json(const nlohmann::json& j);

// Profiles found on PATH (claude, codex, pwsh, powershell, cmd) and WSL distributions, used when profiles.json is missing.
std::vector<Profile> detect_default_profiles();

// What profiles.json keeps about detection: `detected` holds every id AgentDesk added on its own,
// so a profile the person removed is not added back. A file written before that list existed has
// none; `wsl_detected` then says whether its WSL profiles were added (and may have been removed).
struct ProfileFile {
    std::vector<Profile> profiles;
    std::optional<std::set<std::string>> detected;
    bool wsl_detected = false;
};
// Brings the file up to date with what is installed now (`found`, detect_default_profiles()):
// programs found for the first time are added at the end. Once, for a file without `detected`:
// what it lists counts as detected, and the shells an older AgentDesk added that today's detection
// no longer offers (sh, dash, csh, tcsh, ksh on macOS, still as they were added: /…/<id> -l) are
// removed. Returns whether the file changed.
bool sync_detected_profiles(ProfileFile& file, const std::vector<Profile>& found);
// The profiles to offer: those whose program is still there (`exists`, by default a launchable
// file). The others stay in profiles.json and come back when the program does.
std::vector<Profile> available_profiles(const std::vector<Profile>& profiles,
                                        const std::function<bool(const Profile&)>& exists = {});

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
    std::string parent;  // session that started this one (agentctl run inside a session)
    std::map<std::string, std::string> labels;
};

nlohmann::json to_json(const SessionRecord& r);
SessionRecord session_record_from_json(const nlohmann::json& j);

// On host start: records left Running/Starting/Stopping by a previous host become Lost
// (never adopt by PID; design doc 2.2 step 6).
std::vector<SessionRecord> mark_orphans_lost(std::vector<SessionRecord> records);

std::string iso8601_now();
std::string new_uuid();

}  // namespace agentdesk::host
