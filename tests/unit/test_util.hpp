#pragma once

// Helpers the unit tests share.

#include <chrono>
#include <random>
#include <string>

namespace agentdesk::test {

// A suffix no other test run uses at the same time, for temporary folder names (test runs go
// side by side).
inline std::string unique_suffix() {
    std::random_device rd;
    const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(rd()) + "-" + std::to_string(t);
}

}  // namespace agentdesk::test
