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

Json field(const Json& obj, const char* key) {
    return obj.contains(key) ? obj[key] : Json();
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
        auto p = dk::start(ui, {L"--pipe", pipe, L"--start", L"counter", L"--exit-after-ms", L"4000",
                                L"--report", r1.native()});
        if (!dk::wait_exit(p, std::chrono::milliseconds(30000))) {
            dk::kill(p);
            throw std::runtime_error("UI run 1 did not exit");
        }
        dk::sleep_ms(gap_seconds * 1000);
        // Run 2: reopen the UI without creating anything.
        p = dk::start(ui, {L"--pipe", pipe, L"--exit-after-ms", L"3000", L"--report", r2.native()});
        if (!dk::wait_exit(p, std::chrono::milliseconds(30000))) {
            dk::kill(p);
            throw std::runtime_error("UI run 2 did not exit");
        }

        const Json a = dk::read_json(r1);
        const Json b = dk::read_json(r2);
        const Json history = Json::parse(
            dk::run(ctl, {L"--pipe", pipe, L"read", L"counter", L"--lines", L"200", L"--json"}).out);
        // PowerShell's -match is case-insensitive.
        const std::regex count_line("^COUNT (\\d+)$", std::regex::icase);
        std::vector<long long> numbers;
        for (const auto& line : history.value("lines", Json::array())) {
            std::smatch m;
            const std::string text = line.is_string() ? line.get<std::string>() : std::string();
            if (std::regex_match(text, m, count_line))
                numbers.push_back(std::stoll(m[1].str()));
        }
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
        result["same_session_id"] = field(s1, "id") == field(s2, "id");
        result["same_pid"] = field(s1, "pid") == field(s2, "pid");
        result["pid"] = field(s2, "pid");
        result["state_after_reopen"] = field(s2, "process_state");
        result["same_host_instance"] = field(a, "host_instance_id") == field(b, "host_instance_id");
        result["reconnected_ui_controls_input"] = field(s2, "is_controller");
        result["counter_lines_checked"] = numbers.size();
        result["counter_first"] = numbers.empty() ? Json() : Json(numbers.front());
        result["counter_last"] = numbers.empty() ? Json() : Json(numbers.back());
        result["counter_gaps"] = gaps;
        result["pass"] = result["same_session_id"] == true && result["same_pid"] == true &&
                         result["state_after_reopen"] == "Running" && gaps == 0 && numbers.size() > 20;
        const auto text = dk::ps_json(result);
        // Same bytes as the PowerShell version's Set-Content -Encoding UTF8: BOM, trailing newline.
        dk::write_file(root / L"docs" / L"poc" / L"evidence" / L"qa05-ui-detach.json", text + "\n", true);
        std::printf("%s\n", text.c_str());
        dk::remove_tree(data);
        return result["pass"] == true ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "qa05_ui_detach: %s\n", e.what());
        return 1;
    }
}
