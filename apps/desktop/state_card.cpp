// The empty, loading and error states every newer view shares (Focus, Mission Control, Review,
// Timeline): a centred card with a title, a hint and buttons laid out like the "Open a terminal"
// card, and the grey skeleton bars that stand in for content on the way.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

std::string with_chord(const App::State& s, const std::string& label, const char* command) {
    const std::string chord = s.keymap.chord_for(command);
    return chord.empty() ? label : label + "   " + chord;
}

namespace {
constexpr float kButtonH = 36.0f;  // design px, as on the "Open a terminal" card

// The buttons in a row centred on `cx`, 10 px apart; the first in the accent colour.
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
        const bool clicked = ImGui::Button(b.label.c_str(), ImVec2(widths[i], px(kButtonH)));
        ImGui::PopID();
        if (i == 0)
            ImGui::PopStyleColor(3);
        if (clicked && b.action)
            b.action();
        else if (clicked)
            run_command(s, b.command);
    }
}

// Lines of `text` wrapped at `wrap`, each `fs * 1.3` high; draws them centred on `cx` from `y`
// when `dl` is set. Returns the height.
float centred_lines(ImDrawList* dl, ImFont* font, float fs, const std::string& text, float cx, float y,
                    float wrap, ImU32 col) {
    const char* p = text.c_str();
    const char* end = p + text.size();
    float h = 0;
    while (p < end) {
        const char* line_end = font->CalcWordWrapPosition(fs, p, end, wrap);
        if (line_end == p)
            line_end = p + 1;
        if (dl != nullptr) {
            const ImVec2 lw = font->CalcTextSizeA(fs, FLT_MAX, 0, p, line_end);
            dl->AddText(font, fs, ImVec2(cx - lw.x * 0.5f, y + h), col, p, line_end);
        }
        h += fs * 1.3f;
        p = line_end;
        while (p < end && (*p == ' ' || *p == '\n'))
            ++p;
    }
    return h;
}
}  // namespace

void draw_state_card(App::State& s, ImVec2 pos, ImVec2 size, const StateCard& card) {
    if (size.x <= 0 || size.y <= 0)
        return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + size.x, pos.y + size.y);
    if (card.framed) {
        dl->AddRectFilled(pos, end, g_pal->card, px(12));
        dl->AddRect(pos, end, g_pal->border, px(12));
    }
    const float wrap = std::max(px(80), std::min(size.x - px(48), px(560)));
    const float ts = fpx(card.title_size);
    const float hs = fpx(13);
    const float cx = pos.x + size.x * 0.5f;
    const ImU32 detail_col = card.detail_color != 0 ? card.detail_color : g_pal->dim;

    // Measure first so the whole card sits a little above the middle.
    const float spinner_h = card.kind == CardKind::kLoading ? px(22) : 0.0f;
    const float title_h = centred_lines(nullptr, s.fonts.ui_bold, ts, card.title, cx, 0, wrap, 0);
    float total = spinner_h + title_h;
    for (const std::string* line : {&card.hint, &card.detail})
        if (!line->empty())
            total += px(6) + centred_lines(nullptr, s.fonts.ui, hs, *line, cx, 0, wrap, 0);
    if (!card.buttons.empty())
        total += px(16) + px(kButtonH);
    if (!card.link.empty())
        total += px(16) + fpx(12.5f);
    float y = pos.y + std::max(px(12), (size.y - total) * 0.42f);

    if (card.kind == CardKind::kLoading) {
        const float t = static_cast<float>(ImGui::GetTime());
        const float a0 = t * (2.0f * 3.14159265f / 0.9f);
        dl->PathArcTo(ImVec2(cx, y + px(7)), px(7), a0, a0 + 4.4f, 20);
        dl->PathStroke(g_pal->dim, 0, px(1.8f));
        y += spinner_h;
    }
    const ImU32 title_col = card.kind == CardKind::kError ? g_pal->needs_you : g_pal->text;
    y += centred_lines(dl, s.fonts.ui_bold, ts, card.title, cx, y, wrap, title_col);
    for (const std::string* line : {&card.hint, &card.detail}) {
        if (line->empty())
            continue;
        y += px(6);
        y += centred_lines(dl, s.fonts.ui, hs, *line, cx, y, wrap, line == &card.detail ? detail_col : g_pal->dim);
    }
    if (!card.buttons.empty()) {
        draw_card_buttons(s, card, cx, y + px(16));
        y += px(16) + px(kButtonH);
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
