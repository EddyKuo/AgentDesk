// updates_os.hpp on Windows, with sha256 (CNG), target and find_root.
#include <windows.h>

#include <algorithm>

#include <bcrypt.h>

#include "agentdesk/ui/updates.hpp"
#include "system_process.hpp"
#include "updates_os.hpp"

namespace agentdesk::ui::updates {

namespace fs = std::filesystem;

std::string sha256(std::string_view bytes) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    std::string out;
    if (::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return out;
    unsigned char digest[32]{};
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (::BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        // BCryptHashData takes a ULONG length: feed large inputs in 1 GiB pieces.
        std::size_t at = 0;
        do {
            const ULONG n = static_cast<ULONG>(std::min<std::size_t>(bytes.size() - at, 1u << 30));
            ::BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data() + at)), n, 0);
            at += n;
        } while (at < bytes.size());
        ::BCryptFinishHash(hash, digest, sizeof digest, 0);
        ::BCryptDestroyHash(hash);
        static constexpr char kHex[] = "0123456789abcdef";
        for (unsigned char b : digest) {
            out += kHex[b >> 4];
            out += kHex[b & 15];
        }
    }
    ::BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

std::string target() {
    return "windows-x86_64";
}

// An install is the folder unpacked from the portable zip, recognised by the
// THIRD_PARTY_NOTICES.md it holds; a build folder has none.
// An install is the folder unpacked from the portable zip, recognised by the
// THIRD_PARTY_NOTICES.md it holds; a build folder has none.
std::optional<fs::path> find_root(const fs::path& exe) {
    std::error_code ec;
    const fs::path real = fs::canonical(exe, ec);
    if (ec)
        return std::nullopt;
    const fs::path dir = real.parent_path();
    if (fs::is_regular_file(dir / L"THIRD_PARTY_NOTICES.md", ec))
        return dir;
    return std::nullopt;
}

namespace os {

std::wstring packed(const std::string& version) {
    return L"AgentDesk-" + detail::to_wide(version) + L"-win-x64";
}

fs::path ui_exe_in(const fs::path& install) {
    return install / L"agent-ui.exe";
}

fs::path host_exe_in(const fs::path& install) {
    return install / L"agent-host.exe";
}

fs::path ctl_exe_beside(const fs::path& ui_dir) {
    return ui_dir / L"agentctl.exe";
}

// Windows PowerShell waits for this process to exit and then starts the app again.
bool relaunch(const fs::path& install, const std::optional<std::string>& expected) {
    std::wstring quoted;  // a single-quoted PowerShell string: ' doubles
    for (wchar_t c : ui_exe_in(install).native()) {
        quoted += c;
        if (c == L'\'')
            quoted += L'\'';
    }
    const std::wstring script = L"Wait-Process -Id " + std::to_wstring(::GetCurrentProcessId()) +
                                L" -ErrorAction SilentlyContinue; Start-Process '" + quoted + L"'";
    // Windows' own copy, not whatever `powershell` PATH finds.
    const fs::path powershell = [] {
        wchar_t sys[MAX_PATH]{};
        const UINT n = ::GetSystemDirectoryW(sys, MAX_PATH);
        return fs::path(std::wstring(sys, n)) / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
    }();
    return detail::spawn_detached(powershell, {L"-NoProfile", L"-NonInteractive", L"-Command", script},
                                  expected ? L"AGENTDESK_UPDATE_EXPECTED" : L"",
                                  expected ? detail::to_wide(*expected) : L"");
}

bool same_component(const fs::path& a, const fs::path& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

}  // namespace os

}  // namespace agentdesk::ui::updates
