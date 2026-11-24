// The theme editor: a dialog that edits a theme file (themes\<name>.json in the data folder) with
// the whole window previewing the draft, contrast numbers beside each colour, a small sample
// that can show the colours as people with colour-vision deficiencies see them, and a list of
// checks. Saving writes the usual theme file format and loads it at once.
#include "overlay_kit.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace th = ui::theme;
namespace col = ui::color;

namespace {
enum class Confirm { kNone, kOverwrite, kExternal, kLowContrast, kDiscard };

struct Editor {
    bool open = false;
    bool want_open = false;
    bool back_to_settings = false;
    std::string based_on;
    bool start_light = false;
    std::string original;  // the file's name when editing an existing one of the person's
    char name[96] = {};
    th::ThemeFile file, saved;
    th::Derived derived;
    std::array<ImU32, 16> ansi{};
    Palette pal{};
    std::string pal_id = "theme-editor-draft", pal_label;
    bool changed = false;  // the draft's colours changed since the frame last took them
    bool dirty = false;
    std::filesystem::file_time_type mtime{};
    std::string read_error;  // the file could not be read: nothing to edit
    std::filesystem::path read_error_file;
    std::string save_error;
    std::string note;  // "Saved", ...
    Confirm confirm = Confirm::kNone;
    col::Cvd simulate = col::Cvd::kNone;
    std::map<ImGuiID, std::array<char, 12>> hex_bufs;  // by the field's ImGui id
    std::map<ImGuiID, bool> hex_bad;
};
Editor g_ed;

std::vector<std::string> builtin_ids() {
    std::vector<std::string> ids;
    for (const auto& p : builtin_palettes())
        ids.emplace_back(p.id);
    ids.emplace_back("hc-dark");  // kept for the high contrast themes
    ids.emplace_back("hc-light");
    return ids;
}

std::filesystem::path user_theme_file(const std::string& name) {
    return data_dir() / L"themes" / win::to_wide(name + ".json");
}

void rebuild() {
    g_ed.derived = th::derive(g_ed.file);
    g_ed.pal_label = g_ed.name;
    g_ed.pal = palette_from_theme(g_ed.file, g_ed.pal_id.c_str(), g_ed.pal_label.c_str(), g_ed.ansi);
    g_ed.changed = true;
}

void edited() {
    g_ed.dirty = true;
    g_ed.note.clear();
    rebuild();
}

// A theme as a file: its own file when it has one, the preset's values, or (AgentDesk's own
// palettes, which are not derived from anything) every colour written out.
std::optional<th::ThemeFile> file_of_palette(const Palette& p, std::string* error) {
    if (const auto f = theme_file_path(p.id); !f.empty()) {
        bool ok = false;
        const std::string text = read_text_file(f, &ok);
        if (!ok) {
            *error = "the file can't be opened";
            return std::nullopt;
        }
        return th::parse(text, error);
    }
    th::ThemeFile t;
    if (std::string_view(p.id).starts_with(kPresetPrefix)) {
        for (const auto& [name, pre] : preset_palettes())
            if (std::string(kPresetPrefix) + name == p.id) {
                t.background = pre.background;
                t.foreground = pre.foreground;
                t.selection = pre.selection;
                t.cursor = pre.cursor;
                t.ansi = pre.ansi;
                // A file has no status table: the preset's status colours become overrides.
                if (pre.status) {
                    t.colors["working"] = th::hex((*pre.status)[0]);
                    t.colors["blocked"] = th::hex((*pre.status)[1]);
                    t.colors["done"] = th::hex((*pre.status)[2]);
                }
                return t;
            }
    }
    t.background = rgb_of(p.term_bg);
    t.foreground = rgb_of(p.term_fg);
    t.selection = rgb_of(p.selection);
    t.cursor = rgb_of(p.cursor);
    if (p.ansi != nullptr)
        for (std::size_t i = 0; i < 16; ++i)
            t.ansi[i] = rgb_of((*p.ansi)[i]);
    else
        t.ansi = th::default_ansi();
    auto put = [&](const char* k, ImU32 c) {
        t.colors[k] = th::hex(th::Token{rgb_of(c), static_cast<int>(c >> 24)});
    };
    put("canvas", p.bg);
    put("chrome", p.sidebar);
    put("surface", p.card);
    put("border", p.border);
    put("accent", p.focus);
    put("info", p.info);
    put("hover", p.hover);
    put("selected", p.selected);
    put("foreground", p.text);
    put("muted", p.dim);
    put("dim", p.faint);
    put("blocked", p.needs_you);
    put("done", p.done);
    put("working", p.working);
    put("idle", p.idle);
    return t;
}

// A file name that is free: "<base>", else "<base>-2", "-3", ...
std::string free_name(std::string base) {
    if (!th::name_problem(base, builtin_ids()).empty())
        base = "my-theme";
    std::error_code ec;
    std::string name = base;
    for (int n = 2; std::filesystem::exists(user_theme_file(name), ec) || !theme_file_path(name).empty(); ++n)
        name = base + "-" + std::to_string(n);
    return name;
}

void open_editor(App::State& s, const Palette& from, bool in_place, bool from_settings) {
    g_ed = Editor{};
    g_ed.want_open = true;
    g_ed.back_to_settings = from_settings;
    g_ed.based_on = std::string(from.label) + (from.light ? " (Light)" : " (Dark)");
    g_ed.start_light = from.light;
    std::string why;
    const auto f = file_of_palette(from, &why);
    if (!f) {
        g_ed.read_error = why;
        g_ed.read_error_file = theme_file_path(from.id);
        g_ed.open = true;
        return;
    }
    g_ed.file = *f;
    std::string name;
    if (in_place && is_user_theme(from.id)) {
        name = g_ed.original = from.id;
        std::error_code ec;
        g_ed.mtime = std::filesystem::last_write_time(user_theme_file(name), ec);
    } else {
        std::string base = from.id;
        if (base.starts_with(kPresetPrefix))
            base = base.substr(kPresetPrefix.size());
        name = free_name(base + "-copy");
        g_ed.file.raw.erase("name");  // a copy gets its own label: its file name
    }
    std::snprintf(g_ed.name, sizeof g_ed.name, "%s", name.c_str());
    g_ed.saved = g_ed.file;
    g_ed.open = true;
    rebuild();
    (void)s;
}

void close_editor(App::State& s) {
    g_ed.open = false;
    g_ed.changed = true;  // the frame goes back to the theme it had
    ImGui::CloseCurrentPopup();
    if (g_ed.back_to_settings) {
        s.show_settings = true;
        s.settings_tab = "Look";
    }
}

// ------------------------------------------------------------------ saving
std::string write_file_atomically(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto tmp = std::filesystem::path(path).concat(L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(tmp, ec);
            return "Couldn't write the file (is the disk full or the folder read-only?)";
        }
    }
    if (std::error_code why; !os::replace_file(tmp, path, &why)) {
        std::filesystem::remove(tmp, ec);
        return why.message();
    }
    return {};
}

