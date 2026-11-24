// session_bench: M0 S1-9 measurements without UI (design doc 1.5, 5.2 items 5-6).
//   1. flood: vt_probe writes 1 MiB/s for N s through a real Session; checks that every line
//      arrived in order (last line number), reports throughput, backpressure and memory.
//   2. cycles: N x (start session, force stop, destroy); reports handle/thread counts.
//   3. snapshot: average cost of building a 120x40 snapshot.
// usage: session_bench --probe <vt_probe.exe> [--flood-seconds 60] [--rate 1048576] [--cycles 100]
#include <windows.h>
// psapi/tlhelp32 need windows.h first.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <regex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>
#include <psapi.h>
#include <tlhelp32.h>

#include "agentdesk/host/session.hpp"
#include "agentdesk/platform/win/pty_conpty.hpp"
#include "agentdesk/platform/win/error.hpp"

namespace win = agentdesk::platform::win;
namespace host = agentdesk::host;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {

std::filesystem::path exe_dir() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

DWORD handle_count() {
    DWORD n = 0;
    ::GetProcessHandleCount(::GetCurrentProcess(), &n);
    return n;
}

DWORD thread_count() {
    DWORD n = 0;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    THREADENTRY32 te{sizeof(te)};
    for (BOOL ok = ::Thread32First(snap, &te); ok; ok = ::Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID == ::GetCurrentProcessId())
            ++n;
    }
    ::CloseHandle(snap);
    return n;
}

nlohmann::json memory() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    ::GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                           sizeof(pmc));
    return {{"private_mb", static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0)},
            {"peak_working_set_mb", static_cast<double>(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0)}};
}

host::SessionConfig probe_config(const std::filesystem::path& probe,
                                 std::vector<std::string> args) {
    host::SessionConfig c;
    c.spec.executable = probe;
    c.spec.args = std::move(args);
    c.spec.cwd = std::filesystem::temp_directory_path();
    c.spec.size = {120, 40};
    c.engine.rows = 40;
    c.engine.cols = 120;
    return c;
}

// A history line's text: first code point of every cell, blanks as spaces, wide-character
// continuation cells skipped.
std::string history_line_text(const agentdesk::terminal::HistoryLine& line) {
    std::string text;
    for (const auto& cell : line.cells) {
        if (cell.width == 0)
            continue;
        if (cell.chars[0] == 0)
            text.push_back(' ');
        else
            agentdesk::terminal::append_utf8(text, cell.chars[0]);
    }
    return text;
}

struct HistoryCheck {
    bool any_numbered = false;
    long long gaps = 0;
};

// History continuity: consecutive kept lines must carry consecutive counters.
HistoryCheck check_history(const host::Session& session, const std::regex& line_re) {
    HistoryCheck check;
    long long prev = -1;
    for (const auto& line : session.history(0, 20000)) {
        const std::string text = history_line_text(line);
        std::smatch m;
        if (std::regex_search(text, m, line_re)) {
            const long long v = std::stoll(m[1]);
            if (prev >= 0 && v != prev + 1)
                ++check.gaps;
            prev = v;
        }
    }
    check.any_numbered = prev >= 0;
    return check;
}

nlohmann::json run_flood(const std::filesystem::path& probe, int seconds,
                         long long rate) {
    const auto start = Clock::now();
    auto session = host::Session::start(
        probe_config(probe, {"flood", std::to_string(seconds), std::to_string(rate)}));
    const auto code = session->wait_exit(std::chrono::seconds(seconds * 3 + 30));
    const auto elapsed = std::chrono::duration<double>(Clock::now() - start).count();
    const auto info = session->info();
    const auto snap = session->snapshot();

    long long done_lines = -1;
    long long last_line = -1;
    const std::regex done_re(R"(FLOOD-DONE (\d+))");
    const std::regex line_re(R"(^L(\d{10}) )");
    for (int r = 0; r < snap->rows; ++r) {
        const std::string text = snap->row_text(r);
        std::smatch m;
        if (std::regex_search(text, m, done_re))
            done_lines = std::stoll(m[1]);
        if (std::regex_search(text, m, line_re))
            last_line = std::stoll(m[1]);
    }
    const HistoryCheck history = check_history(*session, line_re);
    return {{"seconds", seconds},
            {"target_rate_bytes_per_s", rate},
            {"exit_code", code ? nlohmann::json(*code) : nlohmann::json(nullptr)},
            {"elapsed_s", elapsed},
            {"bytes_from_program", info.bytes_from_program},
            {"effective_rate_mib_per_s",
             static_cast<double>(info.bytes_from_program) / elapsed / (1024.0 * 1024.0)},
            {"lines_reported_by_probe", done_lines},
            {"last_numbered_line_on_screen", last_line},
            {"all_lines_arrived", done_lines > 0 && last_line == done_lines - 1},
            {"history_lines_checked", history.any_numbered},
            {"history_gaps", history.gaps},
            {"history_truncated", info.history_truncated},
            {"backpressure_waits", info.backpressure_waits},
            {"memory_after", memory()}};
}

