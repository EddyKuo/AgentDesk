// pty_run: headless host for M0 experiments (S1-8 end-to-end, S1-9 measurements).
// Runs a program in a pseudoconsole (bundled ConPTY when present) wired to a VtEngine that
// answers terminal queries, optionally types scripted input, then prints a JSON report:
// screen text, OSC 7501 status timeline, query/answer facts, byte counts and timings.
//
// usage: pty_run [--conpty-dir DIR] [--cols N] [--rows N] [--timeout-ms N]
//                [--send-after MS TEXT]... [--quiet-exit-ms N] -- program args...
// TEXT escapes: \r \n \e \x03 (Ctrl+C) \\ .
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentdesk/platform/win/conpty_api.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/pty_process.hpp"
#include "agentdesk/terminal/vt_engine.hpp"

namespace win = agentdesk::platform::win;
namespace term = agentdesk::terminal;
using Clock = std::chrono::steady_clock;

namespace {

struct ScriptStep {
    std::chrono::milliseconds after{};
    std::string bytes;
};

struct Options {
    std::filesystem::path conpty_dir;
    short cols = 120;
    short rows = 40;
    std::chrono::milliseconds timeout{30000};
    std::chrono::milliseconds quiet_exit{0};  // 0 = wait for the program to exit
    std::vector<ScriptStep> script;
    std::filesystem::path program;
    std::vector<std::wstring> args;
};

std::string unescape(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out.push_back(s[i]);
            continue;
        }
        const char n = s[++i];
        switch (n) {
            case 'r':
                out.push_back('\r');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'e':
                out.push_back('\x1b');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case 'x':
                if (i + 2 < s.size()) {  // needs two hex digits after 'x'
                    out.push_back(static_cast<char>(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16)));
                    i += 2;
                }
                break;
            default:
                out.push_back(n);
                break;
        }
    }
    return out;
}

std::filesystem::path exe_dir() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

Options parse(int argc, wchar_t** argv) {
    Options o;
    o.conpty_dir = exe_dir();
    int i = 1;
    for (; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--") {
            ++i;
            break;
        }
        if (a == L"--conpty-dir" && i + 1 < argc)
            o.conpty_dir = argv[++i];
        else if (a == L"--cols" && i + 1 < argc)
            o.cols = static_cast<short>(std::stoi(argv[++i]));
        else if (a == L"--rows" && i + 1 < argc)
            o.rows = static_cast<short>(std::stoi(argv[++i]));
        else if (a == L"--timeout-ms" && i + 1 < argc)
            o.timeout = std::chrono::milliseconds(std::stoll(argv[++i]));
        else if (a == L"--quiet-exit-ms" && i + 1 < argc)
            o.quiet_exit = std::chrono::milliseconds(std::stoll(argv[++i]));
        else if (a == L"--send-after" && i + 2 < argc) {
            ScriptStep step;
            step.after = std::chrono::milliseconds(std::stoll(argv[++i]));
            step.bytes = unescape(win::to_utf8(argv[++i]));
            o.script.push_back(std::move(step));
        } else {
            throw std::invalid_argument("unknown option: " + win::to_utf8(a));
        }
    }
    if (i >= argc)
        throw std::invalid_argument("missing -- program");
    o.program = argv[i++];
    for (; i < argc; ++i)
        o.args.emplace_back(argv[i]);
    return o;
}

std::filesystem::path resolve_program(const std::filesystem::path& p) {
    if (p.has_parent_path())
        return p;
    wchar_t buf[MAX_PATH]{};
    if (::SearchPathW(nullptr, p.c_str(), L".exe", MAX_PATH, buf, nullptr) > 0)
        return buf;
    return p;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const Options opt = parse(argc, argv);
        const auto api = win::load_conpty(opt.conpty_dir);

        std::mutex mu;
        std::condition_variable cv;
        std::deque<std::string> pending;
        bool stop = false;
        std::size_t bytes_in = 0;
        auto last_output = Clock::now();
        const auto start = Clock::now();
        nlohmann::json timeline = nlohmann::json::array();
        std::unique_ptr<win::PtyProcess> proc;

        auto ms_since_start = [&] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        };
        term::EngineEvents events;
        events.on_status = [&](const term::StatusReport& r) {  // runs under `mu` inside feed()
            timeline.push_back({{"t_ms", ms_since_start()},
                                {"id", r.id},
                                {"state", term::to_string(r.state)},
                                {"kind", term::to_string(r.kind)},
                                {"msg", r.msg}});
        };
        term::VtEngineOptions engine_options;
        engine_options.rows = opt.rows;
        engine_options.cols = opt.cols;
        term::VtEngine engine(engine_options, std::move(events));

        std::thread writer([&] {
            std::unique_lock lock(mu);
            for (;;) {
                cv.wait(lock, [&] { return stop || (!pending.empty() && proc); });
                if (stop)
                    return;
                std::string chunk = std::move(pending.front());
                pending.pop_front();
                lock.unlock();
                try {
                    proc->write(chunk);
                } catch (const std::exception&) {
                }
                lock.lock();
            }
        });

        win::PtySpawnSpec spec;
        spec.executable = resolve_program(opt.program);
        spec.args = opt.args;
        spec.cwd = std::filesystem::current_path();
        spec.size = {opt.cols, opt.rows};
        auto p = win::PtyProcess::spawn(api, spec, [&](std::span<const char> b) {
            {
                std::lock_guard lock(mu);
                bytes_in += b.size();
                last_output = Clock::now();
                engine.feed(std::string_view(b.data(), b.size()));
                if (auto r = engine.take_pty_input(); !r.empty())
                    pending.push_back(std::move(r));
            }
            cv.notify_all();
        });
        {
            std::lock_guard lock(mu);
            proc = std::move(p);
        }
        cv.notify_all();

        std::size_t next_step = 0;
        std::optional<DWORD> exit_code;
        for (;;) {
            exit_code = proc->wait_exit(std::chrono::milliseconds(20));
            if (exit_code)
                break;
            const auto now = Clock::now();
            if (now - start > opt.timeout)
                break;
            if (next_step < opt.script.size() && now - start >= opt.script[next_step].after) {
                std::lock_guard lock(mu);
                pending.push_back(opt.script[next_step++].bytes);
                cv.notify_all();
            }
            if (opt.quiet_exit.count() > 0 && next_step >= opt.script.size()) {
                std::lock_guard lock(mu);
                if (now - last_output > opt.quiet_exit)
                    break;
            }
        }
        const bool timed_out = !exit_code.has_value();
        proc->close();
        {
            std::lock_guard lock(mu);
            stop = true;
        }
        cv.notify_all();
        writer.join();
        if (exit_code)
            engine.on_process_exit();

        const auto snap = engine.snapshot();
        nlohmann::json screen = nlohmann::json::array();
        for (int r = 0; r < snap.rows; ++r)
            screen.push_back(snap.row_text(r));
        nlohmann::json report = {
            {"conpty", win::to_utf8(api.source)},
            {"bundled_conpty", api.bundled},
            {"program", win::to_utf8(spec.executable.native())},
            {"exit_code", exit_code ? nlohmann::json(*exit_code) : nlohmann::json(nullptr)},
            {"timed_out_or_quiet", timed_out},
            {"elapsed_ms", ms_since_start()},
            {"bytes_from_program", bytes_in},
            {"osc7501_query_answered", engine.status_query_answered()},
            {"status_timeline", timeline},
            {"title", snap.title},
            {"screen", screen},
        };
        std::cout << report.dump(2) << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "pty_run: " << e.what() << std::endl;
        return 2;
    }
}
