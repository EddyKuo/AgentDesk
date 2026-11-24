// Strict UTF-8 / UTF-16 conversion (invalid input throws instead of being replaced).
#include "agentdesk/platform/text.hpp"

#include <limits>
#include <stdexcept>

#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::platform {

using win::throw_last_error;

std::string to_utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("to_utf8: input too large");
    }
    const int in_len = static_cast<int>(text.size());
    const int out_len = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), in_len, nullptr, 0,
                                              nullptr, nullptr);
    if (out_len <= 0) {
        throw_last_error("WideCharToMultiByte");
    }
    std::string out(static_cast<size_t>(out_len), '\0');
    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), in_len, out.data(), out_len,
                              nullptr, nullptr) != out_len) {
        throw_last_error("WideCharToMultiByte");
    }
    return out;
}

std::wstring to_wide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    if (utf8.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("to_wide: input too large");
    }
    const int in_len = static_cast<int>(utf8.size());
    const int out_len = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), in_len, nullptr, 0);
    if (out_len <= 0) {
        throw_last_error("MultiByteToWideChar");
    }
    std::wstring out(static_cast<size_t>(out_len), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), in_len, out.data(), out_len) !=
        out_len) {
        throw_last_error("MultiByteToWideChar");
    }
    return out;
}

}  // namespace agentdesk::platform
