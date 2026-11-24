// Loads the bundled fonts into ImGui with CJK and symbol fallbacks, and lists installed font
// families from the registry.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "agentdesk/ui/fonts.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <strings.h>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>

#include "agentdesk/platform/system.hpp"
#include "agentdesk/platform/win/error.hpp"
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <imgui.h>

namespace agentdesk::ui {

namespace {

std::string utf8(const std::filesystem::path& p) {
    const auto u8 = p.u8string();  // ImGui opens files from UTF-8 paths
    return {u8.begin(), u8.end()};
}

#ifdef _WIN32
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
#else
// macOS (ADR-30): the bundled fonts are in the app's Resources/fonts (or fonts/ next to the
// program in a build folder). The Windows system font names the callers ask for map to the
// macOS faces that play the same part; PingFang is a downloadable asset outside
// /System/Library/Fonts, so the CJK fallback is Heiti (which has Traditional Chinese).
std::filesystem::path bundled_fonts_dir() {
    const auto dir = platform::executable_path().parent_path();
    std::error_code ec;
    if (const auto res = dir.parent_path() / "Resources" / "fonts"; std::filesystem::is_directory(res, ec))
        return res;
    return dir / "fonts";
}

const std::vector<std::filesystem::path>& system_font_dirs() {
    static const std::vector<std::filesystem::path> dirs = {
        "/System/Library/Fonts", "/System/Library/Fonts/Supplemental", "/Library/Fonts",
        platform::home_dir() / "Library" / "Fonts"};
    return dirs;
}

std::vector<std::string> mac_equivalents(std::wstring_view windows_name) {
    static const std::map<std::wstring_view, std::vector<std::string>> kMap = {
        {L"msjh.ttc", {"STHeiti Medium.ttc", "Hiragino Sans GB.ttc", "Arial Unicode.ttf"}},
        {L"mingliu.ttc", {"Songti.ttc", "Arial Unicode.ttf"}},
        {L"seguisym.ttf", {"Apple Symbols.ttf"}},
        {L"seguiemj.ttf", {"Apple Color Emoji.ttc"}},
        {L"CascadiaMono.ttf", {"SFNSMono.ttf", "Menlo.ttc"}},
        {L"consola.ttf", {"Menlo.ttc"}},
        {L"consolab.ttf", {"Menlo.ttc"}},
        {L"segoeui.ttf", {"SFNS.ttf", "Helvetica.ttc"}},
        {L"seguisb.ttf", {"SFNS.ttf", "Helvetica.ttc"}},
        {L"segoeuib.ttf", {"SFNS.ttf", "Helvetica.ttc"}},
    };
    const auto it = kMap.find(windows_name);
    return it == kMap.end() ? std::vector<std::string>{} : it->second;
}

std::string find_font(std::initializer_list<const wchar_t*> bundled, std::initializer_list<const wchar_t*> system) {
    std::error_code ec;
    for (const wchar_t* name : bundled) {
        const auto p = bundled_fonts_dir() / platform::win::to_utf8(name);
        if (std::filesystem::is_regular_file(p, ec))
            return utf8(p);
    }
    for (const wchar_t* name : system) {
        for (const auto& mac : mac_equivalents(name)) {
            for (const auto& dir : system_font_dirs()) {
                const auto p = dir / mac;
                if (std::filesystem::is_regular_file(p, ec))
                    return utf8(p);
            }
        }
    }
    return {};
}
#endif

// AddFontFromFileTTF reads the whole file into a new buffer on every call, and every face merges
// the same fallbacks (about 40 MB: JhengHei, Nerd Font, Segoe UI Symbol and Emoji), so loading
// them that way kept five copies alive. Each file is read once and shared; ImGui gets a pointer it
// does not own. The buffers live for the whole process, which also covers atlas rebuilds.
ImFont* add_font_file(const std::string& path, float size, ImFontConfig c) {
    static std::map<std::string, std::vector<char>> files;
    auto it = files.find(path);
    if (it == files.end()) {
        std::ifstream in(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.empty())
            return nullptr;
        it = files.emplace(path, std::move(bytes)).first;
    }
    c.FontDataOwnedByAtlas = false;
    if (c.Name[0] == '\0') {  // AddFontFromFileTTF names the source after the file; keep doing so
        const std::string name =
            std::filesystem::path(std::u8string(path.begin(), path.end())).filename().string();
        std::snprintf(c.Name, sizeof(c.Name), "%s", name.c_str());
    }
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(it->second.data(), static_cast<int>(it->second.size()),
                                                      size, &c);
}

void merge(const std::string& path, float extra_scale = 1.0f, float y_offset = 0.0f) {
    if (path.empty())
        return;
    ImFontConfig c;
    c.MergeMode = true;
    c.ExtraSizeScale = extra_scale;
    c.GlyphOffset.y = y_offset;
    add_font_file(path, 0.0f, c);
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
    ImFont* f = add_font_file(path, size, c);
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

#ifdef _WIN32
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
#endif

#ifdef _WIN32
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
#else
// macOS: the installed families from CoreText (family name, weight, file), plus the bundled
// fonts. Italic faces are left out, as on Windows.
std::vector<FontFamily> installed_font_families() {
    std::map<std::string, FontFamily> by_name;
    const auto put = [&](const std::string& family, int weight, const std::filesystem::path& file, bool bundled) {
        std::string ext = file.extension().string();
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (family.empty() || (ext != ".ttf" && ext != ".otf" && ext != ".ttc"))
            return;
        auto& f = by_name[family];
        f.name = family;
        f.bundled = f.bundled || bundled;
        f.weights.try_emplace(weight, file.wstring());
    };
    const auto cf_string = [](CFStringRef s) {
        if (s == nullptr)
            return std::string();
        char buf[512]{};
        return CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8) ? std::string(buf) : std::string();
    };
    if (CTFontCollectionRef collection = CTFontCollectionCreateFromAvailableFonts(nullptr)) {
        if (CFArrayRef list = CTFontCollectionCreateMatchingFontDescriptors(collection)) {
            for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
                auto desc = static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(list, i));
                auto family = static_cast<CFStringRef>(CTFontDescriptorCopyAttribute(desc, kCTFontFamilyNameAttribute));
                auto url = static_cast<CFURLRef>(CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute));
                auto traits = static_cast<CFDictionaryRef>(CTFontDescriptorCopyAttribute(desc, kCTFontTraitsAttribute));
                bool italic = false;
                double weight = 0;  // -1..1, 0 = regular
                if (traits != nullptr) {
                    if (auto sym = static_cast<CFNumberRef>(CFDictionaryGetValue(traits, kCTFontSymbolicTrait))) {
                        std::uint32_t bits = 0;
                        CFNumberGetValue(sym, kCFNumberSInt32Type, &bits);
                        italic = (bits & kCTFontItalicTrait) != 0;
                    }
                    if (auto w = static_cast<CFNumberRef>(CFDictionaryGetValue(traits, kCTFontWeightTrait)))
                        CFNumberGetValue(w, kCFNumberDoubleType, &weight);
                }
                char path[1024]{};
                if (!italic && url != nullptr &&
                    CFURLGetFileSystemRepresentation(url, true, reinterpret_cast<UInt8*>(path), sizeof path)) {
                    // CoreText weights -1..1 onto 100..900 (0 = 400, 0.23 = 500, 0.3 = 600, 0.4 = 700).
                    const int w = weight <= -0.6 ? 100 : weight <= -0.3 ? 300 : weight < 0.2 ? 400
                                : weight < 0.27 ? 500 : weight < 0.35 ? 600 : weight < 0.5 ? 700 : 900;
                    put(cf_string(family), w, path, false);
                }
                if (family != nullptr)
                    CFRelease(family);
                if (url != nullptr)
                    CFRelease(url);
                if (traits != nullptr)
                    CFRelease(traits);
            }
            CFRelease(list);
        }
        CFRelease(collection);
    }
    static const std::pair<const char*, std::pair<const char*, int>> kBundled[] = {
        {"JetBrainsMono-Medium.ttf", {"JetBrains Mono", 500}}, {"JetBrainsMono-Bold.ttf", {"JetBrains Mono", 700}},
        {"Geist-Regular.ttf", {"Geist", 400}},                 {"Geist-Medium.ttf", {"Geist", 500}},
        {"Geist-SemiBold.ttf", {"Geist", 600}}};
    std::error_code ec;
    for (const auto& [file, fam] : kBundled)
        if (std::filesystem::is_regular_file(bundled_fonts_dir() / file, ec))
            put(fam.first, fam.second, bundled_fonts_dir() / file, true);
    std::vector<FontFamily> out;
    for (auto& [k, f] : by_name)
        out.push_back(std::move(f));
    std::sort(out.begin(), out.end(), [](const FontFamily& a, const FontFamily& b) {
        return ::strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return out;
}
#endif

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
        set.icons = add_font_file(nerd, ui_size_px, ImFontConfig());

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
