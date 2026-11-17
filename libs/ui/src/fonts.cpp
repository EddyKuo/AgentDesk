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

// Merges the CJK face into the font added just before. 1.92+: glyphs load on demand, no ranges.
void merge_cjk(const std::string& cjk, float extra_scale) {
    if (cjk.empty())
        return;
    ImFontConfig c;
    c.MergeMode = true;
    // JhengHei's ideographs sit small next to Latin faces of the same size; scale them up a bit
    // so they fill their two-cell slot (terminal) or match the cap height (interface).
    c.ExtraSizeScale = extra_scale;
    ImGui::GetIO().Fonts->AddFontFromFileTTF(cjk.c_str(), 0.0f, &c);
}

// Symbols agents print that neither the Latin nor the CJK face has (warning sign, branch, check
// marks, media arrows, emoji drawn monochrome). Merged last so they only fill gaps.
void merge_symbols() {
    for (const wchar_t* name : {L"seguisym.ttf", L"seguiemj.ttf"}) {
        const std::string path = first_existing({name});
        if (path.empty())
            continue;
        ImFontConfig c;
        c.MergeMode = true;
        ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 0.0f, &c);
    }
}

}  // namespace

FontSet load_fonts(float mono_size_px, float ui_size_px) {
    FontSet set;
    set.mono_size = mono_size_px;
    set.ui_size = ui_size_px;
    ImGuiIO& io = ImGui::GetIO();
    set.mono_source = first_existing({L"CascadiaMono.ttf", L"consola.ttf"});
    set.ui_source = first_existing({L"segoeui.ttf"});
    const std::string ui_bold_source = first_existing({L"seguisb.ttf", L"segoeuib.ttf"});
    set.cjk_source = first_existing({L"msjh.ttc", L"mingliu.ttc"});

    // Interface font first: the first font added is ImGui's default.
    if (!set.ui_source.empty()) {
        set.ui = io.Fonts->AddFontFromFileTTF(set.ui_source.c_str(), ui_size_px);
        merge_cjk(set.cjk_source, 1.0f);
        merge_symbols();
    }
    if (!ui_bold_source.empty()) {
        set.ui_bold = io.Fonts->AddFontFromFileTTF(ui_bold_source.c_str(), ui_size_px);
        merge_cjk(set.cjk_source, 1.0f);
        merge_symbols();
    }
    if (!set.mono_source.empty()) {
        ImFontConfig mono;
        mono.PixelSnapH = true;  // integer advance: every cell lands on a pixel boundary
        set.mono = io.Fonts->AddFontFromFileTTF(set.mono_source.c_str(), mono_size_px, &mono);
        merge_cjk(set.cjk_source, 1.0f);
        merge_symbols();
    }
    if (set.ui == nullptr)
        set.ui = io.Fonts->AddFontDefault();
    if (set.ui_bold == nullptr)
        set.ui_bold = set.ui;
    if (set.mono == nullptr)
        set.mono = set.ui;
    return set;
}

}  // namespace agentdesk::ui
