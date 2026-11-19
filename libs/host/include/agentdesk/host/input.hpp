#pragma once

// Input events a client sends to a session (keys, text, paste, raw bytes, mouse).

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
struct MouseInput {  // a cell event; dropped unless the program turned mouse reporting on
    int row = 0;
    int col = 0;
    int button = 0;  // 1 left, 2 middle, 3 right, 4/5 wheel up/down, 0 move
    terminal::MouseAction action = terminal::MouseAction::kPress;
    std::uint8_t mods = terminal::kModNone;
};
using InputEvent = std::variant<KeyInput, TextInput, PasteInput, RawInput, MouseInput>;

}  // namespace agentdesk::host
