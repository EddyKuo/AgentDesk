#pragma once

// The C runtime names shared code uses (_stricmp, gmtime_s, localtime_s, _mkgmtime, sscanf_s):
// MSVC has them. The POSIX build has its own crt.hpp with the same name that maps them.

#include <cstdio>
#include <cstring>
#include <ctime>