void do_save(App::State& s) {
    const std::string name = g_ed.name;
    const auto path = user_theme_file(name);
    if (const std::string e = write_file_atomically(path, th::write(g_ed.file)); !e.empty()) {
        g_ed.save_error = "Not saved: " + e;
        return;
    }
    if (const std::string e = reload_theme(name); !e.empty()) {
        g_ed.save_error = e;
        return;
    }
    std::error_code ec;
    g_ed.mtime = std::filesystem::last_write_time(path, ec);
    g_ed.original = name;
    g_ed.saved = g_ed.file;
    g_ed.dirty = false;
    g_ed.save_error.clear();
    g_ed.note = "Saved as " + data_file_text("themes", name + ".json");
    run_command(s, "theme:" + name);  // the layout shows it from now on (its light or dark half)
}

// Save, or the question to ask first. `asked` skips the questions already answered.
void try_save(App::State& s, Confirm asked = Confirm::kNone) {
    const std::string name = g_ed.name;
    if (!th::name_problem(name, builtin_ids()).empty())
        return;
    std::error_code ec;
    const auto path = user_theme_file(name);
    const bool exists = std::filesystem::exists(path, ec);
    const int order = static_cast<int>(asked);
    if (order < static_cast<int>(Confirm::kOverwrite) && name != g_ed.original &&
        (exists || !theme_file_path(name).empty())) {
        g_ed.confirm = Confirm::kOverwrite;
        return;
    }
    if (order < static_cast<int>(Confirm::kExternal) && name == g_ed.original && exists &&
        std::filesystem::last_write_time(path, ec) != g_ed.mtime) {
        g_ed.confirm = Confirm::kExternal;
        return;
    }
    if (order < static_cast<int>(Confirm::kLowContrast) &&
        col::contrast(g_ed.file.foreground, g_ed.file.background) < 3.0) {
        g_ed.confirm = Confirm::kLowContrast;
        return;
    }
    g_ed.confirm = Confirm::kNone;
    do_save(s);
}

// ------------------------------------------------------------------ rows
struct Row {
    const char* label;
    const char* key;
    bool file_field;           // background, foreground, selection, cursor of the file itself
    const char* on = nullptr;  // the background its contrast is shown against
    bool text = true;          // text (4.5:1) or a mark (3:1)
    bool alpha = false;        // may be translucent
};

th::Token row_value(const Row& r, bool* automatic) {
    *automatic = false;
    const std::string k = r.key;
    if (r.file_field) {
        if (k == "background")
            return {g_ed.file.background};
        if (k == "foreground")
            return {g_ed.file.foreground};
        if (k == "selection")
            return {g_ed.file.selection};
        *automatic = !g_ed.file.cursor.has_value();
        return {g_ed.file.cursor.value_or(g_ed.file.foreground)};
    }
    *automatic = !g_ed.file.colors.contains(k);
    return g_ed.derived.tokens.at(k);
}

void set_row(const Row& r, const th::Token& t) {
    const std::string k = r.key;
    if (r.file_field) {
        if (k == "background")
            g_ed.file.background = t.rgb;
        else if (k == "foreground")
            g_ed.file.foreground = t.rgb;
        else if (k == "selection")
            g_ed.file.selection = t.rgb;
        else
            g_ed.file.cursor = t.rgb;
        g_ed.file.colors.erase(k == "foreground" ? "terminal_foreground" : k);
    } else {
        g_ed.file.colors[k] = th::hex(t);
    }
    edited();
}

void reset_row(const Row& r) {
    if (r.file_field)
        g_ed.file.cursor.reset();
    else
        g_ed.file.colors.erase(r.key);
    edited();
}

// The colour a row's contrast is measured on (opaque).
col::Rgb row_background(const char* on) {
    const col::PaletteColors p = th::check_colors(g_ed.derived);
    const std::string k = on;
    if (k == "canvas")
        return p.canvas;
    if (k == "chrome")
        return p.chrome;
    if (k == "surface")
        return p.surface;
    return p.term_bg;
}

