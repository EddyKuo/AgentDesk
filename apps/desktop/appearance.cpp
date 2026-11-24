// Appearance by schedule: which half (light or dark) of the layout's theme pair shows when
// Settings > Look > Appearance is "schedule", the Settings rows for it, and the appearance
// commands. No network and no location service: a latitude is only ever typed in.
#include "overlay_kit.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace sch = ui::schedule;

namespace {
// The local date, time and offsets now. AGENTDESK_NOW="YYYY-MM-DDTHH:MM" replaces the date and
// time (screenshots of the schedule; the offsets stay this computer's).
sch::LocalNow local_now() {
    const os::LocalTime t = os::local_time();
    sch::LocalNow n;
    n.year = t.year;
    n.month = t.month;
    n.day = t.day;
    n.minute = t.hour * 60 + t.minute;
    n.utc_offset = t.utc_offset;
    n.std_offset = t.std_offset;
    if (const std::string set = env("AGENTDESK_NOW"); set.size() >= 16 && set.size() < 32) {
        int y = 0, mo = 0, d = 0, h = 0, mi = 0;
        if (sscanf_s(set.c_str(), "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) == 5 && mo >= 1 && mo <= 12 &&
            d >= 1 && d <= 31 && h >= 0 && h < 24 && mi >= 0 && mi < 60) {
            n.year = y;
            n.month = mo;
            n.day = d;
            n.minute = h * 60 + mi;
        }
    }
    return n;
}

// The schedule's verdict, worked out at most once a second (the frame asks every frame).
const sch::Today& today(const App::State& s) {
    static sch::Today cached;
    static std::chrono::steady_clock::time_point at{};
    static std::string key;
    const auto& a = s.settings.appearance_schedule;
    std::string k = a.mode + "|" + std::to_string(a.light_at) + "|" + std::to_string(a.dark_at) + "|" +
                    std::to_string(a.latitude.value_or(999)) + "|" +
                    std::to_string(a.longitude.value_or(999)) + "|" + std::to_string(a.sun_offset);
    const auto now = std::chrono::steady_clock::now();
    if (k != key || now - at >= std::chrono::seconds(1)) {
        key = std::move(k);
        at = now;
        cached = sch::evaluate(a, local_now());
    }
    return cached;
}

bool override_active(const App::State& s) {
    return s.schedule_override_until && std::chrono::system_clock::now() < *s.schedule_override_until;
}
}  // namespace

std::string resolved_appearance(App::State& s) {
    const std::string& a = s.settings.appearance;
    if (a != "schedule")
        return a;
    const sch::Today& t = today(s);
    if (!t.valid)
        return "system";  // equal times, or sunrise mode without a latitude
    if (override_active(s))
        return s.schedule_override_light ? "light" : "dark";
    s.schedule_override_until.reset();
    return t.light ? "light" : "dark";
}

// appearance:<system|light|dark|schedule> and appearance.toggle.
std::optional<std::string> exec_appearance_command(App::State& s, const std::string& name,
                                                   const std::string& arg) {
    if (name == "appearance") {
        if (arg != "system" && arg != "light" && arg != "dark" && arg != "schedule")
            return "appearance is system, light, dark or schedule";
        s.settings.appearance = arg;
        s.schedule_override_until.reset();
        save_settings(s.settings);
        return std::string();
    }
    if (name == "appearance.toggle") {
        const bool light_now = g_pal->light;
        if (s.settings.appearance == "schedule" && today(s).valid) {
            // The other look until the next switch; toggling again ends that early.
            if (override_active(s)) {
                s.schedule_override_until.reset();
            } else {
                s.schedule_override_light = !light_now;
                s.schedule_override_until =
                    std::chrono::system_clock::now() + std::chrono::minutes(today(s).next_switch_in);
            }
            return std::string();
        }
        s.settings.appearance = light_now ? "dark" : "light";
        save_settings(s.settings);
        return std::string();
    }
    return std::nullopt;
}

// ------------------------------------------------------------------ Settings rows
namespace {
// Two or more choices like segmented(), but one choice can be greyed out (not clickable).
bool segmented_with_off(App::State& s, const char* id, std::string& value,
                        std::initializer_list<std::pair<const char*, const char*>> options, const char* off) {
    bool changed = false;
    ImGui::PushID(id);
    const float bw = px(104), bh = px(28);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + bw * static_cast<float>(options.size()) + px(4), a.y + bh + px(4)),
                      raised_color(), px(7));
    float x = a.x + px(2);
    for (const auto& [key, label] : options) {
        const bool disabled = off != nullptr && std::string_view(off) == key;
        ImGui::SetCursorScreenPos(ImVec2(x, a.y + px(2)));
        if (ImGui::InvisibleButton(key, ImVec2(bw, bh)) && !disabled && value != key) {
            value = key;
            changed = true;
        }
        const bool on = value == key;
        const bool hov = ImGui::IsItemHovered() && !disabled;
        if (on || hov)
            dl->AddRectFilled(ImVec2(x, a.y + px(2)), ImVec2(x + bw, a.y + px(2) + bh),
                              on ? g_pal->selected : g_pal->hover, px(6));
        text_centered(dl, s.fonts.ui, fpx(12), ImVec2(x, a.y + px(2)), ImVec2(bw, bh),
                      disabled ? g_pal->faint
                      : on     ? g_pal->text
                               : g_pal->dim,
                      label);
        x += bw;
    }
    ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + bh + px(6)));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopID();
    return changed;
}

