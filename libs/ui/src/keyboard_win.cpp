// keyboard_os.hpp on Windows: Alt is always Alt, Ctrl and Alt go to the program.
#include "agentdesk/ui/keyboard.hpp"
#include "keyboard_os.hpp"

namespace agentdesk::ui {

namespace keyboard {

void note_option_keys(bool /*left*/, bool /*right*/) {}

bool option_is_alt() {
    return true;
}

}  // namespace keyboard

namespace keyboard_os {

Modifiers program_modifiers(const ImGuiIO& io) {
    return Modifiers{io.KeyCtrl, io.KeyShift, io.KeyAlt};
}

ImGuiKeyChord extra_held(const ImGuiIO& /*io*/) {
    return 0;
}

bool is_alt_gr(const Modifiers& program) {
    return program.ctrl && program.alt;  // AltGr arrives as Ctrl+Alt with a printable char
}

bool app_only_modifier(const ImGuiIO& /*io*/) {
    return false;
}

bool always_program(ImGuiKeyChord /*held*/) {
    return false;
}

}  // namespace keyboard_os

}  // namespace agentdesk::ui
