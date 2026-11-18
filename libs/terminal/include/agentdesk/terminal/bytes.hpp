#pragma once

// Little-endian field-by-field encoding for agent-host's handover (design doc 7.4 OQ-09). A
// reader that runs past the end or meets a bad length marks itself failed and returns zeros; the
// caller checks ok() once at the end and refuses the whole handover.

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace agentdesk::terminal {

class ByteWriter {
public:
    void u8(std::uint8_t v) { out_.push_back(static_cast<char>(v)); }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            out_.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
    }
    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i)
            out_.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
    }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
    void boolean(bool v) { u8(v ? 1 : 0); }
    void str(std::string_view s) {
        u64(s.size());
        out_.append(s);
    }
    [[nodiscard]] const std::string& data() const noexcept { return out_; }
    std::string take() { return std::move(out_); }

private:
    std::string out_;
};

class ByteReader {
public:
    explicit ByteReader(std::string_view in) : in_(in) {}
    std::uint8_t u8() {
        if (!need(1))
            return 0;
        const auto v = static_cast<std::uint8_t>(in_[0]);
        in_.remove_prefix(1);
        return v;
    }
    std::uint32_t u32() {
        if (!need(4))
            return 0;
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(static_cast<unsigned char>(in_[static_cast<std::size_t>(i)])) << (8 * i);
        in_.remove_prefix(4);
        return v;
    }
    std::uint64_t u64() {
        if (!need(8))
            return 0;
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= static_cast<std::uint64_t>(static_cast<unsigned char>(in_[static_cast<std::size_t>(i)])) << (8 * i);
        in_.remove_prefix(8);
        return v;
    }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
    bool boolean() { return u8() != 0; }
    std::string str() {
        const std::uint64_t n = u64();
        if (!need(n))
            return {};
        std::string s(in_.substr(0, static_cast<std::size_t>(n)));
        in_.remove_prefix(static_cast<std::size_t>(n));
        return s;
    }
    // A count of items each at least `min_item` bytes long: a larger count than the rest of the
    // data can hold is corrupt (and must not reserve memory for it).
    std::uint64_t count(std::size_t min_item) {
        const std::uint64_t n = u64();
        if (min_item > 0 && n > in_.size() / min_item)
            failed_ = true;
        return failed_ ? 0 : n;
    }
    void fail() noexcept { failed_ = true; }
    [[nodiscard]] bool ok() const noexcept { return !failed_; }
    [[nodiscard]] bool at_end() const noexcept { return in_.empty(); }

private:
    bool need(std::uint64_t n) {
        if (failed_ || in_.size() < n) {
            failed_ = true;
            return false;
        }
        return true;
    }
    std::string_view in_;
    bool failed_ = false;
};

}  // namespace agentdesk::terminal
