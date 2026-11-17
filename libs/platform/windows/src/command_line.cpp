#include "agentdesk/platform/win/command_line.hpp"

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
    const auto dot = path.find_last_of(L'.');
    if (dot == std::wstring_view::npos) {
        return false;
    }
    std::wstring ext(path.substr(dot));
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return ext == L".cmd" || ext == L".bat";
}

}  // namespace agentdesk::platform::win
