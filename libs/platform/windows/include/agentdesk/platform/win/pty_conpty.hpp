#pragma once

// Which ConPTY platform::Pty uses in this process.

#include "agentdesk/platform/win/conpty_api.hpp"

namespace agentdesk::platform::win {

// The bundled ConPTY next to the running executable, else the inbox one; loaded at the first call
// (throws when the bundled DLL is there but broken).
ConPtyApi pty_conpty();

// Another one, before the first terminal starts: tests, and tools outside the install folder.
void use_conpty(ConPtyApi api);

}  // namespace agentdesk::platform::win
