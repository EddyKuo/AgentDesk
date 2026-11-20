// The only file that uses imgui_internal.h; see imgui_adapter.hpp.
#include "agentdesk/ui/imgui_adapter.hpp"

#include <imgui_internal.h>

namespace agentdesk::ui::adapter {

GridItem grid_item(const char* str_id, ImVec2 min, ImVec2 max, bool request_focus) {
    GridItem out;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiID id = window->GetID(str_id);
    const ImRect bb(min, max);
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id, nullptr, ImGuiItemFlags_Inputable)) {
        return out;
    }
    out.visible = true;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    out.hovered = ImGui::ItemHoverable(bb, id, ImGuiItemFlags_None);
    out.clicked = out.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (out.clicked || request_focus) {
        ImGui::SetActiveID(id, window);
        ImGui::SetFocusID(id, window);
        ImGui::FocusWindow(window);
    } else if (g.ActiveId == id && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !out.hovered) {
        ImGui::ClearActiveID();  // click elsewhere: lose focus
    }
    out.active = g.ActiveId == id;
    if (out.active) {
        // Keep the item active across frames (InputText does the same) and keep Tab/arrows
        // from being consumed by keyboard navigation.
        g.ActiveIdNoClearOnFocusLoss = true;
        ImGui::KeepAliveID(id);
        ImGui::SetActiveIdUsingAllKeyboardKeys();
        // Active only to own the keyboard: other items stay hoverable, so the first click on a
        // button elsewhere works and right-click menus (tabs, spaces) open. A selection drag that
        // started in the grid keeps the mouse to itself.
        g.ActiveIdAllowOverlap = !(ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                                   bb.Contains(g.IO.MouseClickedPos[ImGuiMouseButton_Left]));
    }
    return out;
}

void set_ime_caret(ImVec2 caret_top_left, float line_height) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    g.PlatformImeData.WantVisible = true;  // Win32 backend only delivers IME text when visible
    g.PlatformImeData.WantTextInput = true;
    g.PlatformImeData.InputPos = caret_top_left;
    g.PlatformImeData.InputLineHeight = line_height;
    g.PlatformImeData.ViewportId = ImGui::GetWindowViewport()->ID;
}

bool key_pressed_owned(ImGuiKey key, const char* str_id) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiID id = window->GetID(str_id);
    return ImGui::IsKeyPressed(key, ImGuiInputFlags_Repeat, id);
}

void release_keyboard() {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* here = ImGui::GetCurrentWindow();
    if (g.ActiveId != 0 && g.ActiveIdWindow != nullptr && here != nullptr &&
        g.ActiveIdWindow->RootWindow != here->RootWindow)
        ImGui::ClearActiveID();
}

void disable_nav_windowing() {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    g.ConfigNavWindowingKeyNext = 0;
    g.ConfigNavWindowingKeyPrev = 0;
}

void register_texture(ImTextureData* tex) {
    ImGui::RegisterUserTexture(tex);
}

void unregister_texture(ImTextureData* tex) {
    if (ImGui::GetCurrentContext() != nullptr)
        ImGui::UnregisterUserTexture(tex);
}

}  // namespace agentdesk::ui::adapter
