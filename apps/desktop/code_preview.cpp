// The read-only code preview of the Files panel: language detection, highlighting, selection.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// ---- highlighting
// Files open read-only in the terminal font with syntax colours: keyword magenta, string green,
// comment dim, function blue, type yellow, number red, constant cyan. A small lexer stands in for a
// real grammar: it knows comment markers per file type and a shared keyword list.
enum class CodeTok : std::uint8_t {
    kText,
    kKeyword,
    kString,
    kComment,
    kFunction,
    kType,
    kNumber,
    kConstant
};

struct CodeLang {
    std::string_view line_comment;   // "" = none
    std::string_view line_comment2;  // a second form ("#" next to "//" in PHP, ";" in INI)
    std::string_view block_open;     // "" = none
    std::string_view block_close;
    bool code = true;  // false: plain text, no colours
};

std::string lower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

CodeLang code_lang(const std::string& path) {
    std::string ext;
    const auto dot = path.find_last_of('.');
    const auto slash = path.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        ext = lower(path.substr(dot + 1));
    const std::string name = lower(slash == std::string::npos ? path : path.substr(slash + 1));
    static const std::set<std::string> kSlash = {
        "c",     "h",    "cpp", "hpp",  "cc",    "cxx",  "hh",   "inl",   "cs",    "java",  "kt",  "kts",
        "swift", "go",   "rs",  "js",   "jsx",   "ts",   "tsx",  "mjs",   "cjs",   "mts",   "cts", "css",
        "scss",  "less", "zig", "dart", "scala", "glsl", "hlsl", "proto", "jsonc", "json5", "m",   "mm"};
    static const std::set<std::string> kHash = {
        "py",           "pyw",  "sh",          "bash", "zsh",  "fish",  "ps1",       "psm1",
        "psd1",         "rb",   "yaml",        "yml",  "toml", "cmake", "r",         "pl",
        "pm",           "conf", "nix",         "mk",   "tf",   "hcl",   "gitignore", "gitattributes",
        "editorconfig", "env",  "dockerignore"};
    if (kSlash.contains(ext))
        return {"//", "", "/*", "*/"};
    if (ext == "php")
        return {"//", "#", "/*", "*/"};
    if (kHash.contains(ext) || name == "cmakelists.txt" || name == "dockerfile" || name == "makefile")
        return {"#", "", "", ""};
    if (ext == "ini" || ext == "cfg" || ext == "reg")
        return {";", "#", "", ""};
    if (ext == "lua" || ext == "sql" || ext == "hs")
        return {"--", "", ext == "lua" ? "--[[" : "/*", ext == "lua" ? "]]" : "*/"};
    if (ext == "html" || ext == "htm" || ext == "xml" || ext == "svg" || ext == "vue" || ext == "svelte" ||
        ext == "xaml" || ext == "csproj" || ext == "props" || ext == "targets" || ext == "vcxproj")
        return {"", "", "<!--", "-->"};
    if (ext == "json")
        return {"", "", "", ""};
    if (ext == "bat" || ext == "cmd")
        return {"rem ", "::", "", ""};
    return {"", "", "", "", false};
}

bool code_keyword(std::string_view w) {
    static const std::set<std::string_view> kWords = {
        "if",         "else",      "elif",        "for",       "while",     "do",        "switch",
        "case",       "default",   "break",       "continue",  "return",    "goto",      "try",
        "catch",      "finally",   "throw",       "throws",    "new",       "delete",    "class",
        "struct",     "enum",      "union",       "interface", "namespace", "using",     "typedef",
        "template",   "typename",  "public",      "private",   "protected", "static",    "const",
        "constexpr",  "consteval", "constinit",   "volatile",  "extern",    "inline",    "virtual",
        "override",   "final",     "explicit",    "friend",    "operator",  "sizeof",    "alignof",
        "decltype",   "auto",      "mutable",     "noexcept",  "co_await",  "co_return", "co_yield",
        "import",     "export",    "from",        "as",        "package",   "module",    "def",
        "lambda",     "pass",      "with",        "yield",     "async",     "await",     "in",
        "is",         "not",       "and",         "or",        "del",       "global",    "nonlocal",
        "assert",     "raise",     "except",      "fn",        "let",       "mut",       "impl",
        "trait",      "pub",       "crate",       "mod",       "use",       "match",     "loop",
        "where",      "move",      "ref",         "dyn",       "unsafe",    "extends",   "implements",
        "instanceof", "typeof",    "var",         "function",  "this",      "super",     "self",
        "void",       "func",      "go",          "defer",     "chan",      "select",    "type",
        "range",      "map",       "fallthrough", "end",       "then",      "local",     "begin",
        "until",      "elseif",    "fi",          "esac",      "done",      "echo",      "export",
        "readonly",   "when",      "val",         "object",    "companion", "override",  "sealed",
        "open",       "internal",  "suspend",     "foreach",   "of",        "satisfies", "declare",
        "abstract",   "int",       "char",        "bool",      "float",     "double",    "long",
        "short",      "unsigned",  "signed",      "string"};
    return kWords.contains(w);
}

