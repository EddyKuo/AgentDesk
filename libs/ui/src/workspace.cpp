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
    return std::any_of(n.children.begin(), n.children.end(), [&](const Node& c) { return contains(c, s); });
}

void collect(const Node& n, std::vector<std::string>& out) {
    if (n.leaf()) {
        if (!n.session.empty())
            out.push_back(n.session);
        return;
    }
    for (const auto& c : n.children)
        collect(c, out);
}

Node* find_leaf(Node& n, const std::string& s) {
    if (n.leaf())
        return n.session == s ? &n : nullptr;
    for (auto& c : n.children)
        if (auto* f = find_leaf(c, s))
            return f;
    return nullptr;
}

// The container holding leaf `s` directly, and the leaf's index in it.
Node* parent_of(Node& n, const std::string& s, int* index) {
    for (std::size_t i = 0; i < n.children.size(); ++i) {
        Node& c = n.children[i];
        if (c.leaf() && c.session == s) {
            *index = static_cast<int>(i);
            return &n;
        }
        if (!c.leaf())
            if (Node* p = parent_of(c, s, index))
                return p;
    }
    return nullptr;
}

void fix_weights(Node& n) {
    n.weights.resize(n.children.size(), 1.0f);
    for (auto& w : n.weights)
        if (!(w > 0.0f))
            w = 1.0f;
}

// Removes leaf `s`. herdr layout.rs: its share goes back to the siblings in proportion, and a
// container left with one child is replaced by that child. Returns true when removed.
bool remove_leaf(Node& n, const std::string& s) {
    if (n.leaf())
        return false;
    for (std::size_t i = 0; i < n.children.size(); ++i) {
        Node& c = n.children[i];
        if (c.leaf() && c.session == s) {
            fix_weights(n);
            n.children.erase(n.children.begin() + static_cast<std::ptrdiff_t>(i));
            n.weights.erase(n.weights.begin() + static_cast<std::ptrdiff_t>(i));
            if (n.children.size() == 1) {
                Node keep = std::move(n.children.front());
                n = std::move(keep);
            }
            return true;
        }
        if (remove_leaf(c, s)) {
            if (!c.leaf() && c.children.size() == 1) {
                Node keep = std::move(c.children.front());
                c = std::move(keep);
            }
            // A container that now runs on the same axis as its parent merges into it.
            if (!c.leaf() && c.vertical == n.vertical) {
                fix_weights(n);
                fix_weights(c);
                const float total = n.weights[i];
                float sum = 0;
                for (float w : c.weights)
                    sum += w;
                std::vector<Node> kids = std::move(c.children);
                std::vector<float> ws;
                for (float w : c.weights)
                    ws.push_back(total * w / std::max(sum, 1e-6f));
                n.children.erase(n.children.begin() + static_cast<std::ptrdiff_t>(i));
                n.weights.erase(n.weights.begin() + static_cast<std::ptrdiff_t>(i));
                n.children.insert(n.children.begin() + static_cast<std::ptrdiff_t>(i),
                                  std::make_move_iterator(kids.begin()), std::make_move_iterator(kids.end()));
                n.weights.insert(n.weights.begin() + static_cast<std::ptrdiff_t>(i), ws.begin(), ws.end());
            }
            return true;
        }
    }
    return false;
}

// Child rectangles of a container: shares by weight, `gap` between them.
std::vector<Rect> child_rects(const Node& n, Rect r, float gap) {
    std::vector<Rect> out;
    const std::size_t k = n.children.size();
    float sum = 0;
    for (std::size_t i = 0; i < k; ++i)
        sum += i < n.weights.size() && n.weights[i] > 0 ? n.weights[i] : 1.0f;
    const float total = std::max(0.0f, (n.vertical ? r.h : r.w) - gap * static_cast<float>(k - 1));
    float at = n.vertical ? r.y : r.x;
    float used = 0;
    for (std::size_t i = 0; i < k; ++i) {
        const float w = i < n.weights.size() && n.weights[i] > 0 ? n.weights[i] : 1.0f;
        const float size = i + 1 == k ? total - used : std::round(total * w / std::max(sum, 1e-6f));
        Rect c = r;
        if (n.vertical) {
            c.y = at;
            c.h = size;
        } else {
            c.x = at;
            c.w = size;
        }
        out.push_back(c);
        at += size + gap;
        used += size;
    }
    return out;
}

void layout_into(const Node& n, Rect r, float gap, std::map<std::string, Rect>& out) {
    if (n.leaf()) {
        if (!n.session.empty())
            out[n.session] = r;
        return;
    }
    const auto rects = child_rects(n, r, gap);
    for (std::size_t i = 0; i < n.children.size(); ++i)
        layout_into(n.children[i], rects[i], gap, out);
}

