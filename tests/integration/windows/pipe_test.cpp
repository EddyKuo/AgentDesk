// libs/ipc: Named Pipe transport (S2-1, QA-17).
#include "agentdesk/ipc/pipe.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "agentdesk/protocol/messages.hpp"

using namespace std::chrono_literals;
using namespace agentdesk;

namespace {

std::wstring unique_name() {
    static std::atomic<int> n{0};
    return L"\\\\.\\pipe\\AgentDeskTest-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
           std::to_wstring(++n);
}

// Echo server: answers every request with {"echo": params}.
ipc::ServerHandlers echo_handlers() {
    ipc::ServerHandlers h;
    h.on_message = [](const ipc::ConnectionPtr& c, std::string payload) {
        const auto parsed = protocol::parse_message(payload);
        if (!parsed.message)
            return;
        if (const auto* req = std::get_if<protocol::Request>(&*parsed.message)) {
            c->send(protocol::serialize(protocol::ok(req->id, {{"echo", req->params}})));
        }
    };
    return h;
}

}  // namespace

TEST(Pipe, NamesIncludeUserSidAndSession) {
    EXPECT_NE(ipc::default_pipe_name().find(ipc::current_user_sid()), std::wstring::npos);
    EXPECT_NE(ipc::default_mutex_name().find(L"Local\\AgentDesk-host-"), std::wstring::npos);
}

TEST(Pipe, RequestResponseRoundTrip) {
    const auto name = unique_name();
    ipc::PipeServer server(name, echo_handlers());
    auto client = ipc::PipeClient::connect(name, 5s);
    ASSERT_NE(client, nullptr);
    const auto resp = client->call("test.echo", {{"text", "中文"}});
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->result->at("echo").at("text"), "中文");
}

TEST(Pipe, SecondServerOnSameNameIsRefused) {
    const auto name = unique_name();
    ipc::PipeServer server(name, echo_handlers());
    EXPECT_THROW(ipc::PipeServer(name, echo_handlers()), std::system_error);  // FILE_FLAG_FIRST_PIPE_INSTANCE
}

TEST(Pipe, ManyClientsConcurrently) {
    const auto name = unique_name();
    ipc::PipeServer server(name, echo_handlers());
    std::vector<std::thread> threads;
    std::atomic<int> ok{0};
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&, t] {
            auto c = ipc::PipeClient::connect(name, 5s);
            if (!c)
                return;
            for (int i = 0; i < 20; ++i) {
                const auto r = c->call("test.echo", {{"n", t * 100 + i}});
                if (r && r->result->at("echo").at("n") == t * 100 + i)
                    ++ok;
            }
        });
    }
    for (auto& th : threads)
        th.join();
    EXPECT_EQ(ok, 160);
}

TEST(Pipe, EventsAreDelivered) {
    const auto name = unique_name();
    ipc::ServerHandlers h;
    h.on_message = [](const ipc::ConnectionPtr& c, std::string) {
        protocol::Event e;
        e.event = "test.tick";
        e.body = {{"n", 7}};
        c->send(protocol::serialize(e));
    };
    ipc::PipeServer server(name, h);
    auto client = ipc::PipeClient::connect(name, 5s);
    ASSERT_NE(client, nullptr);
    std::atomic<int> got{0};
    client->set_event_handler([&](protocol::Event e) {
        if (e.event == "test.tick")
            got = e.body["n"].get<int>();
    });
    client->call("anything", {}, 300ms);  // no response expected; triggers the event
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (got == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(10ms);
    EXPECT_EQ(got, 7);
}

// QA-17: an oversized frame header drops the connection without buffering the body.
TEST(Pipe, OversizedFrameDisconnectsClient) {
    const auto name = unique_name();
    std::atomic<int> closed{0};
    auto h = echo_handlers();
    h.on_close = [&](const ipc::ConnectionPtr&) { ++closed; };
    ipc::PipeServer server(name, h);
    HANDLE raw =
        ::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(raw, INVALID_HANDLE_VALUE);
    const unsigned char hdr[4] = {0xFF, 0xFF, 0xFF, 0x7F};
    DWORD written = 0;
    ::WriteFile(raw, hdr, 4, &written, nullptr);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (closed == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(10ms);
    EXPECT_EQ(closed, 1);
    ::CloseHandle(raw);
}

TEST(Pipe, DroppableMessagesRespectSendBudget) {
    const auto name = unique_name();
    std::atomic<int> accepted{0};
    std::atomic<int> refused{0};
    ipc::ServerHandlers h;
    h.on_message = [&](const ipc::ConnectionPtr& c, std::string) {
        const std::string big(512 * 1024, 'x');  // client never reads: queue grows
        for (int i = 0; i < 40; ++i) {
            if (c->send("{\"v\":1,\"event\":\"x\",\"pad\":\"" + big + "\"}", true))
                ++accepted;
            else
                ++refused;
        }
    };
    ipc::PipeServer server(name, h);
    HANDLE raw =
        ::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(raw, INVALID_HANDLE_VALUE);
    const std::string req = protocol::encode_frame(R"({"v":1,"id":"a","method":"go"})");
    DWORD written = 0;
    ::WriteFile(raw, req.data(), static_cast<DWORD>(req.size()), &written, nullptr);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (accepted + refused < 40 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(10ms);
    EXPECT_GT(refused, 0) << "budget never applied";
    EXPECT_LT(accepted, 40);
    ::CloseHandle(raw);
}
