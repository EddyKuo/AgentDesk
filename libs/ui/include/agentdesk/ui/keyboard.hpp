#pragma once

// How modifier keys reach terminal programs. On macOS (ADR-30, FR-40) the Option keys either act
// as Alt (an ESC prefix, for terminal shortcuts) or type characters (accents, symbols), as the
// person sets option_as_alt: "auto" (Alt on US and ABC keyboard layouts), "on", "off", "left" or
// "right" (that Option key only). On Windows Alt is always Alt.

#include <string>
#include <string_view>

namespace agentdesk::ui::keyboard {

// The setting (ui.json option_as_alt); anything unknown counts as "auto".
void set_option_as_alt(std::string_view mode);
[[nodiscard]] std::string option_as_alt();
// The window shell notes which Option keys are down at each key event (macOS).
void note_option_keys(bool left, bool right);
// True when the Option held now is Alt for terminal programs rather than text input.
[[nodiscard]] bool option_is_alt();

// The text an input method is composing (macOS marked text), drawn at the terminal's caret until
// it is committed; "" when nothing is being composed. Windows' IME draws its own composition.
void set_preedit(std::string utf8);
[[nodiscard]] std::string preedit();

}  // namespace agentdesk::ui::keyboard
