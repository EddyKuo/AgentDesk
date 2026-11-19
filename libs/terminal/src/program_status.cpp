#include "agentdesk/terminal/program_status.hpp"

#include <algorithm>
#include <array>
#include <charconv>

namespace agentdesk::terminal {

namespace {

constexpr std::size_t kMaxKeyBytes = 16;
constexpr std::size_t kMaxMsgEncoded = 2732;
constexpr std::size_t kMaxMsgDecoded = 2048;
constexpr std::size_t kMaxTitleEncoded = 256;
constexpr std::size_t kMaxTitleDecoded = 192;
constexpr std::size_t kMaxSegmentBytes = 32;
constexpr std::size_t kMaxIdBytes = 128;
constexpr std::size_t kMaxIdDepth = 8;

std::string_view trim(std::string_view s) {
    const auto is_space = [](char c) { return c == ' ' || c == '\t'; };
    while (!s.empty() && is_space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

bool is_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

bool valid_key(std::string_view key) {
    return !key.empty() && key.size() <= kMaxKeyBytes &&
           std::all_of(key.begin(), key.end(), [](char c) { return c >= 'a' && c <= 'z'; });
}

bool valid_value(std::string_view value) {
    return std::all_of(value.begin(), value.end(), [](char c) {
        return is_alnum(c) || c == '_' || c == '.' || c == ',' || c == '+' || c == '/' || c == '=' ||
               c == '-';
    });
}

bool valid_segment(std::string_view seg) {
    return !seg.empty() && seg.size() <= kMaxSegmentBytes && std::all_of(seg.begin(), seg.end(), [](char c) {
        return is_alnum(c) || c == '_' || c == '.' || c == '+' || c == '-';
    });
}

bool valid_id(std::string_view id) {
    if (id.size() > kMaxIdBytes) {
        return false;
    }
    std::size_t depth = 0;
    std::size_t start = 0;
    for (;;) {
        const auto slash = id.find('/', start);
        const auto seg =
            id.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        if (!valid_segment(seg) || ++depth > kMaxIdDepth) {
            return false;
        }
        if (slash == std::string_view::npos) {
            return true;
        }
        start = slash + 1;
    }
}

std::optional<StatusState> parse_state(std::string_view v) {
    if (v == "idle")
        return StatusState::kIdle;
    if (v == "working")
        return StatusState::kWorking;
    if (v == "done")
        return StatusState::kDone;
    if (v == "blocked")
        return StatusState::kBlocked;
    if (v == "error")
        return StatusState::kError;
    if (v == "clear")
        return StatusState::kClear;
    return std::nullopt;
}

StatusKind parse_kind(std::string_view v) {
    if (v == "permission")
        return StatusKind::kPermission;
    if (v == "question")
        return StatusKind::kQuestion;
    if (v == "auth")
        return StatusKind::kAuth;
    return StatusKind::kNone;  // unrecognised kinds are treated as absent
}

int base64_value(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

// Standard base64, padding optional.
std::optional<std::string> decode_base64(std::string_view in) {
    while (!in.empty() && in.back() == '=') {
        in.remove_suffix(1);
    }
    if (in.size() % 4 == 1) {
        return std::nullopt;
    }
    std::string out;
    out.reserve(in.size() * 3 / 4);
    std::uint32_t acc = 0;
    int bits = 0;
    for (const char c : in) {
        const int v = base64_value(c);
        if (v < 0) {
            return std::nullopt;
        }
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFFu));
        }
    }
    return out;
}

// Valid UTF-8 without C0, DEL or C1 control characters.
bool is_clean_utf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0;
        std::size_t len = 0;
        if (b0 < 0x80) {
            cp = b0;
            len = 1;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1Fu;
            len = 2;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0Fu;
            len = 3;
        } else if ((b0 & 0xF8) == 0xF0) {
            cp = b0 & 0x07u;
            len = 4;
        } else {
            return false;
        }
        if (i + len > s.size()) {
            return false;
        }
        for (std::size_t k = 1; k < len; ++k) {
            const auto b = static_cast<unsigned char>(s[i + k]);
            if ((b & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (b & 0x3Fu);
        }
        static constexpr std::array<std::uint32_t, 5> kMin = {0, 0, 0x80, 0x800, 0x10000};
        if (cp < kMin[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;  // overlong, out of range or surrogate
        }
        if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F)) {
            return false;
        }
        i += len;
    }
    return true;
}

std::optional<std::string> decode_text(std::string_view encoded, std::size_t max_encoded,
                                       std::size_t max_decoded) {
    if (encoded.size() > max_encoded) {
        return std::nullopt;
    }
    auto decoded = decode_base64(encoded);
    if (!decoded || decoded->size() > max_decoded || !is_clean_utf8(*decoded)) {
        return std::nullopt;
    }
    return decoded;
}

}  // namespace

