#include "agentdesk/ui/fonts.hpp"

#include <windows.h>

#include <filesystem>
#include <string>

#include <imgui.h>

namespace agentdesk::ui {

namespace {

std::string utf8(const std::filesystem::path& p) {
    const auto u8 = p.u8string();  // ImGui opens files from UTF-8 paths
    return {u8.begin(), u8.end()};
}

std::filesystem::path system_fonts_dir() {
    wchar_t buf[MAX_PATH]{};
    const UINT n = ::GetWindowsDirectoryW(buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)) / L"Fonts";
}

std::filesystem::path bundled_fonts_dir() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path() / L"fonts";
}

// First existing file: bundled names are looked up in fonts\ next to the exe, system names in
// %WINDIR%\Fonts.
std::string find_font(std::initializer_list<const wchar_t*> bundled, std::initializer_list<const wchar_t*> system) {
    std::error_code ec;
    for (const wchar_t* name : bundled) {
        const auto p = bundled_fonts_dir() / name;
        if (std::filesystem::is_regular_file(p, ec))
            return utf8(p);
    }
    for (const wchar_t* name : system) {
        const auto p = system_fonts_dir() / name;
        if (std::filesystem::is_regular_file(p, ec))
            return utf8(p);
    }
    return {};
}

void merge(const std::string& path, float extra_scale = 1.0f, float y_offset = 0.0f) {
    if (path.empty())
        return;
    ImFontConfig c;
    c.MergeMode = true;
    c.ExtraSizeScale = extra_scale;
    c.GlyphOffset.y = y_offset;
    ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 0.0f, &c);
}

// Fills gaps after the main face: CJK (JhengHei, scaled so ideographs fill their slot), Nerd
// Font icons, then Segoe UI Symbol / Emoji for anything else agents print.
void merge_fallbacks(const std::string& cjk, const std::string& nerd) {
    merge(cjk);
    merge(nerd, 0.95f);
    merge(find_font({}, {L"seguisym.ttf"}));
    merge(find_font({}, {L"seguiemj.ttf"}));
}

ImFont* add(const std::string& path, float size, bool pixel_snap, const std::string& cjk, const std::string& nerd) {
    if (path.empty())
        return nullptr;
    ImFontConfig c;
    c.PixelSnapH = pixel_snap;  // terminal: integer advance, every cell on a pixel boundary
    ImFont* f = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), size, &c);
    if (f != nullptr)
        merge_fallbacks(cjk, nerd);
    return f;
}

}  // namespace

FontSet load_fonts(float mono_size_px, float ui_size_px) {
    FontSet set;
    set.mono_size = mono_size_px;
    set.ui_size = ui_size_px;
    ImGuiIO& io = ImGui::GetIO();
    set.ui_source = find_font({L"Geist-Regular.ttf"}, {L"segoeui.ttf"});
    const std::string ui_medium = find_font({L"Geist-Medium.ttf"}, {L"seguisb.ttf", L"segoeui.ttf"});
    const std::string ui_bold = find_font({L"Geist-SemiBold.ttf"}, {L"seguisb.ttf", L"segoeuib.ttf"});
    set.mono_source = find_font({L"JetBrainsMono-Medium.ttf"}, {L"CascadiaMono.ttf", L"consola.ttf"});
    const std::string mono_bold = find_font({L"JetBrainsMono-Bold.ttf"}, {L"consolab.ttf"});
    const std::string nerd = find_font({L"SymbolsNerdFont-Regular.ttf"}, {});
    set.cjk_source = find_font({}, {L"msjh.ttc", L"mingliu.ttc"});

    // Interface font first: the first font added is ImGui's default.
    set.ui = add(set.ui_source, ui_size_px, false, set.cjk_source, nerd);
    set.ui_medium = add(ui_medium, ui_size_px, false, set.cjk_source, nerd);
    set.ui_bold = add(ui_bold, ui_size_px, false, set.cjk_source, nerd);
    set.mono = add(set.mono_source, mono_size_px, true, set.cjk_source, nerd);
    set.mono_bold = add(mono_bold, mono_size_px, true, set.cjk_source, nerd);
    if (!nerd.empty())
        set.icons = io.Fonts->AddFontFromFileTTF(nerd.c_str(), ui_size_px);

    if (set.ui == nullptr)
        set.ui = io.Fonts->AddFontDefault();
    if (set.ui_medium == nullptr)
        set.ui_medium = set.ui;
    if (set.ui_bold == nullptr)
        set.ui_bold = set.ui_medium;
    if (set.mono == nullptr)
        set.mono = set.ui;
    if (set.mono_bold == nullptr)
        set.mono_bold = set.mono;
    if (set.icons == nullptr)
        set.icons = set.ui;
    return set;
}

}  // namespace agentdesk::ui
