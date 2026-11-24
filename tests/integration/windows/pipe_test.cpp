// libs/ipc: Named Pipe transport (S2-1, QA-17).
#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/text.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <gtest/gtest.h>

#include "agentdesk/protocol/messages.hpp"

using namespace std::chrono_literals;
using namespace agentdesk;

namespace {

std::string unique_name() {
    static std::atomic<int> n{0};
    return "\\\\.\\pipe\\AgentDeskTest-" + std::to_string(::GetCurrentProcessId()) + "-" +
           std::to_string(++n);
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
    EXPECT_NE(ipc::default_pipe_name().find(ipc::current_user_sid()), std::string::npos);
    EXPECT_NE(ipc::default_mutex_name().find("Local\\AgentDesk-host-"), std::string::npos);
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
        ::CreateFileW(agentdesk::platform::to_wide(name).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
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
        ::CreateFileW(agentdesk::platform::to_wide(name).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
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

// A client that stopped reading must not hold up the server: stop() used to wait in
// FlushFileBuffers for the client to read everything, forever.
TEST(Pipe, StopDoesNotWaitForAClientThatStoppedReading) {
    const auto name = unique_name();
    std::atomic<int> sent{0};
    ipc::ConnectionPtr conn;
    std::mutex conn_mutex;
    ipc::ServerHandlers h;
    h.on_message = [&](const ipc::ConnectionPtr& c, std::string) {
        // Fits the 64 KiB pipe buffer: the write completes and the bytes wait there, unread.
        c->send("{\"v\":1,\"event\":\"x\",\"pad\":\"" + std::string(16 * 1024, 'x') + "\"}");
        {
            std::lock_guard lock(conn_mutex);
            conn = c;
        }
        ++sent;
    };
    auto server = std::make_unique<ipc::PipeServer>(name, h);
    HANDLE raw =
        ::CreateFileW(agentdesk::platform::to_wide(name).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(raw, INVALID_HANDLE_VALUE);
    const std::string req = protocol::encode_frame(R"({"v":1,"id":"a","method":"go"})");
    DWORD written = 0;
    ::WriteFile(raw, req.data(), static_cast<DWORD>(req.size()), &written, nullptr);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (sent == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(10ms);
    ASSERT_EQ(sent, 1);
    {
        std::lock_guard lock(conn_mutex);
        const auto written_deadline = std::chrono::steady_clock::now() + 5s;
        while (conn->queued_bytes() > 0 && std::chrono::steady_clock::now() < written_deadline)
            std::this_thread::sleep_for(10ms);
        ASSERT_EQ(conn->queued_bytes(), 0u) << "the reply was not written into the pipe";
        conn.reset();
    }
    std::atomic<bool> stopped{false};
    std::thread stopper([&] {
        server->stop();
        stopped = true;
    });
    const auto stop_deadline = std::chrono::steady_clock::now() + 5s;
    while (!stopped && std::chrono::steady_clock::now() < stop_deadline)
        std::this_thread::sleep_for(10ms);
    EXPECT_TRUE(stopped) << "PipeServer::stop waited for a client that does not read";
    ::CloseHandle(raw);  // unblocks a stuck stop() so the test can end either way
    stopper.join();
}

// A request that cannot fit one frame comes back as an error at once, without an exception and
// without waiting for the timeout.
TEST(Pipe, OversizedRequestIsAnErrorNotAnException) {
    const auto name = unique_name();
    ipc::PipeServer server(name, echo_handlers());
    auto client = ipc::PipeClient::connect(name, 5s);
    ASSERT_NE(client, nullptr);
    std::optional<protocol::Response> resp;
    const auto start = std::chrono::steady_clock::now();
    EXPECT_NO_THROW(
        resp = client->call("test.echo", {{"text", std::string(protocol::kMaxFrameBytes, 'a')}}, 30s));
    EXPECT_LT(std::chrono::steady_clock::now() - start, 5s);
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->error, protocol::ErrorCode::kBadRequest);
    // The connection is still usable.
    const auto again = client->call("test.echo", {{"n", 1}});
    ASSERT_TRUE(again && again->result);
    EXPECT_EQ(again->result->at("echo").at("n"), 1);
}

// A response that arrives after its call timed out is dropped, and a later call still gets its own.
TEST(Pipe, LateResponseAfterTimeoutIsDropped) {
    const auto name = unique_name();
    ipc::ServerHandlers h;
    h.on_message = [](const ipc::ConnectionPtr& c, std::string payload) {
        const auto parsed = protocol::parse_message(payload);
        if (!parsed.message)
            return;
        if (const auto* req = std::get_if<protocol::Request>(&*parsed.message)) {
            if (req->method == "slow")
                std::this_thread::sleep_for(300ms);
            c->send(protocol::serialize(protocol::ok(req->id, {{"method", req->method}})));
        }
    };
    ipc::PipeServer server(name, h);
    auto client = ipc::PipeClient::connect(name, 5s);
    ASSERT_NE(client, nullptr);
    EXPECT_FALSE(client->call("slow", protocol::Json::object(), 50ms).has_value());
    const auto next = client->call("fast", protocol::Json::object());
    ASSERT_TRUE(next && next->result);
    EXPECT_EQ(next->result->at("method"), "fast");
}
