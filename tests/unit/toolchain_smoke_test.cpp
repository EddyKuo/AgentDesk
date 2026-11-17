// S1-1: proves the toolchain, vcpkg dependencies and vendored libvterm link together.
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <vterm.h>

namespace {

struct VTermDeleter {
    void operator()(VTerm* vt) const { vterm_free(vt); }
};

void append_output(const char* bytes, size_t len, void* user) {
    static_cast<std::string*>(user)->append(bytes, len);
}

}  // namespace

TEST(ToolchainSmoke, LibvtermVersionIsPinned) {
    EXPECT_EQ(VTERM_VERSION_MAJOR, 0);
    EXPECT_EQ(VTERM_VERSION_MINOR, 3);
    EXPECT_EQ(VTERM_VERSION_PATCH, 3);
}

TEST(ToolchainSmoke, LibvtermAnswersCursorPositionReport) {
    std::unique_ptr<VTerm, VTermDeleter> vt(vterm_new(24, 80));
    ASSERT_NE(vt, nullptr);
    vterm_set_utf8(vt.get(), 1);
    std::string replies;
    vterm_output_set_callback(vt.get(), &append_output, &replies);
    VTermScreen* screen = vterm_obtain_screen(vt.get());
    vterm_screen_reset(screen, 1);

    const std::string input = "AB\x1b[6n";
    vterm_input_write(vt.get(), input.data(), input.size());

    EXPECT_EQ(replies, "\x1b[1;3R");
}

TEST(ToolchainSmoke, NlohmannJsonRoundTripsUtf8) {
    const auto doc = nlohmann::json::parse(R"({"v":1,"name":"工作"})");
    EXPECT_EQ(doc.at("v").get<int>(), 1);
    EXPECT_EQ(doc.dump(), "{\"name\":\"\xE5\xB7\xA5\xE4\xBD\x9C\",\"v\":1}");
}
