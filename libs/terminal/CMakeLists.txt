agentdesk_library(agentdesk_terminal
    src/program_status.cpp
    src/cwd_uri.cpp
    src/vt_engine.cpp)
add_library(agentdesk::terminal ALIAS agentdesk_terminal)
target_include_directories(agentdesk_terminal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(agentdesk_terminal PRIVATE agentdesk::vterm)
