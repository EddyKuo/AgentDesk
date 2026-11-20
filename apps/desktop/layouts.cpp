// Window layouts: the layout files (layouts\*.json), their saved state, and the example mods
// copied in on first run.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "app_internal.hpp"
#include "overlay_kit.hpp"

namespace agentdesk::desktop {
namespace detail {

std::filesystem::path layouts_dir() {
    return data_dir() / L"layouts";
}

std::filesystem::path layout_state_path(const std::string& name) {
    return layouts_dir() / L".state" / win::to_wide(name + ".json");
}

namespace {
bool palette_exists(const std::string& id) {
    const auto all = all_palettes();
    return std::any_of(all.begin(), all.end(), [&](const Palette* p) { return id == p->id; });
}

}  // namespace

// The tree says how it looks and which of AgentDesk's renderers draw it.
void derive_layout(LayoutSpec& l) {
    const lay::Node& t = l.tree;
    std::string theme;
    if (t.look.contains("theme")) {
        const Json& th = t.look["theme"];
        if (th.is_string())
            theme = th.get<std::string>();
        else if (th.is_object())  // {"dark": ..., "light": ...}: Windows' app mode picks
            theme = safe::string_or(th, system_light() ? "light" : "dark");
    }
    l.look_applies = theme.empty() || palette_exists(theme);
    l.theme = palette_exists(theme) ? theme : std::string();
    if (const auto st = t.look.find("suggested_themes"); st != t.look.end() && st->is_object())
        l.suggested = std::pair{st->value("dark", std::string()), st->value("light", std::string())};
    else
        l.suggested = ui::theme::builtin_suggestion(l.name);
    l.style.clear();
    // "", "terminals", "mac", "focus" (Focus mode's tree), "mission" or "review".
    for (const lay::Node* n : t.walk()) {
        const std::string m = n->mod();
        if (m == "mac-menubar" || m == "mac-dock" || m == "mac-window-title")  // wins over the rest
            l.style = "mac";
        else if (m == "terminals-bar" && l.style.empty())
            l.style = "terminals";
        else if (m == "focus-strip" && l.style.empty())
            l.style = "focus";
        else if ((m == "mission-bar" || m == "mission-wall") && l.style.empty())
            l.style = "mission";
        else if (m.rfind("review-", 0) == 0 && l.style.empty())
            l.style = "review";
        if (n->is_block() && n->content.type == lay::Content::Type::kTab && n->content.pane &&
            n->content.pane->header && n->content.pane->header->mod() == "terminals-pane" && l.style.empty())
            l.style = "terminals";
    }
    // Font size: on the path from the root to the first shown panes' block, the deepest node that
    // sets one wins.
    l.font_size.reset();
    for (const lay::Node* n : t.visible_blocks()) {
        if (n->content.type != lay::Content::Type::kTab)
            continue;
        if (const auto path = t.path_to(n->id))
            for (const auto& id : *path)
                if (const auto f = t.find(id)->font_size())
                    l.font_size = std::clamp(*f, 6.0f, 72.0f);
        break;
    }
    if (!l.font_size)
        l.font_size = t.font_size();
}

namespace {
// Before this engine the workspace's sizes were in ui.json: the first time, they become its
// clicks (layouts\.state\workspace.json).
lay::State seed_from_settings(const Settings& st, const lay::Node& file) {
    lay::Node now = file;
    const Settings d;
    if (lay::Node* n = now.find("sidebar")) {
        if (st.sidebar_width != d.sidebar_width)
            n->size = lay::Span::pixels(std::clamp(st.sidebar_width, 44.0f, 480.0f));
        if (!st.sidebar)
            n->hidden = true;
    }
    if (lay::Node* n = now.find("panel"); n != nullptr && st.panel_width != d.panel_width)
        n->size = lay::Span::pixels(std::clamp(st.panel_width, 280.0f, 900.0f));
    lay::Node* sp = now.find("spaces");
    lay::Node* ag = now.find("agents");
    if (sp != nullptr && ag != nullptr && st.spaces_share != d.spaces_share) {
        sp->size = lay::Span::weight(2.0f * st.spaces_share);
        ag->size = lay::Span::weight(2.0f * (1.0f - st.spaces_share));
    }
    return lay::State::between(file, now);
}

// The tree to draw: the file (or the built-in when the file is broken) with the person's clicks;
// a click on a field the file changed since it was made gives way to the file.
void apply_layout_state(LayoutSpec& l, const Settings& st) {
    l.tree = l.file;
    std::error_code ec;
    const auto path = layout_state_path(l.name);
    lay::State state;
    if (std::filesystem::exists(path, ec)) {
        state = lay::State::from_json(Json::parse(read_text_file(path), nullptr, false));
        if (l.error.empty())
            state.forget_since_saved(l.file);
    } else if (l.name == "workspace") {
        state = seed_from_settings(st, l.file);
    }
    state.apply(l.tree);
    derive_layout(l);
}

}  // namespace

// Writes the clicks, with the file they were made on; no clicks, no state file. Not for a broken
// file (it is not what shows).
void save_layout_state(const LayoutSpec& l) {
    if (!l.error.empty())
        return;
    lay::State state = lay::State::between(l.file, l.tree);
    std::error_code ec;
    const auto path = layout_state_path(l.name);
    if (state.empty()) {
        std::filesystem::remove(path, ec);
        return;
    }
    state.file = lay::to_json(l.file);
    Json j = state.to_json();
    lay::normalize(j);
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto tmp = std::filesystem::path(path).concat(L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << j.dump(2) << "\n";
    }
    ::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

namespace {
// Gives every node without an id one ("node-<n>", not used by another node), in memory only, so
// clicks on it can be kept.
void fill_missing_ids(lay::Node& root) {
    int k = 0;
    std::set<std::string> taken;
    for (const lay::Node* n : root.walk())
        taken.insert(n->id);
    root.fill_ids([&] {
        std::string id;
        do
            id = "node-" + std::to_string(++k);
        while (taken.count(id) != 0);
        return id;
    });
}

// A layout file: the current format, or an old AgentDesk one (read as the built-in of its style).
// A broken file keeps its error and shows the built-in of its name (the workspace when it names
// none).
LayoutSpec read_layout_file(const std::filesystem::path& f) {
    LayoutSpec l;
    const auto stem = f.stem().u8string();
    l.name.assign(stem.begin(), stem.end());
    l.from_file = true;
    std::error_code ec;
    l.mtime = std::filesystem::last_write_time(f, ec);
    const Json j = Json::parse(read_text_file(f), nullptr, false);
    if (lay::is_legacy(j)) {
        l.legacy = true;
        l.name = safe::string_or(j, "name", l.name);
        l.file = lay::from_legacy(j);
        if (j.contains("details") && j["details"].is_boolean())
            l.details = j["details"].get<bool>();
    } else if (auto n = j.is_discarded() ? std::nullopt : lay::parse(j, &l.error)) {
        l.file = std::move(*n);
    } else {
        if (j.is_discarded())
            l.error = "not valid JSON";
        l.file = lay::builtin(l.name);
    }
    fill_missing_ids(l.file);
    return l;
}

}  // namespace

// The built-ins, then <data>\layouts\*.json by file name; a file replaces the built-in it names.
// A saved "layout" that names none of them shows the first layout (State::layout()).
std::vector<LayoutSpec> load_layouts(const Settings& st) {
    std::vector<LayoutSpec> list;
    for (const auto& name : lay::builtin_names()) {
        LayoutSpec l;
        l.name = name;
        l.builtin = true;
        l.file = lay::builtin(name);
        list.push_back(std::move(l));
    }
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(layouts_dir(), ec))
        if (e.is_regular_file() && e.path().extension() == L".json")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        LayoutSpec l = read_layout_file(f);
        const auto it = std::find_if(list.begin(), list.end(), [&](const LayoutSpec& x) { return x.name == l.name; });
        if (it != list.end()) {
            l.builtin = it->builtin;
            *it = std::move(l);
        } else {
            list.push_back(std::move(l));
        }
    }
    for (auto& l : list)
        apply_layout_state(l, st);
    return list;
}

std::string toggle_layout(App::State& s, const std::string& style) {
    // Focus replaces whatever layout shows, so a toggle pressed in Focus leaves it and shows
    // `style` rather than going back from a layout the person could not see.
    const bool was_focus = s.settings.focus_on;
    set_focus(s, false);
    if (s.layout().style != style) {
        // The built-in of that name when it has the style, else the first layout that does (a
        // person's own copy counts).
        auto it = std::find_if(s.layouts.begin(), s.layouts.end(),
                               [&](const LayoutSpec& l) { return l.name == style && l.style == style; });
        if (it == s.layouts.end())
            it = std::find_if(s.layouts.begin(), s.layouts.end(),
                              [&](const LayoutSpec& l) { return l.style == style; });
        if (it == s.layouts.end())
            return "no " + layout_display_name(style) + " layout";
        s.settings.layout_return = s.settings.layout;
        s.settings.layout = it->name;
    } else if (!was_focus) {
        const std::string& back = s.settings.layout_return;
        const auto it = std::find_if(s.layouts.begin(), s.layouts.end(),
                                     [&](const LayoutSpec& l) { return l.name == back && l.style != style; });
        s.settings.layout = it != s.layouts.end() ? back : std::string("workspace");
    }
    save_settings(s.settings);
    s.focus_view(s.ws.focused_session());
    return {};
}

// ------------------------------------------------------------------ example mods (first run)
// The example mods (C++ DLLs built from mods\ in the source tree) ship next to agent-ui.exe in
// mods\<name>\. Each is copied into <data>\mods when it is not there yet, and into a folder that
// still holds the old Lua example of the same name (its settings.json is kept). A folder of the
// person's own is left alone. They stay off until enabled.
void write_example_mods() {
    const auto shipped = exe_dir() / L"mods";
    const auto dir = data_dir() / L"mods";
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(shipped, ec)) {
        if (!e.is_directory() || !std::filesystem::is_regular_file(e.path() / L"mod.dll", ec))
            continue;
        const auto to = dir / e.path().filename();
        if (std::filesystem::is_regular_file(to / L"mod.dll", ec))
            continue;  // already there (maybe the person's own build): leave it
        if (std::filesystem::exists(to, ec) && !std::filesystem::is_regular_file(to / L"main.lua", ec))
            continue;  // a folder of the person's own with that name
        std::filesystem::create_directories(to, ec);
        for (const wchar_t* f : {L"mod.dll", L"mod.json"})
            std::filesystem::copy_file(e.path() / f, to / f, std::filesystem::copy_options::overwrite_existing, ec);
        if (!std::filesystem::exists(to / L"settings.json", ec) &&
            std::filesystem::is_regular_file(e.path() / L"settings.json", ec))
            std::filesystem::copy_file(e.path() / L"settings.json", to / L"settings.json", ec);
        std::filesystem::remove(to / L"main.lua", ec);  // the Lua version it replaces
    }
}
}  // namespace detail

}  // namespace agentdesk::desktop
