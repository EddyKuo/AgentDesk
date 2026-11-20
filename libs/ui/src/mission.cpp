// Mission Control's rules; see mission.hpp.
#include "agentdesk/ui/mission.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace agentdesk::ui::mission {

GridShape wall_grid(int count, float w, float h, float gap, float min_w, float min_h, int max_cols) {
    GridShape g;
    if (count <= 0 || w <= 0 || h <= 0)
        return g;
    int cols = count == 1 ? 1 : count <= 4 ? 2 : count <= 9 ? 3 : 4;
    cols = std::clamp(std::min(cols, count), 1, std::max(1, max_cols));
    auto width_of = [&](int c) { return (w - gap * static_cast<float>(c - 1)) / static_cast<float>(c); };
    while (cols > 1 && width_of(cols) < min_w)
        --cols;
    g.cols = cols;
    g.rows = (count + cols - 1) / cols;
    g.tile_w = std::max(1.0f, width_of(cols));
    const float fit_h = (h - gap * static_cast<float>(g.rows - 1)) / static_cast<float>(g.rows);
    if (fit_h >= min_h) {
        g.tile_h = fit_h;
    } else {
        // As many rows as fit at the minimum height share the visible height; the rest scroll.
        const int shown = std::max(1, static_cast<int>(std::floor((h + gap) / (min_h + gap))));
        g.tile_h = std::max(min_h, (h - gap * static_cast<float>(shown - 1)) / static_cast<float>(shown));
        g.scroll = true;
    }
    g.content_h = g.tile_h * static_cast<float>(g.rows) + gap * static_cast<float>(g.rows - 1);
    g.scroll = g.content_h > h + 0.5f;
    return g;
}

std::vector<std::string> wall_order(const std::vector<WallAgent>& agents, bool sub_agents) {
    std::set<std::string> present;
    for (const auto& a : agents)
        present.insert(a.id);
    auto before = [](const WallAgent* a, const WallAgent* b) {
        if (a->unreachable != b->unreachable)
            return !a->unreachable;
        return priority_before({a->rank, a->since, a->seq}, {b->rank, b->since, b->seq});
    };
    std::vector<const WallAgent*> top;
    std::map<std::string, std::vector<const WallAgent*>> kids;
    for (const auto& a : agents) {
        if (!a.parent.empty() && a.parent != a.id && present.contains(a.parent))
            kids[a.parent].push_back(&a);
        else
            top.push_back(&a);
    }
    std::stable_sort(top.begin(), top.end(), before);
    std::vector<std::string> out;
    std::set<std::string> placed;
    std::function<void(const WallAgent*)> add = [&](const WallAgent* a) {
        if (!placed.insert(a->id).second)
            return;
        out.push_back(a->id);
        if (!sub_agents)
            return;
        auto it = kids.find(a->id);
        if (it == kids.end())
            return;
        auto list = it->second;
        std::stable_sort(list.begin(), list.end(), before);
        for (const auto* k : list)
            add(k);
    };
    for (const auto* a : top)
        add(a);
    return out;
}

bool filter_matches(std::string_view f, Attention a) {
    if (f == "active")
        return a == Attention::kBlocked || a == Attention::kWorking || a == Attention::kDone;
    if (f == "blocked")
        return a == Attention::kBlocked;
    if (f == "working")
        return a == Attention::kWorking;
    if (f == "done")
        return a == Attention::kDone;
    return true;
}

std::string filter_name(std::string_view v) {
    for (const char* n : {"all", "active", "blocked", "working", "done"})
        if (v == n)
            return n;
    return "all";
}

namespace {
void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

bool blank(const terminal::Cell& c) {
    return c.chars[0] == 0 || c.chars[0] == U' ' || (c.attrs & terminal::kAttrConceal) != 0;
}

bool box_drawing(char32_t c) {
    return c >= 0x2500 && c <= 0x257F;
}

std::uint32_t rgb_of(const terminal::Rgb& c) {
    return (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) | c.b;
}
}  // namespace

