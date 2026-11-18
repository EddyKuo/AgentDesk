#include "agentdesk/ui/workspace.hpp"

#include <utility>

#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>

namespace agentdesk::ui {

namespace {

using Json = nlohmann::json;

bool contains(const Node& n, const std::string& s) {
    if (n.leaf())
        return n.session == s;
    return contains(n.children[0], s) || contains(n.children[1], s);
}

void collect(const Node& n, std::vector<std::string>& out) {
    if (n.leaf()) {
        if (!n.session.empty())
            out.push_back(n.session);
        return;
    }
    collect(n.children[0], out);
    collect(n.children[1], out);
}

Node* find_leaf(Node& n, const std::string& s) {
    if (n.leaf())
        return n.session == s ? &n : nullptr;
    if (auto* f = find_leaf(n.children[0], s))
        return f;
    return find_leaf(n.children[1], s);
}

// Removes leaf `s`; the sibling takes the parent's place. Returns true when removed.
bool remove_leaf(Node& n, const std::string& s) {
    if (n.leaf())
        return false;
    for (int i = 0; i < 2; ++i) {
        Node& c = n.children[static_cast<std::size_t>(i)];
        if (c.leaf() && c.session == s) {
            Node keep = std::move(n.children[static_cast<std::size_t>(1 - i)]);
            n = std::move(keep);
            return true;
        }
    }
    return remove_leaf(n.children[0], s) || remove_leaf(n.children[1], s);
}

void layout_into(const Node& n, Rect r, float gap, std::map<std::string, Rect>& out) {
    if (n.leaf()) {
        if (!n.session.empty())
            out[n.session] = r;
        return;
    }
    Rect a = r;
    Rect b = r;
    if (n.vertical) {
        const float h = std::max(0.0f, r.h - gap);
        a.h = std::round(h * n.ratio);
        b.y = r.y + a.h + gap;
        b.h = h - a.h;
    } else {
        const float w = std::max(0.0f, r.w - gap);
        a.w = std::round(w * n.ratio);
        b.x = r.x + a.w + gap;
        b.w = w - a.w;
    }
    layout_into(n.children[0], a, gap, out);
    layout_into(n.children[1], b, gap, out);
}

void dividers_into(const Node& n, Rect r, float gap, std::vector<int>& path, std::vector<Divider>& out) {
    if (n.leaf())
        return;
    Rect a = r;
    Rect b = r;
    Divider d;
    d.path = path;
    d.vertical = n.vertical;
    d.parent = r;
    if (n.vertical) {
        const float h = std::max(0.0f, r.h - gap);
        a.h = std::round(h * n.ratio);
        b.y = r.y + a.h + gap;
        b.h = h - a.h;
        d.bar = {r.x, r.y + a.h, r.w, gap};
    } else {
        const float w = std::max(0.0f, r.w - gap);
        a.w = std::round(w * n.ratio);
        b.x = r.x + a.w + gap;
        b.w = w - a.w;
        d.bar = {r.x + a.w, r.y, gap, r.h};
    }
    out.push_back(d);
    path.push_back(0);
    dividers_into(n.children[0], a, gap, path, out);
    path.back() = 1;
    dividers_into(n.children[1], b, gap, path, out);
    path.pop_back();
}

// Neighbour of `from` in `dir`: the candidate beyond that edge with overlap on the other axis,
// nearest edge first, then the largest overlap.
std::optional<std::string> neighbour(const std::map<std::string, Rect>& rects, const std::string& from,
                                     Dir dir) {
    const auto it = rects.find(from);
    if (it == rects.end())
        return std::nullopt;
    const Rect a = it->second;
    std::optional<std::string> best;
    float best_dist = 1e9f;
    float best_overlap = -1.0f;
    for (const auto& [id, b] : rects) {
        if (id == from)
            continue;
        float dist = 0;
        float overlap = 0;
        switch (dir) {
            case Dir::kLeft:
                dist = a.x - (b.x + b.w);
                overlap = std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y);
                break;
            case Dir::kRight:
                dist = b.x - (a.x + a.w);
                overlap = std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y);
                break;
            case Dir::kUp:
                dist = a.y - (b.y + b.h);
                overlap = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x);
                break;
            case Dir::kDown:
                dist = b.y - (a.y + a.h);
                overlap = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x);
                break;
        }
        if (dist < -0.5f || overlap <= 0)
            continue;
        if (dist < best_dist - 0.5f || (std::abs(dist - best_dist) <= 0.5f && overlap > best_overlap)) {
            best = id;
            best_dist = dist;
            best_overlap = overlap;
        }
    }
    return best;
}

