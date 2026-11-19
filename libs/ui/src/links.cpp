#include "agentdesk/ui/links.hpp"

#include <windows.h>

#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <thread>

#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::ui {

namespace {

namespace fs = std::filesystem;

// Ghostty's schemes (herdr src/links.rs:11), the only ones a link may open.
constexpr std::array<std::string_view, 15> kSchemes = {
    "https://", "http://", "mailto:",  "ftp://",    "file:",     "ssh://",   "ssh:",   "git://",
    "tel:",     "magnet:", "ipfs://", "ipns://", "gemini://", "gopher://", "news:",
};

bool is_word(unsigned char c) {
    return c >= 0x80 || std::isalnum(c) != 0 || c == '_';
}
bool is_scheme_char(unsigned char c) {
    return is_word(c) || std::string_view("-.~:/?#@!$&*+,;=%").find(static_cast<char>(c)) != std::string_view::npos;
}
// Ghostty's path characters, plus the backslash: Windows programs print C:\like\this.
bool is_path_char(unsigned char c) {
    return is_word(c) || std::string_view("-.~:/?#@!$&*+;=%\\").find(static_cast<char>(c)) != std::string_view::npos;
}

unsigned char at_or_0(const std::string& t, std::size_t i) {
    return i < t.size() ? static_cast<unsigned char>(t[i]) : 0;
}
bool word_before(const std::string& t, std::size_t i) {
    return i > 0 && is_word(static_cast<unsigned char>(t[i - 1]));
}

// scheme_url: (?:schemes)(?:ipv6|scheme_chars+bracketed)+ with no trailing , or .
std::size_t url_at(const std::string& t, std::size_t i) {
    for (const auto scheme : kSchemes) {
        if (t.compare(i, scheme.size(), scheme) != 0)
            continue;
        std::size_t e = i + scheme.size();
        const std::size_t body = e;
        for (;;) {
            while (e < t.size() && is_scheme_char(at_or_0(t, e)))
                ++e;
            const unsigned char open = at_or_0(t, e);
            if (e > body && (open == '(' || open == '[')) {
                std::size_t k = e + 1;
                while (k < t.size() && is_word(at_or_0(t, k)))
                    ++k;
                if (at_or_0(t, k) == ')' || at_or_0(t, k) == ']') {
                    e = k + 1;
                    continue;
                }
            }
            break;
        }
        while (e > body && (t[e - 1] == ',' || t[e - 1] == '.'))
            --e;
        return e > body ? e : 0;
    }
    return 0;
}

// rooted_path and bare_path (herdr src/links.rs:22-27), ending without a colon.
std::size_t path_at(const std::string& t, std::size_t i) {
    std::size_t p = 0;  // end of the rooted prefix
    const unsigned char c = at_or_0(t, i);
    if (t.compare(i, 3, "../") == 0) {
        p = i + 3;
    } else if (t.compare(i, 2, "./") == 0) {
        p = i + 2;
    } else if (t.compare(i, 2, "~/") == 0 && !word_before(t, i)) {
        p = i + 2;
    } else if (c == '$' && !word_before(t, i) && (std::isalpha(at_or_0(t, i + 1)) != 0 || at_or_0(t, i + 1) == '_')) {
        std::size_t k = i + 2;
        while (k < t.size() && is_word(at_or_0(t, k)))
            ++k;
        if (at_or_0(t, k) == '/')
            p = k + 1;
    } else if (c == '.' && is_word(at_or_0(t, i + 1))) {
        std::size_t k = i + 2;
        while (k < t.size() && (is_word(at_or_0(t, k)) || at_or_0(t, k) == '-' || at_or_0(t, k) == '.'))
            ++k;
        if (at_or_0(t, k) == '/')
            p = k + 1;
    } else if (c == '/' && at_or_0(t, i + 1) != '/' &&
               !(i > 0 && (is_word(static_cast<unsigned char>(t[i - 1])) || t[i - 1] == '~' || t[i - 1] == '/'))) {
        p = i + 1;
    } else if (std::isalpha(c) != 0 && at_or_0(t, i + 1) == ':' &&
               (at_or_0(t, i + 2) == '\\' || at_or_0(t, i + 2) == '/') && !word_before(t, i)) {
        p = i + 3;  // C:\ or C:/ (Windows; not in Ghostty's pattern)
    }
    if (p == 0) {
        // bare_path: word[\w\-.]*/ followed by path characters, with a dot somewhere.
        if (!is_word(c) || word_before(t, i) || (i > 0 && t[i - 1] == '$'))
            return 0;
        std::size_t k = i + 1;
        while (k < t.size() && (is_word(at_or_0(t, k)) || at_or_0(t, k) == '-' || at_or_0(t, k) == '.'))
            ++k;
        if (at_or_0(t, k) != '/' && at_or_0(t, k) != '\\')
            return 0;
        p = k + 1;
        std::size_t run = i;
        bool dotted = false;
        while (run < t.size() && is_path_char(at_or_0(t, run)))
            dotted |= t[run++] == '.';
        if (!dotted)
            return 0;
    }
    std::size_t e = p;
    while (e < t.size() && is_path_char(at_or_0(t, e)))
        ++e;
    if (e == p)
        return 0;
    while (e > p && t[e - 1] == ':')
        --e;
    return e > p ? e : 0;
}

bool path_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

fs::path to_path(const std::string& utf8) {
    try {
        return fs::path(platform::win::to_wide(utf8));
    } catch (const std::exception&) {
        return {};
    }
}

std::string from_path(const fs::path& p) {
    try {
        return platform::win::to_utf8(p.native());
    } catch (const std::exception&) {
        return {};
    }
}

std::optional<std::string> env(const char* name) {
    char* value = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr)
        return std::nullopt;
    std::string out(value);
    std::free(value);
    if (out.empty())
        return std::nullopt;
    return out;
}