ImVec4 to_v4(const th::Token& t) {
    return ImVec4(static_cast<float>(col::red(t.rgb)) / 255.0f,
                  static_cast<float>(col::green(t.rgb)) / 255.0f,
                  static_cast<float>(col::blue(t.rgb)) / 255.0f, static_cast<float>(t.alpha) / 255.0f);
}
th::Token from_v4(const ImVec4& v) {
    auto ch = [](float f) { return static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
    return th::Token{col::rgb(ch(v.x), ch(v.y), ch(v.z)), ch(v.w)};
}

ImU32 im(const th::Token& t) {
    return IM_COL32(col::red(t.rgb), col::green(t.rgb), col::blue(t.rgb), t.alpha);
}

// A 20 px swatch over a checkerboard (for translucent colours); a click opens a colour picker.
// Returns true when the picker changed the colour.
bool swatch(const char* id, th::Token& value, bool alpha_ok) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float sz = px(20);
    if (ImGui::InvisibleButton(id, ImVec2(sz, sz)))
        ImGui::OpenPopup(id);
    const ImVec2 b(a.x + sz, a.y + sz);
    if (value.alpha < 255) {
        dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255), px(4));
        dl->AddRectFilled(a, ImVec2(a.x + sz * 0.5f, a.y + sz * 0.5f), IM_COL32(200, 200, 200, 255));
        dl->AddRectFilled(ImVec2(a.x + sz * 0.5f, a.y + sz * 0.5f), b, IM_COL32(200, 200, 200, 255));
    }
    dl->AddRectFilled(a, b, im(value), px(4));
    dl->AddRect(a, b, ImGui::IsItemHovered() ? g_pal->focus : g_pal->border, px(4));
    bool changed = false;
    if (ImGui::BeginPopup(id)) {
        ImVec4 v = to_v4(value);
        ImGui::SetNextItemWidth(px(220));
        const ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoSidePreview |
                                          ImGuiColorEditFlags_NoSmallPreview |
                                          ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_PickerHueBar;
        if (alpha_ok ? ImGui::ColorPicker4("##pick", &v.x, flags | ImGuiColorEditFlags_AlphaBar)
                     : ImGui::ColorPicker3("##pick", &v.x, flags)) {
            th::Token t = from_v4(v);
            if (!alpha_ok)
                t.alpha = 255;
            if (t != value) {
                value = t;
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    return changed;
}

// A hex field (84 px): applies as soon as it parses; red border while it does not.
bool hex_field(const char* id, th::Token& value, bool alpha_ok) {
    auto& buf = g_ed.hex_bufs[ImGui::GetID(id)];
    bool& bad = g_ed.hex_bad[ImGui::GetID(id)];
    const std::string shown = th::hex(value);
    if (!bad && shown != buf.data() && !ImGui::IsPopupOpen(id))
        std::snprintf(buf.data(), buf.size(), "%s", shown.c_str());
    if (bad) {
        ImGui::PushStyleColor(ImGuiCol_Border, v4(g_pal->needs_you));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    }
    ImGui::SetNextItemWidth(px(84));
    const bool edited_now = ImGui::InputText(id, buf.data(), buf.size(), ImGuiInputTextFlags_CharsNoBlank);
    const bool active = ImGui::IsItemActive();
    if (bad) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    if (!edited_now) {
        if (!active && bad) {  // left with a bad value: show the colour in use again
            bad = false;
        }
        return false;
    }
    std::string text = buf.data();
    if (!text.empty() && text[0] != '#')
        text = "#" + text;
    const auto t = th::parse_hex(text);
    bad = !t || (!alpha_ok && t->alpha != 255);
    if (bad || *t == value)
        return false;
    value = *t;
    return true;
}

// The contrast number after a row: a check when it meets the bar, a warning sign when not, red
// under 3:1 for text.
void contrast_badge(App::State& s, double ratio, bool text) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%.1f", ratio);
    const double bar = text ? col::kTextAA : col::kGraphicAA;
    const bool ok = ratio >= bar;
    const bool bad = text && ratio < 3.0;
    const ImU32 c = ok ? g_pal->dim : bad ? g_pal->needs_you : g_pal->working;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    const ImVec2 ts = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, buf);
    dl->AddText(s.fonts.ui, fpx(12), ImVec2(a.x + px(34) - ts.x, a.y + (h - ts.y) * 0.5f), c, buf);
    draw_icon(s, dl, ok ? ui::icon::kCheck : ui::icon::kWarning, fpx(12),
              ImVec2(a.x + px(46), a.y + h * 0.5f), ok ? g_pal->done : c);
    ImGui::Dummy(ImVec2(px(56), h));
}

