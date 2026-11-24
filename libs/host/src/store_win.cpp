// The Windows half of store.cpp: the data folder, atomic replace, profile detection, UUIDs.
#include <windows.h>
// rpc.h needs windows.h first.
#include <algorithm>
#include <fstream>
#include <stdexcept>

#include <rpc.h>

#include "agentdesk/host/store.hpp"
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
        if (::RegGetValueW(root, sub, L"DistributionName", RRF_RT_REG_SZ, nullptr, name, &bytes) !=
            ERROR_SUCCESS)
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
