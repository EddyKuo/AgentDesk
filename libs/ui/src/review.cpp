// The Review layout's rules: moving between changed files, path shortening, the list/diff split.
#include "agentdesk/ui/review.hpp"

#include <algorithm>
#include <cstdint>

namespace agentdesk::ui::review {

std::string next_change(const std::vector<std::string>& paths, const std::string& current, int step) {
    if (paths.empty())
        return {};
    const auto it = std::find(paths.begin(), paths.end(), current);
    const auto n = static_cast<long long>(paths.size());
    if (it == paths.end())
        return step >= 0 ? paths.front() : paths.back();
    long long i = static_cast<long long>(it - paths.begin()) + (step >= 0 ? 1 : -1);
    i = ((i % n) + n) % n;
    return paths[static_cast<std::size_t>(i)];
}

PathParts split_path(const std::string& path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return {std::string(), path};
    return {path.substr(0, slash + 1), path.substr(slash + 1)};
}

namespace {
// Byte length of the UTF-8 character starting with `lead` (1 for a stray continuation byte).
std::size_t char_len(unsigned char lead) {
    if (lead >= 0xF0)
        return 4;
    if (lead >= 0xE0)
        return 3;
    if (lead >= 0xC0)
        return 2;
    return 1;
}

char32_t decode(std::string_view s, std::size_t i, std::size_t len) {
    const auto b = [&](std::size_t k) { return static_cast<char32_t>(static_cast<unsigned char>(s[i + k])); };
    if (i + len > s.size())
        return 0xFFFD;
    switch (len) {
        case 1:
            return b(0);
        case 2:
            return ((b(0) & 0x1F) << 6) | (b(1) & 0x3F);
        case 3:
            return ((b(0) & 0x0F) << 12) | ((b(1) & 0x3F) << 6) | (b(2) & 0x3F);
        default:
            return ((b(0) & 0x07) << 18) | ((b(1) & 0x3F) << 12) | ((b(2) & 0x3F) << 6) | (b(3) & 0x3F);
    }
}

bool wide(char32_t c) {
    return (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0x303E) || (c >= 0x3041 && c <= 0x33FF) ||
           (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xA000 && c <= 0xA4CF) ||
           (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) ||
           (c >= 0xFF00 && c <= 0xFF60) || (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x1F300 && c <= 0x1F64F) ||
           (c >= 0x1F900 && c <= 0x1F9FF) || (c >= 0x20000 && c <= 0x3FFFD);
}

// Byte offsets where each character starts, plus the end.
std::vector<std::size_t> char_starts(std::string_view s) {
    std::vector<std::size_t> at;
    for (std::size_t i = 0; i < s.size(); i += char_len(static_cast<unsigned char>(s[i])))
        at.push_back(i);
    at.push_back(s.size());
    return at;
}
}  // namespace

int display_width(std::string_view text) {
    int cols = 0;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t len = char_len(static_cast<unsigned char>(text[i]));
        cols += wide(decode(text, i, len)) ? 2 : 1;
        i += len;
    }
    return cols;
}

std::string middle_ellipsis(const std::string& text, float max,
                            const std::function<float(std::string_view)>& measure) {
    static constexpr std::string_view kEllipsis = "\xE2\x80\xA6";
    if (measure(text) <= max)
        return text;
    const std::vector<std::size_t> at = char_starts(text);
    const std::size_t chars = at.size() - 1;
    // The most characters kept (head gets the extra one) that still fit, by bisection.
    auto build = [&](std::size_t keep) {
        const std::size_t head = (keep + 1) / 2;
        const std::size_t tail = keep / 2;
        return text.substr(0, at[head]) + std::string(kEllipsis) + text.substr(at[chars - tail]);
    };
    std::size_t lo = 0;
    std::size_t hi = chars == 0 ? 0 : chars - 1;
    while (lo < hi) {
        const std::size_t mid = (lo + hi + 1) / 2;
        if (measure(build(mid)) <= max)
            lo = mid;
        else
            hi = mid - 1;
    }
    if (lo < 2)
        return std::string(kEllipsis);
    return build(lo);
}

std::string middle_ellipsis(const std::string& text, int max_cols) {
    return middle_ellipsis(text, static_cast<float>(max_cols),
                           [](std::string_view t) { return static_cast<float>(display_width(t)); });
}

Split review_split(float w, float h, float scale, float list_size) {
    Split s;
    const float handle = 6.0f * scale;
    s.side_by_side = w >= 760.0f * scale;
    if (s.side_by_side) {
        const float lo = 180.0f * scale;
        const float hi = std::max(lo, w * 0.5f);
        const float lw = std::clamp(list_size > 0 ? list_size : 260.0f * scale, lo, hi);
        s.list = {0, 0, lw, h};
        s.splitter = {lw, 0, handle, h};
        s.diff = {lw + handle, 0, std::max(0.0f, w - lw - handle), h};
    } else {
        const float lo = 96.0f * scale;
        const float hi = std::max(lo, 300.0f * scale);
        const float lh =
            std::min(std::clamp(list_size > 0 ? list_size : h * 0.35f, lo, hi), std::max(0.0f, h - handle));
        s.list = {0, 0, w, lh};
        s.splitter = {0, lh, w, handle};
        s.diff = {0, lh + handle, w, std::max(0.0f, h - lh - handle)};
    }
    return s;
}

}  // namespace agentdesk::ui::review
