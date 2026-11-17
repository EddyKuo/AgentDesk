agentdesk_library(agentdesk_ipc src/pipe.cpp)
add_library(agentdesk::ipc ALIAS agentdesk_ipc)
target_include_directories(agentdesk_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(agentdesk_ipc PUBLIC agentdesk::protocol agentdesk::platform_win advapi32)
