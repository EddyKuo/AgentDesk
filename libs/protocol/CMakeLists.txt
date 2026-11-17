agentdesk_library(agentdesk_protocol
    src/frame.cpp
    src/messages.cpp)
add_library(agentdesk::protocol ALIAS agentdesk_protocol)
target_include_directories(agentdesk_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(agentdesk_protocol PUBLIC nlohmann_json::nlohmann_json agentdesk::terminal)
