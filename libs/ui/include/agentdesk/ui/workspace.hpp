#pragma once
// Spaces, tabs and split panes (design doc 4.1, ADR-12/ADR-13). Pure data + operations, no ImGui
// and no IPC, so every rule is unit-tested. The UI persists it through the host ("ui.state").
//
//   Workspace -> Space (a place to work, usually one repo/folder)
//             -> Tab   (a piece of work: terminals split right/down)
//             -> Node  (split tree like herdr's layout.rs: a row or column holds any number of
//                       children with weights; leaves are session ids)
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
    bool vertical = false;       // container: false = a row (side by side), true = a column
    std::vector<Node> children;  // 0 (leaf) or 2+
    std::vector<float> weights;  // one per child; shares of the container

    [[nodiscard]] bool leaf() const { return children.empty(); }
};

struct Tab {
    std::string id;
    std::string name;  // empty: shown by its number
    Node root;
    std::string focused;                            // session id
    std::string zoomed;                             // session id shown alone, or empty
    std::map<std::string, std::string> pane_names;  // session id -> name
    std::vector<std::string> focus_history;          // panes focused before, oldest first (herdr Tab::focus)
    bool settled = false;  // herdr tab.settle: put away (not in the tab bar), still running
};

struct Space {
    std::string id;
    std::string name;  // empty: named after its first terminal's folder
    std::vector<Tab> tabs;
    int active_tab = 0;
    std::string machine;  // empty: this computer
    std::string color;    // avatar colour name (herdr: ansi6, ansi3, ansi4, ansi5, ansi2, ansi1)
    std::string panel;    // right panel shown for this space: "", "files" or "changes"
    std::string folder;   // where it was made: names it when its first terminal says nothing (herdr)
};

struct PaneLoc {
    int space = -1;
    int tab = -1;
};

class Workspace {
public:
    std::vector<Space> spaces;
    int active_space = 0;
    std::set<std::string> pinned;  // agents pinned to the top of the agents list
    std::set<std::string> right_click_program;  // terminals whose right-clicks go to the program

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
    // Drag a pane onto another of the same tab (herdr pane drag): onto an edge it moves there
    // (the target splits on that side); with no edge the two swap. The moved pane gets focus.
    bool move_pane(const std::string& moving, const std::string& target, std::optional<Dir> edge);
    // Moves a space within its machine's block (herdr space.move); keeps the active one active.
    bool move_space(int from, int to);
    // Gives spaces without a colour the least-used avatar colour (ties: earlier in the order).
    // With `session_top` (see space_title), a space named "~" (the home folder) gets none yet
    // (herdr spaces.js 526).
    void assign_colors(const std::map<std::string, std::string>* session_top = nullptr);
    // Puts the spaces in `order` (old indexes, new order; a permutation); the active one stays
    // active (herdr spaces.js 316-334: space.move so the model follows the drawn tree).
    bool reorder_spaces(const std::vector<int>& order);
    // Spaces without a creation folder take their first terminal's start folder.
    void remember_folders(const std::map<std::string, std::string>& session_start);

    // ---- focus and selection
    void focus(const std::string& session);  // selects its space and tab too
    void select_space(int index);
    void select_tab(int index);  // in the active space (shows it again if it was put away)
    // Ctrl+1..9: the n-th tab (1-based) not put away, in the active space.
    void select_shown_tab(int n);
    // herdr tab.settle: hides a tab from the bar until it is shown or needs you. The last tab
    // still shown in its space cannot be put away (returns false).
    bool settle_tab(int space, int tab);
    void unsettle_tab(int space, int tab);
    // Shows the tab holding `session` again; true if it was put away.
    bool unsettle_session(const std::string& session);
    void cycle_tab(int delta);
    void cycle_space(int delta);
    // Moves focus to the neighbouring pane in `dir` (tab layout inside `area`).
    bool focus_dir(Dir dir, Rect area);
    // Swaps the focused pane with its neighbour in `dir`; focus stays on the moved pane.
    bool swap_dir(Dir dir, Rect area);
    // Moves the nearest border of the focused pane in `dir` by `px` (tab layout inside `area`).
    bool resize(Dir dir, float px, Rect area);
    void toggle_zoom(const std::string& pane = {});  // herdr pane.zoom {pane}: zooms and focuses it

