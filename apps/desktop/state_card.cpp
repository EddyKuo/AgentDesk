// The empty, loading and error states every newer view shares (Focus, Stage, Review,
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
    if (card.framed)
        tok::card(dl, pos, end, tok::radius_panel());
    const float wrap = std::max(px(80), std::min(size.x - px(48), px(560)));
    const float ts = fpx(card.title_size);
    const float hs = fpx(13);
    const float cx = pos.x + size.x * 0.5f;
    const ImU32 detail_col = card.detail_color != 0 ? card.detail_color : g_pal->dim;

    // Measure first so the whole card sits a little above the middle.
    const float spinner_h = card.kind == CardKind::kLoading ? px(22) : 0.0f;
    const float art_h = card.art != tok::Art::kNone ? tok::art_height() + tok::gap_l() : 0.0f;
    const float title_h = centred_lines(nullptr, s.fonts.ui_bold, ts, card.title, cx, 0, wrap, 0);
    float total = spinner_h + art_h + title_h;
    for (const std::string* line : {&card.hint, &card.detail})
        if (!line->empty())
            total += px(6) + centred_lines(nullptr, s.fonts.ui, hs, *line, cx, 0, wrap, 0);
    if (!card.buttons.empty())
        total += px(16) + px(kButtonH);
    if (!card.link.empty())
        total += px(16) + fpx(12.5f);
    float y = pos.y + std::max(px(12), (size.y - total) * 0.42f);

    if (art_h > 0) {
        tok::draw_art(dl, card.art, ImVec2(cx, y + tok::art_height() * 0.5f), tok::art_height());
        y += art_h;
    }
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
        y += centred_lines(dl, s.fonts.ui, hs, *line, cx, y, wrap,
                           line == &card.detail ? detail_col : g_pal->dim);
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

void draw_empty_state(App::State& s, ImVec2 pos, ImVec2 size, tok::Art art, const std::string& title,
                      const std::string& hint, const std::string& button, const std::string& command) {
    StateCard card;
    card.kind = CardKind::kEmpty;
    card.art = art;
    card.title = title;
    card.hint = hint;
    card.title_size = ui::tokens::kTitle.size;
    if (!button.empty())
        card.buttons.push_back({button, command, nullptr});
    draw_state_card(s, pos, size, card);
}

void draw_empty_row(App::State& s, ImVec2 pos, ImVec2 size, const EmptyRow& row) {
    if (size.x <= 0 || size.y <= 0)
        return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 title_size = tok::measure(s, ui::tokens::kBody, row.title.c_str());
    const ImVec2 hint_size =
        row.hint.empty() ? ImVec2(0, 0) : tok::measure(s, ui::tokens::kCaption, row.hint.c_str());
    const float text_w = std::max(title_size.x, hint_size.x);
    const float text_h = title_size.y + (row.hint.empty() ? 0.0f : tok::gap_xs() + hint_size.y);
    const float art = std::min(px(48), size.y - tok::gap_l());
    const bool with_art = row.art != tok::Art::kNone && art >= px(32);
    const std::string action = !row.button.empty() ? row.button : row.link;
    const float action_w = action.empty() ? 0.0f : ImGui::CalcTextSize(action.c_str()).x + px(28);
    const float action_h = px(30);
    float total =
        (with_art ? art + tok::gap_l() : 0.0f) + text_w + (action.empty() ? 0.0f : tok::gap_xl() + action_w);
    const bool roomy = total <= size.x - tok::gap_l() * 2;
    if (!roomy)  // the line alone, centred
        total = title_size.x;
    float x = pos.x + std::max(tok::gap_l(), (size.x - total) * 0.5f);
    const float cy = pos.y + size.y * 0.5f;
    if (!roomy) {
        tok::text(s, dl, ui::tokens::kBody, ImVec2(x, cy - title_size.y * 0.5f), g_pal->dim,
                  row.title.c_str());
        return;
    }
    if (with_art) {
        tok::draw_art(dl, row.art, ImVec2(x + art * 0.5f, cy), art);
        x += art + tok::gap_l();
    }
    float y = cy - text_h * 0.5f;
    tok::text(s, dl, ui::tokens::kBody, ImVec2(x, y), g_pal->text, row.title.c_str());
    if (!row.hint.empty())
        tok::text(s, dl, ui::tokens::kCaption, ImVec2(x, y + title_size.y + tok::gap_xs()), g_pal->dim,
                  row.hint.c_str());
    x += text_w + tok::gap_xl();
    if (action.empty())
        return;
    ImGui::SetCursorScreenPos(ImVec2(x, cy - action_h * 0.5f));
    if (!row.button.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Button, v4(g_pal->focus));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(alpha(g_pal->focus, 220)));
        ImGui::PushStyleColor(ImGuiCol_Text, v4(accent_foreground()));
        const bool clicked = ImGui::Button(row.button.c_str(), ImVec2(action_w, action_h));
        ImGui::PopStyleColor(3);
        if (clicked)
            run_command(s, row.command);
        return;
    }
    const ImVec2 t = ImGui::CalcTextSize(row.link.c_str());
    const ImVec2 p(x, cy - t.y * 0.5f);
    ImGui::SetCursorScreenPos(p);
    const bool clicked = ImGui::InvisibleButton("##row-link", ImVec2(t.x, t.y + px(4)));
    const bool hov = ImGui::IsItemHovered();
    dl->AddText(p, g_pal->focus, row.link.c_str());
    if (hov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        dl->AddLine(ImVec2(p.x, p.y + t.y + px(1)), ImVec2(p.x + t.x, p.y + t.y + px(1)), g_pal->focus);
    }
    if (clicked && row.on_link)
        row.on_link();
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
