find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

set(AGENTDESK_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# Unit layer: pure logic, no I/O beyond fixture files, no child processes.
agentdesk_executable(unit_tests
    unit/toolchain_smoke_test.cpp
    unit/program_status_test.cpp
    unit/cwd_uri_test.cpp
    unit/vt_engine_test.cpp
    unit/command_line_test.cpp
    unit/input_mapping_test.cpp
    unit/protocol_test.cpp
    unit/remote_screen_test.cpp
    unit/workspace_test.cpp
    unit/chrome_logic_test.cpp
    unit/mods_test.cpp)
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
target_compile_definitions(unit_tests PRIVATE AGENTDESK_FIXTURE_DIR="${AGENTDESK_FIXTURE_DIR}"
    AGENTDESK_TEST_MOD=L"$<TARGET_FILE:test_mod>")
gtest_discover_tests(unit_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS "unit" TIMEOUT 60)

# Windows integration layer: real child processes in ConPTY + job objects.
agentdesk_executable(win_integration_tests
    integration/windows/conpty_test.cpp
    integration/windows/conpty_compare_test.cpp
    integration/windows/session_test.cpp
    integration/windows/pipe_test.cpp
    integration/windows/host_test.cpp)
target_link_libraries(win_integration_tests PRIVATE psapi advapi32
    agentdesk::host agentdesk::ipc agentdesk::platform_win agentdesk::terminal nlohmann_json::nlohmann_json GTest::gtest_main)
add_dependencies(win_integration_tests vt_probe agent-host agentctl agent-ui)
target_compile_definitions(win_integration_tests PRIVATE
    AGENTDESK_VT_PROBE=L"$<TARGET_FILE:vt_probe>"
    AGENTDESK_HOST_EXE=L"$<TARGET_FILE:agent-host>"
    AGENTDESK_CTL_EXE=L"$<TARGET_FILE:agentctl>"
    AGENTDESK_UI_DIR_CTL_EXE=L"$<TARGET_FILE_DIR:agent-ui>/agentctl.exe"
    AGENTDESK_BUNDLED_CONPTY_DIR=L"${PROJECT_SOURCE_DIR}/third_party/conpty/bin")
gtest_discover_tests(win_integration_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "windows_integration" TIMEOUT 120)