void draw_row(App::State& s, const Row& r) {
    ImGui::PushID(r.key);
    bool automatic = false;
    th::Token v = row_value(r, &automatic);
    const float h = ImGui::GetFrameHeight();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + (h - px(20)) * 0.5f));
    if (swatch("##sw", v, r.alpha))
        set_row(r, v);
    ImGui::SetCursorScreenPos(ImVec2(start.x + px(30), start.y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(v4(g_pal->text), "%s", r.label);
    ImGui::SetCursorScreenPos(ImVec2(start.x + px(150), start.y));
    if (hex_field("##hex", v, r.alpha))
        set_row(r, v);
    ImGui::SameLine(0, px(6));
    if (r.on != nullptr)
        contrast_badge(s, col::contrast(v.rgb, row_background(r.on)), r.text);
    else
        ImGui::Dummy(ImVec2(px(56), h));
    ImGui::SameLine(0, px(4));
    if (automatic) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(v4(g_pal->faint), "auto");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Worked out from the background. Change it to set your own.");
    } else if (!r.file_field || std::string_view(r.key) == "cursor") {
        if (icon_button(s, "##reset", ui::icon::kRefresh, ImGui::GetCursorScreenPos(), h, px(13),
                        "Back to the worked-out colour", g_pal->dim))
            reset_row(r);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(
        ImVec2(start.x, start.y + h));  // the next row goes below, whatever ended this one
    ImGui::Dummy(ImVec2(0, 0));
}

void group_title(App::State& s, const char* t) {
    ImGui::Dummy(ImVec2(0, px(6)));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(s.fonts.ui_bold, fpx(11.5f), p, g_pal->faint, t);
    ImGui::Dummy(ImVec2(0, line_h(11.5f) + px(4)));
}

void draw_rows(App::State& s) {
    static constexpr Row kBase[] = {
        {"Background", "background", true},
        {"Terminal text", "foreground", true, "background", true},
        {"Selection", "selection", true},
        {"Cursor", "cursor", true},
    };
    static constexpr Row kInterface[] = {
        {"Canvas", "canvas", false},
        {"Chrome", "chrome", false},
        {"Surface", "surface", false},
        {"Text", "foreground", false, "canvas", true},
        {"Muted", "muted", false, "canvas", true},
        {"Dim", "dim", false, "canvas", true},
        {"Accent", "accent", false, "surface", false},
        {"Info", "info", false},
        {"Border", "border", false, nullptr, true, true},
        {"Hover", "hover", false, nullptr, true, true},
        {"Selected", "selected", false, nullptr, true, true},
    };
    static constexpr Row kStatus[] = {
        {"Needs you", "blocked", false, "chrome", false},
        {"Working", "working", false, "chrome", false},
        {"Done", "done", false, "chrome", false},
        {"Idle", "idle", false, "chrome", false},
    };
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(6), px(5)));
    group_title(s, "BASE");
    for (const Row& r : kBase)
        draw_row(s, r);
    group_title(s, "INTERFACE");
    for (const Row& r : kInterface)
        draw_row(s, r);
    group_title(s, "STATUS");
    for (const Row& r : kStatus)
        draw_row(s, r);
    group_title(s, "TERMINAL COLORS (0-15)");
    for (int i = 0; i < 16; ++i) {
        ImGui::PushID(i);
        th::Token t{g_ed.file.ansi[static_cast<std::size_t>(i)]};
        if (swatch("##ansi", t, false)) {
            g_ed.file.ansi[static_cast<std::size_t>(i)] = t.rgb;
            edited();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d  %s", i, th::hex(t.rgb).c_str());
        ImGui::PopID();
        if (i % 8 != 7)
            ImGui::SameLine(0, px(6));
    }
    ImGui::PopStyleVar();
}

