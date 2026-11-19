// The read-only code preview of the Files panel: language detection, highlighting, selection.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// ---- read-only code preview (herdr files/main.js:141-144 shows files in a read-only editor with
// syntax colours: keyword ansi5, string ansi2, comment dim, function ansi4, type ansi3, number ansi1,
// constant ansi6). A small lexer stands in for the editor's tree-sitter grammars.
enum class CodeTok : std::uint8_t { kText, kKeyword, kString, kComment, kFunction, kType, kNumber, kConstant };

struct CodeLang {
    std::string_view line_comment;   // "" = none
    std::string_view line_comment2;  // a second form ("#" next to "//" in PHP, ";" in INI)
    std::string_view block_open;     // "" = none
    std::string_view block_close;
    bool code = true;  // false: plain text, no colours
};

CodeLang code_lang(const std::string& path) {
    std::string ext;
    const auto dot = path.find_last_of('.');
    const auto slash = path.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        ext = path.substr(dot + 1);
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    for (auto& c : name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const std::set<std::string> kSlash = {"c",  "h",    "cpp", "hpp",   "cc",   "cxx",  "hh",   "inl",  "cs",
                                                 "java", "kt", "kts", "swift", "go",   "rs",   "js",   "jsx",  "ts",
                                                 "tsx", "mjs", "cjs", "mts",   "cts",  "css",  "scss", "less", "zig",
                                                 "dart", "scala", "glsl", "hlsl", "proto", "jsonc", "json5", "m", "mm"};
    static const std::set<std::string> kHash = {"py", "pyw", "sh",  "bash", "zsh", "fish", "ps1",  "psm1", "psd1", "rb",
                                                "yaml", "yml", "toml", "cmake", "r", "pl", "pm",   "conf", "nix",  "mk",
                                                "tf", "hcl", "gitignore", "gitattributes", "editorconfig", "env", "dockerignore"};
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
        "if", "else", "elif", "for", "while", "do", "switch", "case", "default", "break", "continue", "return",
        "goto", "try", "catch", "finally", "throw", "throws", "new", "delete", "class", "struct", "enum", "union",
        "interface", "namespace", "using", "typedef", "template", "typename", "public", "private", "protected",
        "static", "const", "constexpr", "consteval", "constinit", "volatile", "extern", "inline", "virtual",
        "override", "final", "explicit", "friend", "operator", "sizeof", "alignof", "decltype", "auto", "mutable",
        "noexcept", "co_await", "co_return", "co_yield", "import", "export", "from", "as", "package", "module",
        "def", "lambda", "pass", "with", "yield", "async", "await", "in", "is", "not", "and", "or", "del",
        "global", "nonlocal", "assert", "raise", "except", "fn", "let", "mut", "impl", "trait", "pub", "crate",
        "mod", "use", "match", "loop", "where", "move", "ref", "dyn", "unsafe", "extends", "implements",
        "instanceof", "typeof", "var", "function", "this", "super", "self", "void", "func", "go", "defer",
        "chan", "select", "type", "range", "map", "fallthrough", "end", "then", "local", "begin", "until",
        "elseif", "fi", "esac", "done", "echo", "export", "readonly", "when", "val", "object", "companion",
        "override", "sealed", "open", "internal", "suspend", "foreach", "of", "satisfies", "declare", "abstract",
        "int", "char", "bool", "float", "double", "long", "short", "unsigned", "signed", "string"};
    return kWords.contains(w);
}