void dividers_into(const Node& n, Rect r, float gap, std::vector<int>& path, std::vector<Divider>& out) {
    if (n.leaf())
        return;
    const auto rects = child_rects(n, r, gap);
    for (std::size_t i = 0; i + 1 < n.children.size(); ++i) {
        Divider d;
        d.path = path;
        d.index = static_cast<int>(i);
        d.vertical = n.vertical;
        d.parent = r;
        const Rect& c = rects[i];
        d.bar = n.vertical ? Rect{r.x, c.y + c.h, r.w, gap} : Rect{c.x + c.w, r.y, gap, r.h};
        out.push_back(d);
    }
    for (std::size_t i = 0; i < n.children.size(); ++i) {
        path.push_back(static_cast<int>(i));
        dividers_into(n.children[i], rects[i], gap, path, out);
        path.pop_back();
    }
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
    Json kids = Json::array();
    for (const auto& c : n.children)
        kids.push_back(node_to_json(c));
    Node copy = n;
    fix_weights(copy);
    return {{"split", n.vertical ? "down" : "right"}, {"children", kids}, {"weights", copy.weights}};
}

Node node_from_json(const Json& j, int depth = 0) {
    Node n;
    if (!j.is_object() || depth > 32)
        return n;
    if (j.contains("split") && j.contains("children") && j["children"].is_array()) {
        n.vertical = j.value("split", "right") == "down";
        for (const auto& c : j["children"])
            n.children.push_back(node_from_json(c, depth + 1));
        for (const auto& w : j.value("weights", Json::array()))
            if (w.is_number())
                n.weights.push_back(std::clamp(w.get<float>(), 0.01f, 100.0f));
        fix_weights(n);
        if (n.children.size() == 1) {
            Node keep = std::move(n.children.front());
            return keep;
        }
        return n;
    }
    if (j.contains("split") && j.contains("a") && j.contains("b")) {  // documents from before
        n.vertical = j.value("split", "right") == "down";
        const float ratio = std::clamp(j.value("ratio", 0.5f), 0.05f, 0.95f);
        n.children.push_back(node_from_json(j["a"], depth + 1));
        n.children.push_back(node_from_json(j["b"], depth + 1));
        n.weights = {ratio, 1.0f - ratio};
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
    const int at = sp.tabs.empty() ? 0 : std::clamp(sp.active_tab + 1, 0, static_cast<int>(sp.tabs.size()));
    sp.tabs.insert(sp.tabs.begin() + at, std::move(t));
    sp.active_tab = at;
    active_space = space;
    return sp.active_tab;
}

bool Workspace::split(const std::string& target, const std::string& session, Dir dir) {
    const auto loc = find(target);
    if (loc.space < 0)
        return false;
    Tab& tab = spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
    const bool vertical = dir == Dir::kDown || dir == Dir::kUp;
    const bool before = dir == Dir::kLeft || dir == Dir::kUp;
    Node fresh;
    fresh.session = session;
    // herdr layout.rs 598-629: when the target's container already runs along that axis, the new
    // pane goes next to the target and the two share the target's weight; otherwise the target
    // becomes a new container [target, new].
    int index = -1;
    Node* parent = parent_of(tab.root, target, &index);
    if (parent != nullptr && parent->vertical == vertical) {
        fix_weights(*parent);
        const float half = parent->weights[static_cast<std::size_t>(index)] * 0.5f;
        parent->weights[static_cast<std::size_t>(index)] = half;
        const auto at = static_cast<std::ptrdiff_t>(before ? index : index + 1);
        parent->children.insert(parent->children.begin() + at, std::move(fresh));
        parent->weights.insert(parent->weights.begin() + at, half);
    } else {
        Node* leaf = find_leaf(tab.root, target);
        if (leaf == nullptr)
            return false;
        Node old = *leaf;
        leaf->session.clear();
        leaf->vertical = vertical;
        leaf->children.clear();
        leaf->children.push_back(before ? fresh : old);
        leaf->children.push_back(before ? old : fresh);
        leaf->weights = {1.0f, 1.0f};
    }
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
    std::erase(tab.focus_history, session);
    if (tab.zoomed == session)
        tab.zoomed.clear();
    if (tab.focused == session) {
        // herdr model.rs 403-419: back to the pane focused before it, else the first.
        const auto rest = sessions_in(tab);
        tab.focused = rest.empty() ? std::string() : rest.front();
        for (auto it = tab.focus_history.rbegin(); it != tab.focus_history.rend(); ++it)
            if (std::find(rest.begin(), rest.end(), *it) != rest.end()) {
                tab.focused = *it;
                break;
            }
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
    // herdr: closing the shown tab shows the one that slides into its slot, else the last.
    if (sp.active_tab > tab)
        --sp.active_tab;
    sp.active_tab = std::min(sp.active_tab, static_cast<int>(sp.tabs.size()) - 1);
}

void Workspace::close_space(int space) {
    if (space < 0 || space >= static_cast<int>(spaces.size()))
        return;
    spaces.erase(spaces.begin() + space);
    if (active_space > space)
        --active_space;
    active_space = std::clamp(active_space, 0, std::max(0, static_cast<int>(spaces.size()) - 1));
}

bool Workspace::move_tab(int space, int from, int to) {
    if (space < 0 || space >= static_cast<int>(spaces.size()))
        return false;
    auto& tabs = spaces[static_cast<std::size_t>(space)].tabs;
    const int n = static_cast<int>(tabs.size());
    if (from < 0 || from >= n || n < 2)
        return false;
    to = ((to % n) + n) % n;
    auto& sp = spaces[static_cast<std::size_t>(space)];
    const std::string shown = tabs[static_cast<std::size_t>(std::clamp(sp.active_tab, 0, n - 1))].id;
    Tab t = std::move(tabs[static_cast<std::size_t>(from)]);
    tabs.erase(tabs.begin() + from);
    tabs.insert(tabs.begin() + to, std::move(t));
    for (int i = 0; i < n; ++i)
        if (tabs[static_cast<std::size_t>(i)].id == shown)
            sp.active_tab = i;
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
    if (t.focused != session && !t.focused.empty()) {  // herdr Tab::focus history
        std::erase(t.focus_history, t.focused);
        t.focus_history.push_back(t.focused);
        if (t.focus_history.size() > 32)
            t.focus_history.erase(t.focus_history.begin());
    }
    t.focused = session;
    t.settled = false;  // showing a terminal brings its tab back
    if (!t.zoomed.empty() && t.zoomed != session)
        t.zoomed = session;  // herdr: the zoom moves to the focused pane
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
    if (t == nullptr)
        return false;
    const auto next = neighbour(layout_tree(t->root, area, 0), t->focused, dir);
    if (!next)
        return false;
    focus(*next);  // a zoomed tab moves its zoom along (herdr model.rs 131-133)
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
    // Walk down to the focused leaf; remember the deepest container on the right axis where the
    // focused branch has a neighbour on the requested side: that border moves by px.
    const bool want_vertical = dir == Dir::kUp || dir == Dir::kDown;
    const bool after = dir == Dir::kRight || dir == Dir::kDown;
    Node* n = &t->root;
    Rect r = area;
    std::vector<int> path;
    std::optional<Divider> target;
    while (!n->leaf()) {
        const auto rects = child_rects(*n, r, 0);
        int side = 0;
        for (std::size_t i = 0; i < n->children.size(); ++i)
            if (contains(n->children[i], t->focused))
                side = static_cast<int>(i);
        if (n->vertical == want_vertical) {
            const int border = after ? side : side - 1;
            if (border >= 0 && border + 1 < static_cast<int>(n->children.size())) {
                Divider d;
                d.path = path;
                d.index = border;
                d.vertical = n->vertical;
                d.parent = r;
                const Rect& c = rects[static_cast<std::size_t>(border)];
                d.bar = n->vertical ? Rect{r.x, c.y + c.h, r.w, 0} : Rect{c.x + c.w, r.y, 0, r.h};
                target = d;
            }
        }
        path.push_back(side);
        r = rects[static_cast<std::size_t>(side)];
        n = &n->children[static_cast<std::size_t>(side)];
    }
    if (!target)
        return false;
    const float at = target->vertical ? target->bar.y : target->bar.x;
    drag_divider(t->root, *target, at + (after ? px : -px), 0);
    return true;
}

void Workspace::toggle_zoom(const std::string& pane) {
    Tab* t = active_tab();
    if (!pane.empty()) {  // herdr pane.zoom {pane}: that pane, focused
        const auto loc = find(pane);
        if (loc.space < 0)
            return;
        t = &spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
        if (t->root.leaf())
            return;
        t->zoomed = t->zoomed == pane ? std::string() : pane;
        t->focused = pane;
        return;
    }
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
        if (n->leaf() || i < 0 || i >= static_cast<int>(n->children.size()))
            return nullptr;
        n = &n->children[static_cast<std::size_t>(i)];
    }
    return n;
}

void drag_divider(Node& root, const Divider& d, float pos, float gap) {
    Node* n = node_at(root, d.path);
    if (n == nullptr || n->leaf() || d.index < 0 || d.index + 1 >= static_cast<int>(n->children.size()))
        return;
    fix_weights(*n);
    const auto rects = child_rects(*n, d.parent, gap);
    const auto i = static_cast<std::size_t>(d.index);
    const float start = n->vertical ? rects[i].y : rects[i].x;
    const float span = n->vertical ? rects[i].h + rects[i + 1].h : rects[i].w + rects[i + 1].w;
    if (span <= 1.0f)
        return;
    const float first = std::clamp(pos - start, span * 0.05f, span * 0.95f);
    const float both = n->weights[i] + n->weights[i + 1];
    n->weights[i] = both * first / span;
    n->weights[i + 1] = both - n->weights[i];
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
