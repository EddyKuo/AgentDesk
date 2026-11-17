#pragma once

// IPC framing v1 (design doc 3.7): frame = 4-byte little-endian payload length + UTF-8 JSON.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace agentdesk::protocol {

inline constexpr std::uint32_t kMaxFrameBytes = 4u * 1024u * 1024u;  // per frame payload
inline constexpr int kProtocolVersion = 1;

enum class FrameError {
    kNone,
    kZeroLength,
    kTooLarge,
    kInvalidUtf8,
};

std::string encode_frame(std::string_view payload);  // throws std::length_error when too large

// Streaming decoder: feed arbitrary chunks, collect complete payloads in order. After an error
// the connection must be dropped (no resynchronisation is attempted).
class FrameDecoder {
public:
    // Returns kNone on success; complete payloads are appended to `out`.
    FrameError feed(std::string_view bytes, std::vector<std::string>& out);
    [[nodiscard]] std::size_t buffered() const noexcept { return buffer_.size(); }

private:
    std::string buffer_;
    FrameError error_ = FrameError::kNone;
};

bool is_valid_utf8(std::string_view s);
std::string_view to_string(FrameError e);

}  // namespace agentdesk::protocol
