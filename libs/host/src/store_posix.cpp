// The macOS half of store.cpp (ADR-30): the data folder, atomic replace, profile detection, UUIDs.
#include <fstream>
#include <stdexcept>

#include "agentdesk/host/store.hpp"
#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/system.hpp"
#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::host {

namespace win = platform::win;

// macOS (ADR-30): ~/Library/Application Support/AgentDesk.
std::filesystem::path default_data_dir() {
    auto dir = platform::home_dir() / "Library" / "Application Support" / "AgentDesk";
    std::filesystem::create_directories(dir);
    return dir;
}

// The old file becomes the backup, then rename() swaps the new one in atomically.
void write_json_atomic(const std::filesystem::path& file, const nlohmann::json& doc) {
    const auto tmp = std::filesystem::path(file).concat(".tmp");
    const auto bak = std::filesystem::path(file).concat(".bak");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("cannot write " + tmp.string());
        out << doc.dump(2) << '\n';
        out.flush();
        if (!out)
            throw std::runtime_error("write failed: " + tmp.string());
    }
    std::error_code ec;
    if (std::filesystem::exists(file, ec))
        std::filesystem::copy_file(file, bak, std::filesystem::copy_options::overwrite_existing, ec);
    platform::replace_file_atomically(tmp, file);
}

// macOS (ADR-30): Claude Code and Codex when installed, then the shells (the login shell first,
// started as login shells). Programs are looked up on the login shell's PATH.
std::vector<Profile> detect_default_profiles() {
    std::vector<Profile> out;
    const auto add_program = [&](const char* id, const char* name, const char* exe) {
        if (const auto p = platform::find_on_login_path(exe))
            out.push_back({id, name, *p, {}});
    };
    add_program("claude-native", "Claude Code", "claude");
    add_program("codex-native", "Codex CLI", "codex");
    for (const auto& shell : platform::detect_shells()) {
        Profile p{shell.id, shell.name, shell.executable, {}};
        for (const auto& a : shell.args)
            p.args.push_back(win::to_wide(a));
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<Profile> detect_wsl_profiles() {
    return {};
}

std::string new_uuid() {
    return platform::new_uuid();
}

}  // namespace agentdesk::host
