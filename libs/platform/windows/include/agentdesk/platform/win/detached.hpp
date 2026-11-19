#pragma once

// Starting agent-host detached from the launcher (and, when allowed, out of its job).

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace agentdesk::platform::win {

struct DetachedResult {
    DWORD pid = 0;
    bool broke_away = false;                 // left the launcher's job (normal case)
    bool parent_job_kills_on_close = false;  // stayed in a job that kills it when the launcher ends
};

// Starts a background process that outlives its launcher (design doc 2.2 step 1):
// DETACHED_PROCESS + CREATE_BREAKAWAY_FROM_JOB, falling back to staying in the parent job when
// breakaway is denied. Never CREATE_NEW_PROCESS_GROUP (would disable Ctrl+C for its children).
// No handles are inherited.
DetachedResult spawn_detached(const std::filesystem::path& exe, const std::vector<std::wstring>& args);

// True when the current process is in a job with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE.
bool current_job_kills_on_close();

}  // namespace agentdesk::platform::win