// ------------------------------------------------------------------ preview
// A small sample of the window in the draft's colours: a sidebar with agents in each state, a
// tab strip and a terminal with ANSI text. `cvd` shows it as that colour vision would see it.
void draw_preview(App::State& s, ImVec2 a, ImVec2 size) {
    const col::Cvd cvd = g_ed.simulate;
    auto c = [&](ImU32 v) {
        const col::Rgb r = col::simulate(rgb_of(v), cvd);
        return IM_COL32(col::red(r), col::green(r), col::blue(r), (v >> 24) & 255);
    };
    const Palette& p = g_ed.pal;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(a.x + size.x, a.y + size.y);
    dl->PushClipRect(a, b, true);
    dl->AddRectFilled(a, b, c(p.bg), px(8));
    const float side = std::round(size.x * 0.34f);
    dl->AddRectFilled(a, ImVec2(a.x + side, b.y), c(p.sidebar), px(8), ImDrawFlags_RoundCornersLeft);
    dl->AddLine(ImVec2(a.x + side, a.y), ImVec2(a.x + side, b.y), c(p.border));
    const float fs = fpx(11.5f);
    float y = a.y + px(10);
    dl->AddText(s.fonts.ui, fs, ImVec2(a.x + px(10), y), c(p.faint), "Spaces");
    y += px(20);
    dl->AddRectFilled(ImVec2(a.x + px(6), y - px(3)), ImVec2(a.x + side - px(6), y + px(17)), c(p.selected),
                      px(5));
    dl->AddText(s.fonts.ui, fs, ImVec2(a.x + px(12), y), c(p.text), "api");
    y += px(22);
    dl->AddText(s.fonts.ui, fs, ImVec2(a.x + px(12), y), c(p.dim), "web");
    y += px(30);
    dl->AddText(s.fonts.ui, fs, ImVec2(a.x + px(10), y), c(p.faint), "Agents");
    y += px(20);
    const struct {
        const char* name;
        ImU32 color;
        int kind;  // 0 needs you, 1 working, 2 done, 3 idle
    } agents[] = {
        {"build", p.needs_you, 0}, {"docs", p.working, 1}, {"test", p.done, 2}, {"lint", p.idle, 3}};
    for (const auto& ag : agents) {
        const ImVec2 m(a.x + px(17), y + fs * 0.6f);
        const ImU32 mc = c(ag.color);
        if (ag.kind == 0) {
            dl->AddRectFilled(ImVec2(m.x - px(6), m.y - px(6)), ImVec2(m.x + px(6), m.y + px(6)),
                              c(alpha(ag.color, 0x30)), px(3));
            dl->AddText(s.fonts.ui_bold, fpx(10), ImVec2(m.x - px(1.5f), m.y - px(6)), mc, "!");
        } else if (ag.kind == 1) {
            dl->PathArcTo(m, px(4.5f), 0.0f, 4.4f, 16);
            dl->PathStroke(mc, 0, px(1.6f));
        } else if (ag.kind == 2) {
            dl->AddCircleFilled(m, px(3.5f), mc);
        } else {
            dl->AddCircle(m, px(3.5f), mc, 16, px(1.4f));
        }
        dl->AddText(s.fonts.ui, fs, ImVec2(a.x + px(30), y), c(p.text), ag.name);
        y += px(21);
    }
    // Tabs, then the terminal card.
    const float x0 = a.x + side + px(8);
    const float tab_h = px(22);
    const char* tabs[] = {"api", "web", "docs"};
    const ImU32 tab_marks[] = {p.needs_you, p.working, p.done};
    float tx = x0;
    for (int i = 0; i < 3; ++i) {
        const float tw = std::min(px(58), (b.x - x0 - px(8)) / 3.0f - px(4));
        if (i == 0)
            dl->AddRectFilled(ImVec2(tx, a.y + px(7)), ImVec2(tx + tw, a.y + px(7) + tab_h), c(p.selected),
                              px(5));
        dl->AddText(s.fonts.ui, fs, ImVec2(tx + px(8), a.y + px(11)), c(i == 0 ? p.text : p.dim), tabs[i]);
        dl->AddCircleFilled(ImVec2(tx + tw - px(10), a.y + px(7) + tab_h * 0.5f), px(3), c(tab_marks[i]));
        tx += tw + px(4);
    }
    const ImVec2 ta(x0, a.y + px(7) + tab_h + px(8));
    const ImVec2 tb(b.x - px(8), b.y - px(8));
    dl->AddRectFilled(ta, tb, c(p.card), px(6));
    dl->AddRect(ta, tb, c(p.focus), px(6), 0, px(1.5f));
    dl->AddRectFilled(ImVec2(ta.x + px(6), ta.y + px(22)), ImVec2(tb.x - px(6), tb.y - px(6)), c(p.term_bg),
                      px(3));
    dl->AddText(s.fonts.ui, fs, ImVec2(ta.x + px(10), ta.y + px(5)), c(p.dim),
                ui::path_text::local_separator() == '\\' ? "api  D:\\src\\project" : "api  ~/src/project");
    ImFont* mono = s.fonts.mono != nullptr ? s.fonts.mono : s.fonts.ui;
    const float ms = fpx(11);
    float ly = ta.y + px(30);
    dl->AddText(mono, ms, ImVec2(ta.x + px(12), ly), c(p.term_fg), "$ ls");
    ly += px(16);
    const char* words[] = {"red", "green", "yellow", "blue", "magenta", "cyan"};
    float wx = ta.x + px(12);
    for (int i = 0; i < 6; ++i) {
        const ImU32 wc = c(p.ansi != nullptr ? (*p.ansi)[static_cast<std::size_t>(i + 1)] : p.term_fg);
        dl->AddText(mono, ms, ImVec2(wx, ly), wc, words[i]);
        wx += mono->CalcTextSizeA(ms, FLT_MAX, 0, words[i]).x + px(7);
        if (wx > tb.x - px(60)) {
            wx = ta.x + px(12);
            ly += px(16);
        }
    }
    ly += px(16);
    const float sel_w = mono->CalcTextSizeA(ms, FLT_MAX, 0, "selected").x;
    dl->AddRectFilled(ImVec2(ta.x + px(12), ly), ImVec2(ta.x + px(12) + sel_w, ly + ms * 1.25f),
                      c(p.selection));
    dl->AddText(mono, ms, ImVec2(ta.x + px(12), ly), c(p.term_fg), "selected");
    dl->AddRectFilled(ImVec2(ta.x + px(14) + sel_w, ly),
                      ImVec2(ta.x + px(14) + sel_w + px(7), ly + ms * 1.25f), c(p.cursor));
    dl->PopClipRect();
}

