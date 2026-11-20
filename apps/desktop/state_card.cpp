// Empty, loading and error states shared by Focus and Mission Control: a centred card with a
// title, a hint and buttons (laid out like the "Open a terminal" card), and skeleton bars.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

std::string with_chord(const App::State& s, const std::string& label, const char* command) {
    const std::string chord = s.keymap.chord_for(command);
    return chord.empty() ? label : label + "   " + chord;
}

namespace {
// The buttons in a row centred on `cx`, 10 px apart; the first in the accent colour, as on the
// "Open a terminal" card.
void draw_card_buttons(App::State& s, const StateCard& card, float cx, float top) {
    std::vector<float> widths;
    float total = 0;
    for (const auto& b : card.buttons) {
        widths.push_back(ImGui::CalcTextSize(b.label.c_str()).x + px(36));
        total += widths.back();
    }
    total += px(10) * static_cast<float>(card.buttons.size() - 1);
    ImGui::SetCursorScreenPos(ImVec2(cx - total * 0.5f, top));
    for (std::size_t i = 0; i < card.buttons.size(); ++i) {
        const auto& b = card.buttons[i];
        if (i > 0)
            ImGui::SameLine(0, px(10));
        if (i == 0) {
            ImGui::PushStyleColor(ImGuiCol_Button, v4(g_pal->focus));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(alpha(g_pal->focus, 220)));
            ImGui::PushStyleColor(ImGuiCol_Text, v4(accent_foreground()));
        }
        ImGui::PushID(static_cast<int>(i));
        const bool clicked = ImGui::Button(b.label.c_str(), ImVec2(widths[i], px(36)));
        ImGui::PopID();
        if (i == 0)
            ImGui::PopStyleColor(3);
        if (clicked && b.action)
            b.action();
        else if (clicked)
            run_command(s, b.command);
    }
}
}  // namespace

void draw_state_card(App::State& s, ImVec2 pos, ImVec2 size, const StateCard& card) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + size.x, pos.y + size.y);
    if (card.framed) {
        dl->AddRectFilled(pos, end, g_pal->card, px(12));
        dl->AddRect(pos, end, g_pal->border, px(12));
    }
    const float cx = pos.x + size.x * 0.5f;
    const float cy = pos.y + size.y * 0.42f;
    const ImVec2 tsz = s.fonts.ui_bold->CalcTextSizeA(fpx(18), FLT_MAX, 0, card.title.c_str());
    dl->AddText(s.fonts.ui_bold, fpx(18), ImVec2(cx - tsz.x * 0.5f, cy - tsz.y), g_pal->text,
                card.title.c_str());
    float y = cy + px(8);
    for (const std::string* line : {&card.hint, &card.detail}) {
        if (line->empty())
            continue;
        const ImU32 col = line == &card.detail ? card.detail_color : g_pal->dim;
        const float max_w = std::max(px(80), size.x - px(48));
        const float lw = std::min(max_w, s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, line->c_str()).x);
        text_clipped(dl, s.fonts.ui, fpx(13), ImVec2(cx - lw * 0.5f, y), col, *line, max_w);
        y += px(20);
    }
    if (!card.buttons.empty()) {
        draw_card_buttons(s, card, cx, y + px(16));
        y += px(16) + px(36);
    }
    if (!card.link.empty()) {
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(12.5f), FLT_MAX, 0, card.link.c_str());
        const ImVec2 p(cx - t.x * 0.5f, y + px(16));
        ImGui::SetCursorScreenPos(p);
        const bool clicked = ImGui::InvisibleButton("##card-link", ImVec2(t.x, t.y + px(4)));
        const bool hov = ImGui::IsItemHovered();
        dl->AddText(s.fonts.ui, fpx(12.5f), p, g_pal->focus, card.link.c_str());
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            dl->AddLine(ImVec2(p.x, p.y + t.y + px(1)), ImVec2(p.x + t.x, p.y + t.y + px(1)), g_pal->focus);
        }
        if (clicked && card.on_link)
            card.on_link();
    }
}

void draw_skeleton_rows(ImDrawList* dl, ImVec2 pos, float w, std::initializer_list<float> widths, float row_h,
                        float bar_h) {
    // Breathes once every 1.2 s between two strengths of the faint colour.
    const float t = static_cast<float>(std::fmod(ImGui::GetTime(), 1.2) / 1.2);
    const float k = 0.5f - 0.5f * std::cos(t * 2.0f * 3.14159265f);
    const int a = 0x22 + static_cast<int>(k * 0x1C);
    float y = pos.y;
    for (const float f : widths) {
        dl->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + w * f, y + bar_h), alpha(g_pal->faint, a),
                          bar_h * 0.5f);
        y += row_h;
    }
}

}  // namespace detail
}  // namespace agentdesk::desktop
