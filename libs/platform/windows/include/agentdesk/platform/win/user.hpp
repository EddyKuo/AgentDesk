#pragma once

// The current user and logon session, as the names of per-user objects contain them.

#include <cstdint>
#include <string>

namespace agentdesk::platform::win {

std::string user_sid();  // "S-1-5-21-..."
std::uint32_t logon_session();

}  // namespace agentdesk::platform::win
