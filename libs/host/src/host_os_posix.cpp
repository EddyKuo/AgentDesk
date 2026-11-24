// host_os.hpp on macOS (ADR-30): the terminal environment and the zsh and bash prompt hooks.
#include <cstdlib>
#include <fstream>
#include <string_view>

#include <unistd.h>

#include "agentdesk/platform/system.hpp"
#include "host_os.hpp"

namespace agentdesk::host::detail {


std::string os_error_text(const std::exception& /*e*/, std::string what) {
    return what;
}

namespace {

// macOS (ADR-30): every shell gets a terminal type and a UTF-8 locale (apps started from the
// Finder have neither), and the common shells report each prompt: OSC 133 A (the program before
// it, an agent, has ended) and OSC 7 (the folder). zsh reads startup files from $ZDOTDIR: ours
// restores the person's ZDOTDIR, sources their .zshenv and adds a precmd hook; their other files
// load as usual. bash gets PROMPT_COMMAND, which a profile that assigns it replaces.
std::filesystem::path zsh_hook_dir() {
    const auto dir = platform::runtime_dir() / "zsh";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto file = dir / ".zshenv";
    static constexpr const char* kZshenv =
        "# Written by AgentDesk: reports each prompt (OSC 133 A, OSC 7), then gets out of the way.\n"
        "if [[ -n \"${AGENTDESK_USER_ZDOTDIR+x}\" ]]; then ZDOTDIR=\"$AGENTDESK_USER_ZDOTDIR\"; "
        "else unset ZDOTDIR; fi\n"
        "unset AGENTDESK_USER_ZDOTDIR\n"
        "[[ -f \"${ZDOTDIR:-$HOME}/.zshenv\" ]] && source \"${ZDOTDIR:-$HOME}/.zshenv\"\n"
        "if [[ -o interactive ]]; then\n"
        "  __agentdesk_precmd() { printf '\\e]133;A\\a\\e]7;file://%s%s\\a' \"${HOST%%.*}\" \"${PWD// "
        "/%20}\"; }\n"
        "  autoload -Uz add-zsh-hook && add-zsh-hook precmd __agentdesk_precmd\n"
        "fi\n";
    std::ofstream(file, std::ios::trunc) << kZshenv;
    return dir;
}

}  // namespace

void add_folder_hook(platform::win::PtySpawnSpec& spec) {
    if (!spec.env_overrides.contains("TERM"))
        spec.env_overrides["TERM"] = "xterm-256color";
    spec.env_overrides["COLORTERM"] = "truecolor";
    spec.env_overrides["TERM_PROGRAM"] = "AgentDesk";
    // Programs the person installed (claude, node, git) are on the login shell's PATH, not on the
    // minimal one an app started from the Finder gets.
    if (const std::string path = platform::login_path();
        !path.empty() && !spec.env_overrides.contains("PATH"))
        spec.env_overrides["PATH"] = path;
    const char* lang = std::getenv("LANG");
    if (lang == nullptr || std::string_view(lang).find("UTF-8") == std::string_view::npos)
        spec.env_overrides["LANG"] = "en_US.UTF-8";
    const std::string exe = spec.executable.filename().string();
    if (exe == "zsh") {
        if (const char* own = std::getenv("ZDOTDIR"); own != nullptr)
            spec.env_overrides["AGENTDESK_USER_ZDOTDIR"] = own;
        try {
            spec.env_overrides["ZDOTDIR"] = zsh_hook_dir().string();
        } catch (const std::exception&) {
            // No hook: the folder and the end of a program are then unknown, nothing else breaks.
        }
    } else if (exe == "bash") {
        spec.env_overrides["PROMPT_COMMAND"] =
            "printf '\\033]133;A\\007\\033]7;file://%s%s\\007' \"${HOSTNAME%%.*}\" \"${PWD// /%20}\"";
    }
}

std::uint32_t current_pid() {
    return static_cast<std::uint32_t>(::getpid());
}

std::filesystem::path home_dir() {
    return platform::home_dir();
}

}  // namespace agentdesk::host::detail