// An "HH:MM" field, 64 px wide: Up and Down step 15 minutes; a time that does not parse gets a
// red border and is not kept. Returns true when `minutes` changed.
bool time_field(const char* id, int& minutes) {
    struct Field {
        char buf[8] = {};
        bool bad = false;
        bool editing = false;
        int shown = -1;
    };
    static std::map<std::string, Field> fields;
    Field& f = fields[id];
    if (f.shown != minutes && !f.editing) {
        std::snprintf(f.buf, sizeof f.buf, "%s", sch::format_hhmm(minutes).c_str());
        f.shown = minutes;
        f.bad = false;
    }
    const bool bad = f.bad;
    if (bad) {
        ImGui::PushStyleColor(ImGuiCol_Border, v4(g_pal->needs_you));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    }
    ImGui::SetNextItemWidth(px(64));
    auto step = [](ImGuiInputTextCallbackData* d) {
        if (d->EventFlag != ImGuiInputTextFlags_CallbackHistory)
            return 0;
        const int base =
            sch::parse_hhmm(std::string(d->Buf, static_cast<std::size_t>(d->BufTextLen))).value_or(0);
        const int next = d->EventKey == ImGuiKey_UpArrow ? (base / 15 + 1) * 15 : ((base + 14) / 15 - 1) * 15;
        const std::string t = sch::format_hhmm(next);
        d->DeleteChars(0, d->BufTextLen);
        d->InsertChars(0, t.c_str());
        return 0;
    };
    const bool edited = ImGui::InputText(id, f.buf, sizeof f.buf, ImGuiInputTextFlags_CallbackHistory, step);
    f.editing = ImGui::IsItemActive();
    if (bad) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    if (!edited)
        return false;
    const auto m = sch::parse_hhmm(f.buf);
    f.bad = !m.has_value();
    if (!m || *m == minutes)
        return false;
    minutes = *m;
    f.shown = minutes;
    return true;
}

// A latitude or longitude field, 64 px (empty clears it), kept to one decimal place.
bool degrees_field(const char* id, std::optional<double>& value, double limit) {
    struct Field {
        char buf[16] = {};
        bool editing = false;
        bool init = false;
        std::optional<double> shown;
    };
    static std::map<std::string, Field> fields;
    Field& f = fields[id];
    if ((!f.init || f.shown != value) && !f.editing) {
        if (value)
            std::snprintf(f.buf, sizeof f.buf, "%.1f", *value);
        else
            f.buf[0] = '\0';
        f.shown = value;
        f.init = true;
    }
    ImGui::SetNextItemWidth(px(64));
    const bool edited = ImGui::InputTextWithHint(
        id, "-", f.buf, sizeof f.buf, ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsNoBlank);
    const bool was_editing = std::exchange(f.editing, ImGui::IsItemActive());
    if (was_editing && !f.editing)
        f.init = false;  // show the kept value (one decimal place) once typing ends
    if (!edited)
        return false;
    if (f.buf[0] == '\0') {
        if (!value)
            return false;
        value.reset();
        f.shown = value;
        return true;
    }
    char* end = nullptr;
    const double v = std::strtod(f.buf, &end);
    if (end == f.buf || *end != '\0' || std::abs(v) > limit)
        return false;
    value = sch::round_tenth(v);
    f.shown = value;
    return true;
}

