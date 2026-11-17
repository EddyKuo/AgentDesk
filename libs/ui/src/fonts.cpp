#include "agentdesk/ui/fonts.hpp"

#include <windows.h>

#include <filesystem>
#include <string>

#include <imgui.h>

namespace agentdesk::ui {

namespace {

std::filesystem::path fonts_dir() {
    wchar_t buf[MAX_PATH]{};
    const UINT n = ::GetWindowsDirectoryW(buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)) / L"Fonts";
}

std::string first_existing(std::initializer_list<const wchar_t*> names) {
    const auto dir = fonts_dir();
    for (const wchar_t* name : names) {
        const auto p = dir / name;
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) {
            const auto u8 = p.u8string();  // ImGui opens files from UTF-8 paths
            return std::string(u8.begin(), u8.end());
        }
    }
    return {};
}

}  // namespace

FontSet load_fonts(float size_px) {
    FontSet set;
    ImGuiIO& io = ImGui::GetIO();
    set.mono_source = first_existing({L"CascadiaMono.ttf", L"consola.ttf"});
    set.cjk_source = first_existing({L"msjh.ttc", L"mingliu.ttc"});
    if (set.mono_source.empty()) {
        set.mono = io.Fonts->AddFontDefault();
        return set;
    }
    set.mono = io.Fonts->AddFontFromFileTTF(set.mono_source.c_str(), size_px);
    if (!set.cjk_source.empty()) {
        // 1.92+: the DX11 backend loads glyphs on demand, no glyph ranges needed.
        ImFontConfig cjk;
        cjk.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(set.cjk_source.c_str(), 0.0f, &cjk);
    }
    return set;
}

}  // namespace agentdesk::ui