    // ---- naming
    // herdr model.rs 271-279: its name; else its first terminal's checkout top (a worktree's own
    // folder) or, outside git, that terminal's folder (`session_top`: session id -> either); else
    // the folder it was made in. The home folder (or nothing) reads "~".
    [[nodiscard]] std::string space_title(int index,
                                          const std::map<std::string, std::string>& session_top) const;
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
    std::vector<int> path;  // child indices from the root to the container
    int index = 0;          // the border between children `index` and `index + 1`
    bool vertical = false;  // true: horizontal bar between stacked panes
    Rect bar;
    Rect parent;  // the container's area (to turn a drag into weights)
};
// Moves the border `d` so that it sits at `pos` (x for a row, y for a column) inside d.parent.
void drag_divider(Node& root, const Divider& d, float pos, float gap);
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
        bool known = false;  // seen in an earlier listing
    };
    std::map<std::string, Entry> entries_;
};

// Most urgent first: blocked > done > working > idle > none (herdr's ordering).
int attention_rank(Attention a);
Attention most_urgent(const std::vector<Attention>& list);
const char* attention_label(Attention a);

// ---- spaces list (herdr mods/spaces, Apache-2.0)
// One space's checkout: `key` names its repo (machine + common git dir); `linked` = a linked
// worktree (its top is not the repo's main checkout).
struct SpaceRepo {
    bool repo = false;
    bool linked = false;
    std::string key;
};
// spaces.js 298-315: the order the list draws, each linked worktree right after the first main
// checkout of its repo (worktrees of a repo without one stay where they are). `tree[i]` (by new
// position) marks a worktree row.
std::vector<int> nest_worktrees(const std::vector<SpaceRepo>& info, std::vector<bool>* tree = nullptr);

// ---- agents list (herdr mods/agents, Apache-2.0)
// agents.js 28-31: drops what an agent puts before its title (a status glyph or spinner and the
// separator after it), Codex's "[ ! ] " / "[ . ] " mark, and the spaces around.
std::string clean_agent_title(const std::string& title);

// agents.js 44-51: what a tab says about the agent in it. `tab_name` is the tab's name or number.
struct TabLabel {
    std::string tab_name;
    bool tab_named = false;
    std::string title;         // the lead terminal's live title
    std::string program_name;  // "Claude Code"
    std::string program_key;   // "claude"
    std::string address;       // the terminal's id
};
std::string agent_tab_label(const TabLabel& in);

// agents.js 279-296: the detail after a row's space ("" = none): an automatic tab number says
// nothing unless two rows in a space would otherwise read the same. In: (space name, label).
std::vector<std::string> agent_details(const std::vector<std::pair<std::string, std::string>>& rows);

// agents.js 53-58: a sub-agent's status. It never shows done (it reports to its parent): idle.
Attention kid_status(Attention a);

// agents.js 170-180: the live sub-agents under `root`, depth first, each with its tree lines.
enum class TreeLine { kNone, kBar, kTee, kElbow };  // "", "│", "├", "└"
struct TreeRow {
    std::string id;
    std::vector<TreeLine> lines;
};
std::vector<TreeRow> agent_tree(const std::map<std::string, std::vector<std::string>>& kids, const std::string& root);

// agents.js 240: priority order: most urgent first, then latest change, then newest.
struct AgentSortKey {
    int rank = 0;
    long long since = 0;  // ms of the last change
    long long seq = 0;    // creation order
};
bool priority_before(const AgentSortKey& a, const AgentSortKey& b);

}  // namespace agentdesk::ui
