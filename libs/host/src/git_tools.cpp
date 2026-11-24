// Runs git and other helpers without a console, and turns branch names into folder names.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "agentdesk/host/git_tools.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <thread>

#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::host {

namespace win = agentdesk::platform::win;

std::string ToolResult::message() const {
    const std::string& m = error.find_first_not_of(" \t\r\n") != std::string::npos ? error : output;
    const auto a = m.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    return m.substr(a, m.find_last_not_of(" \t\r\n") - a + 1);
}

std::string worktree_slug(const std::string& branch) {
    // The rule is /[^\p{L}\p{M}\p{N}._-]+/ -> "-", then trim dashes.
    // Non-ASCII code points count as letters (no Unicode tables here); NFC is not applied.
    std::string out;
    bool dash = false;
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < branch.size();) {
        const auto b0 = static_cast<unsigned char>(branch[i]);
        const std::size_t len = b0 < 0x80 ? 1 : (b0 & 0xE0) == 0xC0 ? 2 : (b0 & 0xF0) == 0xE0 ? 3 : 4;
        std::uint32_t cp = len == 1 ? b0 : len == 2 ? (b0 & 0x1Fu) : len == 3 ? (b0 & 0x0Fu) : (b0 & 0x07u);
        for (std::size_t m = 1; m < len && i + m < branch.size(); ++m)
            cp = (cp << 6) | (static_cast<unsigned char>(branch[i + m]) & 0x3Fu);
        hash = (hash ^ cp) * 16777619u;
        const bool keep = cp >= 0x80 ? (cp != 0x3000 && !(cp >= 0x2000 && cp <= 0x206F))
                                     : (std::isalnum(static_cast<int>(cp)) != 0 || cp == '.' || cp == '_' || cp == '-');
        if (keep) {
            out.append(branch, i, len);
            dash = false;
        } else if (!dash) {
            out += '-';
            dash = true;
        }
        i += len;
    }
    const auto a = out.find_first_not_of('-');
    if (a == std::string::npos) {
        char buf[24];
        std::snprintf(buf, sizeof buf, "branch-%08x", hash);
        return buf;
    }
    return out.substr(a, out.find_last_not_of('-') - a + 1);
}

std::filesystem::path default_worktree_path(const std::filesystem::path& base,
                                            const std::filesystem::path& repo, const std::string& branch) {
    const std::string safe = worktree_slug(branch);
    auto name = repo.filename();
    if (name.empty())
        name = repo.parent_path().filename();
    return base / name / std::filesystem::path(std::u8string(safe.begin(), safe.end()));
}

}  // namespace agentdesk::host