bool code_constant(std::string_view w) {
    static const std::set<std::string_view> kConst = {"true",      "false", "null", "nullptr",
                                                      "nil",       "None",  "True", "False",
                                                      "undefined", "NULL",  "NaN",  "Infinity"};
    if (kConst.contains(w))
        return true;
    // ALL_CAPS names (two letters or more).
    bool upper = false;
    for (const char c : w) {
        if (std::islower(static_cast<unsigned char>(c)))
            return false;
        upper = upper || std::isupper(static_cast<unsigned char>(c));
    }
    return upper && w.size() > 1;
}

struct CodeSpan {
    std::size_t a = 0;
    std::size_t b = 0;
    CodeTok tok = CodeTok::kText;
};

bool ident_char(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_' || c == '$' || c >= 0x80;
}

// End of a block comment whose body is searched from `from`; sets `in_block` when the comment
// runs past the end of the line.
std::size_t block_comment_end(std::string_view line, const CodeLang& lang, std::size_t from, bool& in_block) {
    const auto end = line.find(lang.block_close, from);
    in_block = end == std::string_view::npos;
    return in_block ? line.size() : end + lang.block_close.size();
}

// End of a quoted string starting at `i`; a backslash escapes the next byte. An unclosed string
// runs to the end of the line.
std::size_t string_end(std::string_view line, std::size_t i) {
    const char quote = line[i];
    std::size_t j = i + 1;
    while (j < line.size() && line[j] != quote)
        j += line[j] == '\\' ? 2 : 1;
    return std::min(j + 1, line.size());
}

// End of a number starting at `i`: digits, letters (hex, suffixes), '.', '_' and the C++ digit
// separator '\''.
std::size_t number_end(std::string_view line, std::size_t i) {
    std::size_t j = i + 1;
    while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) != 0 || line[j] == '.' ||
                               line[j] == '_' || line[j] == '\''))
        ++j;
    return j;
}

// A word is a keyword, a constant, a function (followed by '('), a type (capitalised) or plain.
CodeTok word_token(std::string_view line, std::size_t i, std::size_t j) {
    const std::string_view w = line.substr(i, j - i);
    std::size_t k = j;
    while (k < line.size() && line[k] == ' ')
        ++k;
    if (code_keyword(w))
        return CodeTok::kKeyword;
    if (code_constant(w))
        return CodeTok::kConstant;
    if (k < line.size() && line[k] == '(')
        return CodeTok::kFunction;
    if (std::isupper(static_cast<unsigned char>(line[i])) != 0)
        return CodeTok::kType;
    return CodeTok::kText;
}

// Collects spans so that every byte of the line is covered: the gaps become kText spans.
class SpanSink {
public:
    explicit SpanSink(std::vector<CodeSpan>& out) : out_(out) { out_.clear(); }
    void emit(std::size_t a, std::size_t b, CodeTok t) {
        flush_text(a);
        out_.push_back({a, b, t});
        text_from_ = b;
    }
    void flush_text(std::size_t upto) {
        if (upto > text_from_)
            out_.push_back({text_from_, upto, CodeTok::kText});
    }

private:
    std::vector<CodeSpan>& out_;
    std::size_t text_from_ = 0;
};

