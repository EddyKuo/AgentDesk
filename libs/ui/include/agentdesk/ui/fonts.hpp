#pragma once

#include <map>
#include <string>
#include <vector>

struct ImFont;

namespace agentdesk::ui {

struct FontSet {
    ImFont* mono = nullptr;       // terminal: JetBrains Mono Medium (integer advances) + CJK
    ImFont* mono_bold = nullptr;  // terminal bold text
    ImFont* ui = nullptr;         // interface text: Geist Regular + CJK
    ImFont* ui_medium = nullptr;  // labels, tab names (herdr "medium")
    ImFont* ui_bold = nullptr;    // headings, names (herdr "semibold")
    ImFont* icons = nullptr;      // Symbols Nerd Font alone (icon glyphs)
    float mono_size = 14.0f;
    float ui_size = 13.0f;
    std::string mono_source;  // files used, for diagnostics
    std::string ui_source;
    std::string cjk_source;
};

// Loads the bundled fonts from `fonts\` next to the executable (herdr's families: Geist for the
// interface, JetBrains Mono for terminals, Symbols Nerd Font for icons), each merged with
// Microsoft JhengHei for CJK and Segoe UI Symbol/Emoji for the rest. Falls back to system fonts
// (Segoe UI, Cascadia Mono) when the bundled files are missing.
FontSet load_fonts(float mono_size_px, float ui_size_px);

// A terminal font from a file (empty: the bundled JetBrains Mono), merged like load_fonts does.
// Fonts can be added while running (ImGui 1.92 dynamic atlas).
ImFont* load_terminal_font(const std::wstring& file, float size_px);
// Any font file with the same fallbacks; pixel_snap for terminal cells. nullptr if unreadable.
ImFont* load_font_file(const std::wstring& file, float size_px, bool pixel_snap);

// Installed font families (herdr lists every family in its font pickers): the per-user and
// system font registrations plus the bundled fonts\ folder. Italic faces are left out.
struct FontFamily {
    std::string name;
    std::map<int, std::wstring> weights;  // 100..900 -> file
    bool bundled = false;
    // The file for `weight`, or the nearest weight the family has.
    [[nodiscard]] std::wstring file_for(int weight) const;
};
std::vector<FontFamily> installed_font_families();

// Icon glyphs of the bundled Symbols Nerd Font (VS Code codicons at their own code points; each
// verified present in the font's cmap). Draw them with FontSet::icons.
namespace icon {
inline constexpr const char* kSearch = "\xEE\xA9\xAD";  // U+EA6D codicon search
inline constexpr const char* kSidebar = "\xEE\xAF\xB3";  // U+EBF3 codicon layout-sidebar-left
inline constexpr const char* kPlus = "\xEE\xA9\xA0";  // U+EA60 codicon add
inline constexpr const char* kSettings = "\xEE\xAD\x91";  // U+EB51 codicon settings-gear
inline constexpr const char* kFile = "\xEE\xA9\xBB";  // U+EA7B codicon file
inline constexpr const char* kChanges = "\xEE\xAB\xBD";  // U+EAFD codicon git-compare
inline constexpr const char* kBranch = "\xEE\xB1\xAF";  // U+EC6F codicon git-branch
inline constexpr const char* kMonitor = "\xEE\xA9\xBA";  // U+EA7A codicon vm
inline constexpr const char* kServer = "\xEE\xAD\x90";  // U+EB50 codicon server
inline constexpr const char* kSplitRight = "\xEE\xAD\x96";  // U+EB56 codicon split-horizontal
inline constexpr const char* kSplitDown = "\xEE\xAD\x97";  // U+EB57 codicon split-vertical
inline constexpr const char* kZoom = "\xEE\xAD\x8C";  // U+EB4C codicon screen-full
inline constexpr const char* kUnzoom = "\xEE\xAD\x8D";  // U+EB4D codicon screen-normal
inline constexpr const char* kClose = "\xEE\xA9\xB6";  // U+EA76 codicon close
inline constexpr const char* kChevronDown = "\xEE\xAA\xB4";  // U+EAB4 codicon chevron-down
inline constexpr const char* kChevronRight = "\xEE\xAA\xB6";  // U+EAB6 codicon chevron-right
inline constexpr const char* kPin = "\xEE\xAC\xAB";  // U+EB2B codicon pin
inline constexpr const char* kPinned = "\xEE\xAE\xA0";  // U+EBA0 codicon pinned
inline constexpr const char* kSort = "\xEE\xAE\x83";  // U+EB83 codicon list-filter
inline constexpr const char* kFolder = "\xEE\xAA\x83";  // U+EA83 codicon folder
inline constexpr const char* kFolderOpen = "\xEE\xAB\xB7";  // U+EAF7 codicon folder-opened
inline constexpr const char* kArrowLeft = "\xEE\xAA\x9B";  // U+EA9B codicon arrow-left
inline constexpr const char* kCheck = "\xEE\xAA\xB2";  // U+EAB2 codicon check
inline constexpr const char* kMinimize = "\xEE\xAA\xBA";  // U+EABA codicon chrome-minimize
inline constexpr const char* kMaximize = "\xEE\xAA\xB9";  // U+EAB9 codicon chrome-maximize
inline constexpr const char* kRestore = "\xEE\xAA\xBB";  // U+EABB codicon chrome-restore
inline constexpr const char* kWinClose = "\xEE\xAA\xB8";  // U+EAB8 codicon chrome-close
inline constexpr const char* kTerminal = "\xEE\xAA\x85";  // U+EA85 codicon terminal
inline constexpr const char* kWorktree = "\xEE\xA9\xA3";  // U+EA63 codicon repo-forked
inline constexpr const char* kLayers = "\xEE\xAF\x92";  // U+EBD2 codicon layers
inline constexpr const char* kClock = "\xEE\xAA\x82";  // U+EA82 codicon clock
inline constexpr const char* kCommand = "\xEE\xAA\x93";  // U+EA93 codicon symbol-key
inline constexpr const char* kComment = "\xEE\xA9\xAB";  // U+EA6B codicon comment
inline constexpr const char* kExtensions = "\xEE\xAB\xA6";  // U+EAE6 codicon extensions
inline constexpr const char* kPalette = "\xEE\xAD\x9C";  // U+EB5C codicon symbol-color
inline constexpr const char* kWarning = "\xEE\xA9\xAC";  // U+EA6C codicon warning
}  // namespace icon

}  // namespace agentdesk::ui
