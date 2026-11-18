#include "agentdesk/protocol/messages.hpp"

#include <array>

#include "agentdesk/protocol/frame.hpp"

namespace agentdesk::protocol {

namespace {

constexpr std::array<std::string_view, 10> kErrorNames = {
    "BAD_REQUEST",      "NOT_FOUND",  "PROFILE_INVALID", "SPAWN_FAILED", "NOT_CONTROLLER",
    "VERSION_MISMATCH", "OVERLOADED", "ACTIVE_SESSIONS", "AMBIGUOUS",    "INTERNAL",
};

int rgb_or_default(bool is_default, const terminal::Rgb& c) {
    return is_default ? -1 : (c.r << 16) | (c.g << 8) | c.b;
}

}  // namespace

std::string_view to_string(ErrorCode c) {
    return kErrorNames[static_cast<std::size_t>(c)];
}

std::optional<ErrorCode> parse_error_code(std::string_view s) {
    for (std::size_t i = 0; i < kErrorNames.size(); ++i) {
        if (kErrorNames[i] == s)
            return static_cast<ErrorCode>(i);
    }
    return std::nullopt;
}

ParseResult parse_message(std::string_view payload) {
    ParseResult out;
    Json j = Json::parse(payload, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        out.error = "payload is not a JSON object";
        return out;
    }
    const auto v = j.find("v");
    if (v == j.end() || !v->is_number_integer()) {
        out.error = "missing protocol version";
        return out;
    }
    if (v->get<int>() != kProtocolVersion) {
        out.error = "unsupported protocol version";
        out.version_mismatch = true;
        return out;
    }
    if (const auto ev = j.find("event"); ev != j.end()) {
        if (!ev->is_string()) {
            out.error = "event must be a string";
            return out;
        }
        Event e;
        e.event = ev->get<std::string>();
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.key() != "v" && it.key() != "event")
                e.body[it.key()] = it.value();
        }
        out.message = std::move(e);
        return out;
    }
    const auto id = j.find("id");
    if (id == j.end() || !id->is_string() || id->get<std::string>().empty() ||
        id->get<std::string>().size() > 128) {
        out.error = "id must be a non-empty string of at most 128 bytes";
        return out;
    }
    if (const auto m = j.find("method"); m != j.end()) {
        if (!m->is_string()) {
            out.error = "method must be a string";
            return out;
        }
        Request r;
        r.id = id->get<std::string>();
        r.method = m->get<std::string>();
        if (const auto p = j.find("params"); p != j.end()) {
            if (!p->is_object()) {
                out.error = "params must be an object";
                return out;
            }
            r.params = *p;
        }
        out.message = std::move(r);
        return out;
    }
    Response r;
    r.id = id->get<std::string>();
    if (const auto res = j.find("result"); res != j.end()) {
        r.result = *res;
    } else if (const auto err = j.find("error"); err != j.end() && err->is_object()) {
        const auto code = parse_error_code(err->value("code", ""));
        r.error = code.value_or(ErrorCode::kInternal);
        r.error_message = err->value("message", "");
        r.retryable = err->value("retryable", false);
    } else {
        out.error = "response needs result or error";
        return out;
    }
    out.message = std::move(r);
    return out;
}

std::string serialize(const Request& r) {
    return Json{{"v", kProtocolVersion}, {"id", r.id}, {"method", r.method}, {"params", r.params}}.dump();
}

std::string serialize(const Response& r) {
    Json j{{"v", kProtocolVersion}, {"id", r.id}};
    if (r.error) {
        j["error"] = {
            {"code", to_string(*r.error)}, {"message", r.error_message}, {"retryable", r.retryable}};
    } else {
        j["result"] = r.result.value_or(Json::object());
    }
    return j.dump();
}

std::string serialize(const Event& e) {
    Json j = e.body;
    j["v"] = kProtocolVersion;
    j["event"] = e.event;
    return j.dump();
}

Response ok(std::string id, Json result) {
    Response r;
    r.id = std::move(id);
    r.result = std::move(result);
    return r;
}

Response fail(std::string id, ErrorCode code, std::string message, bool retryable) {
    Response r;
    r.id = std::move(id);
    r.error = code;
    r.error_message = std::move(message);
    r.retryable = retryable;
    return r;
}

Json encode_cell(const terminal::Cell& c) {
    std::string text;
    for (const char32_t cp : c.chars) {
        if (cp == 0)
            break;
        terminal::append_utf8(text, cp);
    }
    // Compact array: [text, width, fg, bg, attrs], plus [fg_ansi, bg_ansi] when either is one of
    // the 16 ANSI colours (older readers ignore the extra two).
    Json a = Json::array(
        {text, c.width, rgb_or_default(c.default_fg, c.fg), rgb_or_default(c.default_bg, c.bg), c.attrs});
    if (c.fg_ansi >= 0 || c.bg_ansi >= 0) {
        a.push_back(c.fg_ansi);
        a.push_back(c.bg_ansi);
    }
    return a;
}

