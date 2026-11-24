# First-party libraries. Each subdirectory defines one static library; the platform layer is the
# system's own (platform/windows, platform/posix: ADR-30).
if(WIN32)
    add_subdirectory(platform/windows)
else()
    add_subdirectory(platform/posix)
endif()
add_subdirectory(terminal)
add_subdirectory(protocol)
add_subdirectory(ipc)
add_subdirectory(host)
add_subdirectory(modapi)
add_subdirectory(ui)