bool has_file_scheme(const std::string& link) {
    constexpr std::string_view kFile = "file:";
    return link.size() >= kFile.size() &&
           std::equal(kFile.begin(), kFile.end(), link.begin(), [](char a, char b) {
               return a == static_cast<char>(std::tolower(static_cast<unsigned char>(b)));
           });
}

// The path a file: URL names on this computer, percent-decoded (herdr links::file_url_path).
// Every spelling counts (FILE:///C:/x, file:/C:/x, file://server/share/x); a host other than
// localhost names a UNC path (\\server\share\x).
std::optional<std::string> file_url_path(const std::string& link) {
    if (!has_file_scheme(link))
        return std::nullopt;
    std::string rest = link.substr(5);
    rest = rest.substr(0, rest.find_first_of("?#"));
    std::string host;
    if (rest.rfind("//", 0) == 0) {
        const auto slash = rest.find('/', 2);
        host = rest.substr(2, slash == std::string::npos ? std::string::npos : slash - 2);
        rest = slash == std::string::npos ? std::string() : rest.substr(slash);
        std::string lower = host;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](char ch) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        });
        if (lower == "localhost")
            host.clear();
        else if (host.find_first_of("\\%") != std::string::npos)
            return std::nullopt;
    }
    if (rest.empty() || rest[0] != '/')
        return std::nullopt;
    std::string bytes;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '%') {
            if (i + 2 >= rest.size() || std::isxdigit(static_cast<unsigned char>(rest[i + 1])) == 0 ||
                std::isxdigit(static_cast<unsigned char>(rest[i + 2])) == 0)
                return std::nullopt;
            bytes.push_back(static_cast<char>(std::stoi(rest.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            bytes.push_back(rest[i]);
        }
    }
    if (bytes.find('\0') != std::string::npos)
        return std::nullopt;
    std::replace(bytes.begin(), bytes.end(), '/', '\\');
    // /C:/Users/... names C:\Users\... (also after this computer's name, as OSC 8 writes it)
    if (bytes.size() > 2 && bytes[2] == ':' && std::isalpha(static_cast<unsigned char>(bytes[1])) != 0)
        return bytes.substr(1);
    if (!host.empty())
        return "\\\\" + host + bytes;  // file://server/share/x names \\server\share\x
    return bytes;
}

bool is_unc(const std::string& path) {
    return path.size() > 2 && path[0] == '\\' && path[1] == '\\';
}

}  // namespace

std::optional<std::pair<std::size_t, std::size_t>> find_link(const std::string& text, std::size_t at) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        // The leftmost match from `pos`, as a regex scan finds them.
        std::size_t start = pos;
        std::size_t end = 0;
        for (; start < text.size(); ++start) {
            end = url_at(text, start);
            if (end == 0)
                end = path_at(text, start);
            if (end != 0)
                break;
        }
        if (end == 0 || start > at)
            return std::nullopt;
        if (at < end)
            return std::make_pair(start, end);
        pos = end;
    }
    return std::nullopt;
}

