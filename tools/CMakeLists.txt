# Developer and test tools (not shipped).
agentdesk_executable(vt_probe vt_probe/vt_probe.cpp)

# Headless host for M0 experiments: program in ConPTY + VtEngine, JSON report.
agentdesk_executable(pty_run pty_run/pty_run.cpp)
target_link_libraries(pty_run PRIVATE agentdesk::platform_win agentdesk::terminal nlohmann_json::nlohmann_json)
# Bundled ConPTY next to the tools so load_conpty(exe_dir) finds it (ADR-08).
set(AGENTDESK_CONPTY_BIN "${PROJECT_SOURCE_DIR}/third_party/conpty/bin/conpty")
if(EXISTS "${AGENTDESK_CONPTY_BIN}/conpty.dll")
    add_custom_command(TARGET pty_run POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${AGENTDESK_CONPTY_BIN}" "$<TARGET_FILE_DIR:pty_run>/conpty")
endif()
