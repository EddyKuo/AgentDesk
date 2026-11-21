# Test executables (GoogleTest): unit_tests and win_integration_tests, plus the helper binaries
# the tests start (a test mod, a fake agent-ui, a newer-version agent-host).
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

set(AGENTDESK_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# Unit layer: pure logic, no I/O beyond fixture files, no child processes.
agentdesk_executable(unit_tests
    unit/toolchain_smoke_test.cpp
    unit/program_status_test.cpp
    unit/cwd_uri_test.cpp
    unit/vt_engine_test.cpp
    unit/vt_handover_test.cpp
    unit/command_line_test.cpp
    unit/input_mapping_test.cpp
    unit/protocol_test.cpp
    unit/remote_screen_test.cpp
    unit/workspace_test.cpp
    unit/chrome_logic_test.cpp
    unit/mods_test.cpp
    unit/updates_test.cpp
    unit/telemetry_test.cpp
    unit/programs_test.cpp
    unit/layout_test.cpp
    unit/sprites_test.cpp
    unit/safe_read_test.cpp
    unit/window_place_test.cpp
    unit/mac_layout_test.cpp
    unit/focus_mode_test.cpp
    unit/mission_test.cpp
    unit/inbox_test.cpp
    unit/review_test.cpp
    unit/timeline_test.cpp
    unit/state_timeline_test.cpp
    unit/color_math_test.cpp
    unit/schedule_test.cpp
    unit/theme_file_test.cpp
    unit/ambient_test.cpp
    unit/layout_styles_test.cpp
    unit/terminals_options_test.cpp
    unit/classic_layout_test.cpp
    unit/window_buttons_test.cpp
    unit/layout_tips_test.cpp
    unit/design_tokens_test.cpp)
# agent-ui's header-only helpers (apps/desktop/safe_read.hpp).
target_include_directories(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/apps/desktop)
target_link_libraries(unit_tests PRIVATE
    agentdesk::ui agentdesk::protocol agentdesk::terminal agentdesk::platform_win agentdesk::vterm
    nlohmann_json::nlohmann_json GTest::gtest_main GTest::gmock shell32)
# A mod DLL for the mod tests (tests/fixtures/mods/test_mod.cpp), built as test_mods/mod.dll.
add_library(test_mod SHARED fixtures/mods/test_mod.cpp)
agentdesk_target_defaults(test_mod)
target_link_libraries(test_mod PRIVATE agentdesk::modapi)
set(_test_mods "${CMAKE_CURRENT_BINARY_DIR}/test_mods")
set_target_properties(test_mod PROPERTIES OUTPUT_NAME mod RUNTIME_OUTPUT_DIRECTORY "${_test_mods}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${_test_mods}" RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_test_mods}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_test_mods}")
add_dependencies(unit_tests test_mod)
# The program table and layout files the tests read straight from the source tree.
target_compile_definitions(unit_tests PRIVATE AGENTDESK_FIXTURE_DIR="${AGENTDESK_FIXTURE_DIR}"
    AGENTDESK_PROGRAMS_DIR="${PROJECT_SOURCE_DIR}/third_party/herdr/programs"
    AGENTDESK_PRESET_LAYOUTS_DIR="${PROJECT_SOURCE_DIR}/third_party/herdr/layouts"
    AGENTDESK_OWN_LAYOUTS_DIR="${PROJECT_SOURCE_DIR}/libs/ui/layouts"
    AGENTDESK_OWN_THEMES_DIR="${PROJECT_SOURCE_DIR}/libs/ui/themes"
    AGENTDESK_TEST_MOD=L"$<TARGET_FILE:test_mod>")
gtest_discover_tests(unit_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS "unit" TIMEOUT 60)

# Windows integration layer: real child processes in ConPTY + job objects.
agentdesk_executable(win_integration_tests
    integration/windows/conpty_test.cpp
    integration/windows/conpty_compare_test.cpp
    integration/windows/session_test.cpp
    integration/windows/pipe_test.cpp
    integration/windows/host_test.cpp
    integration/windows/handover_test.cpp
    integration/windows/updates_test.cpp)
target_link_libraries(win_integration_tests PRIVATE psapi advapi32 ws2_32 agentdesk::ui
    agentdesk::host agentdesk::ipc agentdesk::platform_win agentdesk::terminal nlohmann_json::nlohmann_json GTest::gtest_main)
# A stand-in agent-ui.exe for the update tests (tests/fixtures/fake_agent_ui.cpp).
add_executable(fake_agent_ui fixtures/fake_agent_ui.cpp)
agentdesk_target_defaults(fake_agent_ui)
# agent-host built as a later release (99.0.0), for agent-host's own update (design doc 7.4 OQ-09).
add_executable(agent_host_next WIN32 ${PROJECT_SOURCE_DIR}/apps/host/main.cpp)
agentdesk_target_defaults(agent_host_next)
target_link_libraries(agent_host_next PRIVATE agentdesk::host agentdesk::ipc shell32)
target_compile_definitions(agent_host_next PRIVATE AGENTDESK_VERSION_STRING="99.0.0")
set(_host_next "${CMAKE_CURRENT_BINARY_DIR}/host_next")
set_target_properties(agent_host_next PROPERTIES OUTPUT_NAME agent-host RUNTIME_OUTPUT_DIRECTORY "${_host_next}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${_host_next}" RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_host_next}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_host_next}")
add_dependencies(win_integration_tests vt_probe agent-host agentctl agent-ui fake_agent_ui agent_host_next)
target_compile_definitions(win_integration_tests PRIVATE
    AGENTDESK_VT_PROBE=L"$<TARGET_FILE:vt_probe>"
    AGENTDESK_FAKE_UI_EXE=L"$<TARGET_FILE:fake_agent_ui>"
    AGENTDESK_HOST_EXE=L"$<TARGET_FILE:agent-host>"
    AGENTDESK_HOST_NEXT_EXE=L"$<TARGET_FILE:agent_host_next>"
    AGENTDESK_CTL_EXE=L"$<TARGET_FILE:agentctl>"
    AGENTDESK_UI_DIR_CTL_EXE=L"$<TARGET_FILE_DIR:agent-ui>/agentctl.exe"
    AGENTDESK_BUNDLED_CONPTY_DIR=L"${PROJECT_SOURCE_DIR}/third_party/conpty/bin")
gtest_discover_tests(win_integration_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "windows_integration" TIMEOUT 120)
