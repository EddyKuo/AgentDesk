#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "agentdesk/terminal/vt_engine.hpp"

namespace agentdesk::host {

// Abstract terminal input. The host's engine encodes it according to the program's modes, so
// clients never need to know about cursor-key modes or bracketed paste (design doc 2.3).
struct KeyInput {
    terminal::Key key;
    std::uint8_t mods = terminal::kModNone;
};
struct TextInput {
    std::u32string text;
    std::uint8_t mods = terminal::kModNone;
};
struct PasteInput {
    std::string utf8;
};
struct RawInput {  // already-encoded bytes (agentctl send, tests)
    std::string bytes;
};
using InputEvent = std::variant<KeyInput, TextInput, PasteInput, RawInput>;

}  // namespace agentdesk::host
