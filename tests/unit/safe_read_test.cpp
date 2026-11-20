// agent-ui's reading of files and answers it did not write (apps/desktop/safe_read.hpp): a value
// of the wrong type is the fallback, never an exception.
#include "safe_read.hpp"

#include <gtest/gtest.h>

namespace safe = agentdesk::desktop::safe;
using Json = nlohmann::json;

TEST(SafeRead, WrongTypesFallBack) {
    const Json j = Json::parse(R"({"level": 3, "marks": "yes", "name": ["x"], "at": "soon", "ok": true,
                                   "n": 42, "big": 18446744073709551615, "f": 1.5})");
    EXPECT_EQ(safe::string_or(j, "level", "beginner"), "beginner");
    EXPECT_TRUE(safe::bool_or(j, "marks", true));
    EXPECT_EQ(safe::string_or(j, "name"), "");
    EXPECT_EQ(safe::int_or(j, "at", 7), 7);
    EXPECT_TRUE(safe::bool_or(j, "ok", false));
    EXPECT_EQ(safe::int_or(j, "n", 0), 42);
    EXPECT_EQ(safe::int_or(j, "big", -1), -1);
    EXPECT_EQ(safe::int_or(j, "f", -1), -1);
    EXPECT_EQ(safe::int_or(j, "missing", 5), 5);
}

TEST(SafeRead, NotAnObjectIsEmpty) {
    for (const char* text : {"[1, 2]", "\"text\"", "12", "null"}) {
        const Json j = Json::parse(text);
        EXPECT_EQ(safe::field(j, "x"), nullptr) << text;
        EXPECT_EQ(safe::string_or(j, "day", "none"), "none") << text;
        EXPECT_FALSE(safe::bool_or(j, "missing", false)) << text;
    }
}

// A background from layouts\.state: only two colors and a numeric angle make a gradient.
TEST(SafeRead, GradientIsCheckedLikeTheLayoutFile) {
    const auto g = safe::gradient(Json::parse(R"({"gradient": ["#000000", "#ffffff"], "angle": 90})"));
    ASSERT_TRUE(g);
    EXPECT_EQ(g->from, "#000000");
    EXPECT_EQ(g->to, "#ffffff");
    EXPECT_FLOAT_EQ(g->angle, 90.0f);
    const auto d = safe::gradient(Json::parse(R"({"gradient": ["#000", "#fff"]})"));
    ASSERT_TRUE(d);
    EXPECT_FLOAT_EQ(d->angle, 180.0f);
    for (const char* bad :
         {R"({"gradient": "#fff"})", R"({"gradient": ["#fff"]})", R"({"gradient": ["#fff", 3]})",
          R"({"gradient": ["#fff", "#000", "#111"]})", R"({"gradient": ["#fff", "#000"], "angle": "90"})",
          R"({"image": "a.png"})", R"("#fff")", "null"})
        EXPECT_FALSE(safe::gradient(Json::parse(bad))) << bad;
}

TEST(SafeRead, LossyUtf8KeepsUnpairedSurrogates) {
    EXPECT_EQ(safe::utf8_lossy(L"abc"), "abc");
    EXPECT_EQ(safe::utf8_lossy(L""), "");
    const wchar_t broken[] = {L'a', static_cast<wchar_t>(0xD800), L'b', 0};
    EXPECT_EQ(safe::utf8_lossy(broken),
              "a\xEF\xBF\xBD"
              "b");
}
