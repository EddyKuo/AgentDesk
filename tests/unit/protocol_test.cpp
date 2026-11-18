#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "agentdesk/protocol/frame.hpp"
#include "agentdesk/protocol/messages.hpp"

using namespace agentdesk::protocol;

TEST(Frame, RoundTripsAcrossArbitraryChunking) {
    const std::vector<std::string> payloads = {R"({"v":1,"id":"a","method":"hello"})", "{\"x\":\"中文\"}",
                                               std::string(70000, 'z')};
    std::string wire;
    for (const auto& p : payloads)
        wire += encode_frame(p);
    std::mt19937 rng(42);
    for (int round = 0; round < 50; ++round) {
        FrameDecoder d;
        std::vector<std::string> out;
        std::size_t pos = 0;
        while (pos < wire.size()) {
            const std::size_t n = 1 + rng() % 5000;
            ASSERT_EQ(d.feed(std::string_view(wire).substr(pos, n), out), FrameError::kNone);
            pos += n;
        }
        ASSERT_EQ(out, payloads);
        EXPECT_EQ(d.buffered(), 0u);
    }
}

// QA-17: zero-length, oversized and invalid UTF-8 frames are protocol errors (connection dropped).
TEST(Frame, RejectsZeroOversizedAndInvalidUtf8) {
    std::vector<std::string> out;
    {
        FrameDecoder d;
        EXPECT_EQ(d.feed(std::string("\0\0\0\0", 4), out), FrameError::kZeroLength);
        EXPECT_EQ(d.feed("anything", out), FrameError::kZeroLength);  // sticky
    }
    {
        FrameDecoder d;
        const std::uint32_t big = kMaxFrameBytes + 1;
        std::string hdr = {static_cast<char>(big & 0xFF), static_cast<char>((big >> 8) & 0xFF),
                           static_cast<char>((big >> 16) & 0xFF), static_cast<char>(big >> 24)};
        EXPECT_EQ(d.feed(hdr, out), FrameError::kTooLarge);
        EXPECT_EQ(d.buffered(), 4u);  // nothing of the oversized body is buffered
    }
    {
        FrameDecoder d;
        EXPECT_EQ(d.feed(encode_frame("\xC3\x28"), out), FrameError::kInvalidUtf8);
    }
    EXPECT_THROW(encode_frame(""), std::length_error);
    EXPECT_TRUE(out.empty());
}

TEST(Messages, ParsesRequestResponseEvent) {
    auto r = parse_message(R"({"v":1,"id":"r-001","method":"session.create","params":{"name":"review"}})");
    ASSERT_TRUE(r.message);
    const auto& req = std::get<Request>(*r.message);
    EXPECT_EQ(req.method, "session.create");
    EXPECT_EQ(req.params["name"], "review");

    r = parse_message(R"({"v":1,"id":"r-001","error":{"code":"NOT_FOUND","message":"x","retryable":false}})");
    ASSERT_TRUE(r.message);
    EXPECT_EQ(std::get<Response>(*r.message).error, ErrorCode::kNotFound);

    r = parse_message(R"({"v":1,"event":"screen.delta","session_id":"s","seq":42})");
    ASSERT_TRUE(r.message);
    EXPECT_EQ(std::get<Event>(*r.message).body["seq"], 42);
}

TEST(Messages, RejectsBadEnvelopesAndUnknownVersion) {
    EXPECT_FALSE(parse_message("[]").message);
    EXPECT_FALSE(parse_message(R"({"id":"a","method":"x"})").message);       // no version
    EXPECT_FALSE(parse_message(R"({"v":1,"id":"","method":"x"})").message);  // empty id
    EXPECT_FALSE(parse_message(R"({"v":1,"id":"a","method":"x","params":3})").message);
    const auto vm = parse_message(R"({"v":2,"id":"a","method":"hello"})");
    EXPECT_FALSE(vm.message);
    EXPECT_TRUE(vm.version_mismatch);
}

TEST(Messages, SerializeRoundTrip) {
    const auto text = serialize(fail("r-9", ErrorCode::kActiveSessions, "2 sessions running"));
    const auto parsed = parse_message(text);
    ASSERT_TRUE(parsed.message);
    const auto& resp = std::get<Response>(*parsed.message);
    EXPECT_EQ(resp.error, ErrorCode::kActiveSessions);
    EXPECT_EQ(resp.error_message, "2 sessions running");
}

TEST(Messages, ScreenRowsRoundTrip) {
    agentdesk::terminal::VtEngineOptions o;
    o.rows = 4;
    o.cols = 12;
    agentdesk::terminal::VtEngine e(o);
    e.feed("\x1b[1;38;2;10;20;30m中x\x1b[0m e\xCC\x81");
    const auto snap = e.snapshot();
    agentdesk::terminal::ScreenSnapshot copy;
    copy.rows = snap.rows;
    copy.cols = snap.cols;
    copy.cells.resize(snap.cells.size());
    decode_rows_into(encode_rows(snap, 0, snap.rows), copy);
    EXPECT_EQ(copy.cells, snap.cells);
}

// Soft-wrap flags and OSC 8 links travel with their rows (copy joins wrapped rows in the UI).
TEST(Messages, ScreenRowsCarryWrapAndLinks) {
    agentdesk::terminal::VtEngineOptions o;
    o.rows = 4;
    o.cols = 10;
    agentdesk::terminal::VtEngine e(o);
    e.feed("0123456789abc\r\n\x1b]8;;https://example.com\x1b\\link\x1b]8;;\x1b\\ tail");
    const auto snap = e.snapshot();
    ASSERT_TRUE(snap.row_wrapped(0));
    ASSERT_FALSE(snap.row_wrapped(1));
    ASSERT_EQ(snap.links.size(), 1u);
    agentdesk::terminal::ScreenSnapshot copy;
    copy.rows = snap.rows;
    copy.cols = snap.cols;
    copy.cells.resize(snap.cells.size());
    decode_rows_into(encode_rows(snap, 0, snap.rows), copy);
    EXPECT_EQ(copy.wrapped, snap.wrapped);
    EXPECT_EQ(copy.links, snap.links);
}

TEST(Messages, CellsCarryTheirAnsiIndexOnlyWhenTheyHaveOne) {
    agentdesk::terminal::Cell c;
    c.chars[0] = U'a';
    EXPECT_EQ(encode_cell(c).size(), 5u);  // the old compact form
    c.default_fg = false;
    c.fg = {170, 0, 0};
    c.fg_ansi = 1;
    const Json j = encode_cell(c);
    EXPECT_EQ(j.size(), 7u);
    const auto back = decode_cell(j);
    EXPECT_EQ(back, c);
    // An old 5-element cell still decodes, with no index.
    Json old = j;
    old.erase(old.begin() + 5, old.end());
    EXPECT_EQ(decode_cell(old).fg_ansi, -1);
}