std::vector<TailLine> screen_tail(const terminal::ScreenSnapshot& snap, int max_lines) {
    std::vector<TailLine> out;
    if (max_lines <= 0 || snap.rows <= 0 || snap.cols <= 0 ||
        snap.cells.size() < static_cast<std::size_t>(snap.rows) * static_cast<std::size_t>(snap.cols))
        return out;
    for (int r = snap.rows - 1; r >= 0 && static_cast<int>(out.size()) < max_lines; --r) {
        int last = -1;  // last column with something on it
        bool says_something = false;
        for (int c = 0; c < snap.cols; ++c) {
            const auto& cell = snap.at(r, c);
            if (cell.width == 0 || blank(cell))
                continue;
            last = c;
            if (!box_drawing(cell.chars[0]))
                says_something = true;
        }
        if (!says_something)
            continue;
        TailLine line;
        line.row = r;
        for (int c = 0; c <= last; ++c) {
            const auto& cell = snap.at(r, c);
            if (cell.width == 0)
                continue;  // the right half of a wide character
            const bool def = cell.default_fg;
            const int ansi = def ? -1 : cell.fg_ansi;
            const std::uint32_t rgb = def ? 0u : rgb_of(cell.fg);
            const bool bold = (cell.attrs & terminal::kAttrBold) != 0;
            const bool wide = cell.width == 2 && !blank(cell);
            const TailSegment* prev = line.segments.empty() ? nullptr : &line.segments.back();
            if (prev == nullptr || wide || prev->wide || prev->default_fg != def || prev->ansi != ansi ||
                prev->rgb != rgb || prev->bold != bold) {
                TailSegment seg;
                seg.col = c;
                seg.default_fg = def;
                seg.ansi = ansi;
                seg.rgb = rgb;
                seg.bold = bold;
                seg.wide = wide;
                line.segments.push_back(seg);
            }
            auto& seg = line.segments.back();
            if (blank(cell)) {
                seg.text += ' ';
            } else {
                for (const char32_t ch : cell.chars) {
                    if (ch == 0)
                        break;
                    append_utf8(seg.text, ch);
                }
            }
            seg.cols += std::max<int>(1, cell.width);
        }
        out.push_back(std::move(line));
    }
    std::reverse(out.begin(), out.end());
    return out;
}

int tail_line_budget(float body_h, float line_h, int max_lines) {
    if (line_h <= 0 || body_h <= 0)
        return 0;
    return std::clamp(static_cast<int>(std::floor(body_h / line_h)), 0, max_lines);
}

int move_selection(int current, int count, int cols, Nav nav) {
    if (count <= 0)
        return -1;
    cols = std::max(1, cols);
    if (current < 0 || current >= count)
        return nav == Nav::kEnd ? count - 1 : 0;
    const int last_row = (count - 1) / cols;
    switch (nav) {
        case Nav::kLeft:
            return std::max(0, current - 1);
        case Nav::kRight:
            return std::min(count - 1, current + 1);
        case Nav::kUp:
            return current >= cols ? current - cols : current;
        case Nav::kDown:
            if (current + cols < count)
                return current + cols;
            return current / cols < last_row ? count - 1 : current;
        case Nav::kHome:
            return 0;
        case Nav::kEnd:
            return count - 1;
    }
    return current;
}

std::string clean_message(std::string_view text) {
    std::string out;
    std::size_t i = 0;
    const auto n = text.size();
    auto space = [&] {
        if (!out.empty() && out.back() != ' ')
            out += ' ';
    };
    while (i < n) {
        const auto b0 = static_cast<unsigned char>(text[i]);
        std::size_t len = b0 < 0x80             ? 1
                          : (b0 & 0xE0) == 0xC0 ? 2
                          : (b0 & 0xF0) == 0xE0 ? 3
                          : (b0 & 0xF8) == 0xF0 ? 4
                                                : 0;
        char32_t cp = 0;
        bool ok = len != 0 && i + len <= n;
        if (ok) {
            cp = len == 1 ? b0 : len == 2 ? (b0 & 0x1Fu) : len == 3 ? (b0 & 0x0Fu) : (b0 & 0x07u);
            for (std::size_t k = 1; k < len; ++k) {
                const auto b = static_cast<unsigned char>(text[i + k]);
                if ((b & 0xC0) != 0x80) {
                    ok = false;
                    break;
                }
                cp = (cp << 6) | (b & 0x3Fu);
            }
        }
        if (!ok) {
            out += "\xEF\xBF\xBD";
            i += 1;
            continue;
        }
        i += len;
        if (cp == U'\n' || cp == U'\r' || cp == U'\t' || cp == U' ') {
            space();
        } else if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0) || (cp >= 0x200B && cp <= 0x200F) ||
                   (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x2069) || cp == 0xFEFF) {
            continue;
        } else {
            append_utf8(out, cp);
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

}  // namespace agentdesk::ui::mission
