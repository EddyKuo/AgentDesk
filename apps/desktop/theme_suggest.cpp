// Suggested themes: each layout can name the theme pair it was designed with. Settings > Look
// offers it in a row with a "Use" button and marks the suggested cards; nothing changes the
// person's look unless they click.
#include "overlay_kit.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
bool palette_listed(const std::string& id) {
    const auto all = all_palettes();
    return std::any_of(all.begin(), all.end(), [&](const Palette* p) { return id == p->id; });
}
}  // namespace

std::optional<std::pair<std::string, std::string>> usable_suggestion(const LayoutSpec& l) {
    if (!l.suggested || !l.theme.empty())  // a layout that names its own theme overrides any choice
        return std::nullopt;
    if (!palette_listed(l.suggested->first) || !palette_listed(l.suggested->second)) {
        static std::set<std::string> told;  // once per layout and run
        if (told.insert(l.name).second)
            g_settings_problem = "The layout \"" + l.name + "\" suggests a theme that isn't installed";
        return std::nullopt;
    }
    return l.suggested;
}

void draw_suggested_mark(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    // A four-pointed star: long points on the axes, short ones on the diagonals.
    ImVec2 pts[8];
    for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) * 3.14159265f / 4.0f - 3.14159265f / 2.0f;
        const float rr = i % 2 == 0 ? r : r * 0.32f;
        pts[i] = ImVec2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
    }
    dl->AddConcavePolyFilled(pts, 8, col);
}

const Palette* layout_palette(App::State& s, const LayoutSpec& l) {
    const auto it = s.settings.layout_themes.find(l.name);
    const auto pair = it != s.settings.layout_themes.end()
                          ? it->second
                          : std::pair{s.settings.theme_dark, s.settings.theme_light};
    return effective_palette(resolved_appearance(s), pair.first, pair.second, l.theme);
}

// The row above the theme cards when the shown layout suggests a pair it does not use: a star,
// "Made for <layout>: <dark> (dark) · <light> (light)" and Use. Returns true when Use was clicked.
bool look_theme_suggestion(App::State& s) {
    const auto sug = usable_suggestion(s.layout());
    if (!sug || layout_theme_pair(s.settings) == *sug)
        return false;
    const std::string text = "Made for " + layout_display_name(s.layout().name) + ": " +
                             palette_by_id(sug->first)->label + " (dark) \xc2\xb7 " +
                             palette_by_id(sug->second)->label + " (light)";
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 bs = settings_button_size(s, "Use");
    const float pad = px(10);
    const float h = bs.y + pad * 2;
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), alpha(g_pal->focus, g_pal->light ? 18 : 24), px(8));
    dl->AddRect(a, ImVec2(a.x + w, a.y + h), alpha(g_pal->focus, 90), px(8));
    draw_suggested_mark(dl, ImVec2(a.x + pad + px(7), a.y + h * 0.5f), px(6.5f), g_pal->focus);
    const float th = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, "Mg").y;
    text_clipped(dl, s.fonts.ui, fpx(13), ImVec2(a.x + pad + px(20), a.y + (h - th) * 0.5f), g_pal->text,
                 text, w - pad * 3 - px(20) - bs.x);
    ImGui::SetCursorScreenPos(ImVec2(a.x + w - pad - bs.x, a.y + pad));
    const bool used = settings_button(s, "##use-suggested", "Use");
    if (used)
        run_command(s, "theme.suggested");
    ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + h + px(10)));
    ImGui::Dummy(ImVec2(0, 0));
    return used;
}

// theme.suggested: the shown layout's suggested pair becomes its dark and light theme.
std::optional<std::string> exec_suggest_command(App::State& s, const std::string& name) {
    if (name != "theme.suggested")
        return std::nullopt;
    const auto sug = usable_suggestion(s.layout());
    if (!sug)
        return std::string("this layout has no suggested themes");
    s.settings.layout_themes[s.settings.layout] = *sug;
    save_settings(s.settings);
    return std::string();
}

}  // namespace detail
}  // namespace agentdesk::desktop
