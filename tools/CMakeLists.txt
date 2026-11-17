# Developer and test tools (not shipped).
agentdesk_executable(vt_probe vt_probe/vt_probe.cpp)

# Headless host for M0 experiments: program in ConPTY + VtEngine, JSON report.
agentdesk_executable(pty_run pty_run/pty_run.cpp)
target_link_libraries(pty_run PRIVATE agentdesk::platform_win agentdesk::terminal nlohmann_json::nlohmann_json)
agentdesk_bundle_conpty(pty_run)
