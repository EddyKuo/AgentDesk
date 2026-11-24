// Reads a folder's git branch and repository top from .git files, cached per folder.
#include "agentdesk/ui/git_info.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "agentdesk/platform/text.hpp"

namespace agentdesk::ui {

namespace {

namespace fs = std::filesystem;

std::string read_first_line(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
        line.pop_back();
    return line;
}

struct Found {
    std::string branch;
    std::string top;
};

Found lookup(const std::string& dir_utf8) {
    std::error_code ec;
    fs::path dir(agentdesk::platform::to_wide(dir_utf8));
    for (int depth = 0; depth < 32 && !dir.empty(); ++depth) {
        const fs::path git = dir / L".git";
        fs::path head;
        if (fs::is_directory(git, ec)) {
            head = git / L"HEAD";
        } else if (fs::is_regular_file(git, ec)) {
            // Worktree or submodule: "gitdir: <path>".
            const std::string line = read_first_line(git);
            if (line.rfind("gitdir:", 0) == 0) {
                std::string target = line.substr(7);
                while (!target.empty() && target.front() == ' ')
                    target.erase(target.begin());
                fs::path gd(agentdesk::platform::to_wide(target));
                if (gd.is_relative())
                    gd = dir / gd;
                head = gd / L"HEAD";
            }
        }
        if (!head.empty()) {
            const std::string line = read_first_line(head);
            constexpr std::string_view kRef = "ref: refs/heads/";
            const auto u8 = dir.u8string();
            std::string top(u8.begin(), u8.end());
            if (line.rfind(kRef, 0) == 0)
                return {line.substr(kRef.size()), top};
            return {line.size() >= 7 ? line.substr(0, 7) : line, top};
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir)
            break;
        dir = parent;
    }
    return {};
}

}  // namespace

namespace {
const Found& cached(const std::string& dir_utf8) {
    struct Entry {
        Found found;
        std::chrono::steady_clock::time_point at;
    };
    static std::map<std::string, Entry> cache;
    const auto now = std::chrono::steady_clock::now();
    auto& e = cache[dir_utf8];
    if (e.at == std::chrono::steady_clock::time_point{} || now - e.at > std::chrono::seconds(4)) {
        try {
            e.found = lookup(dir_utf8);
        } catch (const std::exception&) {
            e.found = {};
        }
        e.at = now;
    }
    return e.found;
}
}  // namespace

std::string git_branch(const std::string& dir_utf8) {
    return cached(dir_utf8).branch;
}

std::string git_top(const std::string& dir_utf8) {
    return cached(dir_utf8).top;
}

}  // namespace agentdesk::ui