// A dim word before a field, on the field's text line.
void field_label(const char* text) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(v4(g_pal->dim), "%s", text);
    ImGui::SameLine(0, px(6));
}
}  // namespace

// Under Appearance when it is "schedule": fixed times or sunrise and sunset, the times and the
// place, then a line with today's switches and the look now (or why the schedule is not used).
bool look_schedule(App::State& s) {
    bool changed = false;
    auto& a = s.settings.appearance_schedule;
    const float fh = ImGui::GetFrameHeight();
    settings_row(s, "Switch by", nullptr, px(104) * 2 + px(4), px(32));
    if (segmented_with_off(s, "##schedule-mode", a.mode,
                           {{"fixed", "Fixed times"}, {"sun", "Sunrise, sunset"}},
                           a.latitude ? nullptr : "sun"))
        changed = true;
    settings_row_done(s);
    const float label_w = ImGui::CalcTextSize("Light from").x;
    settings_row(s, "Times", "Up and Down move a time by 15 minutes.",
                 label_w * 2 + px(64) * 2 + px(6) * 2 + px(14), fh);
    field_label("Light from");
    changed |= time_field("##light-at", a.light_at);
    ImGui::SameLine(0, px(14));
    field_label("Dark from");
    changed |= time_field("##dark-at", a.dark_at);
    settings_row_done(s);
    const float deg_w = ImGui::CalcTextSize("Longitude").x;
    settings_row(s, "Place", "Typed in, never looked up. North and east are positive.",
                 deg_w * 2 + px(64) * 2 + px(6) * 2 + px(14), fh);
    field_label("Latitude");
    if (degrees_field("##lat", a.latitude, 90.0)) {
        changed = true;
        if (!a.latitude && a.mode == "sun")
            a.mode = "fixed";
    }
    ImGui::SameLine(0, px(14));
    field_label("Longitude");
    changed |= degrees_field("##lon", a.longitude, 180.0);
    settings_row_done(s);
    if (a.mode == "sun") {
        settings_row(s, "Shift", "Minutes added to sunrise and sunset.", px(kStepperW), px(kStepperH));
        float off = static_cast<float>(a.sun_offset);
        if (stepper(s, "sun-offset", off, 15.0f, -120.0f, 120.0f, "%.0f")) {
            a.sun_offset = static_cast<int>(off);
            changed = true;
        }
        settings_row_done(s);
    }
    const sch::Today t = sch::evaluate(a, local_now());
    std::string line;
    ImU32 col = g_pal->dim;
    if (a.mode == "fixed" && !t.valid) {
        line = "Light and dark can't start at the same time. Until one changes, the system setting is used.";
        col = g_pal->needs_you;
    } else if (!t.valid) {
        line = "Enter a latitude to use sunrise and sunset.";
    } else if (!s.layout().theme.empty()) {
        line = "This layout names its own theme, so the schedule has no effect here.";
    } else {
        const bool light = override_active(s) ? s.schedule_override_light : t.light;
        if (t.light_from < 0)
            line = std::string("Today: ") + (t.light ? "light all day" : "dark all day");
        else
            line = "Today: light " + sch::format_hhmm(t.light_from) + " \xe2\x86\x92 dark " +
                   sch::format_hhmm(t.dark_from);
        line += std::string(" \xc2\xb7 now ") + (light ? "Light" : "Dark");
        if (override_active(s))
            line += " (switched by hand until the next change)";
        if (a.mode == "sun" && !a.longitude)
            line += ". Without a longitude, your time zone's meridian is used.";
    }
    settings_gap(px(10));
    settings_wrapped(s, line.c_str(), col);
    if (!a.latitude && a.mode == "fixed")
        settings_wrapped(s, "Enter a latitude to use sunrise and sunset.", g_pal->faint);
    settings_gap(px(14));
    return changed;
}

}  // namespace detail
}  // namespace agentdesk::desktop
