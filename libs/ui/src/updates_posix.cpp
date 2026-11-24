// updates_os.hpp on macOS (ADR-30), with target and find_root. The
// install is the AgentDesk.app bundle.
#include <algorithm>

#include <strings.h>
#include <unistd.h>

#include "agentdesk/ui/updates.hpp"
#include "system_process.hpp"
#include "updates_os.hpp"

namespace agentdesk::ui::updates {

namespace fs = std::filesystem;

std::string target() {
    return "darwin-aarch64";
}

// An install is the folder unpacked from the portable zip, recognised by the
// THIRD_PARTY_NOTICES.md it holds; a build folder has none.
// An install is an AgentDesk.app bundle holding THIRD_PARTY_NOTICES.md; a build folder has none.
std::optional<fs::path> find_root(const fs::path& exe) {
    std::error_code ec;
    const fs::path real = fs::canonical(exe, ec);
    if (ec)
        return std::nullopt;
    // macOS: <root>.app/Contents/MacOS/agent-ui, with the notices in Contents/Resources.
    const fs::path bundle = real.parent_path().parent_path().parent_path();
    if (bundle.extension() == ".app" &&
        fs::is_regular_file(bundle / "Contents" / "Resources" / "THIRD_PARTY_NOTICES.md", ec))
        return bundle;
    return std::nullopt;
}

namespace os {

// The zip holds AgentDesk-<v>-macos-arm64/AgentDesk.app.
std::wstring packed(const std::string& version) {
    return L"AgentDesk-" + detail::to_wide(version) + L"-macos-arm64/AgentDesk.app";
}

fs::path ui_exe_in(const fs::path& install) {
    return install / "Contents" / "MacOS" / "agent-ui";
}

fs::path host_exe_in(const fs::path& install) {
    return install / "Contents" / "MacOS" / "agent-host";
}

fs::path ctl_exe_beside(const fs::path& ui_dir) {
    return ui_dir / "agentctl";
}

// /bin/sh waits for this process to exit, then `open` starts the app bundle again. The script is
// fixed text; the pid and the bundle's path are its arguments, never parsed as code.
bool relaunch(const fs::path& install, const std::optional<std::string>& expected) {
    const std::wstring script =
        L"while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; exec /usr/bin/open \"$2\"";
    return detail::spawn_detached(
        "/bin/sh", {L"-c", script, L"agentdesk-relaunch", std::to_wstring(::getpid()), install.wstring()},
        expected ? L"AGENTDESK_UPDATE_EXPECTED" : L"", expected ? detail::to_wide(*expected) : L"");
}

bool same_component(const fs::path& a, const fs::path& b) {
    return ::strcasecmp(a.c_str(), b.c_str()) == 0;
}

}  // namespace os

}  // namespace agentdesk::ui::updates