std::optional<LinkTarget> resolve_link(const std::string& link, const std::string& cwd) {
    if (link.empty())
        return std::nullopt;
    const auto colon = link.find(':');
    const bool scheme = colon != std::string::npos && colon > 1 && std::isalpha(static_cast<unsigned char>(link[0])) != 0 &&
                        std::all_of(link.begin(), link.begin() + static_cast<std::ptrdiff_t>(colon), [](char ch) {
                            return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '+' || ch == '-' || ch == '.';
                        });
    if (scheme) {
        // Windows can't open a file URL whose path is percent-encoded, the way programs write them.
        // A file: URL in any spelling opens only as the file it names, so open_link's refusal to
        // run programs applies; one that names nothing here never reaches the shell.
        if (has_file_scheme(link)) {
            const auto p = file_url_path(link);
            if (!p)
                return std::nullopt;
            // A share on another computer is not checked here: hovering terminal text must not
            // connect anywhere. open_link still shows programs there in Explorer.
            if (is_unc(*p) || path_exists(to_path(*p)))
                return LinkTarget{true, *p};
            return std::nullopt;
        }
        const bool allowed = std::any_of(kSchemes.begin(), kSchemes.end(), [&](std::string_view s) {
            const std::string_view name = s.substr(0, s.find(':') + 1);
            return link.size() >= name.size() &&
                   std::equal(name.begin(), name.end(), link.begin(), [](char a, char b) {
                       return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                   });
        });
        if (!allowed)
            return std::nullopt;  // only the link schemes Ghostty recognises open
        return LinkTarget{false, link};
    }
    fs::path expanded;
    if (link.rfind("~/", 0) == 0) {
        const auto home = env("USERPROFILE");
        if (!home)
            return std::nullopt;
        expanded = to_path(*home) / to_path(link.substr(2));
    } else if (link[0] == '$') {
        const std::string var = link.substr(1);
        const auto slash = var.find('/');
        const std::string name = var.substr(0, slash);
        const std::string rest = slash == std::string::npos ? std::string() : var.substr(slash + 1);
        std::optional<std::string> base;
        if (name == "PWD")
            base = cwd.empty() ? std::nullopt : std::optional<std::string>(cwd);
        else if (name == "HOME")
            base = env("USERPROFILE");
        else
            base = env(name.c_str());
        if (!base)
            return std::nullopt;
        expanded = to_path(*base) / to_path(rest);
    } else {
        expanded = to_path(link);
    }
    if (expanded.empty())
        return std::nullopt;
    fs::path full = expanded;
    if (!full.is_absolute()) {
        if (cwd.empty())
            return std::nullopt;
        full = to_path(cwd) / expanded;
    }
    std::string full_s = from_path(full.make_preferred());
    if (full_s.empty())
        return std::nullopt;
    // Windows ignores a trailing period when checking whether a path exists.
    if (full_s.back() != '.' && path_exists(full))
        return LinkTarget{true, full_s};
    // The period ending a sentence, or :42 / :42:7 naming a place in the file.
    std::string bare = full_s;
    if (!bare.empty() && bare.back() == '.')
        bare.pop_back();
    for (int i = 0; i < 2; ++i) {
        const auto c = bare.rfind(':');
        if (c == std::string::npos || c + 1 >= bare.size() ||
            !std::all_of(bare.begin() + static_cast<std::ptrdiff_t>(c) + 1, bare.end(),
                         [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; }))
            break;
        bare.resize(c);
    }
    if (path_exists(to_path(bare)))
        return LinkTarget{true, bare};
    return std::nullopt;
}

void open_link(const LinkTarget& target) {
    std::wstring wide;
    try {
        wide = platform::win::to_wide(target.target);
    } catch (const std::exception&) {
        return;
    }
    if (wide.empty() || wide.find(L'\0') != std::wstring::npos)
        return;
    std::wstring args;
    std::wstring file = wide;
    if (target.file) {
        // A program or script is shown in Explorer, never run from a click on terminal text.
        // Windows drops trailing periods and spaces (evil.exe. runs evil.exe), and a colon past
        // the drive names a stream: neither may hide the real extension.
        std::wstring name = wide;
        while (!name.empty() && (name.back() == L'.' || name.back() == L' '))
            name.pop_back();
        if (name.find(L':', 2) != std::wstring::npos)
            return;
        std::wstring ext = fs::path(name).extension().wstring();
        for (auto& ch : ext)
            ch = static_cast<wchar_t>(std::towlower(ch));
        static constexpr std::wstring_view kRun[] = {L".exe", L".com", L".bat", L".cmd", L".ps1", L".psm1", L".vbs",
                                                     L".vbe", L".js",  L".jse", L".wsf", L".wsh", L".msi", L".msp",
                                                     L".scr", L".lnk", L".pif", L".hta", L".cpl", L".reg", L".url"};
        if (std::find(std::begin(kRun), std::end(kRun), ext) != std::end(kRun)) {
            args = L"/select,\"" + wide + L"\"";
            file = L"explorer.exe";
        }
    }
    // ShellExecuteW can pump messages: never re-enter the UI while it handles a click.
    std::thread([file = std::move(file), args = std::move(args)] {
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ShellExecuteW(nullptr, nullptr, file.c_str(), args.empty() ? nullptr : args.c_str(), nullptr, SW_SHOWNORMAL);
        if (SUCCEEDED(init))
            CoUninitialize();
    }).detach();
}

}  // namespace agentdesk::ui
