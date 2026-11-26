#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <stdexcept>

#include <CommonCrypto/CommonDigest.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/system.hpp"
#include "posix_util.hpp"

namespace agentdesk::platform {

namespace {

std::filesystem::path passwd_home() {
    std::array<char, 4096> buf{};
    passwd pw{};
    passwd* result = nullptr;
    if (::getpwuid_r(::geteuid(), &pw, buf.data(), buf.size(), &result) == 0 && result != nullptr &&
        result->pw_dir != nullptr)
        return result->pw_dir;
    return {};
}

std::string login_shell() {
    std::array<char, 4096> buf{};
    passwd pw{};
    passwd* result = nullptr;
    if (::getpwuid_r(::geteuid(), &pw, buf.data(), buf.size(), &result) == 0 && result != nullptr &&
        result->pw_shell != nullptr && *result->pw_shell != '\0')
        return result->pw_shell;
    const char* env = std::getenv("SHELL");
    return env != nullptr ? env : "";
}

}  // namespace

std::optional<std::string> env_var(std::string_view name) {
    const char* v = std::getenv(std::string(name).c_str());
    if (v == nullptr)
        return std::nullopt;
    return v;
}

void unset_env_var(std::string_view name) {
    ::unsetenv(std::string(name).c_str());
}

std::filesystem::path home_dir() {
    const char* env = std::getenv("HOME");
    if (env != nullptr && *env == '/')
        return env;
    return passwd_home();
}

std::filesystem::path data_dir() {
    std::filesystem::path dir;
    const char* env = std::getenv("AGENTDESK_DATA_DIR");
    if (env != nullptr && *env != '\0')
        dir = env;
    else
        dir = home_dir() / "Library" / "Application Support" / "AgentDesk";
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path runtime_dir() {
    std::filesystem::path base;
    std::array<char, 1024> buf{};
    const std::size_t n = ::confstr(_CS_DARWIN_USER_TEMP_DIR, buf.data(), buf.size());
    if (n > 0 && n <= buf.size())
        base = buf.data();
    else if (const char* env = std::getenv("TMPDIR"); env != nullptr && *env == '/')
        base = env;
    else
        base = "/tmp";
    const auto dir = base / "AgentDesk";
    if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST)
        posix::throw_errno("mkdir(runtime dir)");
    struct stat st{};
    if (::lstat(dir.c_str(), &st) != 0)
        posix::throw_errno("lstat(runtime dir)");
    // Refuse a folder someone else made or a link planted in our place.
    if (!S_ISDIR(st.st_mode) || st.st_uid != ::geteuid())
        throw std::system_error(EPERM, std::generic_category(),
                                "runtime dir not owned by this user: " + dir.string());
    if ((st.st_mode & 0077) != 0)
        ::chmod(dir.c_str(), 0700);
    return dir;
}

std::filesystem::path executable_path() {
    std::uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (::_NSGetExecutablePath(buf.data(), &size) != 0)
        throw std::runtime_error("_NSGetExecutablePath failed");
    buf.resize(std::char_traits<char>::length(buf.c_str()));
    std::error_code ec;
    auto p = std::filesystem::weakly_canonical(buf, ec);
    return ec ? std::filesystem::path(buf) : p;
}

std::filesystem::path program_file(std::string_view name) {
    return std::filesystem::path(name);
}

// The old file is copied to the backup, then rename() swaps the new one in atomically.
void replace_file_atomically(const std::filesystem::path& temp, const std::filesystem::path& target,
                             const std::filesystem::path& backup) {
    std::error_code ec;
    if (!backup.empty() && std::filesystem::exists(target, ec))
        std::filesystem::copy_file(target, backup, std::filesystem::copy_options::overwrite_existing, ec);
    if (::rename(temp.c_str(), target.c_str()) != 0)
        posix::throw_errno("rename");
}

std::string computer_name() {
    std::array<char, 256> buf{};
    if (::gethostname(buf.data(), buf.size() - 1) != 0)
        return {};
    std::string name = buf.data();
    if (name.ends_with(".local"))
        name.resize(name.size() - 6);
    return name;
}

std::uint32_t current_pid() {
    return static_cast<std::uint32_t>(::getpid());
}

std::string new_uuid() {
    uuid_t id;
    uuid_generate_random(id);
    std::array<char, 37> text{};
    uuid_unparse_lower(id, text.data());
    return text.data();
}

void random_bytes(std::span<std::byte> out) {
    ::arc4random_buf(out.data(), out.size());
}

namespace {

std::string digest_hex(CC_SHA256_CTX& ctx) {
    std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
    CC_SHA256_Final(digest.data(), &ctx);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (const unsigned char b : digest) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0xF]);
    }
    return out;
}

}  // namespace