// One line's coloured spans; `in_block` carries an open block comment to the next line.
void highlight_code_line(std::string_view line, const CodeLang& lang, bool& in_block,
                         std::vector<CodeSpan>& out) {
    SpanSink sink(out);
    if (!lang.code) {
        out.push_back({0, line.size(), CodeTok::kText});
        return;
    }
    auto starts = [&](std::size_t at, std::string_view tok) {
        return !tok.empty() && line.substr(at, tok.size()) == tok;
    };
    std::size_t i = 0;
    while (i < line.size()) {
        if (in_block) {
            const std::size_t b = block_comment_end(line, lang, i, in_block);
            sink.emit(i, b, CodeTok::kComment);
            i = b;
            continue;
        }
        const auto c = static_cast<unsigned char>(line[i]);
        if (starts(i, lang.block_open)) {
            const std::size_t b = block_comment_end(line, lang, i + lang.block_open.size(), in_block);
            sink.emit(i, b, CodeTok::kComment);
            i = b;
            continue;
        }
        if (starts(i, lang.line_comment) || starts(i, lang.line_comment2)) {
            sink.emit(i, line.size(), CodeTok::kComment);
            break;
        }
        if (c == '"' || c == '\'' || c == '`') {
            const std::size_t j = string_end(line, i);
            sink.emit(i, j, CodeTok::kString);
            i = j;
            continue;
        }
        if (std::isdigit(c) != 0 && (i == 0 || !ident_char(static_cast<unsigned char>(line[i - 1])))) {
            const std::size_t j = number_end(line, i);
            sink.emit(i, j, CodeTok::kNumber);
            i = j;
            continue;
        }
        if (ident_char(c) && std::isdigit(c) == 0) {
            std::size_t j = i + 1;
            while (j < line.size() && ident_char(static_cast<unsigned char>(line[j])))
                ++j;
            if (const CodeTok t = word_token(line, i, j); t != CodeTok::kText)
                sink.emit(i, j, t);
            i = j;
            continue;
        }
        ++i;
    }
    sink.flush_text(line.size());
}

ImU32 code_color(CodeTok t) {
    switch (t) {
        case CodeTok::kKeyword:
            return mix(g_pal->needs_you, g_pal->info, 0.5f);  // magenta
        case CodeTok::kString:
            return g_pal->done;  // green
        case CodeTok::kComment:
            return g_pal->faint;  // dim
        case CodeTok::kFunction:
            return g_pal->info;  // blue
        case CodeTok::kType:
            return g_pal->working;  // yellow
        case CodeTok::kNumber:
            return g_pal->needs_you;  // red
        case CodeTok::kConstant:
            return mix(g_pal->info, g_pal->done, 0.5f);  // cyan
        default:
            return g_pal->text;
    }
}

// Line starts and the block-comment state at each line start, recomputed when the text changes.
struct CodeLayout {
    const std::string* text = nullptr;
    std::size_t size = 0;
    std::string path;
    std::vector<std::size_t> starts;
    std::vector<std::uint8_t> in_block;
};

void layout_code(CodeLayout& L, const std::string& text, const std::string& path) {
    if (L.text == &text && L.size == text.size() && L.path == path)
        return;
    L.text = &text;
    L.size = text.size();
    L.path = path;
    L.starts = {0};
    for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\n')
            L.starts.push_back(i + 1);
    const CodeLang lang = code_lang(path);
    L.in_block.assign(L.starts.size(), 0);
    bool block = false;
    std::vector<CodeSpan> spans;
    for (std::size_t n = 0; n < L.starts.size(); ++n) {
        L.in_block[n] = block ? 1 : 0;
        if (lang.block_open.empty())
            continue;
        const std::size_t a = L.starts[n];
        const std::size_t b = n + 1 < L.starts.size() ? L.starts[n + 1] - 1 : text.size();
        highlight_code_line(std::string_view(text).substr(a, b - a), lang, block, spans);
    }
}

// ---- the preview view

// Geometry of the preview for this frame: line bounds, text widths and hit-testing.
struct PreviewGeom {
    const std::string& text;
    const CodeLayout& layout;
    ImFont* font;
    float fs;      // font size
    float lh;      // line height
    float text_x;  // where the text starts, right of the line numbers
    ImVec2 origin;
    float top;  // first line's top
    int n;      // line count

    std::size_t line_start(int i) const { return layout.starts[static_cast<std::size_t>(i)]; }