// The checks under the preview: every pair below its bar, as in the palette check.
void draw_checks(App::State& s, float w) {
    const auto problems = col::palette_problems(th::check_colors(g_ed.derived));
    const float fs = fpx(12);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto line = [&](ImU32 c, const std::string& text) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        draw_icon(s, dl, ui::icon::kWarning, fpx(12), ImVec2(p.x + px(7), p.y + line_h(12) * 0.5f), c);
        text_clipped(dl, s.fonts.ui, fs, ImVec2(p.x + px(20), p.y), g_pal->dim, text, w - px(20));
        ImGui::Dummy(ImVec2(w, line_h(12) + px(3)));
    };
    if (problems.empty()) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        draw_icon(s, dl, ui::icon::kCheck, fpx(12), ImVec2(p.x + px(7), p.y + line_h(12) * 0.5f),
                  g_pal->done);
        dl->AddText(s.fonts.ui, fs, ImVec2(p.x + px(20), p.y), g_pal->dim,
                    "All text and marks are easy to read.");
        ImGui::Dummy(ImVec2(w, line_h(12)));
    }
    for (const auto& x : problems) {
        char num[16];
        std::snprintf(num, sizeof num, "%.2f", x.value);
        if (x.kind == col::CheckKind::kApart) {
            const auto pos = x.what.find(" (");
            std::string pair = x.what.substr(0, pos);
            std::replace(pair.begin(), pair.end(), '/', ' ');
            const auto sp = pair.find(' ');
            std::string vision = x.what.substr(pos + 2);
            vision.pop_back();
            char de[16];
            std::snprintf(de, sizeof de, "%.1f", x.value);
            line(g_pal->info, pair.substr(0, sp) + " and " + pair.substr(sp + 1) +
                                  " are hard to tell apart (" + vision +
                                  ", \xce\x94"
                                  "E " +
                                  de + ")");
        } else {
            char th_[8];
            std::snprintf(th_, sizeof th_, "%.1f", x.threshold);
            line(x.kind == col::CheckKind::kText && x.value < 3.0 ? g_pal->needs_you : g_pal->working,
                 x.what + " " + num + " (< " + th_ + ")");
        }
    }
    if (g_ed.derived.light != g_ed.start_light)
        line(g_pal->info, std::string("This theme is now ") + (g_ed.derived.light ? "light" : "dark") +
                              ": it moves to the " + (g_ed.derived.light ? "Light" : "Dark") + " cards.");
}

// A plain button (raised, 1 px border) or the accent one, `w` wide (0: fit the label).
bool editor_button(App::State& s, const char* label, bool primary, bool enabled = true, float w = 0) {
    const ImVec2 sz(w > 0 ? w : settings_button_size(s, label).x + px(8), settings_button_size(s, label).y);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(label, sz);
    ImGui::EndDisabled();
    const bool hov = enabled && ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(a.x + sz.x, a.y + sz.y);
    if (primary) {
        dl->AddRectFilled(
            a, b, enabled ? (hov ? alpha(g_pal->focus, 230) : g_pal->focus) : alpha(g_pal->focus, 90), px(7));
        text_centered(dl, s.fonts.ui_bold, fpx(13), a, sz, accent_foreground(), label);
    } else {
        dl->AddRectFilled(a, b, hov ? g_pal->hover : raised_color(), px(7));
        dl->AddRect(a, b, g_pal->border, px(7));
        text_centered(dl, s.fonts.ui, fpx(13), a, sz, enabled ? g_pal->text : g_pal->faint, label);
    }
    return clicked && enabled;
}

// The question bar above the buttons when Save needs an answer first.
void draw_confirm_bar(App::State& s) {
    const char* text = nullptr;
    const char* yes = nullptr;
    switch (g_ed.confirm) {
        case Confirm::kOverwrite:
            text = "A theme with this name is already installed. Replace it with this one?";
            yes = "Replace";
            break;
        case Confirm::kExternal:
            text = "The file changed outside AgentDesk since you opened it. Overwrite it, or load it again?";
            yes = "Overwrite";
            break;
        case Confirm::kLowContrast:
            text = "Terminal text is hard to read on this background (under 3:1). Save anyway?";
            yes = "Save anyway";
            break;
        case Confirm::kDiscard:
            text = "Close without saving your changes?";
            yes = "Discard";
            break;
        case Confirm::kNone:
            return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = settings_button_size(s, "Ok").y + px(12);
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), alpha(g_pal->working, 0x22), px(7));
    draw_icon(s, dl, ui::icon::kWarning, fpx(13), ImVec2(a.x + px(14), a.y + h * 0.5f), g_pal->working);
    const float bw_yes = settings_button_size(s, yes).x + px(8);
    const float bw_no = settings_button_size(s, "Cancel").x + px(8);
    const float bw_reload =
        g_ed.confirm == Confirm::kExternal ? settings_button_size(s, "Load again").x + px(8) : 0;
    text_clipped(dl, s.fonts.ui, fpx(12.5f), ImVec2(a.x + px(28), a.y + (h - fpx(12.5f) * 1.2f) * 0.5f),
                 g_pal->text, text, w - px(40) - bw_yes - bw_no - bw_reload - px(16));
    float x = a.x + w - px(6) - bw_yes - px(6) - bw_no - (bw_reload > 0 ? bw_reload + px(6) : 0);
    ImGui::SetCursorScreenPos(ImVec2(x, a.y + px(6)));
    if (editor_button(s, "Cancel##confirm", false, true, bw_no))
        g_ed.confirm = Confirm::kNone;
    x += bw_no + px(6);
    if (bw_reload > 0) {
        ImGui::SetCursorScreenPos(ImVec2(x, a.y + px(6)));
        if (editor_button(s, "Load again", false, true, bw_reload)) {
            if (const auto f = th::parse(read_text_file(user_theme_file(g_ed.original)))) {
                g_ed.file = *f;
                g_ed.saved = *f;
                g_ed.dirty = false;
                std::error_code ec;
                g_ed.mtime = std::filesystem::last_write_time(user_theme_file(g_ed.original), ec);
                rebuild();
            }
            g_ed.confirm = Confirm::kNone;
        }
        x += bw_reload + px(6);
    }
    ImGui::SetCursorScreenPos(ImVec2(x, a.y + px(6)));
    if (editor_button(s, yes, true, true, bw_yes)) {
        const Confirm c = g_ed.confirm;
        g_ed.confirm = Confirm::kNone;
        if (c == Confirm::kDiscard)
            close_editor(s);
        else
            try_save(s, c);
    }
    ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + h + px(8)));
    ImGui::Dummy(ImVec2(0, 0));
}

