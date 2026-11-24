#pragma once

// Paths as text (UTF-8), for paths that may belong to another machine: a Windows UI shows folders
// of a Mac and the other way round, so std::filesystem::path, which follows the system this
// program runs on, cannot take them apart. The path itself says which kind it is.

#include <string>
#include <string_view>

namespace agentdesk::ui::path_text {

// "C:\x", "C:/x", "\\server\share" or "//server/share" (git prints UNC paths that way).
bool is_windows(std::string_view path);
// Starts at the root of its system: a Windows path as above ("C:x" is not), or "/x".
bool is_absolute(std::string_view path);
// This system's separator.
char local_separator();
// The separator of the path's system; this system's when the path does not say (relative, empty).
char separator(std::string_view path);
// The path the way its system writes it, without separators at the end: a Windows path with
// backslashes; a POSIX path unchanged (a backslash is an ordinary character in its names).
std::string native(std::string_view path);
// `rel` under `root`, in root's style ('/' in rel always separates).
std::string join(std::string_view root, std::string_view rel);
// `path`, relative to `base` unless absolute, with "." and ".." resolved, in native form.
std::string resolve(std::string_view base, std::string_view path);
// The folder the path is in; the root of a root.
std::string parent(std::string_view path);

}  // namespace agentdesk::ui::path_text