Json node_to_json(const Node& n) {
    if (n.leaf())
        return {{"session", n.session}};
    return {{"split", n.vertical ? "down" : "right"},
            {"ratio", n.ratio},
            {"a", node_to_json(n.children[0])},
            {"b", node_to_json(n.children[1])}};
}

Node node_from_json(const Json& j, int depth = 0) {
    Node n;
    if (!j.is_object() || depth > 32)
        return n;
    if (j.contains("split") && j.contains("a") && j.contains("b")) {
        n.vertical = j.value("split", "right") == "down";
        n.ratio = std::clamp(j.value("ratio", 0.5f), 0.05f, 0.95f);
        n.children.push_back(node_from_json(j["a"], depth + 1));
        n.children.push_back(node_from_json(j["b"], depth + 1));
        return n;
    }
    n.session = j.value("session", "");
    return n;
}

std::string folder_of(const std::string& cwd) {
    std::string s = cwd;
    char home[MAX_PATH]{};
    const DWORD n = ::GetEnvironmentVariableA("USERPROFILE", home, MAX_PATH);
    if (n > 0 && n < MAX_PATH && _stricmp(home, s.c_str()) == 0)
        return "~";
    while (!s.empty() && (s.back() == '\\' || s.back() == '/'))
        s.pop_back();
    const auto p = s.find_last_of("\\/");
    return p == std::string::npos ? s : s.substr(p + 1);
}

}  // namespace

// ------------------------------------------------------------------ queries
PaneLoc Workspace::find(const std::string& session) const {
    for (int s = 0; s < static_cast<int>(spaces.size()); ++s) {
        const auto& tabs = spaces[static_cast<std::size_t>(s)].tabs;
        for (int t = 0; t < static_cast<int>(tabs.size()); ++t) {
            if (contains(tabs[static_cast<std::size_t>(t)].root, session))
                return {s, t};
        }
    }
    return {};
}

std::vector<std::string> Workspace::sessions_in(const Tab& tab) const {
    std::vector<std::string> out;
    collect(tab.root, out);
    return out;
}

std::vector<std::string> Workspace::all_sessions() const {
    std::vector<std::string> out;
    for (const auto& sp : spaces)
        for (const auto& t : sp.tabs)
            collect(t.root, out);
    return out;
}

Space* Workspace::active() {
    if (spaces.empty())
        return nullptr;
    active_space = std::clamp(active_space, 0, static_cast<int>(spaces.size()) - 1);
    return &spaces[static_cast<std::size_t>(active_space)];
}

Tab* Workspace::active_tab() {
    Space* sp = active();
    if (sp == nullptr || sp->tabs.empty())
        return nullptr;
    sp->active_tab = std::clamp(sp->active_tab, 0, static_cast<int>(sp->tabs.size()) - 1);
    return &sp->tabs[static_cast<std::size_t>(sp->active_tab)];
}

std::string Workspace::focused_session() const {
    auto* self = const_cast<Workspace*>(this);
    const Tab* t = self->active_tab();
    return t ? t->focused : std::string();
}

std::string Workspace::new_id(const char* prefix) {
    // Ids only need to be unique inside this document; keep counting past loaded ones.
    for (;;) {
        std::string id = std::string(prefix) + std::to_string(next_id_++);
        bool used = false;
        for (const auto& sp : spaces) {
            used = used || sp.id == id;
            for (const auto& t : sp.tabs)
                used = used || t.id == id;
        }
        if (!used)
            return id;
    }
}