bool code_constant(std::string_view w) {
    static const std::set<std::string_view> kConst = {"true", "false", "null", "nullptr", "nil", "None", "True",
                                                      "False", "undefined", "NULL", "NaN", "Infinity"};
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

// One line's coloured spans; `in_block` carries an open block comment to the next line.
void highlight_code_line(std::string_view line, const CodeLang& lang, bool& in_block, std::vector<CodeSpan>& out) {
    out.clear();
    if (!lang.code) {
        out.push_back({0, line.size(), CodeTok::kText});
        return;
    }
    auto ident = [](unsigned char c) { return std::isalnum(c) != 0 || c == '_' || c == '$' || c >= 0x80; };
    std::size_t i = 0;
    std::size_t text_from = 0;
    auto flush_text = [&](std::size_t upto) {
        if (upto > text_from)
            out.push_back({text_from, upto, CodeTok::kText});
    };
    auto emit = [&](std::size_t a, std::size_t b, CodeTok t) {
        flush_text(a);
        out.push_back({a, b, t});
        text_from = b;
    };
    auto starts = [&](std::size_t at, std::string_view tok) {
        return !tok.empty() && line.substr(at, tok.size()) == tok;
    };
    while (i < line.size()) {
        if (in_block) {
            const auto end = line.find(lang.block_close, i);
            const std::size_t b = end == std::string_view::npos ? line.size() : end + lang.block_close.size();
            emit(i, b, CodeTok::kComment);
            i = b;
            in_block = end == std::string_view::npos;
            continue;
        }
        const auto c = static_cast<unsigned char>(line[i]);
        if (starts(i, lang.block_open)) {
            in_block = true;
            const std::size_t from = i;
            i += lang.block_open.size();
            const auto end = line.find(lang.block_close, i);
            const std::size_t b = end == std::string_view::npos ? line.size() : end + lang.block_close.size();
            emit(from, b, CodeTok::kComment);
            i = b;
            in_block = end == std::string_view::npos;
            continue;
        }
        if (starts(i, lang.line_comment) || starts(i, lang.line_comment2)) {
            emit(i, line.size(), CodeTok::kComment);
            i = line.size();
            break;
        }
        if (c == '"' || c == '\'' || c == '`') {
            std::size_t j = i + 1;
            while (j < line.size() && line[j] != static_cast<char>(c))
                j += line[j] == '\\' ? 2 : 1;
            j = std::min(j + 1, line.size());
            emit(i, j, CodeTok::kString);
            i = j;
            continue;
        }
        if (std::isdigit(c) != 0 && (i == 0 || !ident(static_cast<unsigned char>(line[i - 1])))) {
            std::size_t j = i + 1;
            while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) != 0 || line[j] == '.' ||
                                       line[j] == '_' || line[j] == '\''))
                ++j;
            emit(i, j, CodeTok::kNumber);
            i = j;
            continue;
        }
        if (ident(c) && std::isdigit(c) == 0) {
            std::size_t j = i + 1;
            while (j < line.size() && ident(static_cast<unsigned char>(line[j])))
                ++j;
            const std::string_view w = line.substr(i, j - i);
            std::size_t k = j;
            while (k < line.size() && line[k] == ' ')
                ++k;
            CodeTok t = CodeTok::kText;
            if (code_keyword(w))
                t = CodeTok::kKeyword;
            else if (code_constant(w))
                t = CodeTok::kConstant;
            else if (k < line.size() && line[k] == '(')
                t = CodeTok::kFunction;
            else if (std::isupper(c) != 0)
                t = CodeTok::kType;
            if (t != CodeTok::kText)
                emit(i, j, t);
            i = j;
            continue;
        }
        ++i;
    }
    flush_text(line.size());
}

