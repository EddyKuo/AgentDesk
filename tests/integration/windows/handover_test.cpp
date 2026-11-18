// agent-host's self-update (design doc 7.4 OQ-09; herdr src/update.rs 236: "terminals keep
// running"): the pieces that move a live terminal to another owner.
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "agentdesk/platform/win/conpty_api.hpp"
#include "agentdesk/platform/win/pty_process.hpp"

using namespace std::chrono_literals;
namespace win = agentdesk::platform::win;

namespace {

win::ConPtyApi bundled() {
    return win::load_conpty(std::filesystem::path(AGENTDESK_BUNDLED_CONPTY_DIR));
}

template <typename Pred>
bool wait_until(Pred pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred())
            return true;
        std::this_thread::sleep_for(20ms);
    }
    return pred();
}

HANDLE dup(HANDLE h) {
    HANDLE out = nullptr;
    EXPECT_TRUE(::DuplicateHandle(::GetCurrentProcess(), h, ::GetCurrentProcess(), &out, 0, FALSE,
                                  DUPLICATE_SAME_ACCESS));
    return out;
}

struct Collected {
    std::mutex mutex;
    std::string text;
    void add(std::span<const char> b) {
        std::lock_guard lock(mutex);
        text.append(b.data(), b.size());
    }
    std::string get() {
        std::lock_guard lock(mutex);
        return text;
    }
};

}  // namespace

// The bundled ConPTY can be handed over: its HPCON is rebuilt from duplicated handles
// (ConptyPackPseudoConsole), the first owner lets go without ending anything, and the program,
// its output and the kill-on-close job carry on with the second owner.
TEST(Handover, PseudoConsoleMovesToAnotherOwner) {
    const auto api = bundled();
    ASSERT_TRUE(api.bundled) << "fetch the bundled ConPTY first (fetch_conpty.exe)";
    ASSERT_NE(api.pack, nullptr);
    Collected first;
    win::PtySpawnSpec spec;
    spec.executable = AGENTDESK_VT_PROBE;
    spec.args = {L"counter"};
    spec.cwd = std::filesystem::temp_directory_path();
    auto a = win::PtyProcess::spawn(api, spec, [&](std::span<const char> b) { first.add(b); });
    ASSERT_TRUE(wait_until([&] { return first.get().find("COUNT 3") != std::string::npos; }, 10s));

    ASSERT_TRUE(a->pause_reader(2s));
    std::string why;
    const auto h = a->handover_handles(&why);
    ASSERT_TRUE(h.has_value()) << why;
    win::PtyHandles copy = *h;
    copy.signal = dup(h->signal);
    copy.reference = dup(h->reference);
    copy.conhost = dup(h->conhost);
    copy.input_write = dup(h->input_write);
    copy.output_read = dup(h->output_read);
    copy.process = dup(h->process);
    copy.job = dup(h->job);
    const DWORD pid = a->pid();
    a->release_after_handover();
    a.reset();  // the first owner is gone: its job handle closed, its HPCON freed

    Collected second;
    auto b = win::PtyProcess::adopt(api, copy, [&](std::span<const char> bytes) { second.add(bytes); });
    EXPECT_EQ(b->pid(), pid);
    EXPECT_FALSE(b->wait_exit(0ms).has_value()) << "the program must still run";
    b->resume_reader();
    // Still counting, through the same pipe, nothing skipped at the seam.
    ASSERT_TRUE(wait_until([&] { return second.get().find("COUNT 12") != std::string::npos; }, 15s))
        << second.get();
    const std::string all = first.get() + second.get();
    for (int n = 1; n <= 12; ++n)
        EXPECT_NE(all.find("COUNT " + std::to_string(n) + "\r"), std::string::npos) << n;
    b->resize({100, 30});  // the signal pipe works from the new owner
    // Closing the new owner ends the program: the job still kills on close.
    win::UniqueHandle process(::OpenProcess(SYNCHRONIZE, FALSE, pid));
    b->close();
    ASSERT_TRUE(process);
    EXPECT_EQ(::WaitForSingleObject(process.get(), 10000), WAIT_OBJECT_0);
}

// Adopting checks the handles themselves, never a PID: a process outside the job is refused.
TEST(Handover, AdoptRefusesHandlesThatDoNotBelongTogether) {
    const auto api = bundled();
    ASSERT_TRUE(api.bundled);
    Collected out;
    win::PtySpawnSpec spec;
    spec.executable = AGENTDESK_VT_PROBE;
    spec.args = {L"counter"};
    spec.cwd = std::filesystem::temp_directory_path();
    auto a = win::PtyProcess::spawn(api, spec, [&](std::span<const char> b) { out.add(b); });
    ASSERT_TRUE(a->pause_reader(2s));
    std::string why;
    const auto h = a->handover_handles(&why);
    ASSERT_TRUE(h.has_value()) << why;
    win::PtyHandles wrong = *h;
    wrong.signal = dup(h->signal);
    wrong.reference = dup(h->reference);
    wrong.conhost = dup(h->conhost);
    wrong.input_write = dup(h->input_write);
    wrong.output_read = dup(h->output_read);
    wrong.process = dup(::GetCurrentProcess());  // this test, not the program in the job
    wrong.pid = ::GetCurrentProcessId();
    wrong.job = dup(h->job);
    EXPECT_THROW(win::PtyProcess::adopt(api, wrong, [](std::span<const char>) {}), std::runtime_error);
    // The original owner is untouched and keeps going.
    a->resume_reader();
    const auto before = out.get().size();
    EXPECT_TRUE(wait_until([&] { return out.get().size() > before; }, 5s));
    a->close();
}

// The system's ConPTY has no ConptyPackPseudoConsole: it cannot hand over, and says so.
TEST(Handover, InboxConPtyRefuses) {
    const auto api = win::load_system_conpty();
    win::PtySpawnSpec spec;
    spec.executable = AGENTDESK_VT_PROBE;
    spec.args = {L"counter"};
    spec.cwd = std::filesystem::temp_directory_path();
    auto a = win::PtyProcess::spawn(api, spec, [](std::span<const char>) {});
    std::string why;
    EXPECT_FALSE(a->handover_handles(&why).has_value());
    EXPECT_FALSE(why.empty());
    a->close();
}