// ------------------------------------------------------------------ structure
int Workspace::add_space(const std::string& session, const std::string& name) {
    Space sp;
    sp.id = new_id("s");
    sp.name = name;
    Tab t;
    t.id = new_id("t");
    t.root.session = session;
    t.focused = session;
    sp.tabs.push_back(std::move(t));
    spaces.push_back(std::move(sp));
    active_space = static_cast<int>(spaces.size()) - 1;
    return active_space;
}

int Workspace::add_tab(const std::string& session, int space, const std::string& name) {
    if (spaces.empty())
        return add_space(session) >= 0 ? 0 : -1;
    if (space < 0)
        space = std::clamp(active_space, 0, static_cast<int>(spaces.size()) - 1);
    Space& sp = spaces[static_cast<std::size_t>(space)];
    Tab t;
    t.id = new_id("t");
    t.name = name;
    t.root.session = session;
    t.focused = session;
    sp.tabs.push_back(std::move(t));
    sp.active_tab = static_cast<int>(sp.tabs.size()) - 1;
    active_space = space;
    return sp.active_tab;
}

bool Workspace::split(const std::string& target, const std::string& session, Dir dir) {
    const auto loc = find(target);
    if (loc.space < 0)
        return false;
    Tab& tab = spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
    Node* leaf = find_leaf(tab.root, target);
    if (leaf == nullptr)
        return false;
    Node old = *leaf;
    Node fresh;
    fresh.session = session;
    leaf->session.clear();
    leaf->vertical = dir == Dir::kDown || dir == Dir::kUp;
    leaf->ratio = 0.5f;
    leaf->children.clear();
    const bool before = dir == Dir::kLeft || dir == Dir::kUp;
    leaf->children.push_back(before ? fresh : old);
    leaf->children.push_back(before ? old : fresh);
    tab.focused = session;
    tab.zoomed.clear();
    return true;
}

void Workspace::remove(const std::string& session) {
    const auto loc = find(session);
    if (loc.space < 0)
        return;
    Space& sp = spaces[static_cast<std::size_t>(loc.space)];
    Tab& tab = sp.tabs[static_cast<std::size_t>(loc.tab)];
    tab.pane_names.erase(session);
    if (tab.root.leaf()) {
        close_tab(loc.space, loc.tab);
        return;
    }
    remove_leaf(tab.root, session);
    if (tab.zoomed == session)
        tab.zoomed.clear();
    if (tab.focused == session) {
        const auto rest = sessions_in(tab);
        tab.focused = rest.empty() ? std::string() : rest.front();
    }
}

bool Workspace::replace(const std::string& from, const std::string& to) {
    const auto loc = find(from);
    if (loc.space < 0)
        return false;
    Tab& tab = spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
    Node* leaf = find_leaf(tab.root, from);
    if (leaf == nullptr)
        return false;
    leaf->session = to;
    if (tab.focused == from)
        tab.focused = to;
    if (tab.zoomed == from)
        tab.zoomed = to;
    if (const auto it = tab.pane_names.find(from); it != tab.pane_names.end()) {
        tab.pane_names[to] = it->second;
        tab.pane_names.erase(from);
    }
    return true;
}

void Workspace::prune(const std::set<std::string>& present) {
    for (const auto& s : all_sessions()) {
        if (!present.contains(s))
            remove(s);
    }
}

void Workspace::close_tab(int space, int tab) {
    if (space < 0 || space >= static_cast<int>(spaces.size()))
        return;
    Space& sp = spaces[static_cast<std::size_t>(space)];
    if (tab < 0 || tab >= static_cast<int>(sp.tabs.size()))
        return;
    sp.tabs.erase(sp.tabs.begin() + tab);
    if (sp.tabs.empty()) {
        close_space(space);
        return;
    }
    if (sp.active_tab >= tab && sp.active_tab > 0)
        --sp.active_tab;
}

void Workspace::close_space(int space) {
    if (space < 0 || space >= static_cast<int>(spaces.size()))
        return;
    spaces.erase(spaces.begin() + space);
    if (active_space >= space && active_space > 0)
        --active_space;
}

