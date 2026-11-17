#pragma once
// Spaces, tabs and split panes (design doc 4.1, ADR-12/ADR-13). Pure data + operations, no ImGui
// and no IPC, so every rule is unit-tested. The UI persists it through the host ("ui.state").
//
//   Workspace -> Space (a place to work, usually one repo/folder)
//             -> Tab   (a piece of work: terminals split right/down)
//             -> Node  (binary split tree; leaves are session ids)
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace agentdesk::ui {

enum class Dir { kLeft, kRight, kUp, kDown };

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
};

struct Node {
    std::string session;         // leaf when non-empty
    bool vertical = false;       // split: false = side by side (right), true = stacked (down)
    float ratio = 0.5f;          // share of the first child
    std::vector<Node> children;  // 0 (leaf) or 2

    [[nodiscard]] bool leaf() const { return children.empty(); }
};

struct Tab {
    std::string id;
    std::string name;  // empty: shown by its number
    Node root;
    std::string focused;                            // session id
    std::string zoomed;                             // session id shown alone, or empty
    std::map<std::string, std::string> pane_names;  // session id -> name
};

struct Space {
    std::string id;
    std::string name;  // empty: named after its first terminal's folder
    std::vector<Tab> tabs;
    int active_tab = 0;
    std::string machine;  // empty: this computer
};

struct PaneLoc {
    int space = -1;
    int tab = -1;
};

class Workspace {
public:
    std::vector<Space> spaces;
    int active_space = 0;

    // ---- queries
    [[nodiscard]] PaneLoc find(const std::string& session) const;
    [[nodiscard]] std::vector<std::string> sessions_in(const Tab& tab) const;
    [[nodiscard]] std::vector<std::string> all_sessions() const;
    [[nodiscard]] Space* active();
    [[nodiscard]] Tab* active_tab();
    [[nodiscard]] std::string focused_session() const;

    // ---- structure
    // New space holding one tab with `session`; returns its index.
    int add_space(const std::string& session, const std::string& name = {});
    // New tab with `session` in space `space` (active space when -1); returns the tab index.
    int add_tab(const std::string& session, int space = -1, const std::string& name = {});
    // Splits the pane showing `target` and puts `session` right of / below it.
    bool split(const std::string& target, const std::string& session, Dir dir);
    // Removes the pane; an emptied tab closes, an emptied space closes.
    void remove(const std::string& session);
    // Puts `to` where `from` is shown (restarting a pane in place). Returns false if not shown.
    bool replace(const std::string& from, const std::string& to);
    // Drops panes whose session is not in `present`.
    void prune(const std::set<std::string>& present);
    void close_tab(int space, int tab);
    void close_space(int space);
    bool move_tab(int space, int from, int to);

    // ---- focus and selection
    void focus(const std::string& session);  // selects its space and tab too
    void select_space(int index);
    void select_tab(int index);  // in the active space
    void cycle_tab(int delta);
    void cycle_space(int delta);
    // Moves focus to the neighbouring pane in `dir` (tab layout inside `area`).
    bool focus_dir(Dir dir, Rect area);
    // Swaps the focused pane with its neighbour in `dir`; focus stays on the moved pane.
    bool swap_dir(Dir dir, Rect area);
    // Moves the nearest border of the focused pane in `dir` by `px` (tab layout inside `area`).
    bool resize(Dir dir, float px, Rect area);
    void toggle_zoom();

    // ---- naming
    [[nodiscard]] std::string space_title(int index,
                                          const std::map<std::string, std::string>& session_cwd) const;
    [[nodiscard]] static std::string tab_title(const Tab& tab, int index);

    // ---- persistence (ui.state document)
    [[nodiscard]] nlohmann::json to_json() const;
    static Workspace from_json(const nlohmann::json& j);

    std::string new_id(const char* prefix);

private:
    std::uint64_t next_id_ = 1;
};

// Pane rectangles of a tab's tree laid out in `area` with `gap` between panes.
std::map<std::string, Rect> layout_tree(const Node& root, Rect area, float gap);

// One draggable border of a split: the node path to the split and its hit rectangle.
struct Divider {
    std::vector<int> path;  // child indices from the root to the split node
    bool vertical = false;  // true: horizontal bar between stacked panes
    Rect bar;
    Rect parent;  // the split node's area (to turn a drag into a ratio)
};
std::vector<Divider> dividers(const Node& root, Rect area, float gap);
Node* node_at(Node& root, const std::vector<int>& path);

// ---- attention (herdr-style: "done" = finished while you were not looking)
enum class Attention { kNone, kIdle, kWorking, kDone, kBlocked };

class AttentionTracker {
public:
    // Feed every frame with the host's agent state; `looking` = the pane is visible and the
    // window is focused. Returns true when the session newly needs the user (blocked, or
    // finished while not looked at): the caller plays a sound / shows a toast.
    bool update(const std::string& session, const std::string& agent_state, bool looking);
    [[nodiscard]] Attention get(const std::string& session) const;
    void seen(const std::string& session);  // the user looked: done -> idle
    void forget(const std::string& session);

private:
    struct Entry {
        std::string last;
        bool unseen_done = false;
    };
    std::map<std::string, Entry> entries_;
};

// Most urgent first: blocked > done > working > idle > none (herdr's ordering).
int attention_rank(Attention a);
Attention most_urgent(const std::vector<Attention>& list);
const char* attention_label(Attention a);

}  // namespace agentdesk::ui
