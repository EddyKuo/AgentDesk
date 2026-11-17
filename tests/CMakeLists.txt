find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

set(AGENTDESK_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# Unit layer: pure logic, no I/O beyond fixture files, no child processes.
agentdesk_executable(unit_tests
    unit/toolchain_smoke_test.cpp
    unit/program_status_test.cpp
    unit/vt_engine_test.cpp
    unit/command_line_test.cpp)
target_link_libraries(unit_tests PRIVATE
    agentdesk::terminal agentdesk::platform_win agentdesk::vterm
    nlohmann_json::nlohmann_json GTest::gtest_main GTest::gmock shell32)
target_compile_definitions(unit_tests PRIVATE AGENTDESK_FIXTURE_DIR="${AGENTDESK_FIXTURE_DIR}")
gtest_discover_tests(unit_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS "unit" TIMEOUT 60)

# Windows integration layer: real child processes in ConPTY + job objects.
agentdesk_executable(win_integration_tests
    integration/windows/conpty_test.cpp
    integration/windows/conpty_compare_test.cpp)
target_link_libraries(win_integration_tests PRIVATE
    agentdesk::platform_win agentdesk::terminal GTest::gtest_main)
add_dependencies(win_integration_tests vt_probe)
target_compile_definitions(win_integration_tests PRIVATE
    AGENTDESK_VT_PROBE=L"$<TARGET_FILE:vt_probe>"
    AGENTDESK_BUNDLED_CONPTY_DIR=L"${PROJECT_SOURCE_DIR}/third_party/conpty/bin")
gtest_discover_tests(win_integration_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "windows_integration" TIMEOUT 120)
