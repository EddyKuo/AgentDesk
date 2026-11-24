# Run by ctest (cmake -DROOT=<source dir> -P): shared files must build on every system, so they may
# not name one system's headers or ask the compiler which system it is. What differs per system goes
# behind an interface, in files whose name or folder says so (docs/design/platform-abstraction.md 3).
cmake_minimum_required(VERSION 3.25)

file(GLOB_RECURSE _files RELATIVE "${ROOT}"
    "${ROOT}/libs/*.cpp" "${ROOT}/libs/*.hpp" "${ROOT}/apps/*.cpp" "${ROOT}/apps/*.hpp")

set(_system_file "(^|/)(win|windows|posix|macos)/|_(win|posix|macos)\\.(cpp|hpp)$")
# The mod SDK is compiled by other people's builds, without our CMake to pick files.
set(_exempt "^libs/modapi/")

set(_forbidden
    "#[ \t]*include[ \t]*<windows\\.h>"
    "#[ \t]*include[ \t]*<(unistd|fcntl|spawn|poll|dlfcn|signal|pwd)\\.h>"
    "#[ \t]*include[ \t]*<sys/"
    "agentdesk/platform/win/"
    "platform::win::"
    "#[ \t]*if.*(_WIN32|__APPLE__|_MSC_VER)")

set(_found "")
set(_checked 0)
foreach(_file IN LISTS _files)
    if(_file MATCHES "${_system_file}" OR _file MATCHES "${_exempt}")
        continue()
    endif()
    math(EXPR _checked "${_checked} + 1")
    foreach(_pattern IN LISTS _forbidden)
        file(STRINGS "${ROOT}/${_file}" _lines REGEX "${_pattern}")
        foreach(_line IN LISTS _lines)
            string(APPEND _found "  ${_file}: ${_line}\n")
        endforeach()
    endforeach()
endforeach()

if(_checked LESS 100)
    message(FATAL_ERROR "only ${_checked} shared files found under ${ROOT}: the check looks in the wrong place")
endif()
if(_found)
    message(FATAL_ERROR "shared files that depend on one system:\n${_found}")
endif()
message(STATUS "${_checked} shared files depend on no system")
