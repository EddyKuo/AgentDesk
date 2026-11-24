// Every Session of these tests runs on the bundled ConPTY, as agent-host does: it is not next to
// the test program, so it is chosen here.
#include <filesystem>

#include <gtest/gtest.h>

#include "agentdesk/platform/win/pty_conpty.hpp"

namespace {

class UseBundledConPty : public testing::Environment {
public:
    void SetUp() override {
        namespace win = agentdesk::platform::win;
        win::use_conpty(win::load_conpty(std::filesystem::path(AGENTDESK_BUNDLED_CONPTY_DIR)));
    }
};

[[maybe_unused]] const testing::Environment* const kEnvironment =
    testing::AddGlobalTestEnvironment(new UseBundledConPty);

}  // namespace
