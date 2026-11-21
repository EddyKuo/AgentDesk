// First-use tips: the card that appears the first time each layout (and Focus mode) shows, the
// layout.tips command that brings it back, and the Settings > Look row. What the cards say and
// which have been seen is libs/ui layout_tips.
#include "app_internal.hpp"
#include "overlay_kit.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
constexpr const char* kPopup = "##layout-tips";
constexpr float kCardW = 460.0f;  // design px
// A layout must have been on screen this long before its card appears by itself, so a quick
// pass through several layouts (Ctrl+Shift+L held down) does not stack cards.
constexpr auto kSettle = std::chrono::milliseconds(600);

// The card for what is shown now ("" for a layout of the person's own without a built-in style).
std::string shown_card_key(const App::State& s) {
    const LayoutSpec& l = s.layout();
    return ui::tips::card_key(l.name, l.style, s.settings.focus_on);
}

// Automated runs (--exit-after-ms) never show a card by themselves, so screenshots stay as they
// were; AGENTDESK_TIPS=1 lets them (layout.tips always works).
bool auto_allowed(const App::State& s) {
    if (!s.config.automated)
        return true;
    char buf[8] = {};
    return ::GetEnvironmentVariableA("AGENTDESK_TIPS", buf, sizeof buf) > 0 && buf[0] == '1';
}

void close_card(App::State& s, bool all_off) {
    auto& t = s.layout_tips;
    bool changed = ui::tips::mark_seen(s.settings.tips_seen, t.open_key);
    if (all_off && s.settings.tips) {
        s.settings.tips = false;
        changed = true;
    }
    if (changed)
        save_settings(s.settings);
    t.open_key.clear();
    ImGui::CloseCurrentPopup();
    // The Enter or Esc that closed the card must not reach the terminal that gets the keyboard back.
    ImGui::GetIO().ClearInputKeys();
    s.focus_view(s.ws.focused_session());
}

// One tip: a small accent dot, the text wrapped beside it, and its key as a chip on the right.
void tip_row(App::State& s, ImDrawList* dl, const ui::tips::ShownTip& tip, float w) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float fs = fpx(13);
    const float lh = std::round(fs * 1.24f);
    const float cfs = fpx(11.5f);
    float chip_w = 0;
    if (!tip.chord.empty())
        chip_w = s.fonts.ui_medium->CalcTextSizeA(cfs, FLT_MAX, 0, tip.chord.c_str()).x + px(14);
    const float text_x = p.x + px(16);
    const float wrap = std::max(px(80), w - px(16) - (chip_w > 0 ? chip_w + px(14) : 0.0f));
    dl->AddCircleFilled(ImVec2(p.x + px(4), p.y + lh * 0.5f), px(2.5f), g_pal->focus);
    float y = p.y;
    for_each_wrapped_line(s.fonts.ui, fs, tip.text.c_str(), wrap, [&](const char* a, const char* b) {
        dl->AddText(s.fonts.ui, fs, ImVec2(text_x, y + (lh - fs) * 0.5f - px(1)), g_pal->text, a, b);
        y += lh;
    });
    if (chip_w > 0) {
        const float ch = px(21);
        const ImVec2 a(p.x + w - chip_w, p.y + (lh - ch) * 0.5f);
        dl->AddRectFilled(a, ImVec2(a.x + chip_w, a.y + ch), raised_color(), px(5));
        dl->AddRect(a, ImVec2(a.x + chip_w, a.y + ch), g_pal->border, px(5));
        const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(cfs, FLT_MAX, 0, tip.chord.c_str());
        dl->AddText(s.fonts.ui_medium, cfs, ImVec2(a.x + px(7), a.y + (ch - t.y) * 0.5f), g_pal->dim,
                    tip.chord.c_str());
    }
    ImGui::Dummy(ImVec2(w, std::max(y - p.y, px(21)) + px(11)));
}

// A text link in `col`, underlined while hovered. True when clicked.
bool text_link(App::State& s, const char* id, const char* label, ImVec2 at, ImU32 col) {
    const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(12.5f), FLT_MAX, 0, label);
    ImGui::SetCursorScreenPos(at);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(t.x, t.y + px(4)));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(s.fonts.ui, fpx(12.5f), at, hov ? g_pal->text : col, label);
    if (hov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        dl->AddLine(ImVec2(at.x, at.y + t.y + px(1)), ImVec2(at.x + t.x, at.y + t.y + px(1)), g_pal->text);
    }
    return clicked;
}

