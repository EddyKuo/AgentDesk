// Ports of herdr-app src/update.rs tests that run programs (curl.exe, tar.exe, a stand-in
// agent-ui.exe) against an isolated release server on 127.0.0.1 (update.rs 676-753). Nothing
// here touches an installed AgentDesk or the internet.
#include <winsock2.h>

#include <ws2tcpip.h>
// winsock2 before windows.h
#include <windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "agentdesk/ui/updates.hpp"

namespace fs = std::filesystem;
namespace up = agentdesk::ui::updates;

namespace {

fs::path temp_dir(const std::wstring& name) {
    return fs::temp_directory_path() / (name + L"-" + std::to_wstring(::GetCurrentProcessId()));
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write(const fs::path& p, const std::string& text) {
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}

// A release server for one test: answers GET <path> from `files`; a request for `drop` closes
// the connection without an answer the first time.
class Server {
public:
    explicit Server(std::map<std::string, std::string> files, std::string drop_once = {})
        : files_(std::move(files)), drop_(std::move(drop_once)) {
        WSADATA wsa{};
        ::WSAStartup(MAKEWORD(2, 2), &wsa);
        listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        ::bind(listener_, reinterpret_cast<sockaddr*>(&a), sizeof a);
        ::listen(listener_, 8);
        int len = sizeof a;
        ::getsockname(listener_, reinterpret_cast<sockaddr*>(&a), &len);
        url_ = "http://127.0.0.1:" + std::to_string(ntohs(a.sin_port));
        thread_ = std::thread([this] { serve(); });
    }
    ~Server() {
        stop_ = true;
        ::closesocket(listener_);
        thread_.join();
        ::WSACleanup();
    }
    [[nodiscard]] const std::string& url() const { return url_; }
    std::vector<std::string> requests;

private:
    void serve() {
        while (!stop_) {
            SOCKET c = ::accept(listener_, nullptr, nullptr);
            if (c == INVALID_SOCKET)
                return;
            std::string req;
            char buf[1024];
            while (req.find("\r\n\r\n") == std::string::npos) {
                const int n = ::recv(c, buf, sizeof buf, 0);
                if (n <= 0)
                    break;
                req.append(buf, static_cast<std::size_t>(n));
            }
            const auto sp = req.find(' ');
            const auto sp2 = req.find(' ', sp + 1);
            const std::string path = sp == std::string::npos ? "" : req.substr(sp + 1, sp2 - sp - 1);
            requests.push_back(path);
            if (!drop_.empty() && path == drop_) {
                drop_.clear();
                ::closesocket(c);
                continue;
            }
            const auto it = files_.find(path);
            std::string resp;
            if (it == files_.end())
                resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            else
                resp = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(it->second.size()) +
                       "\r\nConnection: close\r\n\r\n" + it->second;
            std::size_t at = 0;
            while (at < resp.size()) {
                const int n = ::send(c, resp.data() + at, static_cast<int>(resp.size() - at), 0);
                if (n <= 0)
                    break;
                at += static_cast<std::size_t>(n);
            }
            ::shutdown(c, SD_SEND);
            ::closesocket(c);
        }
    }
    std::map<std::string, std::string> files_;
    std::string drop_;
    SOCKET listener_ = INVALID_SOCKET;
    std::string url_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
};

// A stand-in agent-ui.exe answering `--version` with `version`.
void put_fake_ui(const fs::path& dir, const std::string& version) {
    fs::create_directories(dir);
    fs::copy_file(AGENTDESK_FAKE_UI_EXE, dir / L"agent-ui.exe", fs::copy_options::overwrite_existing);
    write(dir / L"fake-version.txt", version);
}

// The portable zip as tools/package makes it: one folder AgentDesk-<version>-win-x64.
std::string make_zip(const fs::path& work, const std::string& version) {
    const std::wstring name = L"AgentDesk-" + std::wstring(version.begin(), version.end()) + L"-win-x64";
    const fs::path stage = work / L"stage";
    put_fake_ui(stage / name, version);
    write(stage / name / L"THIRD_PARTY_NOTICES.md", "notices");
    const fs::path zip = work / L"release.zip";
    const std::wstring cmd = L"\"" + up::system(L"tar").native() + L"\" -a -c -f \"" + zip.native() +
                             L"\" -C \"" + stage.native() + L"\" " + name;
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    std::wstring line = cmd;
    EXPECT_TRUE(::CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                                 nullptr, &si, &pi));
    ::WaitForSingleObject(pi.hProcess, 60000);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return read(zip);
}

}  // namespace

// update.rs 676-714: an isolated release server drops the first connection, then recovers.
TEST(Updates, FailedCheckClearsSuccessAndRecoveryRecordsANewTime) {
    Server server({{"/app/latest.json", R"({"version":"0.1.0"})"}}, "/app/latest.json");
    const fs::path home = temp_dir(L"agentdesk-offline-check");
    auto checked = up::checked_at(true, 100);
    EXPECT_EQ(checked, 100u);
    const auto failed = up::check_from(home, server.url(), "0.1.0");
    EXPECT_FALSE(failed.ok) << "a dropped connection must fail";
    checked = up::checked_at(failed.ok, 200);
    EXPECT_FALSE(checked.has_value());
    const auto recovered = up::check_from(home, server.url(), "0.1.0");
    EXPECT_TRUE(recovered.ok) << recovered.error;
    EXPECT_FALSE(recovered.version.has_value());
    checked = up::checked_at(recovered.ok, 300);
    EXPECT_EQ(checked, 300u);
    ASSERT_GE(server.requests.size(), 2u);
    EXPECT_EQ(server.requests[0], "/app/latest.json");
}

