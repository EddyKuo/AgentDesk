// fonts_os.hpp on macOS (ADR-30): the app's own fonts, the system faces found through CoreText,
// and the installed families.
#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>
#include <ft2build.h>
#include <strings.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <map>
#include <vector>

#include <imgui_freetype.h>

#include "../fonts_os.hpp"
#include "agentdesk/platform/crt.hpp"
#include "agentdesk/platform/system.hpp"
#include "agentdesk/platform/text.hpp"
#include "agentdesk/ui/fonts.hpp"

namespace agentdesk::ui {

namespace {

using fonts_os::utf8;

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

// Faces inside a collection (.ttc) found by PostScript name: file -> face index, for add_font_file.
std::map<std::string, int>& face_indexes() {
    static std::map<std::string, int> m;
    return m;
}

// The face index of `postscript` inside `file` (0 when it is not a collection or not found).
int face_index_of(const std::string& file, const std::string& postscript) {
    FT_Library lib = nullptr;
    if (FT_Init_FreeType(&lib) != 0)
        return 0;
    int found = 0;
    FT_Face face = nullptr;
    if (FT_New_Face(lib, file.c_str(), -1, &face) == 0) {
        const FT_Long count = face->num_faces;
        FT_Done_Face(face);
        for (FT_Long i = 0; i < count; ++i) {
            if (FT_New_Face(lib, file.c_str(), i, &face) != 0)
                continue;
            const char* ps = FT_Get_Postscript_Name(face);
            const bool match = ps != nullptr && postscript == ps;
            FT_Done_Face(face);
            if (match) {
                found = static_cast<int>(i);
                break;
            }
        }
    }
    FT_Done_FreeType(lib);
    return found;
}

// A system font by PostScript name, through CoreText (it also finds fonts macOS keeps outside
// /System/Library/Fonts, such as PingFang). The face index is remembered for collections.
std::string coretext_font(const char* postscript) {
    CFStringRef name = CFStringCreateWithCString(nullptr, postscript, kCFStringEncodingUTF8);
    CTFontRef font = CTFontCreateWithName(name, 12, nullptr);
    CFRelease(name);
    std::string path;
    if (font != nullptr) {
        CFStringRef actual = CTFontCopyPostScriptName(font);
        char actual_name[256]{};
        const bool same = actual != nullptr &&
                          CFStringGetCString(actual, actual_name, sizeof actual_name,
                                             kCFStringEncodingUTF8) &&
                          std::string_view(actual_name) == postscript;  // no silent substitute
        if (actual != nullptr)
            CFRelease(actual);
        if (auto url = static_cast<CFURLRef>(CTFontCopyAttribute(font, kCTFontURLAttribute))) {
            char buf[1024]{};
            if (same &&
                CFURLGetFileSystemRepresentation(url, true, reinterpret_cast<UInt8*>(buf), sizeof buf))
                path = buf;
            CFRelease(url);
        }
        CFRelease(font);
    }
    if (!path.empty() && path.ends_with(".ttc"))
        face_indexes()[path] = face_index_of(path, postscript);
    return path;
}

// The system fonts that play the part of the Windows ones the callers name, by PostScript name
// (CoreText) first, then by file in the system font folders.
std::vector<std::string> mac_postscript_equivalents(std::wstring_view windows_name) {
    static const std::map<std::wstring_view, std::vector<std::string>> kMap = {
        {L"msjh.ttc", {"PingFangTC-Regular"}},  // what herdr's CoreText falls back to
        {L"mingliu.ttc", {"PingFangTC-Regular"}},
        {L"seguisym.ttf", {"AppleSymbols"}},
        {L"CascadiaMono.ttf", {"SFMono-Regular", "Menlo-Regular"}},
        {L"consola.ttf", {"Menlo-Regular"}},
        {L"consolab.ttf", {"Menlo-Bold"}},
    };
    const auto it = kMap.find(windows_name);
    return it == kMap.end() ? std::vector<std::string>{} : it->second;
}

std::vector<std::string> mac_equivalents(std::wstring_view windows_name) {
    static const std::map<std::wstring_view, std::vector<std::string>> kMap = {
        {L"msjh.ttc", {"STHeiti Medium.ttc", "Hiragino Sans GB.ttc", "Arial Unicode.ttf"}},
        {L"mingliu.ttc", {"Songti.ttc", "Arial Unicode.ttf"}},
        {L"seguisym.ttf", {"Apple Symbols.ttf"}},
        // Apple Color Emoji is a 180 MB bitmap (sbix) font FreeType cannot draw at text sizes:
        // emoji fall back to Apple Symbols' monochrome shapes where it has them.
        {L"seguiemj.ttf", {}},
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

}  // namespace

namespace fonts_os {

std::string find_font(std::initializer_list<const wchar_t*> bundled,
                      std::initializer_list<const wchar_t*> system) {
    std::error_code ec;
    for (const wchar_t* name : bundled) {
        const auto p = bundled_fonts_dir() / platform::to_utf8(name);
        if (std::filesystem::is_regular_file(p, ec))
            return utf8(p);
    }
    for (const wchar_t* name : system) {
        for (const auto& ps : mac_postscript_equivalents(name))
            if (std::string path = coretext_font(ps.c_str()); !path.empty())
                return path;
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

// A face inside a collection (PingFang.ttc holds SC, TC, HK...), and glyphs drawn without
// hinting, as macOS's own text (CoreText) looks.
void adjust_config(const std::string& path, ImFontConfig& c) {
    if (const auto it = face_indexes().find(path); it != face_indexes().end())
        c.FontNo = static_cast<ImU32>(it->second);
    c.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_NoHinting;
}

}  // namespace fonts_os

// macOS: the installed families from CoreText (family name, weight, file), plus the bundled
// fonts. Italic faces are left out, as on Windows.
std::vector<FontFamily> installed_font_families() {
    std::map<std::string, FontFamily> by_name;
    const auto put = [&](const std::string& family, int weight, const std::filesystem::path& file,
                         bool bundled) {
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
        return CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8) ? std::string(buf)
                                                                             : std::string();
    };
    if (CTFontCollectionRef collection = CTFontCollectionCreateFromAvailableFonts(nullptr)) {
        if (CFArrayRef list = CTFontCollectionCreateMatchingFontDescriptors(collection)) {
            for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
                auto desc = static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(list, i));
                auto family =
                    static_cast<CFStringRef>(CTFontDescriptorCopyAttribute(desc, kCTFontFamilyNameAttribute));
                auto url = static_cast<CFURLRef>(CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute));
                auto traits =
                    static_cast<CFDictionaryRef>(CTFontDescriptorCopyAttribute(desc, kCTFontTraitsAttribute));
                bool italic = false;
                double weight = 0;  // -1..1, 0 = regular
                if (traits != nullptr) {
                    if (auto sym =
                            static_cast<CFNumberRef>(CFDictionaryGetValue(traits, kCTFontSymbolicTrait))) {
                        std::uint32_t bits = 0;
                        CFNumberGetValue(sym, kCFNumberSInt32Type, &bits);
                        italic = (bits & kCTFontItalicTrait) != 0;
                    }
                    if (auto w = static_cast<CFNumberRef>(CFDictionaryGetValue(traits, kCTFontWeightTrait)))
                        CFNumberGetValue(w, kCFNumberDoubleType, &weight);
                }
                char path[1024]{};
                if (!italic && url != nullptr &&
                    CFURLGetFileSystemRepresentation(url, true, reinterpret_cast<UInt8*>(path),
                                                     sizeof path)) {
                    // CoreText weights -1..1 onto 100..900 (0 = 400, 0.23 = 500, 0.3 = 600, 0.4 = 700).
                    const int w = weight <= -0.6   ? 100
                                  : weight <= -0.3 ? 300
                                  : weight < 0.2   ? 400
                                  : weight < 0.27  ? 500
                                  : weight < 0.35  ? 600
                                  : weight < 0.5   ? 700
                                                   : 900;
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
        {"JetBrainsMono-Medium.ttf", {"JetBrains Mono", 500}},
        {"JetBrainsMono-Bold.ttf", {"JetBrains Mono", 700}},
        {"Geist-Regular.ttf", {"Geist", 400}},
        {"Geist-Medium.ttf", {"Geist", 500}},
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

}  // namespace agentdesk::ui
