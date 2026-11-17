# Copies the bundled ConPTY (ADR-08) next to a target so load_conpty(exe_dir) finds it.
# Fetch it first with scripts/fetch-conpty.ps1; without it targets fall back to the inbox ConPTY.
set(AGENTDESK_CONPTY_BIN "${PROJECT_SOURCE_DIR}/third_party/conpty/bin/conpty")

function(agentdesk_bundle_conpty target)
    if(EXISTS "${AGENTDESK_CONPTY_BIN}/conpty.dll")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${AGENTDESK_CONPTY_BIN}" "$<TARGET_FILE_DIR:${target}>/conpty"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_SOURCE_DIR}/third_party/conpty/LICENSE"
                "${PROJECT_SOURCE_DIR}/third_party/conpty/NOTICE.md"
                "$<TARGET_FILE_DIR:${target}>/conpty")
    else()
        message(WARNING "Bundled ConPTY missing: run scripts/fetch-conpty.ps1 (ADR-08)")
    endif()
endfunction()
