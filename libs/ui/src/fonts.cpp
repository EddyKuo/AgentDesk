// Loads the bundled fonts into ImGui with CJK and symbol fallbacks, and lists installed font
// families (fonts_win.cpp, macos/fonts_macos.cpp).
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "agentdesk/ui/fonts.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <imgui.h>

#include "fonts_os.hpp"

namespace agentdesk::ui {

namespace {

using fonts_os::find_font;
using fonts_os::utf8;

// AddFontFromFileTTF reads the whole file into a new buffer on every call, and every face merges
// the same fallbacks (about 40 MB: JhengHei, Nerd Font, Segoe UI Symbol and Emoji), so loading
// them that way kept five copies alive. Each file is read once and shared; ImGui gets a pointer it
// does not own. The buffers live for the whole process, which also covers atlas rebuilds.
ImFont* add_font_file(const std::string& path, float size, ImFontConfig c) {
    static std::map<std::string, std::vector<char>> files;
    auto it = files.find(path);
    if (it == files.end()) {
        std::ifstream in(std::filesystem::path(std::u8string(path.begin(), path.end())),
                         std::ios::binary | std::ios::ate);
        std::vector<char> bytes;
        if (in) {  // one read of the whole file: fonts are megabytes
            bytes.resize(static_cast<std::size_t>(std::max<std::streamoff>(in.tellg(), 0)));
            in.seekg(0);
            if (!in.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
                bytes.clear();
        }
        if (bytes.empty())
            return nullptr;
        it = files.emplace(path, std::move(bytes)).first;
    }
    c.FontDataOwnedByAtlas = false;
    fonts_os::adjust_config(path, c);
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

// Big-endian reads from a font file; 0 past the end.
std::uint32_t be(const unsigned char* d, std::size_t size, std::size_t at, int bytes) {
    if (at + static_cast<std::size_t>(bytes) > size)
        return 0;
    std::uint32_t v = 0;
    for (int i = 0; i < bytes; ++i)
        v = (v << 8) | d[at + static_cast<std::size_t>(i)];
    return v;
}

std::int16_t be16s(const unsigned char* d, std::size_t size, std::size_t at) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(be(d, size, at, 2)));
}

}  // namespace

float line_height_per_em(const unsigned char* d, std::size_t size, unsigned face) {
    if (d == nullptr || size < 12)
        return 0.0f;
    std::size_t dir = 0;                     // the table directory of the face
    if (be(d, size, 0, 4) == 0x74746366u) {  // 'ttcf': a collection, offsets to each face
        if (face >= be(d, size, 8, 4))
            return 0.0f;
        dir = be(d, size, 12 + 4 * static_cast<std::size_t>(face), 4);
    }
    const std::uint32_t tables = be(d, size, dir + 4, 2);
    std::size_t head = 0;
    std::size_t hhea = 0;
    std::size_t os2 = 0;
    for (std::uint32_t i = 0; i < tables; ++i) {
        const std::size_t rec = dir + 12 + 16 * static_cast<std::size_t>(i);
        const std::uint32_t tag = be(d, size, rec, 4);
        const std::size_t at = be(d, size, rec + 8, 4);
        if (tag == 0x68656164u)  // 'head'
            head = at;
        else if (tag == 0x68686561u)  // 'hhea'
            hhea = at;
        else if (tag == 0x4F532F32u)  // 'OS/2'
            os2 = at;
    }
    const std::uint32_t upem = head != 0 ? be(d, size, head + 18, 2) : 0;
    if (upem == 0)
        return 0.0f;
    int ascent = hhea != 0 ? be16s(d, size, hhea + 4) : 0;
    int descent = hhea != 0 ? be16s(d, size, hhea + 6) : 0;
    if (ascent == 0 && descent == 0 && os2 != 0) {  // FreeType's order when hhea says nothing
        ascent = be16s(d, size, os2 + 68);          // sTypoAscender
        descent = be16s(d, size, os2 + 70);
        if (ascent == 0 && descent == 0) {
            ascent = static_cast<int>(be(d, size, os2 + 74, 2));    // usWinAscent
            descent = -static_cast<int>(be(d, size, os2 + 76, 2));  // usWinDescent, below the baseline
        }
    }
    const int height = ascent - descent;
    return height > 0 ? static_cast<float>(height) / static_cast<float>(upem) : 0.0f;
}

float line_height_per_em(const ImFont* font) {
    if (font == nullptr || font->Sources.empty() || font->Sources[0] == nullptr)
        return 1.0f;
    const ImFontConfig& src = *font->Sources[0];
    const float r = line_height_per_em(static_cast<const unsigned char*>(src.FontData),
                                       static_cast<std::size_t>(std::max(src.FontDataSize, 0)), src.FontNo);
    return r > 0.0f ? r : 1.0f;
}

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

ImFont* load_terminal_font(const std::wstring& file, float size_px) {
    std::string path = file.empty()
                           ? find_font({L"JetBrainsMono-Medium.ttf"}, {L"CascadiaMono.ttf", L"consola.ttf"})
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
    set.mono_line_per_em = line_height_per_em(set.mono);
    if (set.mono_bold == nullptr)
        set.mono_bold = set.mono;
    if (set.icons == nullptr)
        set.icons = set.ui;
    return set;
}

}  // namespace agentdesk::ui
