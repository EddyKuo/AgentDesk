// The host's JSON files (profiles, sessions): atomic writes, backups, and profile detection.
#include "agentdesk/host/store.hpp"

#include <windows.h>
// rpc.h needs windows.h first.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <rpc.h>

#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::host {

namespace win = platform::win;

std::filesystem::path default_data_dir() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    std::filesystem::path dir =
        n > 0 ? std::filesystem::path(std::wstring(buf, n)) : std::filesystem::temp_directory_path();
    dir /= L"AgentDesk";
    std::filesystem::create_directories(dir);
    return dir;
}

void write_json_atomic(const std::filesystem::path& file, const nlohmann::json& doc) {
    const auto tmp = std::filesystem::path(file).concat(L".tmp");
    const auto bak = std::filesystem::path(file).concat(L".bak");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("cannot write " + win::to_utf8(tmp.native()));
        out << doc.dump(2) << '\n';
        out.flush();
        if (!out)
            throw std::runtime_error("write failed: " + win::to_utf8(tmp.native()));
    }
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        // ReplaceFileW keeps the old content as the backup and swaps atomically on NTFS.
        if (!::ReplaceFileW(file.c_str(), tmp.c_str(), bak.c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr,
                            nullptr)) {
            win::throw_last_error("ReplaceFileW");
        }
    } else if (!::MoveFileExW(tmp.c_str(), file.c_str(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        win::throw_last_error("MoveFileExW");
    }
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
    nlohmann::json args = nlohmann::json::array();
    for (const auto& a : p.args)
        args.push_back(win::to_utf8(a));
    return {
        {"id", p.id}, {"name", p.name}, {"executable", win::to_utf8(p.executable.native())}, {"args", args}};
}

// Throws (nlohmann::json::exception, std::system_error) on a damaged entry; callers skip it.
Profile profile_from_json(const nlohmann::json& j) {
    Profile p;
    p.id = j.at("id").get<std::string>();
    p.name = j.value("name", p.id);
    p.executable = win::to_wide(j.at("executable").get<std::string>());
    for (const auto& a : j.value("args", nlohmann::json::array()))
        p.args.push_back(win::to_wide(a.get<std::string>()));
    return p;
}

std::vector<Profile> detect_default_profiles() {
    std::vector<Profile> out;
    const auto add = [&](const char* id, const char* name, const wchar_t* exe,
                         std::vector<std::wstring> args = {}) {
        wchar_t buf[MAX_PATH]{};
        if (::SearchPathW(nullptr, exe, L".exe", MAX_PATH, buf, nullptr) > 0) {
            out.push_back({id, name, buf, std::move(args)});
        }
    };
    add("claude-native", "Claude Code", L"claude.exe");
    add("codex-native", "Codex CLI", L"codex.exe");
    add("pwsh", "PowerShell 7", L"pwsh.exe", {L"-NoLogo"});
    add("powershell", "Windows PowerShell", L"powershell.exe", {L"-NoLogo"});
    add("cmd", "Command Prompt", L"cmd.exe");
    for (auto& p : detect_wsl_profiles())
        out.push_back(std::move(p));
    return out;
}

// Installed distributions are the subkeys of HKCU\...\Lxss (DistributionName value); wsl.exe
// alone exists even without WSL. Docker Desktop's internal distributions are not shells.
std::vector<Profile> detect_wsl_profiles() {
    std::vector<Profile> out;
    wchar_t exe[MAX_PATH]{};
    if (::SearchPathW(nullptr, L"wsl.exe", nullptr, MAX_PATH, exe, nullptr) == 0)
        return out;
    HKEY root = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Lxss", 0, KEY_READ,
                        &root) != ERROR_SUCCESS)
        return out;
    for (DWORD i = 0;; ++i) {
        wchar_t sub[256]{};
        DWORD sub_len = 256;
        if (::RegEnumKeyExW(root, i, sub, &sub_len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        wchar_t name[256]{};
        DWORD bytes = sizeof(name) - sizeof(wchar_t);
        if (::RegGetValueW(root, sub, L"DistributionName", RRF_RT_REG_SZ, nullptr, name, &bytes) != ERROR_SUCCESS)
            continue;
        const std::wstring distro(name);
        if (distro.empty() || distro.starts_with(L"docker-desktop"))
            continue;
        const std::string u = win::to_utf8(distro);
        out.push_back({"wsl-" + u, u + " (WSL)", exe, {L"-d", distro}});
    }
    ::RegCloseKey(root);
    std::sort(out.begin(), out.end(), [](const Profile& a, const Profile& b) { return a.id < b.id; });
    return out;
}

std::optional<std::string> validate_profile(const Profile& p) {
    if (p.id.empty())
        return "profile id is empty";
    if (win::is_batch_file(p.executable.native())) {
        return "batch launchers (.cmd/.bat) need the shell launcher adapter, which is not available yet";
    }
    if (!win::is_launchable_file(p.executable.native())) {
        return "executable not found: " + win::to_utf8(p.executable.native());
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

std::string new_uuid() {
    UUID id{};
    if (::UuidCreate(&id) != RPC_S_OK)
        throw std::runtime_error("UuidCreate failed");
    RPC_CSTR text = nullptr;
    if (::UuidToStringA(&id, &text) != RPC_S_OK)
        throw std::runtime_error("UuidToStringA failed");
    std::string out(reinterpret_cast<const char*>(text));
    ::RpcStringFreeA(&text);
    return out;
}

}  // namespace agentdesk::host
