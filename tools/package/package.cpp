// agentdesk_package: builds the portable zip (design doc 5.1 M4), dist\AgentDesk-<version>-win-x64.zip:
//   the three executables, conpty\ (bundled ConPTY, ADR-08), data folders, example mods, the mod
//   SDK, LICENSE, THIRD_PARTY_NOTICES.md and docs\. Symbols go to a separate ...-symbols.zip.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
// Build Release first (scripts\build.cmd msvc-x64-release all).
//   agentdesk_package [--build-dir <dir>] [--out-dir <dir>] [--version 0.1.0]
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include "devkit.hpp"

namespace dk = agentdesk::devkit;
namespace fs = dk::fs;

namespace {

void copy_into(const fs::path& file, const fs::path& dir) {
    fs::copy_file(file, dir / file.filename(), fs::copy_options::overwrite_existing);
}

// Strips trailing whitespace, including the Unicode spaces (NBSP, NEL, ideographic space, line and
// paragraph separators) a UTF-8 license file may end with.
std::string trim_end(std::string s) {
    for (;;) {
        if (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n' ||
                           s.back() == '\v' || s.back() == '\f')) {
            s.pop_back();
        } else if (s.ends_with("\xC2\xA0") || s.ends_with("\xC2\x85")) {
            s.resize(s.size() - 2);
        } else if (s.ends_with("\xE3\x80\x80") || s.ends_with("\xE2\x80\xA8") ||
                   s.ends_with("\xE2\x80\xA9")) {
            s.resize(s.size() - 3);
        } else {
            return s;
        }
    }
}

std::string read_text(const fs::path& path) {
    auto text = dk::read_file(path);
    if (text.starts_with("\xEF\xBB\xBF"))
        text.erase(0, 3);
    return text;
}

// tar.exe (bsdtar) picks the zip format from the .zip suffix. Entries are relative to `base`.
void zip(const fs::path& out, const fs::path& base, const std::vector<std::wstring>& entries) {
    if (fs::exists(out))
        fs::remove(out);
    if (entries.empty()) {  // bsdtar refuses an empty archive; write the empty-zip record directly
        dk::write_file(out, std::string("PK\x05\x06", 4) + std::string(18, '\0'));
        return;
    }
    std::vector<std::wstring> args = {L"-a", L"-c", L"-f", out.native(), L"-C", base.native()};
    args.insert(args.end(), entries.begin(), entries.end());
    const auto r = dk::run(dk::system_dir() / L"tar.exe", args);
    if (r.exit_code != 0)
        throw std::runtime_error("tar failed for " + dk::u8(out));
}

// The release build's output folder must hold everything the zip ships.
void require_release_build(const fs::path& bin) {
    for (const wchar_t* f : {L"agent-ui.exe", L"agent-host.exe", L"agentctl.exe", L"conpty\\conpty.dll",
                             L"conpty\\x64\\OpenConsole.exe"}) {
        if (!fs::exists(bin / f)) {
            throw std::runtime_error("missing " + dk::u8(std::wstring(f)) + " in " + dk::u8(bin) +
                                     " (build Release and run fetch_conpty)");
        }
    }
}

// The executables and the data folders the build copies next to agent-ui.exe.
void stage_app(const fs::path& bin, const fs::path& stage) {
    for (const wchar_t* f : {L"agent-ui.exe", L"agent-host.exe", L"agentctl.exe"})
        copy_into(bin / f, stage);
    dk::copy_tree(bin / L"fonts", stage / L"fonts");
    dk::copy_tree(bin / L"sounds", stage / L"sounds");
    // AgentDesk 98 layout: its fonts, icons, images, sounds and layout file.
    dk::copy_tree(bin / L"win98", stage / L"win98");
    // Program table (programs.json) and the program icons.
    dk::copy_tree(bin / L"programs", stage / L"programs");
    dk::copy_tree(bin / L"themes", stage / L"themes");  // built-in colour themes
}

// The example mods as built: mods\<name>\ with only mod.dll and its .json files.
void stage_mods(const fs::path& bin, const fs::path& stage) {
    for (const auto& m : fs::directory_iterator(bin / L"mods")) {
        if (!m.is_directory())
            continue;
        const fs::path to = stage / L"mods" / m.path().filename();
        fs::create_directories(to);
        for (const auto& f : fs::directory_iterator(m.path())) {
            const auto ext = f.path().extension().native();
            if (f.is_regular_file() &&
                (_wcsicmp(ext.c_str(), L".dll") == 0 || _wcsicmp(ext.c_str(), L".json") == 0))
                copy_into(f.path(), to);
        }
    }
}

