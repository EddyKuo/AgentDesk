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

void replace_file_atomically(const std::filesystem::path& temp, const std::filesystem::path& target) {
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

std::vector<ShellCandidate> detect_shells() {
    std::vector<std::string> paths;
    if (const std::string login = login_shell(); !login.empty())
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
        seen.insert(id);
        // Login shells read the person's profile, as Terminal.app does.
        out.push_back({id, id, exe, {"-l"}});
    }
    return out;
}

}  // namespace agentdesk::platform
