#pragma once

// Git branch and repository top of a folder, read from the .git files without running git.

#include <string>

namespace agentdesk::ui {

// Current git branch of the repository containing `dir` ("main", or a short commit for a
// detached HEAD), read from .git/HEAD without running git; empty outside a repository. Results
// are cached for a few seconds per folder. UI thread.
std::string git_branch(const std::string& dir_utf8);

// Top folder of the repository containing `dir` (the folder holding .git; a worktree's own
// folder), or empty outside a repository. Cached like git_branch. UI thread.
std::string git_top(const std::string& dir_utf8);

}  // namespace agentdesk::ui