ImU32 code_color(CodeTok t) {
    switch (t) {
        case CodeTok::kKeyword:
            return mix(g_pal->needs_you, g_pal->info, 0.5f);  // magenta (ansi5)
        case CodeTok::kString:
            return g_pal->done;  // green (ansi2)
        case CodeTok::kComment:
            return g_pal->faint;  // dim
        case CodeTok::kFunction:
            return g_pal->info;  // blue (ansi4)
        case CodeTok::kType:
            return g_pal->working;  // yellow (ansi3)
        case CodeTok::kNumber:
            return g_pal->needs_you;  // red (ansi1)
        case CodeTok::kConstant:
            return mix(g_pal->info, g_pal->done, 0.5f);  // cyan (ansi6)
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

}  // namespace

// Selectable read-only text with line numbers: drag selects, double-click a word, Ctrl+A all,
// Ctrl+C (or the right-click Copy) copies. Caller holds the panel mutex.
void draw_code_preview(App::State& s, SidePanels& p, ImVec2 size) {
    static CodeLayout layout;
    const std::string& text = p.preview_text;
    layout_code(layout, text, p.preview_path);
    const CodeLang lang = code_lang(p.preview_path);
    ImGui::BeginChild("##preview", size, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);
    ImGui::PushFont(s.fonts.mono, 12);
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const float lh = std::round(ImGui::GetTextLineHeightWithSpacing());
    const float text_x = px(48);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const int n = static_cast<int>(layout.starts.size());
    auto line_end = [&](int i) {
        std::size_t b = i + 1 < n ? layout.starts[static_cast<std::size_t>(i) + 1] - 1 : text.size();
        const std::size_t a = layout.starts[static_cast<std::size_t>(i)];
        if (b > a && text[b - 1] == '\r')
            --b;
        return b;
    };
    auto width_of = [&](std::size_t a, std::size_t b) {
        return b > a ? font->CalcTextSizeA(fs, FLT_MAX, 0, text.data() + a, text.data() + b).x : 0.0f;
    };
    std::size_t longest = 0;
    for (int i = 0; i < n; ++i)
        longest = std::max(longest, line_end(i) - layout.starts[static_cast<std::size_t>(i)]);
    const float content_w = text_x + static_cast<float>(longest) * fs * 0.62f + px(20);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float top = origin.y + px(4);
    // Mouse -> (line, byte): the byte boundary nearest the pointer.
    auto hit = [&](ImVec2 m) -> std::pair<int, std::size_t> {
        const int line = std::clamp(static_cast<int>(std::floor((m.y - top) / lh)), 0, std::max(0, n - 1));
        const std::size_t a = layout.starts[static_cast<std::size_t>(line)];
        const std::size_t b = line_end(line);
        float x = origin.x + text_x;
        std::size_t i = a;
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
    };
    const bool hovered = ImGui::IsWindowHovered();
    const ImGuiIO& io = ImGui::GetIO();
    if (hovered && io.MousePos.x >= origin.x + text_x - px(4))
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && n > 0) {
        const auto [line, at] = hit(io.MousePos);
        if (io.MouseClickedCount[ImGuiMouseButton_Left] == 2) {
            std::size_t a = at, b = at;
            const std::size_t la = layout.starts[static_cast<std::size_t>(line)], lb = line_end(line);
            auto word = [&](std::size_t k) {
                const auto c = static_cast<unsigned char>(text[k]);
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
    if (p.sel_dragging) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const auto [line, at] = hit(io.MousePos);
            p.sel_line_b = line;
            p.sel_byte_b = at;
            const float wy0 = ImGui::GetWindowPos().y;
            if (io.MousePos.y < wy0)  // drag past an edge scrolls
                ImGui::SetScrollY(ImGui::GetScrollY() - lh);
            else if (io.MousePos.y > wy0 + ImGui::GetWindowHeight())
                ImGui::SetScrollY(ImGui::GetScrollY() + lh);
        } else {
            p.sel_dragging = false;
        }
    }
    auto selected = [&]() -> std::string {
        if (p.sel_line_a < 0 || p.sel_line_b < 0)
            return {};
        std::size_t a = p.sel_byte_a, b = p.sel_byte_b;
        if (a > b)
            std::swap(a, b);
        return text.substr(a, b - a);
    };
    const bool focused = ImGui::IsWindowFocused();
    if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && n > 0) {
        p.sel_line_a = 0;
        p.sel_byte_a = 0;
        p.sel_line_b = n - 1;
        p.sel_byte_b = line_end(n - 1);
    }
    if (focused && io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_C, false) || ImGui::IsKeyPressed(ImGuiKey_Insert, false))) {
        if (const std::string t = selected(); !t.empty())
            ImGui::SetClipboardText(t.c_str());
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##preview-menu");
    if (ImGui::BeginPopup("##preview-menu")) {
        const std::string t = selected();
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, !t.empty()))
            ImGui::SetClipboardText(t.c_str());
        if (ImGui::MenuItem("Select all", "Ctrl+A") && n > 0) {
            p.sel_line_a = 0;
            p.sel_byte_a = 0;
            p.sel_line_b = n - 1;
            p.sel_byte_b = line_end(n - 1);
        }
        ImGui::EndPopup();
    }
    std::size_t s0 = 0, s1 = 0;
    if (p.sel_line_a >= 0 && p.sel_line_b >= 0) {
        s0 = std::min(p.sel_byte_a, p.sel_byte_b);
        s1 = std::max(p.sel_byte_a, p.sel_byte_b);
    }
    ImGuiListClipper clip;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, top));
    clip.Begin(n, lh);
    std::vector<CodeSpan> spans;
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(content_w, lh));
            const std::size_t la = layout.starts[static_cast<std::size_t>(i)];
            const std::size_t lb = line_end(i);
            const float ty = a.y + (lh - fs) * 0.5f;
            const std::string num = std::to_string(i + 1);
            d->AddText(font, fs, ImVec2(a.x + px(10), ty), g_pal->faint, num.c_str());
            const float x0 = a.x + text_x;
            // Selection behind the text; a selected line break shows as a little extra width.
            if (s1 > s0 && s0 <= lb && s1 >= la) {
                const std::size_t ca = std::clamp(s0, la, lb);
                const std::size_t cb = std::clamp(s1, la, lb);
                const float xa = x0 + width_of(la, ca);
                float xb = x0 + width_of(la, cb);
                if (s1 > lb)
                    xb += fs * 0.5f;
                if (xb > xa)
                    d->AddRectFilled(ImVec2(xa, a.y), ImVec2(xb, a.y + lh), alpha(g_pal->focus, 70));
            }
            bool block = layout.in_block[static_cast<std::size_t>(i)] != 0;
            highlight_code_line(std::string_view(text).substr(la, lb - la), lang, block, spans);
            float x = x0;
            for (const auto& sp : spans) {
                const char* b0 = text.data() + la + sp.a;
                const char* b1 = text.data() + la + sp.b;
                d->AddText(font, fs, ImVec2(x, ty), code_color(sp.tok), b0, b1);
                x += font->CalcTextSizeA(fs, FLT_MAX, 0, b0, b1).x;
            }
        }
    }
    ImGui::PopFont();
    ImGui::EndChild();
}
}  // namespace detail

}  // namespace agentdesk::desktop
