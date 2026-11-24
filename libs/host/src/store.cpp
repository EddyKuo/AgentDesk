// The host's JSON files (profiles, sessions): atomic writes, backups, and profile detection.
#include "agentdesk/host/store.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "agentdesk/platform/crt.hpp"
#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/system.hpp"
#include "agentdesk/platform/text.hpp"

namespace agentdesk::host {


void write_json_atomic(const std::filesystem::path& file, const nlohmann::json& doc) {
    const auto tmp = std::filesystem::path(file).concat(L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("cannot write " + platform::to_utf8(tmp.native()));
        out << doc.dump(2) << '\n';
        out.flush();
        if (!out)
            throw std::runtime_error("write failed: " + platform::to_utf8(tmp.native()));
    }
    platform::replace_file_atomically(tmp, file, std::filesystem::path(file).concat(L".bak"));
}

// Claude Code and Codex when installed, then the system's shells. Programs are looked up the way a
// login shell finds them.
std::vector<Profile> detect_default_profiles() {
    std::vector<Profile> out;
    const auto add_program = [&](const char* id, const char* name, const char* program) {
        if (const auto p = platform::find_on_login_path(program))
            out.push_back({id, name, *p, {}});
    };
    add_program("claude-native", "Claude Code", "claude");
    add_program("codex-native", "Codex CLI", "codex");
    for (const auto& s : platform::detect_shells())
        out.push_back({s.id, s.name, s.executable, s.args});
    return out;
}

std::vector<Profile> detect_wsl_profiles() {
    std::vector<Profile> out;
    for (const auto& s : platform::detect_shells())
        if (s.id.starts_with("wsl-"))
            out.push_back({s.id, s.name, s.executable, s.args});
    return out;
}

std::string new_uuid() {
    return platform::new_uuid();
}

std::optional<nlohmann::json> read_json_with_backup(const std::filesystem::path& file) {
    auto try_read = [](const std::filesystem::path& p) -> std::optional<nlohmann::json> {
        std::ifstream in(p, std::ios::binary);
        if (!in)
            return std::nullopt;
        std::stringstream ss;
        ss << in.rdbuf();
        auto j = nlohmann::json::parse(ss.str(), nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return std::nullopt;
        // Compared as JSON: a wrongly typed version is a mismatch, not an exception.
        if (const auto v = j.find("schema_version"); v == j.end() || *v != kSchemaVersion)
            return std::nullopt;
        return j;
    };
    std::error_code ec;
    if (!std::filesystem::exists(file, ec))
        return std::nullopt;
    if (auto j = try_read(file))
        return j;
    if (auto j = try_read(std::filesystem::path(file).concat(L".bak")))
        return j;  // corrupt main file: fall back to the previous version
    // Neither is usable: keep the damaged file for the user and start from defaults. The host
    // must never fail to start because a settings file is broken (design doc 6.1 fault cases).
    std::string stamp = iso8601_now();
    std::replace(stamp.begin(), stamp.end(), ':', '-');
    const auto keep =
        std::filesystem::path(file).concat(L".corrupt-" + std::wstring(stamp.begin(), stamp.end()));
    std::filesystem::rename(file, keep, ec);
    return std::nullopt;
}

nlohmann::json to_json(const Profile& p) {
    return {{"id", p.id},
            {"name", p.name},
            {"executable", platform::to_utf8(p.executable.native())},
            {"args", p.args}};
}

// Throws (nlohmann::json::exception, std::system_error) on a damaged entry; callers skip it.
Profile profile_from_json(const nlohmann::json& j) {
    Profile p;
    p.id = j.at("id").get<std::string>();
    p.name = j.value("name", p.id);
    p.executable = platform::to_wide(j.at("executable").get<std::string>());
    for (const auto& a : j.value("args", nlohmann::json::array()))
        p.args.push_back(a.get<std::string>());
    return p;
}

std::optional<std::string> validate_profile(const Profile& p) {
    if (p.id.empty())
        return "profile id is empty";
    if (platform::is_batch_file(p.executable)) {
        return "batch launchers (.cmd/.bat) need the shell launcher adapter, which is not available yet";
    }
    if (!platform::is_launchable_file(p.executable)) {
        return "executable not found: " + platform::to_utf8(p.executable.native());
    }
    return std::nullopt;
}

nlohmann::json to_json(const SessionRecord& r) {
    return {{"id", r.id},
            {"name", r.name},
            {"cwd", r.cwd},
            {"profile_id", r.profile_id},
            {"created_at", r.created_at},
            {"process_state", r.process_state},
            {"exit_code", r.exit_code ? nlohmann::json(*r.exit_code) : nlohmann::json(nullptr)},
            {"parent", r.parent},
            {"labels", r.labels}};
}

// Throws (nlohmann::json::exception) on a damaged entry; callers skip it.
SessionRecord session_record_from_json(const nlohmann::json& j) {
    SessionRecord r;
    r.id = j.at("id").get<std::string>();
    r.name = j.value("name", "");
    r.cwd = j.value("cwd", "");
    r.profile_id = j.value("profile_id", "");
    r.created_at = j.value("created_at", "");
    r.process_state = j.value("process_state", "Lost");
    if (j.contains("exit_code") && j["exit_code"].is_number_unsigned())
        r.exit_code = j["exit_code"].get<std::uint32_t>();
    r.parent = j.value("parent", "");
    if (const auto it = j.find("labels"); it != j.end() && it->is_object()) {
        for (const auto& [k, v] : it->items())
            if (v.is_string())
                r.labels[k] = v.get<std::string>();
    }
    return r;
}

std::vector<SessionRecord> mark_orphans_lost(std::vector<SessionRecord> records) {
    for (auto& r : records) {
        if (r.process_state == "Running" || r.process_state == "Starting" || r.process_state == "Stopping") {
            r.process_state = "Lost";
        }
    }
    return records;
}

std::string iso8601_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

}  // namespace agentdesk::host
