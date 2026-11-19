// agentdesk_package: builds the portable zip (design doc 5.1 M4): dist\AgentDesk-<version>-win-x64.zip
//   agent-ui.exe, agent-host.exe, agentctl.exe, conpty\ (bundled ConPTY, ADR-08),
//   LICENSE, THIRD_PARTY_NOTICES.md, docs\
// Symbols go to a separate ...-symbols.zip. Build Release first (scripts\build.cmd msvc-x64-release all).
//   agentdesk_package [--build-dir <dir>] [--out-dir <dir>] [--version 0.1.0]
#include <cmath>
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

// String.TrimEnd(): strips trailing whitespace, including the Unicode spaces UTF-8 files may end with.
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
        for (const wchar_t* f : {L"agent-ui.exe", L"agent-host.exe", L"agentctl.exe", L"conpty\\conpty.dll",
                                 L"conpty\\x64\\OpenConsole.exe"}) {
            if (!fs::exists(bin / f)) {
                throw std::runtime_error("missing " + dk::u8(std::wstring(f)) + " in " + dk::u8(bin) +
                                         " (build Release and run fetch_conpty)");
            }
        }

        const std::wstring name = L"AgentDesk-" + version + L"-win-x64";
        const fs::path stage = out_dir / name;
        dk::remove_tree(stage);
        fs::create_directories(stage / L"conpty" / L"x64");
        fs::create_directories(stage / L"docs");

        for (const wchar_t* f : {L"agent-ui.exe", L"agent-host.exe", L"agentctl.exe"})
            copy_into(bin / f, stage);
        dk::copy_tree(bin / L"fonts", stage / L"fonts");
        dk::copy_tree(bin / L"sounds", stage / L"sounds");
        // Herdr 98 (herdr-app defaults/mods/win98): fonts, icons, images, sounds, the layout file.
        dk::copy_tree(bin / L"win98", stage / L"win98");
        // herdr's program table and icons (defaults/programs.json, defaults/icons).
        dk::copy_tree(bin / L"programs", stage / L"programs");
        dk::copy_tree(bin / L"themes", stage / L"themes");  // herdr defaults/themes (atom-one-dark)
        // Example mods (C++ DLLs) and the SDK to build your own: headers and the examples' sources.
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
        const fs::path vcpkg = build_dir / L"vcpkg_installed" / L"x64-windows-static-md";
        dk::copy_tree(vcpkg / L"include" / L"nlohmann", stage / L"sdk" / L"include" / L"nlohmann");
        copy_into(bin / L"conpty" / L"conpty.dll", stage / L"conpty");
        copy_into(bin / L"conpty" / L"x64" / L"OpenConsole.exe", stage / L"conpty" / L"x64");
        copy_into(root / L"third_party" / L"conpty" / L"LICENSE", stage / L"conpty");
        copy_into(root / L"third_party" / L"conpty" / L"NOTICE.md", stage / L"conpty");
        copy_into(root / L"LICENSE", stage);
        copy_into(root / L"docs" / L"user-guide.md", stage / L"docs");
        copy_into(root / L"docs" / L"integrations" / L"agent-hooks.md", stage / L"docs");
        copy_into(root / L"docs" / L"compatibility.md", stage / L"docs");

        // Third-party notices are assembled from the shipped dependencies' own license files.
        // CRLF line ends, as the PowerShell version wrote them.
        const fs::path share = vcpkg / L"share";
        struct Component {
            const char* name;
            const char* url;
            fs::path file;
        };
        const std::vector<Component> components = {
            {"libvterm 0.3.3", "https://www.leonerd.org.uk/code/libvterm/",
             root / L"third_party/libvterm/LICENSE"},
            {"Dear ImGui 1.93.0 (docking)", "https://github.com/ocornut/imgui", share / L"imgui/copyright"},
            {"nlohmann/json 3.12.0", "https://github.com/nlohmann/json", share / L"nlohmann-json/copyright"},
            {"Microsoft.Windows.Console.ConPTY 1.24.260710001", "https://github.com/microsoft/terminal",
             root / L"third_party/conpty/LICENSE"},
            {"herdr-app (behaviour and parts ported to C++, modified)",
             "https://github.com/herdrdev/herdr-app", root / L"third_party/herdr/LICENSE"},
            {"Geist font", "https://github.com/vercel/geist-font", root / L"third_party/fonts/OFL-Geist.txt"},
            {"JetBrains Mono font", "https://github.com/JetBrains/JetBrainsMono",
             root / L"third_party/fonts/OFL-JetBrainsMono.txt"},
            {"Symbols Nerd Font", "https://github.com/ryanoasis/nerd-fonts",
             root / L"third_party/fonts/LICENSE-SymbolsNerdFont.txt"},
            {"lunasvg 3.5.0 (SVG rendering)", "https://github.com/sammycage/lunasvg", share / L"lunasvg/copyright"},
            {"plutovg 1.3.3 (used by lunasvg)", "https://github.com/sammycage/plutovg", share / L"plutovg/copyright"},
            {"Pixelated MS Sans Serif fonts (Herdr 98, win98\\fonts)", "https://github.com/jdan/98.css",
             root / L"third_party/herdr/win98/fonts/LICENSE-MSSansSerif.txt"},
            {"Creative Commons Attribution-ShareAlike 3.0 (the Pixelated MS Sans Serif fonts)",
             "https://creativecommons.org/licenses/by-sa/3.0/", root / L"third_party/herdr/win98/fonts/CC-BY-SA-3.0.txt"},
            {"PxPlus IBM VGA 8x16 font (Herdr 98, win98\\fonts)", "https://int10h.org/oldschool-pc-fonts/",
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
            line(trim_end(read_text(fs::path(c.file).make_preferred())));
            line("```");
        }
        line();
        line("The ConPTY package also carries Microsoft's third-party notices: see conpty\\NOTICE.md.");
        dk::write_file(stage / L"THIRD_PARTY_NOTICES.md", notices);

        // The archive holds the stage folder itself, like ZipFile.CreateFromDirectory(..., true).
        const fs::path zip_path = out_dir / (name + L".zip");
        zip(zip_path, out_dir, {name});

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

        const auto hash = dk::sha256_hex(zip_path);
        // [math]::Round(bytes / 1MB, 2): banker's rounding, shortest number text.
        const double mb =
            std::nearbyint(static_cast<double>(fs::file_size(zip_path)) / (1024.0 * 1024.0) * 100.0) / 100.0;
        char size_text[32];
        std::snprintf(size_text, sizeof(size_text), "%.2f", mb);
        std::string size = size_text;
        while (size.ends_with('0'))
            size.pop_back();
        if (size.ends_with('.'))
            size.pop_back();
        std::printf("%s\nsha256 %s\nsize %s MB\n", dk::u8(zip_path).c_str(), hash.c_str(), size.c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "agentdesk_package: %s\n", e.what());
        return 1;
    }
}