// sdk\: the mod headers, the examples' sources and a CMakeLists.txt that builds them, so a third
// party can write a mod without the AgentDesk source tree.
void stage_mod_sdk(const fs::path& root, const fs::path& vcpkg, const fs::path& stage) {
    const fs::path sdk_include = stage / L"sdk" / L"include" / L"agentdesk";
    fs::create_directories(sdk_include);
    fs::create_directories(stage / L"sdk" / L"examples");
    const fs::path modapi = root / L"libs" / L"modapi";
    copy_into(modapi / L"include" / L"agentdesk" / L"mod_api.h", sdk_include);
    copy_into(modapi / L"include" / L"agentdesk" / L"mod_sdk.hpp", sdk_include);
    for (const auto& m : fs::directory_iterator(root / L"mods")) {
        if (m.is_directory())
            dk::copy_tree(m.path(), stage / L"sdk" / L"examples" / m.path().filename());
    }
    copy_into(modapi / L"sdk" / L"CMakeLists.txt", stage / L"sdk");
    // mod_sdk.hpp uses nlohmann/json (MIT, listed in the notices): ship its headers with the SDK.
    dk::copy_tree(vcpkg / L"include" / L"nlohmann", stage / L"sdk" / L"include" / L"nlohmann");
}

// The bundled ConPTY with its license, the project license and the user documents.
void stage_conpty_and_docs(const fs::path& root, const fs::path& bin, const fs::path& stage) {
    copy_into(bin / L"conpty" / L"conpty.dll", stage / L"conpty");
    copy_into(bin / L"conpty" / L"x64" / L"OpenConsole.exe", stage / L"conpty" / L"x64");
    copy_into(root / L"third_party" / L"conpty" / L"LICENSE", stage / L"conpty");
    copy_into(root / L"third_party" / L"conpty" / L"NOTICE.md", stage / L"conpty");
    copy_into(root / L"LICENSE", stage);
    copy_into(root / L"docs" / L"user-guide.md", stage / L"docs");
    copy_into(root / L"docs" / L"integrations" / L"agent-hooks.md", stage / L"docs");
    copy_into(root / L"docs" / L"compatibility.md", stage / L"docs");
}

struct Component {
    const char* name;
    const char* url;
    fs::path license_file;
};

// Everything in the zip that is not AgentDesk's own, with the license file quoted for it. These
// are legal attributions: every entry must stay.
std::vector<Component> shipped_components(const fs::path& root, const fs::path& share) {
    return {
        {"libvterm 0.3.3", "https://www.leonerd.org.uk/code/libvterm/",
         root / L"third_party/libvterm/LICENSE"},
        {"Dear ImGui 1.93.0 (docking)", "https://github.com/ocornut/imgui", share / L"imgui/copyright"},
        {"nlohmann/json 3.12.0", "https://github.com/nlohmann/json", share / L"nlohmann-json/copyright"},
        {"Microsoft.Windows.Console.ConPTY 1.24.260710001", "https://github.com/microsoft/terminal",
         root / L"third_party/conpty/LICENSE"},
        {"herdr-app (behaviour and parts ported to C++, modified)", "https://github.com/herdrdev/herdr-app",
         root / L"third_party/herdr/LICENSE"},
        {"Geist font", "https://github.com/vercel/geist-font", root / L"third_party/fonts/OFL-Geist.txt"},
        {"JetBrains Mono font", "https://github.com/JetBrains/JetBrainsMono",
         root / L"third_party/fonts/OFL-JetBrainsMono.txt"},
        {"Symbols Nerd Font", "https://github.com/ryanoasis/nerd-fonts",
         root / L"third_party/fonts/LICENSE-SymbolsNerdFont.txt"},
        {"lunasvg 3.5.0 (SVG rendering)", "https://github.com/sammycage/lunasvg",
         share / L"lunasvg/copyright"},
        {"plutovg 1.3.3 (used by lunasvg)", "https://github.com/sammycage/plutovg",
         share / L"plutovg/copyright"},
        {"Pixelated MS Sans Serif fonts (AgentDesk 98, win98\\fonts)", "https://github.com/jdan/98.css",
         root / L"third_party/herdr/win98/fonts/LICENSE-MSSansSerif.txt"},
        {"Creative Commons Attribution-ShareAlike 3.0 (the Pixelated MS Sans Serif fonts)",
         "https://creativecommons.org/licenses/by-sa/3.0/",
         root / L"third_party/herdr/win98/fonts/CC-BY-SA-3.0.txt"},
        {"PxPlus IBM VGA 8x16 font (AgentDesk 98, win98\\fonts)", "https://int10h.org/oldschool-pc-fonts/",
         root / L"third_party/herdr/win98/fonts/LICENSE-PxPlus.txt"},
        {"LobeHub icons (agent icons, programs\\icons)", "https://github.com/lobehub/lobe-icons",
         root / L"third_party/herdr/programs/icons/LICENSE-LobeHub.txt"},
        {"Lucide icons (glyphs, programs\\icons)", "https://lucide.dev",
         root / L"third_party/herdr/programs/icons/LICENSE-Lucide.txt"},
        {"oh-my-pi (the omp icon, programs\\icons)", "https://github.com/can1357/oh-my-pi",
         root / L"third_party/herdr/programs/icons/LICENSE-oh-my-pi.txt"},
        {"Simple Icons (brand icons, programs\\icons; CC0 1.0, brands are their owners' trademarks)",
         "https://simpleicons.org", root / L"third_party/herdr/programs/icons/LICENSES.md"},
    };
}

