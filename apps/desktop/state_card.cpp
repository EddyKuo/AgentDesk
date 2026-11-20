// The empty / loading / error card and the grey placeholder rows the newer views (Review,
// Timeline) share, so their states read the same.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

void draw_state_card(App::State& s, ImVec2 pos, ImVec2 size, const StateCard& card) {
    if (size.x <= 0 || size.y <= 0)
        return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (card.framed) {
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->card, px(12));
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->border, px(12));
    }
    const float wrap = std::min(size.x - px(32), px(420));
    const float ts = fpx(card.title_size);
    const float hs = fpx(13);
    const ImVec2 tsz = s.fonts.ui_bold->CalcTextSizeA(ts, FLT_MAX, wrap, card.title.c_str());
    const ImVec2 hsz =
        card.hint.empty() ? ImVec2(0, 0) : s.fonts.ui->CalcTextSizeA(hs, FLT_MAX, wrap, card.hint.c_str());
    const float bh = px(32);
    const float total =
        tsz.y + (card.hint.empty() ? 0.0f : px(8) + hsz.y) + (card.buttons.empty() ? 0.0f : px(18) + bh);
    const float cx = pos.x + size.x * 0.5f;
    float y = pos.y + std::max(px(12), (size.y - total) * 0.45f);
    const ImU32 title_col = card.kind == CardKind::kError ? g_pal->needs_you : g_pal->text;
    // Centre each wrapped line: ImGui wraps but does not centre, so draw line by line.
    auto centered = [&](ImFont* font, float fs, const std::string& text, ImU32 col) {
        const char* p = text.c_str();
        const char* end = p + text.size();
        while (p < end) {
            const char* line_end = font->CalcWordWrapPosition(fs, p, end, wrap);
            if (line_end == p)
                line_end = p + 1;
            const ImVec2 lw = font->CalcTextSizeA(fs, FLT_MAX, 0, p, line_end);
            dl->AddText(font, fs, ImVec2(cx - lw.x * 0.5f, y), col, p, line_end);
            y += fs * 1.3f;
            p = line_end;
            while (p < end && (*p == ' ' || *p == '\n'))
                ++p;
        }
    };
    if (card.kind == CardKind::kLoading) {
        const float t = static_cast<float>(ImGui::GetTime());
        const float r = px(7);
        const float a0 = t * (2.0f * 3.14159265f / 0.9f);
        dl->PathArcTo(ImVec2(cx, y - px(14)), r, a0, a0 + 4.4f, 20);
        dl->PathStroke(g_pal->dim, 0, px(1.8f));
        y += px(8);
    }
    centered(s.fonts.ui_bold, ts, card.title, title_col);
    if (!card.hint.empty()) {
        y += px(4);
        centered(s.fonts.ui, hs, card.hint, g_pal->dim);
    }
    if (card.buttons.empty())
        return;
    y += px(14);
    std::vector<float> widths;
    float row = 0;
    for (const auto& b : card.buttons) {
        widths.push_back(s.fonts.ui_medium->CalcTextSizeA(fpx(13), FLT_MAX, 0, b.label.c_str()).x + px(28));
        row += widths.back();
    }
    row += px(8) * static_cast<float>(card.buttons.size() - 1);
    float x = cx - row * 0.5f;
    for (std::size_t i = 0; i < card.buttons.size(); ++i) {
        const auto& b = card.buttons[i];
        const ImVec2 a(x, y);
        const ImVec2 e(x + widths[i], y + bh);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(static_cast<int>(i));
        ImGui::PushID(b.command.c_str());
        const bool clicked = ImGui::InvisibleButton("##cardbtn", ImVec2(widths[i], bh));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        ImGui::PopID();
        if (hov)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const bool primary = i == 0;
        const ImU32 fill =
            primary ? (hov ? alpha(g_pal->text, 220) : g_pal->text) : (hov ? g_pal->hover : raised_color());
        dl->AddRectFilled(a, e, fill, px(7));
        if (!primary)
            dl->AddRect(a, e, g_pal->border, px(7));
        const ImVec2 lt = s.fonts.ui_medium->CalcTextSizeA(fpx(13), FLT_MAX, 0, b.label.c_str());
        dl->AddText(s.fonts.ui_medium, fpx(13),
                    ImVec2(a.x + (widths[i] - lt.x) * 0.5f, a.y + (bh - lt.y) * 0.5f),
                    primary ? g_pal->card : g_pal->text, b.label.c_str());
        if (clicked)
            run_command(s, b.command);
        x += widths[i] + px(8);
    }
}

void draw_skeleton_rows(ImDrawList* dl, ImVec2 pos, float w, int rows, float row_h) {
    static constexpr float kWidths[] = {0.70f, 0.50f, 0.60f, 0.45f};
    // A slow pulse (1.2 s per cycle) so the rows read as "on the way", not as content.
    const float t = static_cast<float>(ImGui::GetTime());
    const float pulse = 0.5f + 0.5f * std::sin(t * (2.0f * 3.14159265f / 1.2f));
    const ImU32 c = alpha(g_pal->faint, static_cast<int>(0x22 + 0x22 * pulse));
    for (int i = 0; i < rows; ++i) {
        const float y = pos.y + static_cast<float>(i) * row_h + (row_h - px(8)) * 0.5f;
        const float bw = w * kWidths[static_cast<std::size_t>(i) % std::size(kWidths)];
        dl->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + bw, y + px(8)), c, px(4));
    }
}

}  // namespace detail
}  // namespace agentdesk::desktop
