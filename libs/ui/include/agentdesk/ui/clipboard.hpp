#pragma once

// What the Windows clipboard holds, for the terminal's paste (herdr src/terminal.rs:788-815).

#include <optional>
#include <string>
#include <vector>

namespace agentdesk::ui {

struct ClipboardContent {
    std::vector<std::string> files;  // CF_HDROP (files copied in Explorer), UTF-8 paths
    std::optional<std::string> text;  // CF_UNICODETEXT, UTF-8
    bool image = false;              // a bitmap (CF_DIB / CF_BITMAP / PNG): a screenshot
};

ClipboardContent read_clipboard();

}  // namespace agentdesk::ui
