#pragma once

// How the keys held now split between the app's shortcuts and the terminal program, per system:
// keyboard_win.cpp (Ctrl and Alt go to the program; AltGr types text) and
// macos/keyboard_macos.mm (Cmd is the app's, Control and, as set, Option the program's; FR-40).
// CMake builds one; keyboard::option_is_alt and note_option_keys (keyboard.hpp) are there too.

#include <imgui.h>

#include "agentdesk/ui/input_mapping.hpp"

namespace agentdesk::ui::keyboard_os {

// The modifiers the terminal program gets.
Modifiers program_modifiers(const ImGuiIO& io);
// Modifiers held beyond Ctrl/Shift/Alt that keys.json chords can name (macOS: the Control key,
// which ImGui reports as Super because it swaps Cmd and Ctrl).
ImGuiKeyChord extra_held(const ImGuiIO& io);
// Ctrl+Alt typing a character (Windows' AltGr): text, not a chord.
bool is_alt_gr(const Modifiers& program);
// A key held that only ever makes app chords, never input for the program (macOS: Cmd).
bool app_only_modifier(const ImGuiIO& io);
// A held chord that is always the program's (macOS: Control+C is the interrupt, never a copy).
bool always_program(ImGuiKeyChord held);

}  // namespace agentdesk::ui::keyboard_os
