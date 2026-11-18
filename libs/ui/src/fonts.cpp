#include "agentdesk/ui/fonts.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
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

ImFont* load_font_file(const std::wstring& file, float size_px, bool pixel_snap) {
    if (file.empty())
        return nullptr;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec))
        return nullptr;
    const std::string nerd = find_font({L"SymbolsNerdFont-Regular.ttf"}, {});
    const std::string cjk = find_font({}, {L"msjh.ttc", L"mingliu.ttc"});
    return add(utf8(std::filesystem::path(file)), size_px, pixel_snap, cjk, nerd);
}

std::wstring FontFamily::file_for(int weight) const {
    if (weights.empty())
        return {};
    auto best = weights.begin();
    for (auto it = weights.begin(); it != weights.end(); ++it)
        if (std::abs(it->first - weight) < std::abs(best->first - weight))
            best = it;
    return best->second;
}

namespace {

// "Cascadia Mono SemiBold (TrueType)" -> family "Cascadia Mono", weight 600; italic -> false.
bool parse_font_name(std::wstring name, std::wstring& family, int& weight) {
    if (const auto p = name.find(L" ("); p != std::wstring::npos)
        name.resize(p);
    if (name.find(L" & ") != std::wstring::npos)
        name.resize(name.find(L" & "));  // collections: "MS Gothic & MS UI Gothic & ..." -> the first
    static const std::pair<const wchar_t*, int> kWeights[] = {
        {L"ExtraLight", 200}, {L"Extra Light", 200}, {L"UltraLight", 200}, {L"SemiBold", 600}, {L"Semibold", 600},
        {L"Semi Bold", 600},  {L"DemiBold", 600},    {L"ExtraBold", 800},  {L"Extra Bold", 800}, {L"Thin", 100},
        {L"Light", 300},      {L"Regular", 400},     {L"Normal", 400},     {L"Book", 400},       {L"Medium", 500},
        {L"Bold", 700},       {L"Black", 900},       {L"Heavy", 900}};
    for (const wchar_t* italic : {L"Italic", L"Oblique"})
        if (name.find(italic) != std::wstring::npos)
            return false;
    weight = 400;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& [word, w] : kWeights) {
            const std::wstring suffix = std::wstring(L" ") + word;
            if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                name.resize(name.size() - suffix.size());
                weight = w;
                changed = true;
                break;
            }
        }
    }
    family = name;
    return !family.empty();
}

}  // namespace

std::vector<FontFamily> installed_font_families() {
    std::map<std::wstring, FontFamily> by_name;
    auto put = [&](const std::wstring& reg_name, std::filesystem::path file, bool bundled) {
        std::wstring fam;
        int w = 400;
        if (!parse_font_name(reg_name, fam, w))
            return;
        const auto ext = file.extension().native();
        if (_wcsicmp(ext.c_str(), L".ttf") != 0 && _wcsicmp(ext.c_str(), L".otf") != 0 && _wcsicmp(ext.c_str(), L".ttc") != 0)
            return;
        auto& f = by_name[fam];
        f.name = utf8(std::filesystem::path(fam));
        f.bundled = f.bundled || bundled;
        f.weights.try_emplace(w, file.native());
    };
    for (const HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        HKEY key = nullptr;
        if (::RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ, &key) !=
            ERROR_SUCCESS)
            continue;
        for (DWORD i = 0;; ++i) {
            wchar_t name[512]{};
            wchar_t data[MAX_PATH]{};
            DWORD name_len = 512;
            DWORD data_len = sizeof(data) - sizeof(wchar_t);
            DWORD type = 0;
            const LONG r = ::RegEnumValueW(key, i, name, &name_len, nullptr, &type, reinterpret_cast<BYTE*>(data), &data_len);
            if (r == ERROR_NO_MORE_ITEMS)
                break;
            if (r != ERROR_SUCCESS || type != REG_SZ)
                continue;
            std::filesystem::path file(data);
            if (!file.is_absolute())
                file = system_fonts_dir() / file;
            put(name, file, false);
        }
        ::RegCloseKey(key);
    }
    // Bundled: file stems like "JetBrainsMono-Medium" -> "JetBrains Mono" Medium.
    static const std::pair<const wchar_t*, const wchar_t*> kBundled[] = {
        {L"JetBrainsMono-Medium.ttf", L"JetBrains Mono Medium"}, {L"JetBrainsMono-Bold.ttf", L"JetBrains Mono Bold"},
        {L"Geist-Regular.ttf", L"Geist Regular"},                {L"Geist-Medium.ttf", L"Geist Medium"},
        {L"Geist-SemiBold.ttf", L"Geist SemiBold"}};
    std::error_code ec;
    for (const auto& [file, name] : kBundled)
        if (std::filesystem::is_regular_file(bundled_fonts_dir() / file, ec))
            put(name, bundled_fonts_dir() / file, true);
    std::vector<FontFamily> out;
    for (auto& [k, f] : by_name)
        out.push_back(std::move(f));
    std::sort(out.begin(), out.end(), [](const FontFamily& a, const FontFamily& b) {
        return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return out;
}

ImFont* load_terminal_font(const std::wstring& file, float size_px) {
    std::string path = file.empty() ? find_font({L"JetBrainsMono-Medium.ttf"}, {L"CascadiaMono.ttf", L"consola.ttf"})
                                    : utf8(std::filesystem::path(file));
    const std::string nerd = find_font({L"SymbolsNerdFont-Regular.ttf"}, {});
    const std::string cjk = find_font({}, {L"msjh.ttc", L"mingliu.ttc"});
    ImFont* f = add(path, size_px, true, cjk, nerd);
    return f != nullptr ? f : ImGui::GetIO().Fonts->Fonts.front();
}

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