terminal::Cell decode_cell(const Json& j) {
    terminal::Cell c;
    const std::string text = j.at(0).get<std::string>();
    std::size_t k = 0;
    for (std::size_t i = 0; i < text.size() && k < terminal::kMaxCharsPerCell;) {
        const auto b0 = static_cast<unsigned char>(text[i]);
        std::size_t len = b0 < 0x80 ? 1 : (b0 & 0xE0) == 0xC0 ? 2 : (b0 & 0xF0) == 0xE0 ? 3 : 4;
        std::uint32_t cp = len == 1 ? b0 : len == 2 ? (b0 & 0x1Fu) : len == 3 ? (b0 & 0x0Fu) : (b0 & 0x07u);
        for (std::size_t m = 1; m < len && i + m < text.size(); ++m) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + m]) & 0x3Fu);
        }
        c.chars[k++] = static_cast<char32_t>(cp);
        i += len;
    }
    c.width = j.at(1).get<std::uint8_t>();
    const int fg = j.at(2).get<int>();
    const int bg = j.at(3).get<int>();
    c.default_fg = fg < 0;
    c.default_bg = bg < 0;
    if (fg >= 0)
        c.fg = {static_cast<std::uint8_t>(fg >> 16), static_cast<std::uint8_t>(fg >> 8),
                static_cast<std::uint8_t>(fg)};
    if (bg >= 0)
        c.bg = {static_cast<std::uint8_t>(bg >> 16), static_cast<std::uint8_t>(bg >> 8),
                static_cast<std::uint8_t>(bg)};
    c.attrs = j.at(4).get<std::uint8_t>();
    if (j.size() >= 7) {
        const int fa = j.at(5).get<int>();
        const int ba = j.at(6).get<int>();
        c.fg_ansi = static_cast<std::int8_t>(fa >= 0 && fa < 16 && !c.default_fg ? fa : -1);
        c.bg_ansi = static_cast<std::int8_t>(ba >= 0 && ba < 16 && !c.default_bg ? ba : -1);
    }
    return c;
}

Json encode_rows(const terminal::ScreenSnapshot& s, int first_row, int last_row) {
    Json rows = Json::array();
    for (int r = std::max(0, first_row); r < std::min(s.rows, last_row); ++r) {
        Json cells = Json::array();
        for (int c = 0; c < s.cols; ++c)
            cells.push_back(encode_cell(s.at(r, c)));
        Json row{{"row", r}, {"cells", std::move(cells)}};
        if (s.row_wrapped(r))
            row["wrap"] = true;  // soft-wrapped: continues on the next row
        Json links = Json::array();
        for (const auto& l : s.links) {
            if (l.row == r)
                links.push_back(Json::array({l.col0, l.col1, l.uri}));
        }
        if (!links.empty())
            row["links"] = std::move(links);
        rows.push_back(std::move(row));
    }
    return rows;
}

Json encode_snapshot_meta(const terminal::ScreenSnapshot& s) {
    return {{"generation", s.generation},
            {"seq", s.seq},
            {"rows", s.rows},
            {"cols", s.cols},
            {"cursor", {{"row", s.cursor.row}, {"col", s.cursor.col}, {"visible", s.cursor.visible}}},
            {"alt_screen", s.alt_screen},
            {"mouse", static_cast<int>(s.mouse)},
            {"title", s.title}};
}

void decode_rows_into(const Json& rows, terminal::ScreenSnapshot& s) {
    for (const auto& row : rows) {
        const int r = row.at("row").get<int>();
        if (r < 0 || r >= s.rows)
            continue;
        const auto& cells = row.at("cells");
        for (int c = 0; c < s.cols && c < static_cast<int>(cells.size()); ++c) {
            s.cells[static_cast<std::size_t>(r) * static_cast<std::size_t>(s.cols) +
                    static_cast<std::size_t>(c)] = decode_cell(cells[static_cast<std::size_t>(c)]);
        }
        if (s.wrapped.size() != static_cast<std::size_t>(s.rows))
            s.wrapped.resize(static_cast<std::size_t>(s.rows), 0);
        s.wrapped[static_cast<std::size_t>(r)] = row.value("wrap", false) ? 1 : 0;
        std::erase_if(s.links, [r](const terminal::Hyperlink& l) { return l.row == r; });
        if (const auto it = row.find("links"); it != row.end() && it->is_array()) {
            for (const auto& l : *it) {
                if (l.is_array() && l.size() == 3 && l[0].is_number_integer() && l[1].is_number_integer() &&
                    l[2].is_string())
                    s.links.push_back({r, l[0].get<int>(), l[1].get<int>(), l[2].get<std::string>()});
            }
        }
    }
}

}  // namespace agentdesk::protocol
