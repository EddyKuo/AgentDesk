#pragma once

// The only place that touches imgui_internal.h (skill rd/frontend-imgui, prohibited items).
// Re-verify these calls whenever Dear ImGui is upgraded.

#include <imgui.h>

namespace agentdesk::ui::adapter {

struct GridItem {
    bool visible = false;  // false: clipped, nothing to draw
    bool hovered = false;
    bool active = false;   // owns keyboard input this frame
    bool clicked = false;  // left click inside this frame
};

// Registers a focusable custom item covering [min, max) and handles click-to-focus,
// click-away-to-blur and taking ownership of all keyboard keys while active.
GridItem grid_item(const char* str_id, ImVec2 min, ImVec2 max, bool request_focus);

// Places the IME composition/candidate window at the caret (screen coordinates).
void set_ime_caret(ImVec2 caret_top_left, float line_height);

// True when the key was pressed this frame (with repeat) and is owned by the active grid item.
bool key_pressed_owned(ImGuiKey key, const char* str_id);

// Turns off ImGui's Ctrl+Tab window switcher: AgentDesk uses Ctrl+Tab to switch tabs.
void disable_nav_windowing();

// ImGui 1.92 user textures (ImageCache): the renderer creates them from the pixels and recreates
// them after a lost device.
void register_texture(ImTextureData* tex);
void unregister_texture(ImTextureData* tex);

// Puts a window above the others in drawing order (Herdr 98: the window pressed last is on top).
void bring_to_front(const char* window_name);

}  // namespace agentdesk::ui::adapter
