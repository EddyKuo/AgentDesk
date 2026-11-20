// Tint by agent state: while an agent needs you, the interface's backgrounds lean slightly toward
// the "needs you" colour and the accent turns a little toward it (a cool tint for finished agents
// when asked). Terminal colours, text and status colours never change.
#include "agentdesk/ui/ambient.hpp"

#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
ui::AmbientFade g_fade;
Palette g_ambient_pal{};  // the base palette with the tint; g_pal points here while tinted
const Palette* g_tinted_from = nullptr;
int g_step = 0;              // the strength in 64ths that g_ambient_pal was made with
std::uint32_t g_toward = 0;  // the colour the tint leans toward (kept while it fades out)
double g_k = 0;
bool g_moving = false;
Clock::time_point g_last_frame{};

ImU32 with_rgb(ImU32 old, std::uint32_t rgb) {
    return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (old >> 24) & 255);
}

// Windows' contrast themes: the tint stays off (checked at most every 2 s).
bool windows_high_contrast() {
    static auto checked = Clock::time_point{};
    static bool on = false;
    if (Clock::now() - checked > std::chrono::seconds(2)) {
        checked = Clock::now();
        HIGHCONTRASTW hc{sizeof hc};
        on = ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0) != FALSE &&
             (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
    }
    return on;
}
}  // namespace

bool motion_reduced() {
    static auto checked = Clock::time_point{};
    static bool reduced = false;
    if (Clock::now() - checked > std::chrono::seconds(2)) {
        checked = Clock::now();
        BOOL animate = TRUE;  // Settings > Accessibility > Visual effects > Animation effects
        reduced =
            ::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0) != FALSE && animate == FALSE;
    }
    return reduced;
}

AmbientResult ambient_palette(App::State& s, const Palette* base, bool paused) {
    const auto now = Clock::now();
    const bool was_away =
        g_last_frame != Clock::time_point{} && now - g_last_frame > std::chrono::milliseconds(500);
    g_last_frame = now;
    double target = 0;
    if (s.settings.ambient == "subtle" && !paused && !windows_high_contrast()) {
        // The most urgent state over every session, other machines' included; a machine that
        // dropped its connection reports its sessions as lost, which do not count.
        Attention top = Attention::kNone;
        for (const auto& [id, v] : s.views_by_id)
            if (ui::attention_rank(s.attention_of(id)) > ui::attention_rank(top))
                top = s.attention_of(id);
        if (top == Attention::kBlocked) {
            target = 1.0;
            g_toward = rgb_of(base->needs_you);
        } else if (top == Attention::kDone && s.settings.ambient_done) {
            target = 0.5;
            g_toward = rgb_of(base->done);
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();
    // Back from the background: straight to where it should be, no catching up on the fade.
    g_k = g_fade.update(target, ms, motion_reduced() || was_away || paused);
    g_moving = g_fade.moving(ms);
    const int step = static_cast<int>(std::lround(g_k * 64));
    AmbientResult r;
    if (step == 0) {
        r.palette = base;
        r.recolor = g_step != 0;
        g_step = 0;
        g_tinted_from = nullptr;
        return r;
    }
    r.palette = &g_ambient_pal;
    if (step == g_step && g_tinted_from == base)
        return r;
    g_step = step;
    g_tinted_from = base;
    r.recolor = true;
    ui::color::TintInput in;
    in.base = check_colors_of(*base);
    in.hover = rgb_of(base->hover);
    in.selected = rgb_of(base->selected);
    in.hover_opaque = (base->hover >> 24) == 255;
    in.selected_opaque = (base->selected >> 24) == 255;
    // The tint moves opaque colours only; translucent ones (hover, selected over the chrome)
    // pick it up from what is under them.
    const bool opaque_bg =
        (base->bg >> 24) == 255 && (base->sidebar >> 24) == 255 && (base->card >> 24) == 255;
    const ui::color::TintOutput t = ui::color::tint_protected(in, g_toward, opaque_bg ? step / 64.0 : 0.0);
    g_ambient_pal = *base;
    g_ambient_pal.bg = with_rgb(base->bg, t.canvas);
    g_ambient_pal.sidebar = with_rgb(base->sidebar, t.chrome);
    g_ambient_pal.card = with_rgb(base->card, t.surface);
    g_ambient_pal.hover = with_rgb(base->hover, t.hover);
    g_ambient_pal.selected = with_rgb(base->selected, t.selected);
    g_ambient_pal.focus = with_rgb(base->focus, t.accent);
    return r;
}

bool ambient_moving() {
    return g_moving;
}

double ambient_strength() {
    return g_k;
}

// Settings > Look > Theme: "Tint by agent state" (Off / Subtle) and "Also when done".
bool look_ambient(App::State& s) {
    bool changed = false;
    settings_row(s, "Tint by agent state", "When an agent needs you, the interface turns a little warmer.",
                 px(78) * 2 + px(4), px(32));
    changed |= segmented(s, "##ambient", s.settings.ambient, {{"off", "Off"}, {"subtle", "Subtle"}});
    settings_row_done(s);
    if (s.settings.ambient == "subtle") {
        settings_row(s, "Also when done", "A cooler tint, half as strong, while a finished agent is unseen.",
                     px(36), px(20));
        if (settings_toggle("##ambient-done", s.settings.ambient_done)) {
            s.settings.ambient_done = !s.settings.ambient_done;
            changed = true;
        }
        settings_row_done(s);
    }
    return changed;
}

}  // namespace detail
}  // namespace agentdesk::desktop