std::optional<Osc7501Message> parse_osc7501(std::string_view body) {
    if (body.size() > kMaxBodyBytes) {
        return std::nullopt;
    }
    if (trim(body) == "?") {
        return StatusQuery{};
    }

    // Repeated keys keep their last value; malformed pairs are skipped.
    std::map<std::string_view, std::string_view> pairs;
    std::size_t start = 0;
    while (start <= body.size()) {
        const auto colon = body.find(':', start);
        const auto pair =
            body.substr(start, colon == std::string_view::npos ? std::string_view::npos : colon - start);
        const auto eq = pair.find('=');
        if (eq != std::string_view::npos) {
            const auto key = trim(pair.substr(0, eq));
            const auto value = trim(pair.substr(eq + 1));
            if (valid_key(key) && valid_value(value)) {
                pairs[key] = value;
            }
        }
        if (colon == std::string_view::npos) {
            break;
        }
        start = colon + 1;
    }

    const auto state_it = pairs.find("state");
    if (state_it == pairs.end()) {
        return std::nullopt;
    }
    const auto state = parse_state(state_it->second);
    if (!state) {
        return std::nullopt;
    }

    StatusReport report;
    report.state = *state;

    if (const auto it = pairs.find("id"); it != pairs.end()) {
        if (!valid_id(it->second)) {
            return std::nullopt;
        }
        report.id = std::string(it->second);
    }
    if (const auto it = pairs.find("kind"); it != pairs.end() && report.state == StatusState::kBlocked) {
        report.kind = parse_kind(it->second);
    }
    if (const auto it = pairs.find("progress");
        it != pairs.end() &&
        (report.state == StatusState::kWorking || report.state == StatusState::kBlocked)) {
        unsigned value = 0;
        const auto* first = it->second.data();
        const auto* last = first + it->second.size();
        const auto [ptr, ec] = std::from_chars(first, last, value);
        if (ec == std::errc{} && ptr == last && value <= 100) {
            report.progress = static_cast<std::uint8_t>(value);
        }  // invalid progress is treated as absent
    }
    if (const auto it = pairs.find("app"); it != pairs.end() && !it->second.empty()) {
        if (!valid_segment(it->second)) {
            return std::nullopt;
        }
        report.app = std::string(it->second);
    }
    if (const auto it = pairs.find("title"); it != pairs.end()) {
        auto text = decode_text(it->second, kMaxTitleEncoded, kMaxTitleDecoded);
        if (!text) {
            return std::nullopt;
        }
        report.title = std::move(*text);
    }
    if (const auto it = pairs.find("msg"); it != pairs.end()) {
        auto text = decode_text(it->second, kMaxMsgEncoded, kMaxMsgDecoded);
        if (!text) {
            return std::nullopt;
        }
        report.msg = std::move(*text);
    }
    return report;
}

std::string osc7501_query_reply() {
    return "\x1b]7501;?\x1b\\";
}

std::string_view to_string(StatusState state) {
    switch (state) {
        case StatusState::kIdle:
            return "idle";
        case StatusState::kWorking:
            return "working";
        case StatusState::kDone:
            return "done";
        case StatusState::kBlocked:
            return "blocked";
        case StatusState::kError:
            return "error";
        case StatusState::kClear:
            return "clear";
    }
    return "unknown";
}

std::string_view to_string(StatusKind kind) {
    switch (kind) {
        case StatusKind::kNone:
            return "";
        case StatusKind::kPermission:
            return "permission";
        case StatusKind::kQuestion:
            return "question";
        case StatusKind::kAuth:
            return "auth";
    }
    return "";
}

void ProgramStatusStore::apply(const StatusReport& report) {
    if (report.state == StatusState::kClear) {
        if (report.id.empty()) {
            records_.clear();
            return;
        }
        const std::string prefix = report.id + "/";
        std::erase_if(records_,
                      [&](const auto& kv) { return kv.first == report.id || kv.first.starts_with(prefix); });
        return;
    }
    if (!records_.contains(report.id) && records_.size() >= kMaxRecords) {
        const auto oldest = std::min_element(
            records_.begin(), records_.end(),
            [](const auto& a, const auto& b) { return a.second.updated_order < b.second.updated_order; });
        records_.erase(oldest);
    }
    records_[report.id] = StatusRecord{report, ++counter_};
}

void ProgramStatusStore::drop_transient() {
    std::erase_if(records_, [](const auto& kv) {
        const auto s = kv.second.report.state;
        return s == StatusState::kWorking || s == StatusState::kBlocked;
    });
}

void ProgramStatusStore::on_process_exit() {
    drop_transient();
}

void ProgramStatusStore::on_shell_prompt() {
    // The shell shows its prompt again: whatever reported before it has ended, so its status goes
    // too (done and idle included). The person's rule (2026-11-19, design doc 3.4): an agent that
    // exits leaves no status behind.
    records_.clear();
}

void ProgramStatusStore::reset() {
    records_.clear();
}

const StatusRecord* ProgramStatusStore::root() const {
    const auto it = records_.find(std::string());
    return it == records_.end() ? nullptr : &it->second;
}

std::string ProgramStatusStore::effective_app(const std::string& id) const {
    std::string current = id;
    for (;;) {
        if (const auto it = records_.find(current); it != records_.end() && !it->second.report.app.empty()) {
            return it->second.report.app;
        }
        if (current.empty()) {
            return {};
        }
        const auto slash = current.rfind('/');
        current = slash == std::string::npos ? std::string() : current.substr(0, slash);
    }
}

}  // namespace agentdesk::terminal