    // End of line i, without its "\r\n" or "\n".
    std::size_t line_end(int i) const {
        std::size_t b = i + 1 < n ? layout.starts[static_cast<std::size_t>(i) + 1] - 1 : text.size();
        const std::size_t a = line_start(i);
        if (b > a && text[b - 1] == '\r')
            --b;
        return b;
    }

    float width_of(std::size_t a, std::size_t b) const {
        return b > a ? font->CalcTextSizeA(fs, FLT_MAX, 0, text.data() + a, text.data() + b).x : 0.0f;
    }

    // Mouse -> (line, byte): the character boundary nearest the pointer.
    std::pair<int, std::size_t> hit(ImVec2 m) const {
        const int line = std::clamp(static_cast<int>(std::floor((m.y - top) / lh)), 0, std::max(0, n - 1));
        const std::size_t b = line_end(line);
        float x = origin.x + text_x;
        std::size_t i = line_start(line);
        while (i < b) {
            std::size_t j = i + 1;
            while (j < b && (static_cast<unsigned char>(text[j]) & 0xC0) == 0x80)
                ++j;
            const float w = width_of(i, j);
            if (m.x < x + w * 0.5f)
                break;
            x += w;
            i = j;
        }
        return {line, i};
    }
};

void select_all(SidePanels& p, const PreviewGeom& g) {
    p.sel_line_a = 0;
    p.sel_byte_a = 0;
    p.sel_line_b = g.n - 1;
    p.sel_byte_b = g.line_end(g.n - 1);
}

std::string selected_text(const SidePanels& p, const std::string& text) {
    if (p.sel_line_a < 0 || p.sel_line_b < 0)
        return {};
    std::size_t a = p.sel_byte_a, b = p.sel_byte_b;
    if (a > b)
        std::swap(a, b);
    return text.substr(a, b - a);
}

// A click places the caret, a double-click selects a word, a drag extends the selection and
// scrolls when it passes the top or bottom edge.
void handle_preview_mouse(SidePanels& p, const PreviewGeom& g, bool hovered) {
    const ImGuiIO& io = ImGui::GetIO();
    if (hovered && io.MousePos.x >= g.origin.x + g.text_x - px(4))
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && g.n > 0) {
        const auto [line, at] = g.hit(io.MousePos);
        if (io.MouseClickedCount[ImGuiMouseButton_Left] == 2) {
            std::size_t a = at, b = at;
            const std::size_t la = g.line_start(line), lb = g.line_end(line);
            auto word = [&](std::size_t k) {
                const auto c = static_cast<unsigned char>(g.text[k]);
                return std::isalnum(c) != 0 || c == '_' || c >= 0x80;
            };
            while (a > la && word(a - 1))
                --a;
            while (b < lb && word(b))
                ++b;
            p.sel_line_a = p.sel_line_b = line;
            p.sel_byte_a = a;
            p.sel_byte_b = b;
        } else {
            p.sel_line_a = p.sel_line_b = line;
            p.sel_byte_a = p.sel_byte_b = at;
            p.sel_dragging = true;
        }
    }
    if (!p.sel_dragging)
        return;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        p.sel_dragging = false;
        return;
    }
    const auto [line, at] = g.hit(io.MousePos);
    p.sel_line_b = line;
    p.sel_byte_b = at;
    const float wy0 = ImGui::GetWindowPos().y;
    if (io.MousePos.y < wy0)
        ImGui::SetScrollY(ImGui::GetScrollY() - g.lh);
    else if (io.MousePos.y > wy0 + ImGui::GetWindowHeight())
        ImGui::SetScrollY(ImGui::GetScrollY() + g.lh);
}

// Ctrl+A selects all, Ctrl+C or Ctrl+Insert copies; the right-click menu offers the same.
void handle_preview_keys_and_menu(SidePanels& p, const PreviewGeom& g, bool hovered) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool focused = ImGui::IsWindowFocused();
    if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && g.n > 0)
        select_all(p, g);
    if (focused && io.KeyCtrl &&
        (ImGui::IsKeyPressed(ImGuiKey_C, false) || ImGui::IsKeyPressed(ImGuiKey_Insert, false))) {
        if (const std::string t = selected_text(p, g.text); !t.empty())
            ImGui::SetClipboardText(t.c_str());
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##preview-menu");
    if (ImGui::BeginPopup("##preview-menu")) {
        const std::string t = selected_text(p, g.text);
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, !t.empty()))
            ImGui::SetClipboardText(t.c_str());
        if (ImGui::MenuItem("Select all", "Ctrl+A") && g.n > 0)
            select_all(p, g);
        ImGui::EndPopup();
    }
}

