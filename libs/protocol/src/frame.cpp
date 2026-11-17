#include "agentdesk/protocol/frame.hpp"

#include <stdexcept>

namespace agentdesk::protocol {

std::string encode_frame(std::string_view payload) {
    if (payload.empty() || payload.size() > kMaxFrameBytes) {
        throw std::length_error("frame payload must be 1..4 MiB");
    }
    const auto n = static_cast<std::uint32_t>(payload.size());
    std::string out;
    out.reserve(4 + payload.size());
    out.push_back(static_cast<char>(n & 0xFF));
    out.push_back(static_cast<char>((n >> 8) & 0xFF));
    out.push_back(static_cast<char>((n >> 16) & 0xFF));
    out.push_back(static_cast<char>((n >> 24) & 0xFF));
    out.append(payload);
    return out;
}

FrameError FrameDecoder::feed(std::string_view bytes, std::vector<std::string>& out) {
    if (error_ != FrameError::kNone)
        return error_;
    buffer_.append(bytes);
    std::size_t pos = 0;
    while (buffer_.size() - pos >= 4) {
        const auto b = [&](std::size_t i) {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(buffer_[pos + i]));
        };
        const std::uint32_t len = b(0) | (b(1) << 8) | (b(2) << 16) | (b(3) << 24);
        if (len == 0)
            return error_ = FrameError::kZeroLength;
        if (len > kMaxFrameBytes)
            return error_ = FrameError::kTooLarge;  // never buffer it
        if (buffer_.size() - pos - 4 < len)
            break;
        std::string payload = buffer_.substr(pos + 4, len);
        if (!is_valid_utf8(payload))
            return error_ = FrameError::kInvalidUtf8;
        out.push_back(std::move(payload));
        pos += 4 + len;
    }
    buffer_.erase(0, pos);
    return FrameError::kNone;
}

bool is_valid_utf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t len = 0;
        std::uint32_t cp = 0;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0) {
            len = 2;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            cp = c & 0x07u;
        } else {
            return false;
        }
        if (i + len > s.size())
            return false;
        for (std::size_t k = 1; k < len; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) ||
            cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += len;
    }
    return true;
}

std::string_view to_string(FrameError e) {
    switch (e) {
        case FrameError::kNone:
            return "none";
        case FrameError::kZeroLength:
            return "zero-length frame";
        case FrameError::kTooLarge:
            return "frame larger than 4 MiB";
        case FrameError::kInvalidUtf8:
            return "payload is not valid UTF-8";
    }
    return "unknown";
}

}  // namespace agentdesk::protocol
