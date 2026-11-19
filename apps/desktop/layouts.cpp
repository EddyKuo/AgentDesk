// Window layouts: the layout files (layouts\*.json), their saved state, and the example mods
// written on first run.
#include "app_internal.hpp"

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
        else if (th.is_object())  // layout.rs 197-210: the system's appearance picks
            theme = safe::string_or(th, system_light() ? "light" : "dark");
    }
    l.look_applies = theme.empty() || palette_exists(theme);
    l.theme = palette_exists(theme) ? theme : std::string();
    l.style.clear();
    for (const lay::Node* n : t.walk()) {
        const std::string m = n->mod();
        if (m == "win98")
            l.style = "win98";
        else if (m == "terminals-bar" && l.style.empty())
            l.style = "terminals";
        if (n->is_block() && n->content.type == lay::Content::Type::kTab && n->content.pane &&
            n->content.pane->header && n->content.pane->header->mod() == "terminals-pane" && l.style.empty())
            l.style = "terminals";
    }
    // layout.rs 150-151: the nearest node setting it wins; the panes' block decides.
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
// clicks (what herdr keeps in layouts/.state).
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

// herdr window.rs 997-1043 load_layout: the file (or its shipped default when it is broken),
// with the clicks; a click on a field the file changed since gives way.
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

// herdr window.rs 1199-1220 write_state: the clicks, with the file they were made on; none, no
// file. Not for a broken file (it is not what shows).
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
        LayoutSpec l;
        const auto stem = f.stem().u8string();
        l.name.assign(stem.begin(), stem.end());
        l.from_file = true;
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
            l.file = lay::builtin(l.name);  // window.rs 180-182: as shipped, main when not one
        }
        // window.rs 1222-1243 keep_ids: nodes without an id get one (here only in memory).
        int k = 0;
        std::set<std::string> taken;
        for (const lay::Node* n : l.file.walk())
            taken.insert(n->id);
        l.file.fill_ids([&] {
            std::string id;
            do
                id = "node-" + std::to_string(++k);
            while (taken.count(id) != 0);
            return id;
        });
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

// ------------------------------------------------------------------ example mods (first run)
// Written to <data>\mods when that folder does not exist yet; ours, small, and meant to be read.
// The example mods (C++ DLLs built from mods\ in the source tree) ship next to agent-ui.exe in
// mods\<name>\. Copied into <data>\mods on first run, and into folders that still hold the old
// Lua example of the same name (its settings.json is kept). They stay off until enabled.
void write_example_mods() {
    wchar_t exe[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto shipped = std::filesystem::path(exe).parent_path() / L"mods";
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
