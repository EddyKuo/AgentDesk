// QA-21 on macOS: the host answers "OSC 7501 ; ?" before the DA1 reply, through a Unix pty
// (which, unlike the inbox ConPTY, passes every byte through).
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "agentdesk/host/session.hpp"

using namespace std::chrono_literals;
using agentdesk::host::Session;
using agentdesk::host::SessionConfig;

namespace {

std::string from_hex(const std::string& hex) {
    std::string out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    return out;
}

}  // namespace

TEST(PosixOsc7501, QueryIsAnsweredBeforeDa1) {
    SessionConfig c;
    c.spec.executable = AGENTDESK_VT_PROBE;
    c.spec.args = {"query"};
    c.spec.cwd = std::filesystem::temp_directory_path();
    c.spec.size = {200, 24};
    c.engine.rows = 24;
    c.engine.cols = 200;
    auto s = Session::start(c);
    ASSERT_TRUE(s->wait_exit(15s).has_value());
    std::string hex;
    const auto snap = s->snapshot();
    for (int r = 0; r < snap->rows && hex.empty(); ++r) {
        const std::string row = snap->row_text(r);
        if (const auto at = row.find("PROBE-REPLY:"); at != std::string::npos)
            hex = row.substr(at + 12);
    }
    while (!hex.empty() && (hex.back() == ' '))
        hex.pop_back();
    ASSERT_FALSE(hex.empty());
    const std::string reply = from_hex(hex);
    const auto osc = reply.find("\x1b]7501");
    const auto da1 = reply.find("\x1b[?");
    ASSERT_NE(osc, std::string::npos) << hex;
    ASSERT_NE(da1, std::string::npos) << hex;
    EXPECT_LT(osc, da1);
}
