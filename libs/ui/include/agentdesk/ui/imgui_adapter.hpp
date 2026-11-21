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

// Called inside a window (a modal): takes the keyboard away from an item of another window that
// holds it (a terminal grid stays active through a modal otherwise, and would see its keys).
void release_keyboard();
// Takes the keyboard away from whatever item holds it (a terminal grid in the same window too):
// a view that owns its own keys (the Inbox queue) starts with nothing holding them.
void clear_keyboard();

// The modal dialog on top this frame (after the dialogs are drawn): its rectangle, corner radius
// and draw list, so its elevation can be drawn around it. False when no modal is open.
struct WindowBox {
    ImVec2 min{};
    ImVec2 max{};
    float rounding = 0.0f;
    ImDrawList* draw_list = nullptr;
};
bool topmost_modal(WindowBox& out);

// Turns off ImGui's Ctrl+Tab window switcher: AgentDesk uses Ctrl+Tab to switch tabs.
void disable_nav_windowing();

// ImGui 1.92 user textures (ImageCache): the renderer creates them from the pixels and recreates
// them after a lost device.
void register_texture(ImTextureData* tex);
void unregister_texture(ImTextureData* tex);

}  // namespace agentdesk::ui::adapter