// The visible lines: number, selection behind the text, then the coloured spans.
void draw_preview_lines(const SidePanels& p, const PreviewGeom& g, const CodeLang& lang, float content_w) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    std::size_t s0 = 0, s1 = 0;
    if (p.sel_line_a >= 0 && p.sel_line_b >= 0) {
        s0 = std::min(p.sel_byte_a, p.sel_byte_b);
        s1 = std::max(p.sel_byte_a, p.sel_byte_b);
    }
    ImGuiListClipper clip;
    ImGui::SetCursorScreenPos(ImVec2(g.origin.x, g.top));
    clip.Begin(g.n, g.lh);
    std::vector<CodeSpan> spans;
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(content_w, g.lh));
            const std::size_t la = g.line_start(i);
            const std::size_t lb = g.line_end(i);
            const float ty = a.y + (g.lh - g.fs) * 0.5f;
            const std::string num = std::to_string(i + 1);
            d->AddText(g.font, g.fs, ImVec2(a.x + px(10), ty), g_pal->faint, num.c_str());
            const float x0 = a.x + g.text_x;
            // A selected line break shows as a little extra width.
            if (s1 > s0 && s0 <= lb && s1 >= la) {
                const std::size_t ca = std::clamp(s0, la, lb);
                const std::size_t cb = std::clamp(s1, la, lb);
                const float xa = x0 + g.width_of(la, ca);
                float xb = x0 + g.width_of(la, cb);
                if (s1 > lb)
                    xb += g.fs * 0.5f;
                if (xb > xa)
                    d->AddRectFilled(ImVec2(xa, a.y), ImVec2(xb, a.y + g.lh), alpha(g_pal->focus, 70));
            }
            bool block = g.layout.in_block[static_cast<std::size_t>(i)] != 0;
            highlight_code_line(std::string_view(g.text).substr(la, lb - la), lang, block, spans);
            float x = x0;
            for (const auto& sp : spans) {
                const char* b0 = g.text.data() + la + sp.a;
                const char* b1 = g.text.data() + la + sp.b;
                d->AddText(g.font, g.fs, ImVec2(x, ty), code_color(sp.tok), b0, b1);
                x += g.font->CalcTextSizeA(g.fs, FLT_MAX, 0, b0, b1).x;
            }
        }
    }
}

}  // namespace

// Selectable read-only text with line numbers: drag selects, double-click a word, Ctrl+A all,
// Ctrl+C (or the right-click Copy) copies. Caller holds the panel mutex.
void draw_code_preview(App::State& s, SidePanels& p, ImVec2 size) {
    static CodeLayout layout;
    const std::string& text = p.preview_text;
    layout_code(layout, text, p.preview_path);
    const CodeLang lang = code_lang(p.preview_path);
    ImGui::BeginChild("##preview", size, ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);
    ImGui::PushFont(s.fonts.mono, 12);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const PreviewGeom g{text,
                        layout,
                        ImGui::GetFont(),
                        ImGui::GetFontSize(),
                        std::round(ImGui::GetTextLineHeightWithSpacing()),
                        px(48),
                        origin,
                        origin.y + px(4),
                        static_cast<int>(layout.starts.size())};
    std::size_t longest = 0;
    for (int i = 0; i < g.n; ++i)
        longest = std::max(longest, g.line_end(i) - g.line_start(i));
    // A rough width (0.62 em per byte) for the horizontal scrollbar.
    const float content_w = g.text_x + static_cast<float>(longest) * g.fs * 0.62f + px(20);
    const bool hovered = ImGui::IsWindowHovered();
    handle_preview_mouse(p, g, hovered);
    handle_preview_keys_and_menu(p, g, hovered);
    draw_preview_lines(p, g, lang, content_w);
    ImGui::PopFont();
    ImGui::EndChild();
}
}  // namespace detail

}  // namespace agentdesk::desktop
