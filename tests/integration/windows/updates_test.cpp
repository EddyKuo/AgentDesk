// Updater tests that run programs (curl.exe, tar.exe, a stand-in agent-ui.exe, agentctl and
// agent-host) against an isolated release server on 127.0.0.1. Nothing here touches an installed
// AgentDesk or the internet.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
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
#include <nlohmann/json.hpp>

#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/job.hpp"
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

// An isolated release server drops the first connection, then recovers.
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

// A newer release is downloaded, checked, unpacked and staged with a receipt;
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

// A damaged download is refused and nothing is staged.
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

// ---------------------------------------------------------------- agent-host's own update
// `agentctl update` and Settings > Updates "Update agent-host" (updates::update_core, design doc
// 7.4 OQ-09). The release server is local; agent-host runs from a folder of its own.

namespace {

namespace win = agentdesk::platform::win;
using Json = nlohmann::json;

struct Run {
    DWORD exit_code = 9999;
    std::string out;
    std::string err;
};

Run run(const fs::path& exe, const std::vector<std::wstring>& args) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr;
    ::CreatePipe(&out_r, &out_w, &sa, 0);
    ::CreatePipe(&err_r, &err_w, &sa, 0);
    ::SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_w;
    si.hStdError = err_w;
    std::wstring cmd = win::build_command_line(exe.native(), args);
    PROCESS_INFORMATION pi{};
    Run r;
    const BOOL ok = ::CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                     nullptr, &si, &pi);
    ::CloseHandle(out_w);
    ::CloseHandle(err_w);
    if (ok) {
        std::thread err_reader([&] {
            char buf[4096];
            DWORD n = 0;
            while (::ReadFile(err_r, buf, sizeof buf, &n, nullptr) && n > 0)
                r.err.append(buf, n);
        });
        char buf[4096];
        DWORD n = 0;
        while (::ReadFile(out_r, buf, sizeof buf, &n, nullptr) && n > 0)
            r.out.append(buf, n);
        err_reader.join();
        ::WaitForSingleObject(pi.hProcess, 120000);
        ::GetExitCodeProcess(pi.hProcess, &r.exit_code);
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
    }
    ::CloseHandle(out_r);
    ::CloseHandle(err_r);
    return r;
}

// agent-host + agentctl + conpty\ copied into a folder of their own, running on a private pipe
// with a private data folder, one counting terminal open.
class HostInstall {
public:
    explicit HostInstall(const std::wstring& tag) {
        dir = temp_dir(L"agentdesk-hostinstall-" + tag);
        data = temp_dir(L"agentdesk-hostdata-" + tag);
        pipe = L"\\\\.\\pipe\\AgentDeskHostInstall-" + tag + L"-" + std::to_wstring(::GetCurrentProcessId());
        fs::remove_all(dir);
        fs::remove_all(data);
        fs::create_directories(dir);
        fs::create_directories(data);
        fs::copy_file(AGENTDESK_HOST_EXE, dir / L"agent-host.exe");
        fs::copy_file(AGENTDESK_CTL_EXE, dir / L"agentctl.exe");
        fs::copy(fs::path(AGENTDESK_BUNDLED_CONPTY_DIR) / L"conpty", dir / L"conpty", fs::copy_options::recursive);
        const Json profiles = Json::array({{{"id", "counter"},
                                            {"name", "probe counter"},
                                            {"executable", win::to_utf8(AGENTDESK_VT_PROBE)},
                                            {"args", {"counter"}}}});
        write(data / L"profiles.json", Json{{"schema_version", 1}, {"profiles", profiles}}.dump());
        const fs::path host = dir / L"agent-host.exe";
        std::wstring cmd = win::build_command_line(host.native(), {L"--pipe", pipe, L"--data-dir", data.native()});
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (::CreateProcessW(host.c_str(), cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr,
                             &si, &pi)) {
            job.assign(pi.hProcess);  // every host and terminal of this test ends with it
            ::CloseHandle(pi.hThread);
            ::CloseHandle(pi.hProcess);
        }
        client = agentdesk::ipc::PipeClient::connect(agentdesk::platform::to_utf8(pipe), std::chrono::seconds(10));
        if (!client)
            return;
        client->call("hello", {{"version", 1}});
        const auto r = client->call("session.create", {{"profile_id", "counter"},
                                                       {"cwd", win::to_utf8(fs::temp_directory_path().native())},
                                                       {"rows", 8},
                                                       {"cols", 60}});
        if (r && r->result)
            session = r->result->value("session_id", "");
    }
    ~HostInstall() {
        if (auto c = agentdesk::ipc::PipeClient::connect(agentdesk::platform::to_utf8(pipe), std::chrono::seconds(2)))
            c->call("host.shutdown", {{"stop_sessions", true}}, std::chrono::seconds(5));
        client.reset();
        ::Sleep(500);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::remove_all(data, ec);
    }
    Json hello() {
        auto c = agentdesk::ipc::PipeClient::connect(agentdesk::platform::to_utf8(pipe), std::chrono::seconds(10));
        if (!c)
            return {};
        const auto r = c->call("hello", {{"version", 1}});
        return r && r->result ? *r->result : Json();
    }
    Json get() {
        auto c = agentdesk::ipc::PipeClient::connect(agentdesk::platform::to_utf8(pipe), std::chrono::seconds(10));
        if (!c)
            return {};
        c->call("hello", {{"version", 1}});
        const auto r = c->call("session.get", {{"session", session}});
        return r && r->result ? *r->result : Json();
    }
    fs::path dir;
    fs::path data;
    std::wstring pipe;
    std::string session;
    win::KillOnCloseJob job;
    std::unique_ptr<agentdesk::ipc::PipeClient> client;
};

