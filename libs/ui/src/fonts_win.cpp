// fonts_os.hpp on Windows: fonts\ next to the program, %WINDIR%\Fonts, and the installed
// families from the registry.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <map>
#include <string>
#include <vector>

#include "agentdesk/ui/fonts.hpp"
#include "fonts_os.hpp"

namespace agentdesk::ui {

namespace {

using fonts_os::utf8;

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

}  // namespace

namespace fonts_os {

// First existing file: bundled names are looked up in fonts\ next to the exe, system names in
// %WINDIR%\Fonts.
std::string find_font(std::initializer_list<const wchar_t*> bundled,
                      std::initializer_list<const wchar_t*> system) {
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

void adjust_config(const std::string& /*path*/, ImFontConfig& /*c*/) {}

}  // namespace fonts_os

namespace {

// "Cascadia Mono SemiBold (TrueType)" -> family "Cascadia Mono", weight 600; italic -> false.
bool parse_font_name(std::wstring name, std::wstring& family, int& weight) {
    if (const auto p = name.find(L" ("); p != std::wstring::npos)
        name.resize(p);
    if (name.find(L" & ") != std::wstring::npos)
        name.resize(name.find(L" & "));  // collections: "MS Gothic & MS UI Gothic & ..." -> the first
    static const std::pair<const wchar_t*, int> kWeights[] = {
        {L"ExtraLight", 200}, {L"Extra Light", 200}, {L"UltraLight", 200}, {L"SemiBold", 600},
        {L"Semibold", 600},   {L"Semi Bold", 600},   {L"DemiBold", 600},   {L"ExtraBold", 800},
        {L"Extra Bold", 800}, {L"Thin", 100},        {L"Light", 300},      {L"Regular", 400},
        {L"Normal", 400},     {L"Book", 400},        {L"Medium", 500},     {L"Bold", 700},
        {L"Black", 900},      {L"Heavy", 900}};
    for (const wchar_t* italic : {L"Italic", L"Oblique"})
        if (name.find(italic) != std::wstring::npos)
            return false;
    weight = 400;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& [word, w] : kWeights) {
            const std::wstring suffix = std::wstring(L" ") + word;
            if (name.size() > suffix.size() &&
                name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
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
        if (_wcsicmp(ext.c_str(), L".ttf") != 0 && _wcsicmp(ext.c_str(), L".otf") != 0 &&
            _wcsicmp(ext.c_str(), L".ttc") != 0)
            return;
        auto& f = by_name[fam];
        f.name = utf8(std::filesystem::path(fam));
        f.bundled = f.bundled || bundled;
        f.weights.try_emplace(w, file.native());
    };
    for (const HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        HKEY key = nullptr;
        if (::RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ,
                            &key) != ERROR_SUCCESS)
            continue;
        for (DWORD i = 0;; ++i) {
            wchar_t name[512]{};
            wchar_t data[MAX_PATH]{};
            DWORD name_len = 512;
            DWORD data_len = sizeof(data) - sizeof(wchar_t);
            DWORD type = 0;
            const LONG r = ::RegEnumValueW(key, i, name, &name_len, nullptr, &type,
                                           reinterpret_cast<BYTE*>(data), &data_len);
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
        {L"JetBrainsMono-Medium.ttf", L"JetBrains Mono Medium"},
        {L"JetBrainsMono-Bold.ttf", L"JetBrains Mono Bold"},
        {L"Geist-Regular.ttf", L"Geist Regular"},
        {L"Geist-Medium.ttf", L"Geist Medium"},
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

}  // namespace agentdesk::ui
