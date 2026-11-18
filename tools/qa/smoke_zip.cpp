// smoke_zip (S5-4): smoke test of the portable zip in a clean directory. Uses only the zip's
// executables (vt_probe from the build is the fake agent that reports OSC 7501). Isolated pipe
// and data dir. Evidence: docs\poc\evidence\m4-smoke-zip.json
//   smoke_zip [--zip <zip>] [--probe <vt_probe.exe>]
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

#include "devkit.hpp"

namespace dk = agentdesk::devkit;
namespace fs = dk::fs;
using dk::Json;

namespace {

Json find_session(const Json& list, const std::string& name) {
    if (list.contains("sessions")) {
        for (const auto& s : list["sessions"]) {
            if (s.value("name", "") == name)
                return s;
        }
    }
    return Json::object();
}

Json field(const Json& obj, const char* key) {
    return obj.contains(key) ? obj[key] : Json();
}

bool starts_with_icase(const std::wstring& text, const std::wstring& prefix) {
    return text.size() >= prefix.size() && _wcsnicmp(text.c_str(), prefix.c_str(), prefix.size()) == 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const dk::Args args(argc, argv);
        const fs::path root = dk::source_root();
        const fs::path zip = args.path_or(L"zip", (root / L"dist" / L"AgentDesk-0.1.0-win-x64.zip").native());
        const fs::path probe = args.path_or(
            L"probe", (root / L"build" / L"msvc-x64-release" / L"tools" / L"vt_probe.exe").native());
        const fs::path work = dk::temp_dir() / (L"agentdesk-smoke-" + dk::pid_string());
        fs::create_directories(work);
        if (dk::run(dk::system_dir() / L"tar.exe", {L"-x", L"-f", zip.native(), L"-C", work.native()})
                .exit_code != 0)
            throw std::runtime_error("cannot extract " + dk::u8(zip));
        const fs::path app = work / L"AgentDesk-0.1.0-win-x64";
        const fs::path data = work / L"data";
        fs::create_directories(data);
        const auto cmd = dk::u8(dk::system_dir() / L"cmd.exe");
        dk::write_file(
            data / L"profiles.json",
            dk::profiles_json(Json::array({
                {{"id", "probe"}, {"name", "probe"}, {"executable", dk::u8(probe)}, {"args", {"report"}}},
                {{"id", "cmd"}, {"name", "cmd"}, {"executable", cmd}, {"args", Json::array()}},
            })));
        ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", data.c_str());
        const std::wstring pipe = L"\\\\.\\pipe\\AgentDeskSmoke-" + dk::pid_string();
        const fs::path ctl = app / L"agentctl.exe";
        auto list = [&] { return Json::parse(dk::run(ctl, {L"--pipe", pipe, L"list", L"--json"}).out); };

        Json r;
        r["checked_at"] = dk::utc_now_iso();
        r["zip"] = dk::u8(zip.filename());
        const auto first = dk::run(ctl, {L"--pipe", pipe, L"run", L"--profile", L"cmd", L"--name", L"shell",
                                         L"--cwd", work.native()});
        r["host_autostarted"] = first.exit_code == 0;
        dk::run(ctl, {L"--pipe", pipe, L"run", L"--profile", L"probe", L"--name", L"agent", L"--cwd",
                      work.native()});
        dk::sleep_ms(3000);
        const Json agent = find_session(list(), "agent");
        r["osc7501_state"] = field(agent, "agent_state");
        // probe exits after reporting: state is Unknown, last kept
        r["osc7501_last_state"] = field(agent, "last_agent_state");
        r["osc7501_source"] = field(agent, "agent_source");
        r["bundled_conpty"] = dk::read_file(data / L"host.log").find("conpty=bundled") != std::string::npos;
        const DWORD host = dk::host_pid(pipe);
        r["host_from_zip"] = host != 0 && starts_with_icase(dk::process_image_path(host), app.native());

        const fs::path report = work / L"ui.json";
        const auto ui = dk::start(app / L"agent-ui.exe", {L"--pipe", pipe, L"--exit-after-ms", L"4000",
                                                          L"--report", report.native()});
        dk::wait_exit(ui, std::chrono::milliseconds(30000));
        const Json ui_report = dk::read_json(report);
        r["ui_exit_code"] = dk::exit_code(ui);
        r["ui_connected"] = field(ui_report, "connected");
        r["ui_sessions"] = ui_report.contains("sessions") && ui_report["sessions"].is_array()
                               ? ui_report["sessions"].size()
                               : std::size_t{0};
        r["ui_tabs"] = field(ui_report, "tabs");
        int running = 0;
        for (const auto& s : list().value("sessions", Json::array())) {
            if (s.value("process_state", "") == "Running")
                ++running;
        }
        r["sessions_survive_ui_exit"] = running;
        dk::run(ctl, {L"--pipe", pipe, L"shutdown", L"--stop-sessions"});
        dk::sleep_ms(1000);
        r["notices_present"] = fs::exists(app / L"THIRD_PARTY_NOTICES.md") && fs::exists(app / L"LICENSE") &&
                               fs::exists(app / L"conpty" / L"NOTICE.md");
        r["pass"] = r["host_autostarted"] == true && r["host_from_zip"] == true &&
                    r["bundled_conpty"] == true && r["osc7501_last_state"] == "Blocked" &&
                    r["osc7501_source"] == "osc7501" && r["ui_exit_code"] == 0 && r["ui_connected"] == true &&
                    r["ui_sessions"] == 2 && running >= 1 && r["notices_present"] == true;
        const auto text = dk::ps_json(r);
        dk::write_file(root / L"docs" / L"poc" / L"evidence" / L"m4-smoke-zip.json", text);
        std::printf("%s\n", text.c_str());
        dk::remove_tree(work);
        return r["pass"] == true ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "smoke_zip: %s\n", e.what());
        return 1;
    }
}
