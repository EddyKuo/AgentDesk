#pragma once

// The MSVC C runtime names shared code uses (with MSVC's argument order), on the POSIX C library.
// Windows has its own crt.hpp with the same name; the build picks one by include path.

#include <cstdio>
#include <ctime>

#include <strings.h>

inline int _stricmp(const char* a, const char* b) {
    return ::strcasecmp(a, b);
}
inline int _strnicmp(const char* a, const char* b, std::size_t n) {
    return ::strncasecmp(a, b, n);
}
inline int gmtime_s(std::tm* out, const std::time_t* t) {
    return ::gmtime_r(t, out) != nullptr ? 0 : 1;
}
inline int localtime_s(std::tm* out, const std::time_t* t) {
    return ::localtime_r(t, out) != nullptr ? 0 : 1;
}
inline std::time_t _mkgmtime(std::tm* tm) {
    return ::timegm(tm);
}
// Used with numeric conversions only, where the _s variant takes the same arguments.
template <class... Args>
int sscanf_s(const char* s, const char* format, Args... args) {
    return std::sscanf(s, format, args...);  // NOLINT(cert-err34-c)
}
