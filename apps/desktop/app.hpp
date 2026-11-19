#pragma once
// AgentDesk window contents (design doc 4, ADR-12/ADR-13). main.cpp owns Win32, Direct3D and
// the event loop; everything drawn and every command lives here.
#include <windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentdesk/ui/fonts.hpp"
#include "agentdesk/ui/host_client.hpp"

namespace agentdesk::desktop {

struct AppConfig {
    HWND hwnd = nullptr;
    std::function<void()> wake;             // ask the loop for another frame
    std::string start_profile;              // developer option --start
    std::vector<std::string> run_commands;  // developer option --run "cmd;cmd" (after start)
    bool automated = false;                 // --exit-after-ms: no first-run notice, no sounds
    // Developer option --input "<step>;...": mouse and key events fed to ImGui as a person would
    // make them (QA screenshots; nothing outside the window is touched). "shot:<png>" writes the
    // path to `shot_signal` and waits until the screenshot tool has captured and removed it.
    std::string input_script;
    std::filesystem::path shot_signal;
};

// %AGENTDESK_DATA_DIR%, else %LOCALAPPDATA%\AgentDesk (created when missing).
std::filesystem::path data_folder();

class App {
public:
    App(AppConfig config, std::unique_ptr<ui::HostClient> host);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void load_fonts();  // after the ImGui context exists, before the first frame
    void apply_scale(float dpi);
    // Every wake of the loop, drawn or not (minimized, locked): sessions, attention (sounds,
    // toasts), mods and persistence. Touches no ImGui frame.
    void tick();
    void frame();               // between ImGui::NewFrame() and ImGui::Render()
    void before_new_frame();    // after the platform backend's NewFrame, before ImGui::NewFrame
    void on_close_requested();  // window X
    void on_focus_changed(bool focused);
    // A window chord keys.json binds in the shown layout or globally (Herdr 98's Ctrl+Esc).
    [[nodiscard]] bool binds_chord(int chord) const;
    void run_chord(int chord);  // runs the command keys.json binds to that window chord
    // Files dropped on the window: their paths go to the terminal under the pointer (else the
    // focused one) as a paste.
    void on_files_dropped(std::vector<std::string> paths);
    [[nodiscard]] bool exit_requested() const;
    // Earliest redraw needed without input (debounced resize, toasts, persistence).
    [[nodiscard]] long long next_wakeup_ms() const;
    [[nodiscard]] nlohmann::json report() const;  // developer option --report
    [[nodiscard]] ui::HostClient& host();
    // The loop ended (herdr main.rs 177-192 on_app_quit): telemetry's clean quit, then the
    // downloaded update goes in place.
    void on_quit();

    struct State;

private:
    std::unique_ptr<State> s_;
};

}  // namespace agentdesk::desktop