// update.rs 727-753
TEST(Updates, StagesNeedAReceiptAndAMatchingExecutable) {
    const fs::path work = temp_dir(L"agentdesk-update");
    fs::remove_all(work);
    const fs::path root = work / L"AgentDesk-0.1.0-win-x64";
    const fs::path staged = up::beside(root, L".new");
    const fs::path receipt = up::beside(root, L".new.json");
    put_fake_ui(staged, "0.1.8");
    EXPECT_FALSE(up::staged_version(root).has_value());
    EXPECT_TRUE(fs::is_directory(staged));  // Legacy stages are left for the checker.
    write(receipt, R"({"version":"0.1.8"})");
    EXPECT_EQ(up::staged_version(root), std::optional<std::string>("0.1.8"));
    write(receipt, R"({"version":"0.1.9"})");
    EXPECT_FALSE(up::staged_version(root).has_value());
    EXPECT_FALSE(fs::exists(staged));
    EXPECT_FALSE(fs::exists(receipt));
    fs::create_directories(staged);
    write(staged / L"agent-ui.exe", "corrupt");
    write(receipt, R"({"version":"0.1.8"})");
    EXPECT_FALSE(up::staged_version(root).has_value());
    EXPECT_FALSE(fs::exists(staged));
    fs::remove_all(work);
}

// update.rs 264-325: a newer release is downloaded, checked, unpacked and staged with a receipt;
// a second look finds the stage; swap puts it in place.
TEST(Updates, ANewerReleaseIsStagedThenSwapped) {
    const fs::path work = temp_dir(L"agentdesk-update-flow");
    fs::remove_all(work);
    fs::create_directories(work);
    const std::string zip = make_zip(work, "0.2.0");
    ASSERT_FALSE(zip.empty());
    const fs::path root = work / L"AgentDesk-0.1.0-win-x64";
    put_fake_ui(root, "0.1.0");
    // The archive on one server, the manifest naming it on another.
    Server archive({{"/app/0.2.0/AgentDesk-0.2.0-win-x64.zip", zip}});
    const std::string manifest = R"({"version":"0.2.0","targets":{"windows-x86_64":{"url":")" +
                                 archive.url() + R"(/app/0.2.0/AgentDesk-0.2.0-win-x64.zip","sha256":")" +
                                 up::sha256(zip) + R"("}}})";
    Server release({{"/app/latest.json", manifest}});
    const auto found = up::check_from(root, release.url(), "0.1.0");
    ASSERT_TRUE(found.ok) << found.error;
    EXPECT_EQ(found.version, std::optional<std::string>("0.2.0"));
    EXPECT_EQ(read(up::beside(root, L".new.json")), R"({"version":"0.2.0"})");
    EXPECT_FALSE(fs::exists(up::beside(root, L".download")));
    // Found again: the stage is used, nothing is downloaded.
    const auto again = up::check_from(root, release.url(), "0.1.0");
    EXPECT_EQ(again.version, std::optional<std::string>("0.2.0"));
    std::string error;
    ASSERT_TRUE(up::swap(root, &error)) << error;
    EXPECT_EQ(read(root / L"fake-version.txt"), "0.2.0");
    EXPECT_EQ(read(up::beside(root, L".old") / L"fake-version.txt"), "0.1.0");
    EXPECT_FALSE(fs::exists(up::beside(root, L".new")));
    EXPECT_FALSE(fs::exists(up::beside(root, L".new.json")));
    fs::remove_all(work);
}

// update.rs 313-316: a damaged download is refused and nothing is staged.
TEST(Updates, AChecksumMismatchStagesNothing) {
    const fs::path work = temp_dir(L"agentdesk-update-damaged");
    fs::remove_all(work);
    fs::create_directories(work);
    const std::string zip = make_zip(work, "0.2.0");
    const fs::path root = work / L"AgentDesk-0.1.0-win-x64";
    put_fake_ui(root, "0.1.0");
    Server archive({{"/a.zip", zip}});
    const std::string manifest = R"({"version":"0.2.0","targets":{"windows-x86_64":{"url":")" +
                                 archive.url() + R"(/a.zip","sha256":")" + std::string(64, '0') + R"("}}})";
    Server release({{"/app/latest.json", manifest}});
    const auto found = up::check_from(root, release.url(), "0.1.0");
    EXPECT_FALSE(found.ok);
    EXPECT_EQ(found.error, "the download is damaged (checksum mismatch)");
    EXPECT_FALSE(fs::exists(up::beside(root, L".new")));
    EXPECT_FALSE(fs::exists(up::beside(root, L".download")));
    fs::remove_all(work);
}
