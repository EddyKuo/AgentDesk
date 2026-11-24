// system.hpp on Windows.
#include <windows.h>
// rpc.h and bcrypt.h need windows.h first.
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

#include <bcrypt.h>
#include <rpc.h>

#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/system.hpp"
#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::platform {

namespace {

// The value of a variable; nullopt when it is unset.
std::optional<std::wstring> variable(const wchar_t* name) {
    std::wstring buf(256, L'\0');
    for (;;) {
        ::SetLastError(ERROR_SUCCESS);
        const DWORD n = ::GetEnvironmentVariableW(name, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0 && ::GetLastError() == ERROR_ENVVAR_NOT_FOUND)
            return std::nullopt;
        if (n < buf.size()) {
            buf.resize(n);
            return buf;
        }
        buf.resize(n);  // too small: n is the size it needs
    }
}

// SHA-256 over pieces, through CNG.
class Sha256 {
public:
    Sha256() {
        if (::BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash_, nullptr, 0, nullptr, 0, 0) != 0)
            throw std::runtime_error("BCryptCreateHash failed");
    }
    ~Sha256() { ::BCryptDestroyHash(hash_); }
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    Sha256(Sha256&&) = delete;
    Sha256& operator=(Sha256&&) = delete;

    void update(std::string_view bytes) {
        while (!bytes.empty()) {  // a ULONG length: 1 GiB at most at a time
            const auto n = static_cast<ULONG>(std::min<std::size_t>(bytes.size(), 1u << 30));
            if (::BCryptHashData(hash_, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), n, 0) != 0)
                throw std::runtime_error("BCryptHashData failed");
            bytes.remove_prefix(n);
        }
    }
    std::string hex() {
        std::array<unsigned char, 32> digest{};
        if (::BCryptFinishHash(hash_, digest.data(), static_cast<ULONG>(digest.size()), 0) != 0)
            throw std::runtime_error("BCryptFinishHash failed");
        static constexpr char kHex[] = "0123456789abcdef";
        std::string out;
        for (const unsigned char b : digest) {
            out += kHex[b >> 4];
            out += kHex[b & 15];
        }
        return out;
    }

private:
    BCRYPT_HASH_HANDLE hash_ = nullptr;
};

// Installed WSL distributions are the subkeys of HKCU\...\Lxss (DistributionName value); wsl.exe
// alone exists even without WSL. Docker Desktop's internal distributions are not shells.
std::vector<ShellCandidate> wsl_shells() {
    std::vector<ShellCandidate> out;
    const auto exe = find_executable("wsl");
    if (!exe)
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
        const std::string u = to_utf8(distro);
        out.push_back({"wsl-" + u, u + " (WSL)", *exe, {"-d", u}});
    }
    ::RegCloseKey(root);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return out;
}

}  // namespace

std::optional<std::string> env_var(std::string_view name) {
    const auto v = variable(to_wide(name).c_str());
    if (!v)
        return std::nullopt;
    return to_utf8(*v);
}

void unset_env_var(std::string_view name) {
    ::SetEnvironmentVariableW(to_wide(name).c_str(), nullptr);
}

std::filesystem::path data_dir() {
    std::filesystem::path dir;
    if (const auto set = variable(L"AGENTDESK_DATA_DIR"); set && !set->empty()) {
        dir = *set;
    } else {
        const auto local = variable(L"LOCALAPPDATA");
        dir = (local && !local->empty() ? std::filesystem::path(*local) : std::filesystem::temp_directory_path()) /
              L"AgentDesk";
    }
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path home_dir() {
    return std::filesystem::path(variable(L"USERPROFILE").value_or(std::wstring()));
}

std::filesystem::path runtime_dir() {
    const auto dir = std::filesystem::temp_directory_path() / L"AgentDesk";
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path executable_path() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0)
            win::throw_last_error("GetModuleFileNameW");
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::filesystem::path program_file(std::string_view name) {
    return std::filesystem::path(to_wide(name) + L".exe");
}

void replace_file_atomically(const std::filesystem::path& temp, const std::filesystem::path& target,
                             const std::filesystem::path& backup) {
    std::error_code ec;
    if (!backup.empty() && std::filesystem::exists(target, ec)) {
        // ReplaceFileW keeps the old content as the backup and swaps atomically on NTFS.
        if (!::ReplaceFileW(target.c_str(), temp.c_str(), backup.c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS,
                            nullptr, nullptr))
            win::throw_last_error("ReplaceFileW");
        return;
    }
    if (!::MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        win::throw_last_error("MoveFileExW");
}

std::string computer_name() {
    std::array<wchar_t, MAX_COMPUTERNAME_LENGTH + 1> buf{};
    auto n = static_cast<DWORD>(buf.size());
    return ::GetComputerNameW(buf.data(), &n) ? to_utf8(std::wstring_view(buf.data(), n)) : std::string();
}

std::uint32_t current_pid() {
    return ::GetCurrentProcessId();
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

void random_bytes(std::span<std::byte> out) {
    if (::BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(out.data()), static_cast<ULONG>(out.size()),
                          BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("BCryptGenRandom failed");
}

std::string sha256_hex(std::string_view bytes) {
    Sha256 h;
    h.update(bytes);
    return h.hex();
}

std::string sha256_file_hex(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        throw std::system_error(std::make_error_code(std::errc::no_such_file_or_directory), "open a file to hash");
    Sha256 h;
    std::string buf(64 * 1024, '\0');
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        h.update(std::string_view(buf).substr(0, static_cast<std::size_t>(in.gcount())));
    }
    if (in.bad())
        throw std::system_error(std::make_error_code(std::errc::io_error), "read a file to hash");
    return h.hex();
}

std::vector<ShellCandidate> detect_shells() {
    std::vector<ShellCandidate> out;
    const auto add = [&](const char* id, const char* name, std::vector<std::string> args = {}) {
        if (const auto exe = find_executable(id))
            out.push_back({id, name, *exe, std::move(args)});
    };
    add("pwsh", "PowerShell 7", {"-NoLogo"});
    add("powershell", "Windows PowerShell", {"-NoLogo"});
    add("cmd", "Command Prompt");
    for (auto& w : wsl_shells())
        out.push_back(std::move(w));
    return out;
}

// Windows programs have one PATH, whoever started them.
std::string login_path() {
    return {};
}

std::optional<std::filesystem::path> find_on_login_path(std::string_view name) {
    return find_executable(name);
}

}  // namespace agentdesk::platform
