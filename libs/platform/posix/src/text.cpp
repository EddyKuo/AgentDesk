// Strict UTF-8 / UTF-32 conversion (invalid input throws instead of being replaced).
#include "agentdesk/platform/text.hpp"

#include <cstdint>
#include <stdexcept>

namespace agentdesk::platform {

std::string to_utf8(std::wstring_view text) {
    std::string out;
    out.reserve(text.size());
    for (const wchar_t wc : text) {
        const auto c = static_cast<std::uint32_t>(wc);
        if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
            throw std::range_error("invalid code point");
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

std::wstring to_wide(std::string_view utf8) {
    std::wstring out;
    out.reserve(utf8.size());
    std::size_t i = 0;
    while (i < utf8.size()) {
        const auto b = static_cast<unsigned char>(utf8[i]);
        std::uint32_t c = 0;
        std::size_t len = 0;
        if (b < 0x80) {
            c = b;
            len = 1;
        } else if ((b & 0xE0) == 0xC0) {
            c = b & 0x1F;
            len = 2;
        } else if ((b & 0xF0) == 0xE0) {
            c = b & 0x0F;
            len = 3;
        } else if ((b & 0xF8) == 0xF0) {
            c = b & 0x07;
            len = 4;
        } else {
            throw std::range_error("invalid UTF-8");
        }
        if (i + len > utf8.size())
            throw std::range_error("truncated UTF-8");
        for (std::size_t k = 1; k < len; ++k) {
            const auto cb = static_cast<unsigned char>(utf8[i + k]);
            if ((cb & 0xC0) != 0x80)
                throw std::range_error("invalid UTF-8");
            c = (c << 6) | (cb & 0x3F);
        }
        static constexpr std::uint32_t kMin[] = {0, 0, 0x80, 0x800, 0x10000};
        if (c < kMin[len] || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
            throw std::range_error("invalid UTF-8");
        out.push_back(static_cast<wchar_t>(c));
        i += len;
    }
    return out;
}

}  // namespace agentdesk::platform