std::string manifest_for(const std::string& base, const std::string& sha) {
    return Json{{"version", "99.0.0"},
                {"generation", 1},
                {"targets", {{"windows-x86_64", {{"url", base + "/core/99.0.0/agent-host.exe"}, {"sha256", sha}}}}}}
        .dump();
}

struct ReleasesUrl {
    explicit ReleasesUrl(const std::string& url) {
        ::SetEnvironmentVariableW(L"AGENTDESK_RELEASES_URL", std::wstring(url.begin(), url.end()).c_str());
    }
    ~ReleasesUrl() { ::SetEnvironmentVariableW(L"AGENTDESK_RELEASES_URL", nullptr); }
};

}  // namespace

// `agentctl update --json`: finds the release, puts it in place, the
// running agent-host hands its terminals over, and it answers {"now": "agent-host 99.0.0"}.
TEST(CoreUpdate, AgentctlUpdateHandsTheTerminalsToTheRelease) {
    HostInstall install(L"ctl");
    ASSERT_FALSE(install.session.empty()) << "host did not start";
    const std::string have = up::host_version_of(AGENTDESK_HOST_EXE).value_or("");
    ASSERT_FALSE(have.empty());
    const auto pid = install.get().value("pid", 0u);
    ASSERT_NE(pid, 0u);
    const std::string next = read(AGENTDESK_HOST_NEXT_EXE);
    Server server({{"/core/99.0.0/agent-host.exe", next}});
    Server release({{"/core/latest.json", manifest_for(server.url(), up::sha256(next))}});
    // The binary comes from `server`, the manifest from `release`.
    ReleasesUrl env(release.url());
    const std::wstring pipe = install.pipe;
    const auto r = run(install.dir / L"agentctl.exe", {L"update", L"--json", L"--pipe", pipe});
    ASSERT_EQ(r.exit_code, 0u) << r.out << r.err;
    EXPECT_EQ(Json::parse(r.out, nullptr, false), Json({{"now", "agent-host 99.0.0"}})) << r.out;
    const Json h = install.hello();
    EXPECT_EQ(h.value("host_version", ""), "99.0.0");
    const Json got = install.get();
    EXPECT_EQ(got.value("process_state", ""), "Running");
    EXPECT_EQ(got.value("pid", 0u), pid) << "the same program, still running";
    EXPECT_EQ(up::host_version_of(install.dir / L"agent-host.exe"), std::optional<std::string>("99.0.0"));
    // The old program file goes once the old host has exited.
    for (int i = 0; i < 100 && fs::exists(install.dir / L"agent-host.exe.old"); ++i)
        ::Sleep(100);
    EXPECT_FALSE(fs::exists(install.dir / L"agent-host.exe.old"));
    EXPECT_FALSE(fs::exists(install.dir / L"agent-host.exe.new"));

    // Already the latest: nothing moves.
    const auto again = run(install.dir / L"agentctl.exe", {L"update", L"--json", L"--pipe", pipe});
    EXPECT_EQ(again.exit_code, 0u) << again.err;
    EXPECT_EQ(Json::parse(again.out, nullptr, false), Json({{"now", "agent-host 99.0.0"}}));
    EXPECT_EQ(install.hello().value("host_instance_id", ""), h.value("host_instance_id", ""));
}

// Settings > Updates: "Update agent-host" runs update_core: Updating... then the new version, or
// "The update failed: <why>" with everything left as it was.
TEST(CoreUpdate, UpdateCoreGoesFromUpdatingToDoneOrError) {
    HostInstall install(L"ui");
    ASSERT_FALSE(install.session.empty()) << "host did not start";
    const std::string have = up::host_version_of(AGENTDESK_HOST_EXE).value_or("");
    // agentctl update runs from agent-ui's folder: here, the install folder.
    up::configure({have, install.dir / L"agent-ui.exe", install.data, {}});
    up::set_pipe(agentdesk::platform::to_utf8(install.pipe));
    up::set_core(have, std::nullopt);
    const std::string next = read(AGENTDESK_HOST_NEXT_EXE);
    const auto wait_done = [] {
        for (int i = 0; i < 1200 && up::core().updating; ++i)
            ::Sleep(100);
        return !up::core().updating;
    };
    {
        Server binary({{"/core/99.0.0/agent-host.exe", next}});
        Server release({{"/core/latest.json", manifest_for(binary.url(), std::string(64, '0'))}});
        ReleasesUrl env(release.url());
        up::update_core();
        EXPECT_TRUE(up::core().updating);  // "Updating... your terminals keep running."
        ASSERT_TRUE(wait_done());
        EXPECT_EQ(up::core().error, std::optional<std::string>("the download is damaged (checksum mismatch)"));
        EXPECT_EQ(up::core().version, std::optional<std::string>(have));
        EXPECT_EQ(up::host_version_of(install.dir / L"agent-host.exe"), std::optional<std::string>(have));
        EXPECT_FALSE(fs::exists(install.dir / L"agent-host.exe.new"));
        EXPECT_EQ(install.get().value("process_state", ""), "Running");
    }
    {
        Server binary({{"/core/99.0.0/agent-host.exe", next}});
        Server release({{"/core/latest.json", manifest_for(binary.url(), up::sha256(next))}});
        ReleasesUrl env(release.url());
        up::update_core();  // "Try again"
        ASSERT_TRUE(wait_done());
        EXPECT_EQ(up::core().error, std::nullopt);
        EXPECT_EQ(up::core().version, std::optional<std::string>("99.0.0"));
        EXPECT_EQ(up::core().ready, std::nullopt);
        EXPECT_EQ(install.hello().value("host_version", ""), "99.0.0");
        EXPECT_EQ(install.get().value("process_state", ""), "Running");
    }
    up::set_pipe("");
}