std::string sha256_hex(std::string_view bytes) {
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    while (!bytes.empty()) {
        const auto n = static_cast<CC_LONG>(std::min<std::size_t>(bytes.size(), 1u << 30));
        CC_SHA256_Update(&ctx, bytes.data(), n);
        bytes.remove_prefix(n);
    }
    return digest_hex(ctx);
}

std::string sha256_file_hex(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        throw std::system_error(ENOENT, std::generic_category(), "open " + file.string());
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    std::array<char, 64 * 1024> buf{};
    while (in) {
        in.read(buf.data(), buf.size());
        const auto got = in.gcount();
        if (got > 0)
            CC_SHA256_Update(&ctx, buf.data(), static_cast<CC_LONG>(got));
    }
    if (in.bad())
        throw std::system_error(EIO, std::generic_category(), "read " + file.string());
    return digest_hex(ctx);
}

std::string login_path() {
    static const std::string cached = [] {
        const std::string shell = login_shell();
        if (shell.empty() || !is_launchable_file(shell))
            return std::string();
        ProcessSpec spec;
        spec.executable = shell;
        spec.args = {"-l", "-c", "printf '%s' \"$PATH\""};
        spec.cwd = home_dir();
        try {
            const auto r = run_capture(spec, std::chrono::seconds(5));
            if (r.timed_out || r.exit_code != 0)
                return std::string();
            // A profile may print things: the PATH is the last line.
            const auto nl = r.out.find_last_of('\n');
            return nl == std::string::npos ? r.out : r.out.substr(nl + 1);
        } catch (const std::exception&) {
            return std::string();
        }
    }();
    return cached;
}

std::optional<std::filesystem::path> find_on_login_path(std::string_view name) {
    std::string path = login_path();
    if (path.empty()) {
        const char* env = std::getenv("PATH");
        path = env != nullptr ? env : "";
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = std::min(path.find(':', start), path.size());
        const std::string dir = path.substr(start, end - start);
        if (!dir.empty()) {
            const auto candidate = std::filesystem::path(dir) / std::string(name);
            if (is_launchable_file(candidate))
                return candidate;
        }
        start = end + 1;
    }
    return std::nullopt;
}

std::vector<ShellCandidate> detect_shells() {
    std::vector<std::string> paths;
    const std::string login = login_shell();
    if (!login.empty())
        paths.push_back(login);
    std::ifstream shells("/etc/shells");
    std::string line;
    while (std::getline(shells, line)) {
        if (!line.empty() && line.front() == '/')
            paths.push_back(line);
    }
    std::vector<ShellCandidate> out;
    std::set<std::string> seen;
    for (const auto& p : paths) {
        const std::filesystem::path exe(p);
        const std::string id = exe.filename().string();
        if (id.empty() || seen.contains(id) || !is_launchable_file(exe))
            continue;
        // /etc/shells also lists sh, dash, csh, tcsh and ksh, which few people open on purpose:
        // offered only when one of them is the login shell.
        static const std::set<std::string> kOffered = {"zsh", "bash", "fish", "nu", "pwsh", "elvish", "xonsh"};
        if (p != login && !kOffered.contains(id))
            continue;
        seen.insert(id);
        // Login shells read the person's profile, as Terminal.app does.
        out.push_back({id, id, exe, {"-l"}});
    }
    return out;
}

}  // namespace agentdesk::platform
