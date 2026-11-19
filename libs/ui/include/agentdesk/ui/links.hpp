#pragma once

// Links in terminal text (URLs and file paths), what a link points to, and handing it to the
// system. The link pattern is Ghostty's, matched by hand (std::regex has no look-behind).
// Opening only ever follows an explicit Ctrl+click by the person: terminal output never opens
// anything (design doc 3.9).
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace agentdesk::ui {

// Byte range [first, second) of the link in `text` covering byte `at`.
std::optional<std::pair<std::size_t, std::size_t>> find_link(const std::string& text, std::size_t at);

struct LinkTarget {
    bool file = false;   // a file or folder on this computer (else a URL)
    std::string target;  // UTF-8 path or URL
    friend bool operator==(const LinkTarget&, const LinkTarget&) = default;
};

// What a link opens. File paths count only when they exist here; relative
// ones need the terminal's folder `cwd` (empty: unknown). A trailing period and ":line[:col]"
// are tried off when the full path does not exist.
std::optional<LinkTarget> resolve_link(const std::string& link, const std::string& cwd);

// Hands the target to the system's default handler (ShellExecuteW) on a worker thread.
// Programs and scripts are shown in Explorer instead of being run.
void open_link(const LinkTarget& target);

}  // namespace agentdesk::ui