// The colour-vision choice under the sample: like segmented(), sized to fit `width`.
void simulate_choice(App::State& s, float width) {
    static constexpr std::pair<col::Cvd, const char*> kChoices[] = {{col::Cvd::kNone, "None"},
                                                                    {col::Cvd::kProtan, "Protan"},
                                                                    {col::Cvd::kDeutan, "Deutan"},
                                                                    {col::Cvd::kTritan, "Tritan"}};
    const float bw = std::min(px(78), (width - px(4)) / 4.0f), bh = px(26);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + bw * 4 + px(4), a.y + bh + px(4)), raised_color(), px(7));
    float x = a.x + px(2);
    for (const auto& [kind, label] : kChoices) {
        ImGui::SetCursorScreenPos(ImVec2(x, a.y + px(2)));
        if (ImGui::InvisibleButton(label, ImVec2(bw, bh)))
            g_ed.simulate = kind;
        const bool on = g_ed.simulate == kind;
        if (on || ImGui::IsItemHovered())
            dl->AddRectFilled(ImVec2(x, a.y + px(2)), ImVec2(x + bw, a.y + px(2) + bh),
                              on ? g_pal->selected : g_pal->hover, px(6));
        text_centered(dl, s.fonts.ui, fpx(12), ImVec2(x, a.y + px(2)), ImVec2(bw, bh),
                      on ? g_pal->text : g_pal->dim, label);
        x += bw;
    }
    ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + bh + px(10)));
    ImGui::Dummy(ImVec2(0, 0));
}

// When the theme's file can't be read: why, and a way to open it.
void draw_read_error(App::State& s) {
    const std::string msg = "This file can't be read: " + g_ed.read_error + ".";
    settings_wrapped(s, msg.c_str(), g_pal->needs_you, 13.0f, 19.0f);
    ImGui::Dummy(ImVec2(0, px(10)));
    if (!g_ed.read_error_file.empty()) {
        if (editor_button(s, "Open in a text editor", false))
            os::open_text_file(g_ed.read_error_file);
        ImGui::SameLine(0, px(8));
    }
    if (editor_button(s, "Close", true) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        close_editor(s);
}
}  // namespace

const Palette* theme_editor_draft(const App::State& /*s*/, bool* changed) {
    *changed = std::exchange(g_ed.changed, false);
    return g_ed.open && g_ed.read_error.empty() ? &g_ed.pal : nullptr;
}

bool theme_editor_open(const App::State& /*s*/) {
    return g_ed.open;
}

void edit_theme(App::State& s, const std::string& id, bool in_place, bool from_settings) {
    const Palette* p = palette_by_id(id);
    open_editor(s, *p, in_place, from_settings);
}

// theme.new[:<id>] starts a copy (of the shown theme by default); theme.edit[:<id>] edits the
// person's own theme in place or a copy of any other; theme.delete:<id> sets the person's theme
// file aside; theme.folder opens <data>\themes.
std::optional<std::string> exec_theme_editor_command(App::State& s, const std::string& name,
                                                     const std::string& arg) {
    if (name == "theme.new" || name == "theme.edit") {
        const std::string id = arg.empty() ? std::string(g_pal == &g_ed.pal ? "midnight" : g_pal->id) : arg;
        edit_theme(s, id, name == "theme.edit", false);
        return std::string();
    }
    if (name == "theme.folder") {
        const auto dir = data_dir() / L"themes";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        os::open_path(dir);
        return std::string();
    }
    if (name == "theme.delete") {
        if (!is_user_theme(arg))
            return "only your own themes (in the themes folder) can be deleted";
        const auto file = user_theme_file(arg);
        // Set aside, not deleted: <name>.json.old-<time>, as layout.reset does.
        const auto aside = std::filesystem::path(file).concat(L".old-" + std::to_wstring(std::time(nullptr)));
        std::error_code ec;
        std::filesystem::rename(file, aside, ec);
        if (ec)
            return ec.message();
        forget_theme(arg);
        // Wherever it was in use, the defaults take over.
        auto& st = s.settings;
        if (st.theme_dark == arg)
            st.theme_dark = "midnight";
        if (st.theme_light == arg)
            st.theme_light = "paper";
        for (auto& [layout, pair] : st.layout_themes) {
            if (pair.first == arg)
                pair.first = st.theme_dark;
            if (pair.second == arg)
                pair.second = st.theme_light;
        }
        save_settings(st);
        s.toast("Theme deleted", data_file_text("themes", arg + ".json") + " was set aside.", "", g_pal->info,
                4);
        return std::string();
    }
    return std::nullopt;
}

