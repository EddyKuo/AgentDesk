#include "agentdesk/terminal/program_status.hpp"

#include <string>

#include <gtest/gtest.h>

using agentdesk::terminal::Osc7501Message;
using agentdesk::terminal::parse_osc7501;
using agentdesk::terminal::ProgramStatusStore;
using agentdesk::terminal::StatusKind;
using agentdesk::terminal::StatusQuery;
using agentdesk::terminal::StatusReport;
using agentdesk::terminal::StatusState;

namespace {

StatusReport report_of(std::string_view body) {
    const auto msg = parse_osc7501(body);
    EXPECT_TRUE(msg.has_value()) << body;
    if (!msg || !std::holds_alternative<StatusReport>(*msg)) {
        return {};
    }
    return std::get<StatusReport>(*msg);
}

StatusReport make(StatusState state, std::string id = {}) {
    StatusReport r;
    r.state = state;
    r.id = std::move(id);
    return r;
}

}  // namespace

TEST(Osc7501Parse, QueryIsRecognised) {
    const auto msg = parse_osc7501("?");
    ASSERT_TRUE(msg.has_value());
    EXPECT_TRUE(std::holds_alternative<StatusQuery>(*msg));
}

TEST(Osc7501Parse, FullReportDecodesAllKeys) {
    // title "Build" and msg "need approval" in base64
    const auto r = report_of(
        "state=blocked:id=build/test:kind=permission:progress=40:app=cargo:"
        "title=QnVpbGQ=:msg=bmVlZCBhcHByb3ZhbA==");
    EXPECT_EQ(r.state, StatusState::kBlocked);
    EXPECT_EQ(r.id, "build/test");
    EXPECT_EQ(r.kind, StatusKind::kPermission);
    ASSERT_TRUE(r.progress.has_value());
    EXPECT_EQ(*r.progress, 40);
    EXPECT_EQ(r.app, "cargo");
    EXPECT_EQ(r.title, "Build");
    EXPECT_EQ(r.msg, "need approval");
}

TEST(Osc7501Parse, UnknownStateDiscardsReport) {
    EXPECT_FALSE(parse_osc7501("state=sleeping").has_value());
    EXPECT_FALSE(parse_osc7501("app=x").has_value());  // state is required
}

TEST(Osc7501Parse, MalformedPairsAreSkippedAndLastKeyWins) {
    const auto r = report_of(" state = idle :garbage:UPPER=1:state=working:x=%%");
    EXPECT_EQ(r.state, StatusState::kWorking);
}

TEST(Osc7501Parse, KindOnlyAppliesToBlocked) {
    EXPECT_EQ(report_of("state=working:kind=permission").kind, StatusKind::kNone);
    EXPECT_EQ(report_of("state=blocked:kind=unknown").kind, StatusKind::kNone);
}

TEST(Osc7501Parse, InvalidProgressIsTreatedAsAbsent) {
    EXPECT_FALSE(report_of("state=working:progress=101").progress.has_value());
    EXPECT_FALSE(report_of("state=working:progress=abc").progress.has_value());
    EXPECT_FALSE(report_of("state=done:progress=50").progress.has_value());
}

TEST(Osc7501Parse, MalformedIdDiscardsReport) {
    EXPECT_FALSE(parse_osc7501("state=idle:id=a//b").has_value());
    EXPECT_FALSE(parse_osc7501("state=idle:id=" + std::string(33, 'a')).has_value());
    EXPECT_FALSE(parse_osc7501("state=idle:id=a/b/c/d/e/f/g/h/i").has_value());  // 9 levels
    EXPECT_TRUE(parse_osc7501("state=idle:id=a/b/c/d/e/f/g/h").has_value());     // 8 levels
}

TEST(Osc7501Parse, ControlCharactersInDecodedTextDiscardReport) {
    // "a\nb" -> YQpi ; "\x1b[31m" -> G1szMW0=
    EXPECT_FALSE(parse_osc7501("state=idle:msg=YQpi").has_value());
    EXPECT_FALSE(parse_osc7501("state=idle:title=G1szMW0=").has_value());
    // valid value charset but impossible base64 length: the whole report is discarded
    EXPECT_FALSE(parse_osc7501("state=idle:msg=a").has_value());
    // characters outside the value charset make the pair malformed, so it is skipped
    EXPECT_TRUE(parse_osc7501("state=idle:msg=@@@@").has_value());
}

TEST(Osc7501Parse, Utf8TextIsAccepted) {
    // "需要核准" base64
    EXPECT_EQ(report_of("state=blocked:msg=6ZyA6KaB5qC45YeG").msg, "需要核准");
}

TEST(Osc7501Parse, OversizedBodyIsDiscarded) {
    const std::string body = "state=idle:app=a:x=" + std::string(agentdesk::terminal::kMaxBodyBytes, 'a');
    EXPECT_FALSE(parse_osc7501(body).has_value());
}

TEST(ProgramStatusStore, ReportReplacesRecordAndClearRemovesDescendants) {
    ProgramStatusStore store;
    store.apply(make(StatusState::kWorking));
    store.apply(make(StatusState::kWorking, "build"));
    store.apply(make(StatusState::kWorking, "build/test"));
    store.apply(make(StatusState::kWorking, "buildx"));
    store.apply(make(StatusState::kClear, "build"));
    EXPECT_EQ(store.records().size(), 2u);  // root + buildx
    ASSERT_NE(store.root(), nullptr);
    store.apply(make(StatusState::kClear));
    EXPECT_TRUE(store.records().empty());
}

TEST(ProgramStatusStore, ProcessExitDropsWorkingAndBlockedKeepsDone) {
    ProgramStatusStore store;
    store.apply(make(StatusState::kBlocked));
    store.apply(make(StatusState::kWorking, "a"));
    store.apply(make(StatusState::kDone, "b"));
    store.apply(make(StatusState::kError, "c"));
    store.on_process_exit();
    EXPECT_EQ(store.root(), nullptr);
    EXPECT_EQ(store.records().size(), 2u);
}

TEST(ProgramStatusStore, EvictsLeastRecentlyUpdatedBeyondLimit) {
    ProgramStatusStore store;
    for (std::size_t i = 0; i < agentdesk::terminal::kMaxRecords; ++i) {
        store.apply(make(StatusState::kIdle, "r" + std::to_string(i)));
    }
    store.apply(make(StatusState::kWorking, "r0"));  // refresh r0
    store.apply(make(StatusState::kIdle, "new"));
    EXPECT_EQ(store.records().size(), agentdesk::terminal::kMaxRecords);
    EXPECT_TRUE(store.records().contains("r0"));
    EXPECT_FALSE(store.records().contains("r1"));  // oldest untouched one evicted
}

TEST(ProgramStatusStore, AppIsInheritedFromNearestAncestor) {
    ProgramStatusStore store;
    auto root = make(StatusState::kWorking);
    root.app = "cargo";
    store.apply(root);
    store.apply(make(StatusState::kWorking, "build/test"));
    EXPECT_EQ(store.effective_app("build/test"), "cargo");
}
