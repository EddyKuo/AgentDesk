// qa05_ui_detach (QA-05, design doc 6.2): close the UI, reopen it, and check that the session
// survived with the same PID and an unbroken counter. Uses an isolated pipe and data dir, so it
// never touches the user's real host. Writes evidence JSON to docs\poc\evidence\qa05-ui-detach.json.
//   qa05_ui_detach [--build-dir <dir>] [--gap-seconds 3]
#include <cstdio>
#include <exception>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include "devkit.hpp"

namespace dk = agentdesk::devkit;
namespace fs = dk::fs;
using dk::Json;

namespace {

Json first_session(const Json& report) {
    if (report.contains("sessions") && report["sessions"].is_array() && !report["sessions"].empty())
        return report["sessions"][0];
    return Json::object();
}

// The COUNT numbers in agentctl's `read --json` output, in order.
std::vector<long long> counter_numbers(const Json& history) {
    // Case-insensitive, as the earlier PowerShell script's -match was.
    const std::regex count_line("^COUNT (\\d+)$", std::regex::icase);
    std::vector<long long> numbers;
    for (const auto& line : history.value("lines", Json::array())) {
        std::smatch m;
        const std::string text = line.is_string() ? line.get<std::string>() : std::string();
        if (std::regex_match(text, m, count_line))
            numbers.push_back(std::stoll(m[1].str()));
    }
    return numbers;
}

// Runs agent-ui until it exits on its own (--exit-after-ms); kills it after 30 s.
void run_ui(const fs::path& ui, const std::vector<std::wstring>& args, const char* what) {
    const auto p = dk::start(ui, args);
    if (!dk::wait_exit(p, std::chrono::milliseconds(30000))) {
        dk::kill(p);
        throw std::runtime_error(std::string(what) + " did not exit");
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const dk::Args args(argc, argv);
        const fs::path root = dk::source_root();
        const fs::path build_dir = args.path_or(L"build-dir", (root / L"build" / L"msvc-x64-debug").native());
        const int gap_seconds = args.get_int(L"gap-seconds", 3);
        const fs::path ui = build_dir / L"apps" / L"desktop" / L"agent-ui.exe";
        const fs::path ctl = build_dir / L"apps" / L"desktop" / L"agentctl.exe";
        const fs::path probe = fs::canonical(build_dir / L"tools" / L"vt_probe.exe");
        const std::wstring pipe = L"\\\\.\\pipe\\AgentDeskQa05-" + dk::pid_string();
        const fs::path data = dk::temp_dir() / (L"agentdesk-qa05-" + dk::pid_string());
        fs::create_directories(data);
        dk::write_file(data / L"profiles.json", dk::profiles_json(Json::array({{{"id", "counter"},
                                                                                {"name", "counter"},
                                                                                {"executable", dk::u8(probe)},
                                                                                {"args", {"counter"}}}})));
        ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", data.c_str());
        const fs::path r1 = data / L"run1.json";
        const fs::path r2 = data / L"run2.json";

        // Run 1: the UI starts the host, creates the session, then closes after 4 s (detach).
        run_ui(
            ui,
            {L"--pipe", pipe, L"--start", L"counter", L"--exit-after-ms", L"4000", L"--report", r1.native()},
            "UI run 1");
        dk::sleep_ms(gap_seconds * 1000);
        // Run 2: reopen the UI without creating anything.
        run_ui(ui, {L"--pipe", pipe, L"--exit-after-ms", L"3000", L"--report", r2.native()}, "UI run 2");

        const Json a = dk::read_json(r1);
        const Json b = dk::read_json(r2);
        // By id: a session has a name only when someone gave it one.
        const std::wstring id = dk::wide(dk::field(first_session(a), "id").get<std::string>());
        const std::vector<long long> numbers = counter_numbers(Json::parse(
            dk::run(ctl, {L"--pipe", pipe, L"read", id, L"--lines", L"200", L"--json"}).out));
        int gaps = 0;
        for (std::size_t i = 1; i < numbers.size(); ++i) {
            if (numbers[i] != numbers[i - 1] + 1)
                ++gaps;
        }
        dk::run(ctl, {L"--pipe", pipe, L"shutdown", L"--stop-sessions"});

        const Json s1 = first_session(a);
        const Json s2 = first_session(b);
        Json result;
        result["checked_at"] = dk::utc_now_iso();
        result["same_session_id"] = dk::field(s1, "id") == dk::field(s2, "id");
        result["same_pid"] = dk::field(s1, "pid") == dk::field(s2, "pid");
        result["pid"] = dk::field(s2, "pid");
        result["state_after_reopen"] = dk::field(s2, "process_state");
        result["same_host_instance"] = dk::field(a, "host_instance_id") == dk::field(b, "host_instance_id");
        result["reconnected_ui_controls_input"] = dk::field(s2, "is_controller");
        result["counter_lines_checked"] = numbers.size();
        result["counter_first"] = numbers.empty() ? Json() : Json(numbers.front());
        result["counter_last"] = numbers.empty() ? Json() : Json(numbers.back());
        result["counter_gaps"] = gaps;
        result["pass"] = result["same_session_id"] == true && result["same_pid"] == true &&
                         result["state_after_reopen"] == "Running" && gaps == 0 && numbers.size() > 20;
        const auto text = dk::ps_json(result);
        // Same bytes as the PowerShell version's Set-Content -Encoding UTF8: BOM, trailing newline.
        dk::write_file(dk::evidence_path(L"qa05-ui-detach.json"), text + "\n", true);
        std::printf("%s\n", text.c_str());
        dk::remove_tree(data);
        return result["pass"] == true ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "qa05_ui_detach: %s\n", e.what());
        return 1;
    }
}