// The card's body. Returns 1 for "Got it" (or Esc, or a click outside), 2 for "Don't show tips".
int card_body(App::State& s, const ui::tips::Card& card) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    // A small label over the title says what this is.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        tok::text(s, dl, ui::tokens::kCaption, p, g_pal->focus, "LAYOUT TIPS");
        ImGui::Dummy(ImVec2(w, px(ui::tokens::kCaption.line) + tok::gap_xs()));
    }
    int result = overlay_title(s, card.title.c_str(), EscChip::kButton, 18.0f) ? 1 : 0;
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        float y = p.y;
        const float fs = fpx(13);
        for_each_wrapped_line(s.fonts.ui, fs, card.purpose.c_str(), w, [&](const char* a, const char* b) {
            dl->AddText(s.fonts.ui, fs, ImVec2(p.x, y), g_pal->dim, a, b);
            y += std::round(fs * 1.3f);
        });
        ImGui::Dummy(ImVec2(w, y - p.y + px(14)));
    }
    for (const auto& tip :
         ui::tips::resolve(card, [&](std::string_view c) { return style_chord_for(s, std::string(c)); }))
        tip_row(s, dl, tip, w);

    // Footer: "Don't show tips" on the left, "Got it" on the right.
    ImGui::Dummy(ImVec2(w, px(8)));
    const ImVec2 f = ImGui::GetCursorScreenPos();
    tok::hline(dl, f.x, f.x + w, f.y, g_pal->border);  // a crisp 1 px line
    const float bh = px(32);
    const float by = f.y + px(14);
    const char* ok = "Got it";
    const float bw = ImGui::CalcTextSize(ok).x + px(40);
    ImGui::SetCursorScreenPos(ImVec2(f.x + w - bw, by));
    ImGui::PushStyleColor(ImGuiCol_Button, v4(g_pal->focus));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(alpha(g_pal->focus, 220)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, v4(alpha(g_pal->focus, 200)));
    // White on the accent as elsewhere, black on a pale accent (Phosphor's) where white would not read.
    const bool dark_ink = ui::color::contrast(rgb_of(g_pal->focus), 0xffffff) < 3.0;
    ImGui::PushStyleColor(ImGuiCol_Text, dark_ink ? ImVec4(0, 0, 0, 1) : v4(accent_foreground()));
    if (ImGui::Button(ok, ImVec2(bw, bh)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
        result = 1;
    ImGui::PopStyleColor(4);
    const float lt = s.fonts.ui->CalcTextSizeA(fpx(12.5f), FLT_MAX, 0, "Don't show tips").y;
    if (text_link(s, "##tips-off", "Don't show tips", ImVec2(f.x, by + (bh - lt) * 0.5f - px(1)),
                  g_pal->faint))
        result = 2;
    ImGui::SetCursorScreenPos(ImVec2(f.x, by + bh));
    ImGui::Dummy(ImVec2(w, 0));
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || dialog_outside_click())
        result = std::max(result, 1);
    return result;
}
}  // namespace

void draw_layout_tips(App::State& s) {
    auto& t = s.layout_tips;
    const std::string key = shown_card_key(s);
    if (key != t.shown_key) {
        t.shown_key = key;
        t.shown_since = Clock::now();
    }
    // By itself: the first time a layout with a card has been on screen for a moment, while
    // nothing else is open and the welcome is behind us.
    if (t.open_key.empty() && t.request.empty() && s.doc_loaded && !s.show_welcome &&
        (!s.settings.welcomed.empty() || s.config.automated) &&
        Clock::now() - t.shown_since >= kSettle && auto_allowed(s) &&
        ui::tips::should_show(key, s.settings.tips, s.settings.tips_seen) &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        t.request = key;
    if (!t.request.empty() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        t.open_key = std::exchange(t.request, std::string());
        ImGui::OpenPopup(kPopup);
    }
    const auto card = ui::tips::card(t.open_key);
    if (!card) {
        t.open_key.clear();
        return;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.46f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(px(kCardW), vp->WorkSize.x - px(16)), 0));
    push_overlay_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(24), px(22)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(12));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), 0));
    const bool open = ImGui::BeginPopupModal(
        kPopup, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar(3);
    pop_overlay_style();
    if (!open) {
        t.open_key.clear();
        return;
    }
    ui::adapter::release_keyboard();  // keys pressed in the card stay out of the terminal
    const int r = card_body(s, *card);
    if (r > 0)
        close_card(s, r == 2);
    ImGui::EndPopup();
}

std::optional<std::string> exec_tips_command(App::State& s, const std::string& name, const std::string& arg) {
    if (name != "layout.tips")
        return std::nullopt;
    // layout.tips:<key> shows that card (any layout's); plain layout.tips the shown one's, and
    // the AgentDesk card for a layout of the person's own.
    std::string key = arg.empty() ? shown_card_key(s) : arg;
    if (key.empty())
        key = "workspace";
    if (!ui::tips::card(key))
        return "No tips for \"" + key + "\"";
    s.layout_tips.request = key;
    return std::string();
}

bool look_tips_row(App::State& s) {
    bool changed = false;
    const char* show = "Show tips for this layout";
    const ImVec2 bs = settings_button_size(s, show);
    settings_row(s, "Layout tips", "A short card the first time each layout opens.", bs.x + px(12) + px(36),
                 bs.y);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    if (settings_button(s, "##tips-show", show)) {
        exec_tips_command(s, "layout.tips", "");
        s.layout_tips.close_settings = true;  // the card opens once Settings has closed
    }
    ImGui::SetCursorScreenPos(ImVec2(at.x + bs.x + px(12), at.y + (bs.y - px(20)) * 0.5f));
    if (settings_toggle("##tips-on", s.settings.tips)) {
        s.settings.tips = !s.settings.tips;
        changed = true;
    }
    settings_row_done(s);
    return changed;
}

}  // namespace detail
}  // namespace agentdesk::desktop
