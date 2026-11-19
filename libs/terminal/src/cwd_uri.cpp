// Maps an OSC 7 file:// URI to a Windows path (drives, MSYS, WSL mounts and distributions).
#include "agentdesk/terminal/cwd_uri.hpp"

#include <cctype>
#include <optional>

namespace agentdesk::terminal {

namespace {

int hex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

std::optional<std::string> percent_decode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '%') {
            const int hi = i + 1 < s.size() ? hex(s[i + 1]) : -1;
            const int lo = i + 2 < s.size() ? hex(s[i + 2]) : -1;
            if (hi < 0 || lo < 0)
                return std::nullopt;
            c = static_cast<char>(hi * 16 + lo);
            i += 2;
        }
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
            return std::nullopt;
        out.push_back(c);
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// "/x" or "/x/..." -> drive letter x.
std::optional<char> drive_prefix(std::string_view p) {
    if (p.size() >= 2 && p[0] == '/' && is_letter(p[1]) && (p.size() == 2 || p[2] == '/'))
        return static_cast<char>(std::toupper(static_cast<unsigned char>(p[1])));
    return std::nullopt;
}

std::string backslashes(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        if (c == '/')
            c = '\\';
    return out;
}

std::string drive_path(char letter, std::string_view rest) {
    std::string out{letter, ':'};
    out += rest.empty() ? std::string("\\") : backslashes(rest);
    return out;
}

bool has_dotdot(std::string_view p) {
    std::size_t i = 0;
    while (i <= p.size()) {
        const std::size_t j = p.find('/', i);
        const std::string_view seg = p.substr(i, j == std::string_view::npos ? std::string_view::npos : j - i);
        if (seg == "..")
            return true;
        if (j == std::string_view::npos)
            break;
        i = j + 1;
    }
    return false;
}

bool valid_distro(std::string_view h) {
    if (h.empty() || h.size() > 64)
        return false;
    for (const char c : h)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.'))
            return false;
    return true;
}

}  // namespace

std::string windows_path_from_cwd_uri(std::string_view uri, std::string_view local_host) {
    constexpr std::string_view kScheme = "file://";
    if (uri.size() < kScheme.size() || !iequals(uri.substr(0, kScheme.size()), kScheme))
        return {};
    uri.remove_prefix(kScheme.size());
    const std::size_t slash = uri.find('/');
    if (slash == std::string_view::npos)
        return {};
    const std::string_view host = uri.substr(0, slash);
    const auto decoded = percent_decode(uri.substr(slash));
    if (!decoded || decoded->find('\\') != std::string::npos || has_dotdot(*decoded))
        return {};
    const std::string_view path = *decoded;
    const bool local = host.empty() || iequals(host, "localhost") || (!local_host.empty() && iequals(host, local_host));

    // /C:/x (Windows shells, PowerShell's OSC 7 recipes)
    if (path.size() >= 3 && path[0] == '/' && is_letter(path[1]) && path[2] == ':') {
        if (path.size() > 3 && path[3] != '/')
            return {};
        return drive_path(static_cast<char>(std::toupper(static_cast<unsigned char>(path[1]))), path.substr(3));
    }
    // /mnt/c/x: a Windows drive seen from WSL
    if (path.starts_with("/mnt/")) {
        if (const auto d = drive_prefix(path.substr(4)))
            return drive_path(*d, path.substr(6));
    }
    if (local) {
        if (const auto d = drive_prefix(path))  // MSYS: /c/x
            return drive_path(*d, path.substr(2));
        return {};
    }
    if (!valid_distro(host))
        return {};
    return "\\\\wsl.localhost\\" + std::string(host) + backslashes(path);
}

}  // namespace agentdesk::terminal
