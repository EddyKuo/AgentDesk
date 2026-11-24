// Unix socket streams, the instance lock and child-process streams on macOS.
#include <cstring>
#include <future>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "agentdesk/platform/ipc_transport.hpp"
#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/system.hpp"

namespace ap = agentdesk::platform;
using namespace std::chrono_literals;

namespace {

std::string unique_endpoint() {
    return (ap::runtime_dir() / ("test-" + ap::new_uuid().substr(0, 8) + ".sock")).string();
}

std::string read_exactly(ap::ByteStream& s, std::size_t n) {
    std::string out;
    char buf[256];
    while (out.size() < n) {
        const auto got = s.read_some(std::span<char>(buf, std::min(sizeof(buf), n - out.size())));
        if (got == 0)
            break;
        out.append(buf, got);
    }
    return out;
}

}  // namespace

TEST(PosixIpc, RoundTripAndPeerIdentity) {
    const auto ep = unique_endpoint();
    auto listener = ap::StreamListener::listen(ep);
    ap::PeerIdentity peer;
    auto accepted = std::async(std::launch::async, [&] { return listener->accept(&peer); });
    auto client = ap::connect_stream(ep, 2s);
    ASSERT_NE(client, nullptr);
    auto server = accepted.get();
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(peer.pid, static_cast<std::uint32_t>(::getpid()));
    EXPECT_TRUE(peer.same_user);

    ASSERT_TRUE(client->write_all("hello"));
    EXPECT_EQ(read_exactly(*server, 5), "hello");
    ASSERT_TRUE(server->write_all("world!"));
    EXPECT_EQ(read_exactly(*client, 6), "world!");
}

TEST(PosixIpc, EndpointIsPrivate) {
    const auto ep = unique_endpoint();
    auto listener = ap::StreamListener::listen(ep);
    struct stat st{};
    ASSERT_EQ(::stat(ep.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600u);
    ASSERT_EQ(::stat(ap::runtime_dir().c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0700u);
}

TEST(PosixIpc, StopWakesABlockedAccept) {
    auto listener = ap::StreamListener::listen(unique_endpoint());
    auto accepted = std::async(std::launch::async, [&] { return listener->accept(); });
    std::this_thread::sleep_for(100ms);
    listener->stop();
    ASSERT_EQ(accepted.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(accepted.get(), nullptr);
}

TEST(PosixIpc, CancelWakesABlockedRead) {
    const auto ep = unique_endpoint();
    auto listener = ap::StreamListener::listen(ep);
    auto accepted = std::async(std::launch::async, [&] { return listener->accept(); });
    auto client = ap::connect_stream(ep, 2s);
    auto server = accepted.get();
    ASSERT_NE(client, nullptr);
    auto reading = std::async(std::launch::async, [&] {
        char buf[16];
        return client->read_some(buf);
    });
    std::this_thread::sleep_for(100ms);
    client->cancel();
    ASSERT_EQ(reading.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(reading.get(), 0u);
    EXPECT_FALSE(client->write_all("x"));
}

TEST(PosixIpc, ReadEndsWhenThePeerCloses) {
    const auto ep = unique_endpoint();
    auto listener = ap::StreamListener::listen(ep);
    auto accepted = std::async(std::launch::async, [&] { return listener->accept(); });
    auto client = ap::connect_stream(ep, 2s);
    auto server = accepted.get();
    server.reset();
    char buf[16];
    EXPECT_EQ(client->read_some(buf), 0u);
    // Writing into a closed socket fails instead of raising SIGPIPE.
    bool ok = true;
    for (int i = 0; i < 100 && ok; ++i)
        ok = client->write_all(std::string(64 * 1024, 'x'));
    EXPECT_FALSE(ok);
}

TEST(PosixIpc, ALiveEndpointCannotBeTakenOver) {
    const auto ep = unique_endpoint();
    auto first = ap::StreamListener::listen(ep);
    try {
        ap::StreamListener::listen(ep);
        FAIL() << "second listener on a live endpoint";
    } catch (const std::system_error& e) {
        EXPECT_EQ(e.code().value(), EADDRINUSE);
    }
}

TEST(PosixIpc, AStaleEndpointIsReplaced) {
    const auto ep = unique_endpoint();
    {
        // A server that crashed: its socket file stays, nobody listens.
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, ep.c_str(), ep.size() + 1);
        ASSERT_EQ(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
        ::close(fd);
    }
    auto listener = ap::StreamListener::listen(ep);
    auto accepted = std::async(std::launch::async, [&] { return listener->accept(); });
    EXPECT_NE(ap::connect_stream(ep, 2s), nullptr);
    EXPECT_NE(accepted.get(), nullptr);
}

TEST(PosixIpc, ConnectGivesUpAfterTheTimeout) {
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(ap::connect_stream(unique_endpoint(), 200ms), nullptr);
    const auto took = std::chrono::steady_clock::now() - start;
    EXPECT_GE(took, 200ms);
    EXPECT_LT(took, 2s);
}

TEST(PosixIpc, ConnectWaitsForAServerThatStartsLate) {
    const auto ep = unique_endpoint();
    auto connecting = std::async(std::launch::async, [&] { return ap::connect_stream(ep, 3s); });
    std::this_thread::sleep_for(300ms);
    auto listener = ap::StreamListener::listen(ep);
    auto accepted = std::async(std::launch::async, [&] { return listener->accept(); });
    EXPECT_NE(connecting.get(), nullptr);
    EXPECT_NE(accepted.get(), nullptr);
}

TEST(PosixIpc, InstanceLockIsExclusive) {
    const std::string name = "test-lock-" + ap::new_uuid().substr(0, 8);
    auto first = ap::InstanceLock::try_acquire(name);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(ap::InstanceLock::try_acquire(name), nullptr);
    first.reset();
    EXPECT_NE(ap::InstanceLock::try_acquire(name), nullptr);
    std::filesystem::remove(ap::runtime_dir() / (name + ".lock"));
}

TEST(PosixIpc, ProcessStreamRelaysTheChildsStdio) {
    ap::ProcessSpec spec;
    spec.executable = "/bin/cat";
    spec.pipe_stdin = true;
    auto stream = ap::process_stream(ap::Process::spawn(spec));
    ASSERT_TRUE(stream->write_all("relay"));
    EXPECT_EQ(read_exactly(*stream, 5), "relay");
    auto reading = std::async(std::launch::async, [&] {
        char buf[16];
        return stream->read_some(buf);
    });
    std::this_thread::sleep_for(100ms);
    stream->cancel();  // kills cat, which ends the read
    ASSERT_EQ(reading.wait_for(3s), std::future_status::ready);
    EXPECT_EQ(reading.get(), 0u);
}
