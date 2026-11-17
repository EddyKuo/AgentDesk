// soak: long-running stability check of agent-host (design doc 1.5 "穩定性", 6.1).
// Starts an isolated host, keeps 4 sessions producing output, repeatedly attaches/detaches
// clients and creates/stops sessions, and samples the host's handles, threads and private
// bytes. Prints a JSON report; "pass" requires no sustained growth.
//   soak --probe <vt_probe.exe> --host <agent-host.exe> [--minutes 30] [--sample-seconds 30]
#include <windows.h>
// psapi/tlhelp32 need windows.h first.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <psapi.h>
#include <tlhelp32.h>

#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/unique_handle.hpp"

namespace win = agentdesk::platform::win;
namespace ipc = agentdesk::ipc;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {

struct Sample {
    double minute = 0;
    DWORD handles = 0;
    DWORD threads = 0;
    double private_mb = 0;
};

DWORD thread_count(DWORD pid) {
    DWORD n = 0;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    THREADENTRY32 te{sizeof(te)};
    for (BOOL ok = ::Thread32First(snap, &te); ok; ok = ::Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID == pid)
            ++n;
    }
    ::CloseHandle(snap);
    return n;
}

Sample sample(HANDLE process, DWORD pid, double minute) {
    Sample s;
    s.minute = minute;
    ::GetProcessHandleCount(process, &s.handles);
    s.threads = thread_count(pid);
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    ::GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
    s.private_mb = static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0);
    return s;
}

