# Project-wide compiler settings for first-party targets (ADR_001 thresholds).
# Third-party targets (third_party/*) set their own, looser flags.

function(agentdesk_target_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /WX /utf-8 /EHsc /Zc:__cplusplus /Zc:preprocessor)
        target_compile_definitions(${target} PRIVATE
            UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    else()
        # The clang counterpart of /W4 /WX (ADR_001 thresholds).
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

# Static analysis pass (replaces cppcheck, ADR_001): cmake -DAGENTDESK_ANALYZE=ON
option(AGENTDESK_ANALYZE "Enable MSVC /analyze on first-party targets" OFF)

function(agentdesk_library target)
    add_library(${target} STATIC ${ARGN})
    agentdesk_target_defaults(${target})
    if(AGENTDESK_ANALYZE AND MSVC)
        target_compile_options(${target} PRIVATE /analyze /analyze:external-)
    endif()
endfunction()

function(agentdesk_executable target)
    add_executable(${target} ${ARGN})
    agentdesk_target_defaults(${target})
    if(AGENTDESK_ANALYZE AND MSVC)
        target_compile_options(${target} PRIVATE /analyze /analyze:external-)
    endif()
endfunction()
