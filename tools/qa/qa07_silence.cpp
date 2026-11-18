// qa07_silence (QA-07, design doc 6.2): 60 s of silence never turns a state into Done. One
// session without any status source must stay Unknown; one with a trusted Working report must
// stay Working. Evidence: docs\poc\evidence\qa07-silence-60s.json
//   qa07_silence [--build-dir <dir>] [--seconds 60]
#include <cstdio>
#include <exception>
#include <string>

#include "devkit.hpp"

namespace dk = agentdesk::devkit;
namespace fs = dk::fs;
using dk::Json;

namespace {

Json state_of(const Json& list, const std::string& name, const char* key) {
    for (const auto& s : list.value("sessions", Json::array())) {
        if (s.value("name", "") == name)
            return s.contains(key) ? s[key] : Json();
    }
    return Json();
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const dk::Args args(argc, argv);
        const fs::path root = dk::source_root();
        const fs::path build_dir =
            args.path_or(L"build-dir", (root / L"build" / L"msvc-x64-release").native());
        const int seconds = args.get_int(L"seconds", 60);
        const fs::path ctl = build_dir / L"apps" / L"desktop" / L"agentctl.exe";
        const fs::path data = dk::temp_dir() / (L"agentdesk-qa07-" + dk::pid_string());
        fs::create_directories(data);
        const auto cmd = dk::u8(dk::system_dir() / L"cmd.exe");
        dk::write_file(
            data / L"profiles.json",
            dk::profiles_json(Json::array(
                {{{"id", "cmd"}, {"name", "cmd"}, {"executable", cmd}, {"args", Json::array()}}})));
        ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", data.c_str());
        const std::wstring pipe = L"\\\\.\\pipe\\AgentDeskQa07-" + dk::pid_string();
        const std::wstring cwd = dk::temp_dir().native() + L"\\";
        dk::run(ctl, {L"--pipe", pipe, L"run", L"--profile", L"cmd", L"--name", L"silent", L"--cwd", cwd});
        dk::run(ctl, {L"--pipe", pipe, L"run", L"--profile", L"cmd", L"--name", L"working", L"--cwd", cwd});
        dk::run(ctl, {L"--pipe", pipe, L"report-state", L"working", L"--source", L"qa07", L"--state",
                      L"working", L"--seq", L"1"});
        dk::sleep_ms(seconds * 1000);
        const Json list = Json::parse(dk::run(ctl, {L"--pipe", pipe, L"list", L"--json"}).out);
        dk::run(ctl, {L"--pipe", pipe, L"shutdown", L"--stop-sessions"});
        dk::sleep_ms(1000);
        dk::remove_tree(data);

        Json r;
        r["checked_at"] = dk::utc_now_iso();
        r["silence_seconds"] = seconds;
        r["no_source_state"] = state_of(list, "silent", "agent_state");
        r["trusted_state"] = state_of(list, "working", "agent_state");
        r["trusted_source"] = state_of(list, "working", "agent_source");
        r["pass"] = r["no_source_state"] == "Unknown" && r["trusted_state"] == "Working";
        const auto text = dk::ps_json(r);
        dk::write_file(root / L"docs" / L"poc" / L"evidence" / L"qa07-silence-60s.json", text);
        std::printf("%s\n", text.c_str());
        return r["pass"] == true ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "qa07_silence: %s\n", e.what());
        return 1;
    }
}