bool Workspace::move_tab(int space, int from, int to) {
    if (space < 0 || space >= static_cast<int>(spaces.size()))
        return false;
    auto& tabs = spaces[static_cast<std::size_t>(space)].tabs;
    const int n = static_cast<int>(tabs.size());
    if (from < 0 || from >= n || n < 2)
        return false;
    to = ((to % n) + n) % n;
    Tab t = std::move(tabs[static_cast<std::size_t>(from)]);
    tabs.erase(tabs.begin() + from);
    tabs.insert(tabs.begin() + to, std::move(t));
    spaces[static_cast<std::size_t>(space)].active_tab = to;
    return true;
}

// ------------------------------------------------------------------ focus
void Workspace::focus(const std::string& session) {
    const auto loc = find(session);
    if (loc.space < 0)
        return;
    active_space = loc.space;
    Space& sp = spaces[static_cast<std::size_t>(loc.space)];
    sp.active_tab = loc.tab;
    Tab& t = sp.tabs[static_cast<std::size_t>(loc.tab)];
    t.focused = session;
    t.settled = false;  // showing a terminal brings its tab back
    if (!t.zoomed.empty() && t.zoomed != session)
        t.zoomed.clear();
}

void Workspace::select_space(int index) {
    if (index >= 0 && index < static_cast<int>(spaces.size()))
        active_space = index;
}

void Workspace::select_tab(int index) {
    Space* sp = active();
    if (sp != nullptr && index >= 0 && index < static_cast<int>(sp->tabs.size())) {
        sp->active_tab = index;
        sp->tabs[static_cast<std::size_t>(index)].settled = false;
    }
}

void Workspace::select_shown_tab(int n) {
    Space* sp = active();
    if (sp == nullptr)
        return;
    for (int i = 0; i < static_cast<int>(sp->tabs.size()); ++i)
        if (!sp->tabs[static_cast<std::size_t>(i)].settled && --n == 0) {
            sp->active_tab = i;
            return;
        }
}

bool Workspace::settle_tab(int space, int tab) {
    if (space < 0 || space >= static_cast<int>(spaces.size()))
        return false;
    Space& sp = spaces[static_cast<std::size_t>(space)];
    if (tab < 0 || tab >= static_cast<int>(sp.tabs.size()) || sp.tabs[static_cast<std::size_t>(tab)].settled)
        return false;
    // The nearest tab still shown, preferring the right (herdr selects the one that slides in).
    int next = -1;
    for (int d = 1; d < static_cast<int>(sp.tabs.size()) && next < 0; ++d)
        for (const int i : {tab + d, tab - d})
            if (i >= 0 && i < static_cast<int>(sp.tabs.size()) && !sp.tabs[static_cast<std::size_t>(i)].settled) {
                next = i;
                break;
            }
    if (next < 0)
        return false;
    sp.tabs[static_cast<std::size_t>(tab)].settled = true;
    if (sp.active_tab == tab)
        sp.active_tab = next;
    return true;
}

void Workspace::unsettle_tab(int space, int tab) {
    if (space >= 0 && space < static_cast<int>(spaces.size()) && tab >= 0 &&
        tab < static_cast<int>(spaces[static_cast<std::size_t>(space)].tabs.size()))
        spaces[static_cast<std::size_t>(space)].tabs[static_cast<std::size_t>(tab)].settled = false;
}

bool Workspace::unsettle_session(const std::string& session) {
    const auto loc = find(session);
    if (loc.space < 0)
        return false;
    Tab& t = spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
    return std::exchange(t.settled, false);
}

void Workspace::cycle_tab(int delta) {
    Space* sp = active();
    if (sp == nullptr || sp->tabs.empty())
        return;
    // Wraps and skips tabs put away (herdr tab.next / tab.prev).
    const int n = static_cast<int>(sp->tabs.size());
    int i = sp->active_tab;
    for (int step = 0; step < n; ++step) {
        i = ((i + delta) % n + n) % n;
        if (!sp->tabs[static_cast<std::size_t>(i)].settled) {
            sp->active_tab = i;
            return;
        }
    }
}