nlohmann::json run_cycles(const std::filesystem::path& probe, int cycles) {
    nlohmann::json samples = nlohmann::json::array();
    {
        auto warm = host::Session::start(probe_config(probe, {"echo"}));
        warm->force_stop();
        warm->wait_exit(5s);
    }
    const DWORD h0 = handle_count();
    const DWORD t0 = thread_count();
    const auto start = Clock::now();
    for (int i = 1; i <= cycles; ++i) {
        auto s = host::Session::start(probe_config(probe, {"echo"}));
        s->force_stop();
        s->wait_exit(5s);
        s.reset();
        if (i == 1 || i % 25 == 0) {
            std::this_thread::sleep_for(50ms);
            samples.push_back({{"cycle", i}, {"handles", handle_count()}, {"threads", thread_count()}});
        }
    }
    return {
        {"cycles", cycles},
        {"baseline_handles", h0},
        {"baseline_threads", t0},
        {"avg_cycle_ms", std::chrono::duration<double, std::milli>(Clock::now() - start).count() / cycles},
        {"samples", samples}};
}

nlohmann::json run_snapshot_cost() {
    agentdesk::terminal::VtEngineOptions o;
    o.rows = 40;
    o.cols = 120;
    agentdesk::terminal::VtEngine e(o);
    for (int i = 0; i < 60; ++i)
        e.feed("\x1b[1;32mcoloured\x1b[0m text with 中文寬字 and more padding to fill the row\r\n");
    constexpr int kRounds = 500;
    const auto start = Clock::now();
    std::size_t cells = 0;
    for (int i = 0; i < kRounds; ++i)
        cells += e.snapshot().cells.size();
    const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count() / kRounds;
    return {{"grid", "120x40"}, {"avg_snapshot_us", us}, {"cells", cells / kRounds}};
}

// Interaction latency (design doc 1.5): key sent -> echoed character visible in the published
// snapshot.
// cmd.exe echoes typed characters itself (cooked input), so no model is involved.
nlohmann::json run_echo_latency(int rounds) {
    host::SessionConfig c;
    wchar_t sys[MAX_PATH]{};
    const UINT n = ::GetSystemDirectoryW(sys, MAX_PATH);
    c.spec.executable = std::filesystem::path(std::wstring(sys, n)) / L"cmd.exe";
    c.spec.args = {"/d", "/q"};
    c.spec.cwd = std::filesystem::temp_directory_path();
    c.spec.size = {250, 30};
    c.engine.rows = 30;
    c.engine.cols = 250;
    auto s = host::Session::start(c);
    std::this_thread::sleep_for(800ms);
    auto count_x = [&] {
        const auto snap = s->snapshot();
        const std::string row = snap->row_text(snap->cursor.row);
        return static_cast<int>(std::count(row.begin(), row.end(), 'x'));
    };
    const int base = count_x();
    std::vector<double> ms;
    for (int i = 1; i <= rounds; ++i) {
        const auto t0 = Clock::now();
        s->send(host::TextInput{U"x"});
        while (count_x() < base + i) {
            if (Clock::now() - t0 > 5s)
                break;
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        ms.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
    }
    s->force_stop();
    std::sort(ms.begin(), ms.end());
    auto pct = [&](double p) { return ms[static_cast<std::size_t>(p * static_cast<double>(ms.size() - 1))]; };
    return {{"rounds", rounds},    {"p50_ms", pct(0.50)}, {"p95_ms", pct(0.95)},
            {"p99_ms", pct(0.99)}, {"max_ms", ms.back()}, {"target_p95_ms", 100}};
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        std::filesystem::path probe;
        int flood_seconds = 60;
        long long rate = 1024 * 1024;
        int cycles = 100;
        for (int i = 1; i < argc; ++i) {
            const std::wstring a = argv[i];
            if (a == L"--probe" && i + 1 < argc)
                probe = argv[++i];
            else if (a == L"--flood-seconds" && i + 1 < argc)
                flood_seconds = std::stoi(argv[++i]);
            else if (a == L"--rate" && i + 1 < argc)
                rate = std::stoll(argv[++i]);
            else if (a == L"--cycles" && i + 1 < argc)
                cycles = std::stoi(argv[++i]);
        }
        if (probe.empty())
            throw std::invalid_argument("--probe is required");
        const auto api = win::pty_conpty();
        nlohmann::json report = {{"conpty", win::to_utf8(api.source)}, {"bundled_conpty", api.bundled}};
        report["snapshot"] = run_snapshot_cost();
        report["echo_latency"] = run_echo_latency(100);
        if (flood_seconds <= 0) {
            std::cout << report.dump(2) << std::endl;
            return 0;
        }
        report["flood"] = run_flood(probe, flood_seconds, rate);
        report["cycles"] = run_cycles(probe, cycles);
        report["memory_end"] = memory();
        std::cout << report.dump(2) << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "session_bench: " << e.what() << std::endl;
        return 2;
    }
}
