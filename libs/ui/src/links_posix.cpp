// links_os.hpp on macOS (ADR-30), with open_link (/usr/bin/open).
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include <unistd.h>

#include "agentdesk/platform/process.hpp"
#include "agentdesk/ui/links.hpp"
#include "links_os.hpp"

namespace agentdesk::ui {

namespace fs = std::filesystem;

namespace links_os {

// A POSIX path on this computer; another host's file is not one we can open.
std::optional<std::string> local_file_path(std::string path, const std::string& host) {
    if (!host.empty())
        return std::nullopt;
    return path;
}

}  // namespace links_os

// macOS: /usr/bin/open hands the target to its default app. A program or script is revealed in
// the Finder (open -R), never run from a click on terminal text.
void open_link(const LinkTarget& target) {
    const std::string& t = target.target;
    if (t.empty() || t.find('\0') != std::string::npos)
        return;
    std::vector<std::string> args;
    if (target.file) {
        std::error_code ec;
        const fs::path p(t);
        std::string ext = p.extension().string();
        for (auto& ch : ext)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        static constexpr std::string_view kRun[] = {".app", ".command",  ".sh",   ".tool", ".pkg",
                                                    ".dmg", ".workflow", ".scpt", ".jar",  ".terminal",
                                                    ".py",  ".rb",       ".pl"};
        const bool runnable = std::find(std::begin(kRun), std::end(kRun), ext) != std::end(kRun) ||
                              (fs::is_regular_file(p, ec) && ::access(t.c_str(), X_OK) == 0);
        if (runnable)
            args.push_back("-R");
    }
    args.push_back("--");
    args.push_back(t);
    try {
        platform::spawn_detached("/usr/bin/open", args);
    } catch (const std::exception&) {
    }
}

}  // namespace agentdesk::ui
