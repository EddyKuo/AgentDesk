// Command-line quoting for CreateProcessW and the checks that keep batch files out.
#include "agentdesk/platform/win/command_line.hpp"

#include <windows.h>

#include <algorithm>
#include <cwctype>

namespace agentdesk::platform::win {

std::wstring quote_arg(std::wstring_view arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring(arg);
    }
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        // Backslashes before a quote are doubled and the quote itself is escaped.
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        out.push_back(c);
    }
    // Trailing backslashes are doubled so they do not escape the closing quote.
    out.append(slashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::wstring build_command_line(std::wstring_view executable, const std::vector<std::wstring>& args) {
    std::wstring line;
    // argv[0] follows simpler rules: quotes cannot appear in a path, so plain wrapping is exact.
    if (executable.find_first_of(L" \t") != std::wstring_view::npos) {
        line.append(L"\"").append(executable).append(L"\"");
    } else {
        line.append(executable);
    }
    for (const auto& arg : args) {
        line.push_back(L' ');
        line.append(quote_arg(arg));
    }
    return line;
}

bool is_batch_file(std::wstring_view path) {
    // Windows drops trailing dots and spaces from a file name, and "::$DATA" names a file's own
    // data stream: "run.bat.", "run.cmd " and "run.bat::$DATA" all start run.bat.
    for (;;) {
        if (!path.empty() && (path.back() == L'.' || path.back() == L' ')) {
            path.remove_suffix(1);
            continue;
        }
        constexpr std::wstring_view kDataStream = L"::$data";
        if (path.size() >= kDataStream.size()) {
            const auto tail = path.substr(path.size() - kDataStream.size());
            if (std::equal(tail.begin(), tail.end(), kDataStream.begin(), [](wchar_t a, wchar_t b) {
                    return static_cast<wchar_t>(std::towlower(a)) == b;
                })) {
                path.remove_suffix(kDataStream.size());
                continue;
            }
        }
        break;
    }
    const auto dot = path.find_last_of(L'.');
    if (dot == std::wstring_view::npos) {
        return false;
    }
    std::wstring ext(path.substr(dot));
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return ext == L".cmd" || ext == L".bat";
}

bool is_launchable_file(const std::wstring& path) {
    const DWORD a = ::GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

}  // namespace agentdesk::platform::win
