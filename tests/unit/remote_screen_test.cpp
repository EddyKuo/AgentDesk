// RemoteScreen: rebuilding a terminal screen from snapshot and delta events.
#include "agentdesk/ui/remote_screen.hpp"

#include <gtest/gtest.h>

#include "agentdesk/protocol/messages.hpp"
#include "agentdesk/terminal/vt_engine.hpp"

using agentdesk::terminal::ScreenSnapshot;
using agentdesk::terminal::VtEngine;
using agentdesk::terminal::VtEngineOptions;
using agentdesk::ui::DeltaResult;
using agentdesk::ui::RemoteScreen;
using Json = nlohmann::json;
namespace protocol = agentdesk::protocol;

namespace {

VtEngine make_engine() {
    VtEngineOptions o;
    o.rows = 6;
    o.cols = 20;
    return VtEngine(o);
}

Json snapshot_chunk(const ScreenSnapshot& s, const std::string& id, int index, int count, int first,
                    int last) {
    return {{"snapshot_id", id},
            {"chunk_index", index},
            {"chunk_count", count},
            {"meta", protocol::encode_snapshot_meta(s)},
            {"rows", protocol::encode_rows(s, first, last)}};
}

Json delta(const ScreenSnapshot& from, const ScreenSnapshot& to) {
    return {{"generation", to.generation},
            {"base_seq", from.seq},
            {"seq", to.seq},
            {"meta", protocol::encode_snapshot_meta(to)},
            {"rows", protocol::encode_rows(to, 0, to.rows)}};
}

}  // namespace

TEST(RemoteScreen, ChunkedSnapshotSwitchesOnlyWhenComplete) {
    auto e = make_engine();
    e.feed("hello\r\nworld");
    const auto s = e.snapshot();
    RemoteScreen rs;
    EXPECT_FALSE(rs.apply_snapshot_chunk(snapshot_chunk(s, "a", 0, 2, 0, 3)));
    EXPECT_EQ(rs.current(), nullptr);  // partial snapshots are never shown
    EXPECT_TRUE(rs.apply_snapshot_chunk(snapshot_chunk(s, "a", 1, 2, 3, 6)));
    ASSERT_NE(rs.current(), nullptr);
    EXPECT_EQ(rs.current()->cells, s.cells);
    EXPECT_EQ(rs.current()->row_text(1), "world");
    EXPECT_FALSE(rs.needs_snapshot());
}

TEST(RemoteScreen, ContiguousDeltasApplyAndGapsRequireSnapshot) {
    auto e = make_engine();
    const auto s0 = e.snapshot();
    RemoteScreen rs;
    rs.apply_snapshot_chunk(snapshot_chunk(s0, "a", 0, 1, 0, 6));
    e.feed("one");
    const auto s1 = e.snapshot();
    EXPECT_EQ(rs.apply_delta(delta(s0, s1)), DeltaResult::kApplied);
    EXPECT_EQ(rs.current()->row_text(0), "one");
    EXPECT_EQ(rs.apply_delta(delta(s0, s1)), DeltaResult::kIgnoredStale);  // replay
    e.feed(" two");
    const auto s2 = e.snapshot();
    e.feed(" three");
    const auto s3 = e.snapshot();
    EXPECT_EQ(rs.apply_delta(delta(s2, s3)), DeltaResult::kNeedSnapshot);  // s1->s2 missing
    EXPECT_TRUE(rs.needs_snapshot());
    EXPECT_EQ(rs.apply_delta(delta(s1, s2)), DeltaResult::kNeedSnapshot);  // stays blocked until resync
    EXPECT_TRUE(rs.apply_snapshot_chunk(snapshot_chunk(s3, "b", 0, 1, 0, 6)));
    EXPECT_EQ(rs.current()->row_text(0), "one two three");
}

TEST(RemoteScreen, GenerationChangeRequiresSnapshot) {
    auto e = make_engine();
    const auto s0 = e.snapshot();
    RemoteScreen rs;
    rs.apply_snapshot_chunk(snapshot_chunk(s0, "a", 0, 1, 0, 6));
    e.resize(8, 30);
    const auto s1 = e.snapshot();
    EXPECT_EQ(rs.apply_delta(delta(s0, s1)), DeltaResult::kNeedSnapshot);
}

// A snapshot that loses a chunk never completes: its assembly is dropped when the next snapshot
// starts or completes, or when the client asks for a fresh one.
TEST(RemoteScreen, AbandonedSnapshotAssembliesAreFreed) {
    auto e = make_engine();
    e.feed("hello");
    const auto s = e.snapshot();
    RemoteScreen rs;
    EXPECT_FALSE(rs.apply_snapshot_chunk(snapshot_chunk(s, "a", 0, 2, 0, 3)));
    EXPECT_EQ(rs.pending_count(), 1u);
    rs.mark_needs_snapshot();
    EXPECT_EQ(rs.pending_count(), 0u);
    EXPECT_FALSE(rs.apply_snapshot_chunk(snapshot_chunk(s, "b", 0, 2, 0, 3)));
    EXPECT_TRUE(rs.apply_snapshot_chunk(snapshot_chunk(s, "c", 0, 1, 0, 6)));
    EXPECT_EQ(rs.pending_count(), 0u);
    EXPECT_EQ(rs.current()->row_text(0), "hello");
}

// Malformed IPC data throws a json exception (the client catches it and asks for a snapshot);
// it never leaves a half-built snapshot behind or changes the current screen.
TEST(RemoteScreen, MalformedEventsThrowWithoutChangingTheScreen) {
    auto e = make_engine();
    e.feed("hello");
    const auto s0 = e.snapshot();
    RemoteScreen rs;
    ASSERT_TRUE(rs.apply_snapshot_chunk(snapshot_chunk(s0, "a", 0, 1, 0, 6)));
    auto bad = snapshot_chunk(s0, "b", 0, 1, 0, 6);
    bad["meta"]["rows"] = -5;
    EXPECT_THROW(rs.apply_snapshot_chunk(bad), nlohmann::json::exception);
    bad = snapshot_chunk(s0, "b", 0, 1, 0, 6);
    bad["rows"] = Json::array({Json{{"row", "zero"}}});
    EXPECT_THROW(rs.apply_snapshot_chunk(bad), nlohmann::json::exception);
    EXPECT_EQ(rs.pending_count(), 0u);
    bad = snapshot_chunk(s0, "b", 0, 1, 0, 6);
    bad.erase("meta");
    EXPECT_THROW(rs.apply_snapshot_chunk(bad), nlohmann::json::exception);
    e.feed(" world");
    auto d = delta(s0, e.snapshot());
    d["rows"] = "nope";
    EXPECT_THROW(rs.apply_delta(d), nlohmann::json::exception);
    EXPECT_EQ(rs.current()->row_text(0), "hello");
}