// Average of the middle (last=false) or last (last=true) third. The first third is warm-up:
// exited sessions are retained until the retention limit fills, which is expected growth.
template <typename F>
double third_avg(const std::vector<Sample>& v, bool last, F field) {
    const std::size_t n = std::max<std::size_t>(1, v.size() / 3);
    const std::size_t begin = last ? v.size() - n : std::min(n, v.size() - 1);
    double sum = 0;
    for (std::size_t i = 0; i < n && begin + i < v.size(); ++i)
        sum += field(v[begin + i]);
    return sum / static_cast<double>(n);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::filesystem::path probe;
    std::filesystem::path host_exe;
    double minutes = 30;
    int sample_seconds = 30;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--probe" && i + 1 < argc)
            probe = argv[++i];
        else if (a == L"--host" && i + 1 < argc)
            host_exe = argv[++i];
        else if (a == L"--minutes" && i + 1 < argc)
            minutes = std::stod(argv[++i]);
        else if (a == L"--sample-seconds" && i + 1 < argc)
            sample_seconds = std::stoi(argv[++i]);
    }
    if (probe.empty() || host_exe.empty()) {
        std::cerr << "usage: soak --probe <vt_probe.exe> --host <agent-host.exe> [--minutes N]\n";
        return 2;
    }

    const std::wstring pipe = L"\\\\.\\pipe\\AgentDeskSoak-" + std::to_wstring(::GetCurrentProcessId());
    const auto data = std::filesystem::temp_directory_path() /
                      (L"agentdesk-soak-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(data);
    const std::string probe_u8 = win::to_utf8(probe.native());
    Json profiles = Json::array({
        {{"id", "counter"}, {"name", "counter"}, {"executable", probe_u8}, {"args", {"counter"}}},
        {{"id", "flood"}, {"name", "flood"}, {"executable", probe_u8}, {"args", {"flood", "30", "262144"}}},
        {{"id", "echo"}, {"name", "echo"}, {"executable", probe_u8}, {"args", {"echo"}}},
    });
    std::ofstream(data / L"profiles.json") << Json{{"schema_version", 1}, {"profiles", profiles}}.dump(2);
    ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", data.c_str());

    std::wstring cmd = win::build_command_line(host_exe.native(), {L"--pipe", pipe});
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(host_exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr,
                          nullptr, &si, &pi)) {
        std::cerr << "cannot start host\n";
        return 3;
    }
    win::UniqueHandle host(pi.hProcess);
    ::CloseHandle(pi.hThread);
    auto control = ipc::PipeClient::connect(pipe, 10s);
    if (!control) {
        std::cerr << "host unreachable\n";
        return 3;
    }
    const auto cwd = win::to_utf8(std::filesystem::temp_directory_path().native());
    auto create = [&](const char* profile, const std::string& name) {
        const auto r =
            control->call("session.create", {{"profile_id", profile}, {"cwd", cwd}, {"name", name}}, 15s);
        return r && r->result ? r->result->value("session_id", "") : std::string();
    };
    std::vector<std::string> steady = {create("counter", "c1"), create("counter", "c2"),
                                       create("flood", "f1"), create("flood", "f2")};

    std::vector<Sample> samples;
    const auto start = Clock::now();
    const auto end = start + std::chrono::duration_cast<Clock::duration>(
                                 std::chrono::duration<double, std::ratio<60>>(minutes));
    auto next_sample = start;
    auto next_churn = start;
    auto next_attach = start;
    std::uint64_t attach_cycles = 0;
    std::uint64_t churn_cycles = 0;
    std::uint64_t flood_restarts = 0;
    int round = 0;
    while (Clock::now() < end) {
        const auto now = Clock::now();
        const double minute = std::chrono::duration<double, std::ratio<60>>(now - start).count();
        if (now >= next_sample) {
            samples.push_back(sample(host.get(), pi.dwProcessId, minute));
            next_sample = now + std::chrono::seconds(sample_seconds);
        }
        if (now >= next_attach) {  // a UI coming and going
            if (auto c = ipc::PipeClient::connect(pipe, 5s)) {
                for (const auto& id : steady)
                    c->call("session.attach", {{"session", id}});
                std::this_thread::sleep_for(500ms);
                c->close();
                ++attach_cycles;
            }
            next_attach = now + 5s;
        }
        if (now >= next_churn) {  // create/stop lifecycle churn (design doc 1.5: 100 create/stop)
            const auto id = create("echo", "churn" + std::to_string(round++));
            control->call("session.stop", {{"session", id}, {"mode", "force"}});
            ++churn_cycles;
            // Replace finished flood sessions so output keeps flowing.
            const auto list = control->call("session.list", Json::object());
            if (list && list->result) {
                for (auto& sid : steady) {
                    for (const auto& s : list->result->at("sessions")) {
                        if (s["id"] == sid && s["process_state"] == "Exited" &&
                            s["name"].get<std::string>().starts_with("f")) {
                            sid = create("flood", s["name"].get<std::string>());
                            ++flood_restarts;
                        }
                    }
                }
            }
            next_churn = now + 20s;
        }
        std::this_thread::sleep_for(200ms);
    }
    samples.push_back(sample(host.get(), pi.dwProcessId, minutes));
    control->call("host.shutdown", {{"stop_sessions", true}});
    ::WaitForSingleObject(host.get(), 10000);
    std::filesystem::remove_all(data);

    const double h0 =
        third_avg(samples, false, [](const Sample& s) { return static_cast<double>(s.handles); });
    const double h1 =
        third_avg(samples, true, [](const Sample& s) { return static_cast<double>(s.handles); });
    const double t0 =
        third_avg(samples, false, [](const Sample& s) { return static_cast<double>(s.threads); });
    const double t1 =
        third_avg(samples, true, [](const Sample& s) { return static_cast<double>(s.threads); });
    const double m0 = third_avg(samples, false, [](const Sample& s) { return s.private_mb; });
    const double m1 = third_avg(samples, true, [](const Sample& s) { return s.private_mb; });
    Json js = Json::array();
    for (const auto& s : samples)
        js.push_back({{"minute", s.minute},
                      {"handles", s.handles},
                      {"threads", s.threads},
                      {"private_mb", s.private_mb}});
    // "No sustained growth": the last third may not exceed the first third by more than a small
    // allowance (sessions are replaced, so counts fluctuate).
    const bool pass = h1 <= h0 * 1.10 + 10 && t1 <= t0 + 4 && m1 <= m0 * 1.25 + 8;
    const Json report = {{"minutes", minutes},
                         {"attach_cycles", attach_cycles},
                         {"create_stop_cycles", churn_cycles},
                         {"flood_restarts", flood_restarts},
                         {"middle_third", {{"handles", h0}, {"threads", t0}, {"private_mb", m0}}},
                         {"last_third", {{"handles", h1}, {"threads", t1}, {"private_mb", m1}}},
                         {"pass", pass},
                         {"samples", js}};
    std::cout << report.dump(2) << std::endl;
    return pass ? 0 : 1;
}