void Workspace::cycle_space(int delta) {
    if (spaces.empty())
        return;
    const int n = static_cast<int>(spaces.size());
    active_space = ((active_space + delta) % n + n) % n;
}

bool Workspace::focus_dir(Dir dir, Rect area) {
    Tab* t = active_tab();
    if (t == nullptr || !t->zoomed.empty())
        return false;
    const auto next = neighbour(layout_tree(t->root, area, 0), t->focused, dir);
    if (!next)
        return false;
    t->focused = *next;
    return true;
}

bool Workspace::swap_dir(Dir dir, Rect area) {
    Tab* t = active_tab();
    if (t == nullptr || !t->zoomed.empty())
        return false;
    const auto next = neighbour(layout_tree(t->root, area, 0), t->focused, dir);
    if (!next)
        return false;
    Node* a = find_leaf(t->root, t->focused);
    Node* b = find_leaf(t->root, *next);
    if (a == nullptr || b == nullptr)
        return false;
    std::swap(a->session, b->session);
    return true;
}

bool Workspace::resize(Dir dir, float px, Rect area) {
    Tab* t = active_tab();
    if (t == nullptr || !t->zoomed.empty())
        return false;
    // Walk down to the focused leaf, remembering the deepest split on the right axis whose
    // border lies on the requested side of the pane.
    const bool want_vertical = dir == Dir::kUp || dir == Dir::kDown;
    Node* n = &t->root;
    Rect r = area;
    Node* target = nullptr;
    Rect target_rect{};
    while (!n->leaf()) {
        const int side = contains(n->children[0], t->focused) ? 0 : 1;
        if (n->vertical == want_vertical) {
            target = n;
            target_rect = r;
        }
        Rect a = r;
        Rect b = r;
        if (n->vertical) {
            a.h = r.h * n->ratio;
            b.y = r.y + a.h;
            b.h = r.h - a.h;
        } else {
            a.w = r.w * n->ratio;
            b.x = r.x + a.w;
            b.w = r.w - a.w;
        }
        r = side == 0 ? a : b;
        n = &n->children[static_cast<std::size_t>(side)];
    }
    if (target == nullptr)
        return false;
    const float total = want_vertical ? target_rect.h : target_rect.w;
    if (total <= 0)
        return false;
    const float delta = (dir == Dir::kRight || dir == Dir::kDown ? px : -px) / total;
    target->ratio = std::clamp(target->ratio + delta, 0.1f, 0.9f);
    return true;
}

void Workspace::toggle_zoom() {
    Tab* t = active_tab();
    if (t == nullptr || t->root.leaf())
        return;
    t->zoomed = t->zoomed.empty() ? t->focused : std::string();
}

// ------------------------------------------------------------------ naming
std::string Workspace::space_title(int index, const std::map<std::string, std::string>& session_cwd) const {
    if (index < 0 || index >= static_cast<int>(spaces.size()))
        return {};
    const Space& sp = spaces[static_cast<std::size_t>(index)];
    if (!sp.name.empty())
        return sp.name;
    // Unnamed: after the folder of its first terminal (herdr behaves the same way).
    for (const auto& t : sp.tabs) {
        std::vector<std::string> ids;
        collect(t.root, ids);
        for (const auto& id : ids) {
            const auto it = session_cwd.find(id);
            if (it != session_cwd.end() && !it->second.empty())
                return folder_of(it->second);
        }
    }
    return "space " + std::to_string(index + 1);
}

std::string Workspace::tab_title(const Tab& tab, int index) {
    return tab.name.empty() ? std::to_string(index + 1) : tab.name;
}

