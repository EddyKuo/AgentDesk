// qa10_ui_host_crash (QA-10, design doc 6.2), UI side: agent-host dies while the UI is open. The
// UI must show the session as disconnected, then, once a new host is running, as Lost; nothing
// is re-run. Isolated pipe and data dir. Evidence: docs\poc\evidence\qa10-ui-host-crash.json
//   qa10_ui_host_crash [--build-dir <dir>]
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

#include "devkit.hpp"

namespace dk = agentdesk::devkit;
namespace fs = dk::fs;
using dk::Json;

namespace {

std::string trim(std::string s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    const auto e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const dk::Args args(argc, argv);
        const fs::path root = dk::source_root();
        const fs::path build_dir = args.path_or(L"build-dir", (root / L"build" / L"msvc-x64-debug").native());
        const fs::path dir = build_dir / L"apps" / L"desktop";
        const fs::path ui = dir / L"agent-ui.exe";
        const fs::path ctl = dir / L"agentctl.exe";
        const fs::path probe = fs::canonical(build_dir / L"tools" / L"vt_probe.exe");
        const std::wstring pipe = L"\\\\.\\pipe\\AgentDeskQa10-" + dk::pid_string();
        const fs::path data = dk::temp_dir() / (L"agentdesk-qa10-" + dk::pid_string());
        fs::create_directories(data);
        dk::write_file(data / L"profiles.json", dk::profiles_json(Json::array({{{"id", "counter"},
                                                                                {"name", "counter"},
                                                                                {"executable", dk::u8(probe)},
                                                                                {"args", {"counter"}}}})));
        ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", data.c_str());
        const fs::path report = data / L"ui.json";

        const std::string sid = trim(dk::run(ctl, {L"--pipe", pipe, L"run", L"--profile", L"counter",
                                                   L"--name", L"victim", L"--cwd", dk::temp_dir().native()})
                                         .out);
        const Json before = Json::parse(dk::run(ctl, {L"--pipe", pipe, L"list", L"--json"}).out);
        const Json old_instance = before.value("host_instance_id", Json());
        const auto child_pid = before.at("sessions").at(0).at("pid").get<DWORD>();

        // UI open for 9 s; the host is killed at ~2 s and a new one started at ~4 s.
        const auto p =
            dk::start(ui, {L"--pipe", pipe, L"--exit-after-ms", L"9000", L"--report", report.native()});
        dk::sleep_ms(2000);
        const DWORD host = dk::host_pid(pipe);
        if (host == 0)
            throw std::runtime_error("host on the test pipe not found");
        {
            const agentdesk::platform::win::UniqueHandle h(
                ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, host));
            if (!h || !::TerminateProcess(h.get(), 1))
                throw std::runtime_error("cannot stop the test host");
            ::WaitForSingleObject(h.get(), 5000);
        }
        dk::sleep_ms(2000);
        const bool child_alive = dk::process_alive(child_pid);
        dk::run(ctl, {L"--pipe", pipe, L"list"});  // auto-starts a new host
        if (!dk::wait_exit(p, std::chrono::milliseconds(30000))) {
            dk::kill(p);
            throw std::runtime_error("UI did not exit");
        }
        const DWORD ui_exit = dk::exit_code(p);
        const Json after = dk::read_json(report);
        Json s = Json::object();
        for (const auto& x : after.value("sessions", Json::array())) {
            if (x.value("id", "") == sid)
                s = x;
        }
        dk::run(ctl, {L"--pipe", pipe, L"shutdown", L"--stop-sessions"});

        Json result;
        result["checked_at"] = dk::utc_now_iso();
        result["ui_exit_code"] = ui_exit;
        result["child_killed_with_host"] = !child_alive;
        result["ui_reconnected"] = dk::field(after, "connected");
        result["new_host_instance"] = after.value("host_instance_id", Json()) != old_instance;
        result["session_marked_lost"] = s.value("lost", false);
        result["session_state"] = dk::field(s, "process_state");
        result["session_not_controller"] = !s.value("is_controller", false);
        result["pass"] = ui_exit == 0 && !child_alive && result["ui_reconnected"] == true &&
                         result["new_host_instance"] == true && result["session_marked_lost"] == true;
        const auto text = dk::ps_json(result);
        // Same bytes as the PowerShell version's Set-Content -Encoding UTF8: BOM, trailing newline.
        dk::write_file(dk::evidence_path(L"qa10-ui-host-crash.json"), text + "\n", true);
        std::printf("%s\n", text.c_str());
        dk::remove_tree(data);
        return result["pass"] == true ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "qa10_ui_host_crash: %s\n", e.what());
        return 1;
    }
}
