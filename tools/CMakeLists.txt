# Developer and test tools (not shipped). vt_probe, the fake agent, runs on both systems.
agentdesk_executable(vt_probe vt_probe/vt_probe.cpp
    $<IF:$<BOOL:${WIN32}>,vt_probe/vt_probe_win.cpp,vt_probe/vt_probe_posix.cpp>)
if(NOT WIN32)
    return()  # the other tools drive ConPTY and the Win32 UI (macOS: ADR-30)
endif()

# Headless host for M0 experiments: program in ConPTY + VtEngine, JSON report.
agentdesk_executable(pty_run pty_run/pty_run.cpp)
target_link_libraries(pty_run PRIVATE agentdesk::platform_win agentdesk::terminal nlohmann_json::nlohmann_json)
agentdesk_bundle_conpty(pty_run)

# M0 S1-9 measurements: flood throughput, lifecycle leaks, snapshot cost.
agentdesk_executable(session_bench session_bench/session_bench.cpp)
target_link_libraries(session_bench PRIVATE agentdesk::host nlohmann_json::nlohmann_json psapi)
agentdesk_bundle_conpty(session_bench)

# Long-running stability check of agent-host (M4).
agentdesk_executable(soak soak/soak.cpp)
target_link_libraries(soak PRIVATE agentdesk::ipc nlohmann_json::nlohmann_json psapi)

# Shared helpers of the tools below: processes, files, hashes, evidence JSON layout.
agentdesk_library(agentdesk_devkit devkit/devkit.cpp)
target_include_directories(agentdesk_devkit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/devkit)
target_compile_definitions(agentdesk_devkit PRIVATE AGENTDESK_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(agentdesk_devkit PUBLIC agentdesk::ipc nlohmann_json::nlohmann_json bcrypt)

# Byte-exact VT fixtures under tests/fixtures/bytes.
agentdesk_executable(make_fixtures make_fixtures/make_fixtures.cpp)
target_link_libraries(make_fixtures PRIVATE agentdesk_devkit)

# Pinned ConPTY download (ADR-08) into third_party/conpty/bin.
agentdesk_executable(fetch_conpty fetch_conpty/fetch_conpty.cpp)
target_link_libraries(fetch_conpty PRIVATE agentdesk_devkit winhttp)

# Portable zip and symbols zip in dist/ (M4).
agentdesk_executable(agentdesk_package package/package.cpp)
target_link_libraries(agentdesk_package PRIVATE agentdesk_devkit)

# QA checks with an isolated host each; they write docs/poc/evidence/*.json.
foreach(qa smoke_zip qa05_ui_detach qa07_silence qa10_ui_host_crash screenshot_ui)
    agentdesk_executable(${qa} qa/${qa}.cpp)
    target_link_libraries(${qa} PRIVATE agentdesk_devkit)
    set_target_properties(${qa} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qa)
endforeach()
target_link_libraries(screenshot_ui PRIVATE windowscodecs ole32)