// ------------------------------------------------------------------ persistence
Json Workspace::to_json() const {
    Json sp = Json::array();
    for (const auto& s : spaces) {
        Json tabs = Json::array();
        for (const auto& t : s.tabs) {
            tabs.push_back({{"id", t.id},
                            {"name", t.name},
                            {"root", node_to_json(t.root)},
                            {"focused", t.focused},
                            {"zoomed", t.zoomed},
                            {"settled", t.settled},
                            {"pane_names", t.pane_names}});
        }
        sp.push_back({{"id", s.id},
                      {"name", s.name},
                      {"machine", s.machine},
                      {"color", s.color},
                      {"panel", s.panel},
                      {"active_tab", s.active_tab},
                      {"tabs", std::move(tabs)}});
    }
    return {{"version", 1},
            {"active_space", active_space},
            {"spaces", std::move(sp)},
            {"pinned", std::vector<std::string>(pinned.begin(), pinned.end())},
            {"right_click_program", std::vector<std::string>(right_click_program.begin(), right_click_program.end())}};
}

Workspace Workspace::from_json(const Json& j) {
    Workspace w;
    if (!j.is_object() || j.value("version", 0) != 1)
        return w;
    for (const auto& s : j.value("spaces", Json::array())) {
        Space sp;
        sp.id = s.value("id", "");
        sp.name = s.value("name", "");
        sp.machine = s.value("machine", "");
        sp.color = s.value("color", "");
        sp.panel = s.value("panel", "");
        sp.active_tab = s.value("active_tab", 0);
        for (const auto& t : s.value("tabs", Json::array())) {
            Tab tab;
            tab.id = t.value("id", "");
            tab.name = t.value("name", "");
            tab.root = node_from_json(t.value("root", Json::object()));
            tab.focused = t.value("focused", "");
            tab.zoomed = t.value("zoomed", "");
            tab.settled = t.value("settled", false);
            if (const auto pn = t.find("pane_names"); pn != t.end() && pn->is_object())
                tab.pane_names = pn->get<std::map<std::string, std::string>>();
            if (!w.sessions_in(tab).empty())
                sp.tabs.push_back(std::move(tab));
        }
        if (!sp.tabs.empty()) {
            sp.active_tab = std::clamp(sp.active_tab, 0, static_cast<int>(sp.tabs.size()) - 1);
            sp.tabs[static_cast<std::size_t>(sp.active_tab)].settled = false;  // the shown tab is never away
            w.spaces.push_back(std::move(sp));
        }
    }
    for (const auto& p : j.value("pinned", Json::array()))
        if (p.is_string())
            w.pinned.insert(p.get<std::string>());
    for (const auto& p : j.value("right_click_program", Json::array()))
        if (p.is_string())
            w.right_click_program.insert(p.get<std::string>());
    w.active_space =
        std::clamp(j.value("active_space", 0), 0, std::max(0, static_cast<int>(w.spaces.size()) - 1));
    return w;
}

bool Workspace::move_pane(const std::string& moving, const std::string& target, std::optional<Dir> edge) {
    if (moving == target)
        return false;
    const auto a = find(moving);
    const auto b = find(target);
    if (a.space < 0 || a.space != b.space || a.tab != b.tab)
        return false;
    Tab& tab = spaces[static_cast<std::size_t>(a.space)].tabs[static_cast<std::size_t>(a.tab)];
    if (!edge) {
        Node* x = find_leaf(tab.root, moving);
        Node* y = find_leaf(tab.root, target);
        if (x == nullptr || y == nullptr)
            return false;
        std::swap(x->session, y->session);
        tab.focused = moving;
        return true;
    }
    if (tab.root.leaf() || !remove_leaf(tab.root, moving))
        return false;
    tab.zoomed.clear();
    return split(target, moving, *edge);
}