void draw_theme_editor(App::State& s) {
    if (g_ed.want_open) {
        ImGui::OpenPopup("##theme-editor");
        g_ed.want_open = false;
    }
    const ImVec2 size = dialog_size(px(880), px(620));
    center_next_window(size);
    push_overlay_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(20), px(16)));
    const bool open = ImGui::BeginPopupModal("##theme-editor", nullptr,
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    pop_overlay_style();
    if (!open) {
        if (g_ed.open) {  // closed some other way (another dialog took its place)
            g_ed.open = false;
            g_ed.changed = true;
        }
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float inner_w = size.x - px(40);
    dl->AddText(s.fonts.ui_bold, fpx(15), ImVec2(wp.x + px(20), wp.y + px(16)), g_pal->text, "Theme editor");
    ImGui::SetCursorScreenPos(ImVec2(wp.x + px(20), wp.y + px(16) + line_h(15) + px(10)));
    if (!g_ed.read_error.empty()) {
        draw_read_error(s);
        ImGui::EndPopup();
        return;
    }
    // Name, and what it started from.
    const std::string problem = th::name_problem(g_ed.name, builtin_ids());
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(v4(g_pal->dim), "Name");
    ImGui::SameLine(0, px(8));
    if (!problem.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Border, v4(g_pal->needs_you));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    }
    ImGui::SetNextItemWidth(px(240));
    ImGui::InputText("##theme-name", g_ed.name, sizeof g_ed.name);
    if (!problem.empty()) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::SameLine(0, px(16));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(v4(g_pal->faint), "Based on: %s", g_ed.based_on.c_str());
    if (!problem.empty()) {
        ImGui::SameLine(0, px(16));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(v4(g_pal->needs_you), "%s", problem.c_str());
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    // Body: the colours on the left, the sample and the checks on the right.
    const float footer_h = settings_button_size(s, "Save").y + px(24) +
                           (g_ed.confirm != Confirm::kNone ? settings_button_size(s, "Ok").y + px(20) : 0.0f);
    const float body_top = ImGui::GetCursorScreenPos().y;
    const float body_h = wp.y + size.y - px(16) - footer_h - body_top;
    const float left_w = std::round(inner_w * 0.5f);
    const ImVec2 body_a(wp.x + px(20), body_top);
    dl->AddLine(ImVec2(body_a.x, body_top - px(2)), ImVec2(body_a.x + inner_w, body_top - px(2)),
                g_pal->border);
    ImGui::SetCursorScreenPos(body_a);
    ImGui::BeginChild("##theme-rows", ImVec2(left_w, body_h), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    draw_rows(s);
    ImGui::EndChild();
    const float right_x = body_a.x + left_w + px(20);
    const float right_w = inner_w - left_w - px(20);
    dl->AddLine(ImVec2(right_x - px(10), body_top), ImVec2(right_x - px(10), body_top + body_h),
                g_pal->border);
    ImGui::SetCursorScreenPos(ImVec2(right_x, body_top + px(6)));
    ImGui::BeginChild("##theme-side", ImVec2(right_w, body_h - px(6)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    {
        const ImVec2 pa = ImGui::GetCursorScreenPos();
        const float ph = std::clamp(body_h - px(150), px(150), px(236));  // leaves room for the checks
        draw_preview(s, pa, ImVec2(right_w - px(4), ph));
        ImGui::Dummy(ImVec2(right_w - px(4), ph + px(8)));
        ImGui::TextColored(v4(g_pal->dim), "Simulate colour vision (the sample only)");
        simulate_choice(s, right_w - px(8));
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText(s.fonts.ui_bold, fpx(11.5f), cp, g_pal->faint, "CHECKS");
        ImGui::Dummy(ImVec2(0, line_h(11.5f) + px(4)));
        draw_checks(s, right_w - px(8));
    }
    ImGui::EndChild();
    // Footer.
    ImGui::SetCursorScreenPos(ImVec2(wp.x + px(20), wp.y + size.y - px(16) - footer_h + px(10)));
    dl->AddLine(ImVec2(wp.x + px(20), ImGui::GetCursorScreenPos().y - px(4)),
                ImVec2(wp.x + px(20) + inner_w, ImGui::GetCursorScreenPos().y - px(4)), g_pal->border);
    ImGui::Dummy(ImVec2(0, px(4)));
    draw_confirm_bar(s);
    const float row_y = ImGui::GetCursorScreenPos().y;
    if (editor_button(s, "Revert", false, g_ed.dirty)) {
        g_ed.file = g_ed.saved;
        g_ed.dirty = false;
        rebuild();
    }
    if (!g_ed.save_error.empty() || !g_ed.note.empty()) {
        ImGui::SameLine(0, px(12));
        ImGui::AlignTextToFramePadding();
        if (!g_ed.save_error.empty())
            ImGui::TextColored(v4(g_pal->needs_you), "%s", g_ed.save_error.c_str());
        else
            ImGui::TextColored(v4(g_pal->dim), "%s", g_ed.note.c_str());
    }
    const float w_cancel = settings_button_size(s, "Close").x + px(8);
    const float w_save = settings_button_size(s, "Save").x + px(24);
    float x = wp.x + px(20) + inner_w - w_save - px(8) - w_cancel;
    ImGui::SetCursorScreenPos(ImVec2(x, row_y));
    bool cancel = editor_button(s, g_ed.dirty ? "Cancel" : "Close", false, true, w_cancel);
    ImGui::SetCursorScreenPos(ImVec2(x + w_cancel + px(8), row_y));
    const bool can_save = problem.empty();
    bool save = editor_button(s, "Save", true, can_save, w_save);
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && can_save)
        save = true;
    // Esc closes (asking first when there are changes), unless a colour picker or a field has it.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive() &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        cancel = true;
    if (save)
        try_save(s);
    if (cancel) {
        if (g_ed.dirty)
            g_ed.confirm = Confirm::kDiscard;
        else
            close_editor(s);
    }
    ImGui::EndPopup();
}

}  // namespace detail
}  // namespace agentdesk::desktop
