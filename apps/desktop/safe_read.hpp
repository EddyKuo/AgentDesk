#pragma once
// Reading what the UI did not write itself: ui.json, win98.json, layouts\.state, telemetry
// files, other machines' answers (fs.list, git.run) and file names. A value of the wrong type is
// the fallback and text that is not valid UTF-16 is still shown, never an exception: nlohmann's
// value() throws type_error when the stored type differs, and win::to_utf8 throws on an unpaired
// surrogate (on a worker thread either ends the process).
#include <windows.h>

#include <climits>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace agentdesk::desktop::safe {

using Json = nlohmann::json;

// j[key] when j is an object that has it, else nullptr.
inline const Json* field(const Json& j, const char* key) {
    if (!j.is_object())
        return nullptr;
    const auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

inline std::string string_or(const Json& j, const char* key, std::string fallback = {}) {
    const Json* v = field(j, key);
    return v != nullptr && v->is_string() ? v->get<std::string>() : fallback;
}

inline bool bool_or(const Json& j, const char* key, bool fallback) {
    const Json* v = field(j, key);
    return v != nullptr && v->is_boolean() ? v->get<bool>() : fallback;
}

// A whole number (no fraction, no text).
inline long long int_or(const Json& j, const char* key, long long fallback) {
    const Json* v = field(j, key);
    if (v == nullptr)
        return fallback;
    if (v->is_number_unsigned())
        return v->get<unsigned long long>() > static_cast<unsigned long long>(LLONG_MAX)
                   ? fallback
                   : v->get<long long>();
    return v->is_number_integer() ? v->get<long long>() : fallback;
}

// v when it is a whole number in [lo, hi].
inline std::optional<int> int_in(const Json& v, int lo, int hi) {
    if (!v.is_number_integer())
        return std::nullopt;
    if (v.is_number_unsigned() && v.get<unsigned long long>() > static_cast<unsigned long long>(INT_MAX))
        return std::nullopt;
    const long long n = v.get<long long>();
    return n >= lo && n <= hi ? std::optional<int>(static_cast<int>(n)) : std::nullopt;
}

// A finite number, whole or not.
inline std::optional<float> number(const Json& v) {
    if (!v.is_number())
        return std::nullopt;
    const double d = v.get<double>();
    return std::isfinite(d) ? std::optional<float>(static_cast<float>(d)) : std::nullopt;
}

// layout.rs 107-112 (libs/ui/src/layout.cpp read_background): two colors at an angle, 180
// unless said. Anything else (ui.state is read without that check) is no gradient.
struct Gradient {
    std::string from;
    std::string to;
    float angle = 180.0f;
};
inline std::optional<Gradient> gradient(const Json& background) {
    const Json* g = field(background, "gradient");
    if (g == nullptr || !g->is_array() || g->size() != 2 || !(*g)[0].is_string() || !(*g)[1].is_string())
        return std::nullopt;
    Gradient out{(*g)[0].get<std::string>(), (*g)[1].get<std::string>()};
    if (const Json* a = field(background, "angle")) {
        const auto angle = number(*a);
        if (!angle)
            return std::nullopt;
        out.angle = *angle;
    }
    return out;
}

// UTF-16 to UTF-8 for display: an unpaired surrogate becomes U+FFFD instead of an exception.
inline std::string utf8_lossy(std::wstring_view text) {
    if (text.empty() || text.size() > static_cast<std::size_t>(INT_MAX))
        return {};
    const int in = static_cast<int>(text.size());
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), in, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), in, out.data(), n, nullptr, nullptr);
    return out;
}

}  // namespace agentdesk::desktop::safe
