// Paths as text, whichever system they belong to.
#include "agentdesk/ui/path_text.hpp"

#include <algorithm>
#include <filesystem>
#include <vector>

namespace agentdesk::ui::path_text {

namespace {

bool is_sep(char c) {
    return c == '/' || c == '\\';
}

bool has_drive(std::string_view p) {
    return p.size() >= 2 && p[1] == ':' && ((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z'));
}

bool is_unc(std::string_view p) {
    return p.size() > 2 && is_sep(p[0]) && p[0] == p[1] && !is_sep(p[2]);
}

// How much of a path is its root: "C:\" 3, "/" 1, "\\server\share" through the share; 0 when it
// is relative.
std::size_t root_length(std::string_view p) {
    if (has_drive(p))
        return p.size() > 2 && is_sep(p[2]) ? 3 : 2;
    if (is_unc(p)) {
        const auto server = p.find_first_of("/\\", 2);
        if (server == std::string_view::npos)
            return p.size();
        return std::min(p.find_first_of("/\\", server + 1), p.size());
    }
    return !p.empty() && p[0] == '/' ? 1 : 0;
}

}  // namespace

bool is_windows(std::string_view path) {
    return has_drive(path) || is_unc(path);
}

bool is_absolute(std::string_view path) {
    return is_unc(path) || (has_drive(path) && path.size() > 2 && is_sep(path[2])) ||
           (!path.empty() && path[0] == '/');
}

char local_separator() {
    return static_cast<char>(std::filesystem::path::preferred_separator);
}

char separator(std::string_view path) {
    if (is_windows(path))
        return '\\';
    return !path.empty() && path[0] == '/' ? '/' : local_separator();
}

std::string native(std::string_view path) {
    const char sep = separator(path);
    std::string out(path);
    if (sep == '\\')
        std::replace(out.begin(), out.end(), '/', '\\');
    const std::size_t keep = std::max<std::size_t>(root_length(out), 1);
    while (out.size() > keep && out.back() == sep)
        out.pop_back();
    return out;
}

std::string join(std::string_view root, std::string_view rel) {
    std::string out(root);
    if (rel.empty())
        return out;
    const char sep = separator(root);
    if (!out.empty() && out.back() != sep && out.back() != '/')
        out += sep;
    for (const char c : rel)
        out += sep == '\\' && c == '/' ? '\\' : c;
    return out;
}

std::string resolve(std::string_view base, std::string_view path) {
    const std::string full = native(is_absolute(path) ? std::string(path) : join(base, path));
    const char sep = separator(full);
    const std::size_t root = root_length(full);
    const std::string_view rest = std::string_view(full).substr(root);
    std::vector<std::string_view> parts;
    for (std::size_t i = 0; i < rest.size();) {
        const std::size_t end = std::min(rest.find(sep, i), rest.size());
        const std::string_view part = rest.substr(i, end - i);
        i = end + 1;
        if (part.empty() || part == ".")
            continue;
        if (part != "..")
            parts.push_back(part);
        else if (!parts.empty() && parts.back() != "..")
            parts.pop_back();
        else if (root == 0)  // the parent of a root is the root
            parts.push_back(part);
    }
    std::string out = full.substr(0, root);
    for (const std::string_view part : parts) {
        if (!out.empty() && out.back() != sep)
            out += sep;
        out += part;
    }
    return out;
}

std::string parent(std::string_view path) {
    const std::string p = native(path);
    const std::size_t root = root_length(p);
    const auto at = p.find_last_of(separator(p));
    return p.substr(0, at == std::string::npos ? root : std::max(root, at));
}

}  // namespace agentdesk::ui::path_text
