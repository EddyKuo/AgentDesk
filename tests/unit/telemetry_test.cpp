// Telemetry tests: what a report may contain, its key order, the buckets and today.json.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "agentdesk/ui/telemetry.hpp"

#include <gtest/gtest.h>

namespace tel = agentdesk::ui::telemetry;

TEST(Telemetry, DatesAreUtcCalendarDays) {
    EXPECT_EQ(tel::date(0), "1970-01-01");
    EXPECT_EQ(tel::date(19'723), "2024-01-01");
    EXPECT_EQ(tel::date(19'782), "2024-02-29");
    EXPECT_EQ(tel::date(20'745), "2026-10-19");
}

TEST(Telemetry, VersionsAreReleasesOrOther) {
    EXPECT_EQ(tel::version("0.2.0"), "0.2.0");
    EXPECT_EQ(tel::version("hc 0.1.5"), "0.1.5");
    EXPECT_EQ(tel::version("0.2.0-rc.1"), "0.2.0-rc.1");
    EXPECT_EQ(tel::version("0.2.0-my/secret path"), "other");
    EXPECT_EQ(tel::version("hc 0.1.5-acmeclient"), "other");
    EXPECT_EQ(tel::version("0.1.5-rc.x"), "other");
    EXPECT_EQ(tel::version("dev"), "other");
}

TEST(Telemetry, AReportHoldsNoFreeText) {
    tel::Day day{"2026-10-19"};
    tel::Observation o;
    o.agents = {std::string("claude-code"), std::string("/home/me/secret-tool"), std::nullopt};
    o.layout = "my-private-layout";
    o.mods = {"agents", "my-secret-mod", "frame-settings"};
    o.theme = "my-theme";
    day.observe(o, {"main", "terminals"}, {"midnight"});
    day.core = "hc /home/me/build";
    tel::Config config;
    config.app_version = "0.1.0";
    // "agents" is shipped; the rest are the person's own (counted, never named).
    config.mod_kinds = [](const std::set<std::string>& names) {
        std::vector<std::string> builtin;
        std::size_t custom = 0;
        for (const auto& n : names)
            n == "agents" ? builtin.push_back(n) : static_cast<void>(++custom);
        return std::make_tuple(builtin, std::size_t{0}, custom);
    };
    const std::string text = day.report("0123456789abcdef0123456789abcdef", config).dump();
    for (const char* leak : {"secret", "private", "/home", "my-theme", "frame-"})
        EXPECT_EQ(text.find(leak), std::string::npos) << leak << " leaked: " << text;
    EXPECT_NE(text.find("\"claude\""), std::string::npos) << "known agents are named: " << text;
    EXPECT_NE(text.find("\"other\""), std::string::npos) << text;
    EXPECT_NE(text.find("\"custom\""), std::string::npos) << text;
}

// The keys in the order the report writes them.
TEST(Telemetry, ReportShape) {
    tel::Day day{"2026-11-19"};
    day.startups = 120;
    day.startup_ms = {2500, 300, 700};
    day.open_secs = 20 * 60;
    tel::Config config;
    config.app_version = "0.1.0";
    const auto r = day.report("0123456789abcdef0123456789abcdef", config);
    std::vector<std::string> keys;
    for (const auto& [k, v] : r.items())
        keys.push_back(k);
    EXPECT_EQ(keys, (std::vector<std::string>{"v", "install", "day", "app", "core", "usage", "agents",
                                              "agents_open", "agents_working", "spaces", "tabs", "terminals",
                                              "layouts", "mods", "commands", "custom", "welcome", "events"}));
    EXPECT_EQ(r["v"], 1);
    EXPECT_EQ(r["app"]["os"], "windows");
    EXPECT_EQ(r["app"]["channel"], "stable");
    EXPECT_TRUE(r["core"]["version"].is_null());
    EXPECT_EQ(r["usage"]["startups"], 99);
    EXPECT_EQ(r["usage"]["startup"], "0.5-1s");  // the median of 300, 700, 2500
    EXPECT_EQ(r["usage"]["open"], "15m-1h");
    EXPECT_EQ(r["welcome"], "none");
}

TEST(Telemetry, Buckets) {
    EXPECT_EQ(tel::bucket(0), "0");
    EXPECT_EQ(tel::bucket(1), "1");
    EXPECT_EQ(tel::bucket(5), "2-5");
    EXPECT_EQ(tel::bucket(6), "6-20");
    EXPECT_EQ(tel::bucket(21), "21+");
    EXPECT_EQ(tel::time_bucket(59), "0");
    EXPECT_EQ(tel::time_bucket(14 * 60), "<15m");
    EXPECT_EQ(tel::time_bucket(60 * 60), "1-4h");
    EXPECT_EQ(tel::time_bucket(8 * 3600), "8h+");
    EXPECT_EQ(tel::startup_bucket(499), "<0.5s");
    EXPECT_EQ(tel::startup_bucket(1000), "1-2s");
    EXPECT_EQ(tel::startup_bucket(5000), "5s+");
}

TEST(Telemetry, AgentNamesComeFromTheTable) {
    EXPECT_EQ(tel::agent_name(std::string("claude-code")), "claude");
    EXPECT_EQ(tel::agent_name(std::string("Codex")), "codex");
    EXPECT_EQ(tel::agent_name(std::string("agent")), "cursor");
    EXPECT_EQ(tel::agent_name(std::string("  ")), "unknown");
    EXPECT_EQ(tel::agent_name(std::nullopt), "unknown");
    EXPECT_EQ(tel::agent_name(std::string("my-tool")), "other");
}

// today.json reads back as it was written.
TEST(Telemetry, DayRoundTrips) {
    tel::Day d{"2026-11-19"};
    d.open_secs = 90;
    d.agents = {"claude"};
    d.events["core_lost"] = 2;
    d.welcome = "finished";
    const tel::Day back = tel::Day::from_json(tel::Json::parse(d.to_json().dump()));
    EXPECT_EQ(back.day, d.day);
    EXPECT_EQ(back.open_secs, 90u);
    EXPECT_EQ(back.agents, d.agents);
    EXPECT_EQ(back.events, d.events);
    EXPECT_EQ(back.welcome, d.welcome);
}