bool Workspace::move_space(int from, int to) {
    const int n = static_cast<int>(spaces.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return false;
    if (spaces[static_cast<std::size_t>(from)].machine != spaces[static_cast<std::size_t>(to)].machine)
        return false;  // herdr: a space moves only inside its machine's block
    const std::string active_id = spaces[static_cast<std::size_t>(active_space)].id;
    Space moved = std::move(spaces[static_cast<std::size_t>(from)]);
    spaces.erase(spaces.begin() + from);
    spaces.insert(spaces.begin() + to, std::move(moved));
    for (int i = 0; i < n; ++i)
        if (spaces[static_cast<std::size_t>(i)].id == active_id)
            active_space = i;
    return true;
}

void Workspace::assign_colors() {
    static const char* const kOrder[] = {"ansi6", "ansi3", "ansi4", "ansi5", "ansi2", "ansi1"};
    for (auto& sp : spaces) {
        if (!sp.color.empty())
            continue;
        std::map<std::string, int> used;
        for (const auto& o : spaces)
            if (!o.color.empty())
                ++used[o.color];
        const char* best = kOrder[0];
        int best_n = INT_MAX;
        for (const char* c : kOrder) {
            const int u = used.count(c) ? used[c] : 0;
            if (u < best_n) {
                best_n = u;
                best = c;
            }
        }
        sp.color = best;
    }
}

// ------------------------------------------------------------------ geometry
std::map<std::string, Rect> layout_tree(const Node& root, Rect area, float gap) {
    std::map<std::string, Rect> out;
    layout_into(root, area, gap, out);
    return out;
}

std::vector<Divider> dividers(const Node& root, Rect area, float gap) {
    std::vector<Divider> out;
    std::vector<int> path;
    dividers_into(root, area, gap, path, out);
    return out;
}

Node* node_at(Node& root, const std::vector<int>& path) {
    Node* n = &root;
    for (const int i : path) {
        if (n->leaf() || i < 0 || i > 1)
            return nullptr;
        n = &n->children[static_cast<std::size_t>(i)];
    }
    return n;
}

// ------------------------------------------------------------------ attention
bool AttentionTracker::update(const std::string& session, const std::string& agent_state, bool looking) {
    // herdr model.rs 529-572: to blocked/error from anything else always alerts (also while
    // looking: the caller then plays the sound but shows no note); from busy (working/blocked/
    // error) to anything else alerts only when not looking, and that agent is "done" until seen.
    // States in the first listing never alert; one already done shows as done.
    auto& e = entries_[session];
    const std::string prev = e.last;
    const bool first = !e.known;
    e.known = true;
    e.last = agent_state;
    const auto needs = [](const std::string& st) { return st == "Blocked" || st == "Error"; };
    const auto busy = [&](const std::string& st) { return st == "Working" || needs(st); };
    if (first) {
        e.unseen_done = agent_state == "Done" && !looking;
        return false;
    }
    if (prev == agent_state) {
        if (looking)
            e.unseen_done = false;
        return false;
    }
    if (needs(agent_state))
        return !needs(prev);
    if (busy(prev)) {
        e.unseen_done = !looking;
        return !looking;
    }
    if (looking)
        e.unseen_done = false;
    return false;
}

Attention AttentionTracker::get(const std::string& session) const {
    const auto it = entries_.find(session);
    if (it == entries_.end())
        return Attention::kNone;
    const auto& e = it->second;
    if (e.last == "Blocked" || e.last == "Error")
        return Attention::kBlocked;
    if (e.unseen_done)
        return Attention::kDone;
    if (e.last == "Working")
        return Attention::kWorking;
    if (e.last == "Idle" || e.last == "Done")
        return Attention::kIdle;
    return Attention::kNone;
}

void AttentionTracker::seen(const std::string& session) {
    if (const auto it = entries_.find(session); it != entries_.end())
        it->second.unseen_done = false;
}

void AttentionTracker::forget(const std::string& session) {
    entries_.erase(session);
}

int attention_rank(Attention a) {
    switch (a) {
        case Attention::kBlocked:
            return 4;
        case Attention::kDone:
            return 3;
        case Attention::kWorking:
            return 2;
        case Attention::kIdle:
            return 1;
        case Attention::kNone:
            return 0;
    }
    return 0;
}

Attention most_urgent(const std::vector<Attention>& list) {
    Attention best = Attention::kNone;
    for (const auto a : list) {
        if (attention_rank(a) > attention_rank(best))
            best = a;
    }
    return best;
}

const char* attention_label(Attention a) {
    switch (a) {
        case Attention::kBlocked:
            return "needs you";
        case Attention::kDone:
            return "done";
        case Attention::kWorking:
            return "working";
        case Attention::kIdle:
            return "idle";
        case Attention::kNone:
            return "";
    }
    return "";
}

}  // namespace agentdesk::ui