// THIRD_PARTY_NOTICES.md, assembled from the components' own license files. Lines end in CRLF so
// the file is byte-identical to the one earlier releases shipped.
std::string third_party_notices(const std::vector<Component>& components) {
    std::string notices;
    auto line = [&notices](const std::string& text = {}) { notices += text + "\r\n"; };
    line("# Third-party notices");
    line();
    line(
        "AgentDesk (MIT, see LICENSE) includes the following components. GoogleTest is used for testing "
        "only and is not shipped.");
    for (const auto& c : components) {
        line();
        line(std::string("## ") + c.name);
        line();
        line(std::string("Source: ") + c.url);
        line();
        line("```");
        line(trim_end(read_text(fs::path(c.license_file).make_preferred())));
        line("```");
    }
    line();
    line("The ConPTY package also carries Microsoft's third-party notices: see conpty\\NOTICE.md.");
    return notices;
}

// <name>-symbols.zip with the PDBs of the three shipped executables.
void zip_symbols(const fs::path& bin, const fs::path& out_dir, const std::wstring& name) {
    const fs::path sym_stage = out_dir / (name + L"-symbols");
    dk::remove_tree(sym_stage);
    fs::create_directories(sym_stage);
    std::vector<std::wstring> pdbs;
    const std::wregex shipped(L"^(agent-ui|agent-host|agentctl)\\.pdb$", std::regex::icase);
    for (const auto& f : fs::directory_iterator(bin)) {
        const auto file = f.path().filename().native();
        if (f.is_regular_file() && std::regex_match(file, shipped)) {
            copy_into(f.path(), sym_stage);
            pdbs.push_back(file);
        }
    }
    zip(out_dir / (name + L"-symbols.zip"), sym_stage, pdbs);
    dk::remove_tree(sym_stage);
}

// Size in MiB with at most two decimals and no trailing zeros ("12.5", "3"). Halves round to even,
// as the earlier PowerShell script's [math]::Round did, so the printed size has not changed.
std::string megabytes_text(std::uintmax_t bytes) {
    const double mb = std::nearbyint(static_cast<double>(bytes) / (1024.0 * 1024.0) * 100.0) / 100.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", mb);
    std::string text = buf;
    while (text.ends_with('0'))
        text.pop_back();
    if (text.ends_with('.'))
        text.pop_back();
    return text;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const dk::Args args(argc, argv);
        const fs::path root = dk::source_root();
        const fs::path build_dir =
            args.path_or(L"build-dir", (root / L"build" / L"msvc-x64-release").native());
        const fs::path out_dir = args.path_or(L"out-dir", (root / L"dist").native());
        const std::wstring version = args.get_or(L"version", L"0.1.0");
        const fs::path bin = build_dir / L"apps" / L"desktop";
        const fs::path vcpkg = build_dir / L"vcpkg_installed" / L"x64-windows-static-md";
        require_release_build(bin);

        const std::wstring name = L"AgentDesk-" + version + L"-win-x64";
        const fs::path stage = out_dir / name;
        dk::remove_tree(stage);
        fs::create_directories(stage / L"conpty" / L"x64");
        fs::create_directories(stage / L"docs");
        stage_app(bin, stage);
        stage_mods(bin, stage);
        stage_mod_sdk(root, vcpkg, stage);
        stage_conpty_and_docs(root, bin, stage);
        dk::write_file(stage / L"THIRD_PARTY_NOTICES.md",
                       third_party_notices(shipped_components(root, vcpkg / L"share")));

        // The archive's top level is the stage folder itself, not its contents.
        const fs::path zip_path = out_dir / (name + L".zip");
        zip(zip_path, out_dir, {name});
        zip_symbols(bin, out_dir, name);

        std::printf("%s\nsha256 %s\nsize %s MB\n", dk::u8(zip_path).c_str(), dk::sha256_hex(zip_path).c_str(),
                    megabytes_text(fs::file_size(zip_path)).c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "agentdesk_package: %s\n", e.what());
        return 1;
    }
}
