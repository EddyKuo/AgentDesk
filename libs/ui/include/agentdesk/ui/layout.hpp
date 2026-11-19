#pragma once
// herdr-app's layout engine (ADR-15), the parts with no drawing in them: the tree of blocks and
// containers and its JSON (herdr src/layout.rs), the sizes the window gives each node
// (src/window.rs 4439-4502 render_node and 5184-5209 sized; src/window/pane_size.rs 7-118), a
// separator drag (window.rs 4671-4723 drag_to), floating blocks (layout.rs 732-751,
// src/window/floats.rs 77-96), the person's clicks kept beside the file (src/layout_sync.rs) and
// the shipped layouts (defaults/layouts, window.rs 160-182). agent-ui draws the blocks
// (apps/desktop/app.cpp, layout_view).
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace agentdesk::ui::layout {

using Json = nlohmann::json;

// layout.rs 33-59: which side of its node a floating block sits on, and where along it.
enum class Edge { kTop, kRight, kBottom, kLeft };
enum class Align { kStart, kCenter, kEnd };
[[nodiscard]] inline bool vertical(Edge e) {
    return e == Edge::kLeft || e == Edge::kRight;
}
const char* edge_name(Edge e);    // "top", "right", "bottom", "left"
const char* align_name(Align a);  // "start", "center", "end"

// layout.rs 79-84: {"px": n} or {"weight": n}.
struct Span {
    bool px = false;
    float value = 1.0f;
    static Span pixels(float v) { return {true, v}; }
    static Span weight(float v) { return {false, v}; }
    bool operator==(const Span&) const = default;
};

// layout.rs 86-99: width 1 and the theme's "border" unless said.
struct Line {
    float width = 1.0f;
    std::string color = "border";
    bool operator==(const Line&) const = default;
};

// layout.rs 132-148: one number, or [vertical, horizontal].
struct Padding {
    float y = 0.0f;
    float x = 0.0f;
    bool axes = false;  // written as a pair
    bool operator==(const Padding&) const = default;
};

// layout.rs 234-256: a stack's or a pane's header.
struct Header {
    Json content;                         // "tabs" (the plain built-in strip) or {"mod": ..., "props": ...}
    std::optional<std::string> reveal;    // "always" (default) or "hover"
    std::optional<float> height;          // px, default 30 (window.rs 32)
    std::optional<std::string> position;  // "top" (default) or "bottom"
    [[nodiscard]] bool hover() const { return reveal && *reveal == "hover"; }
    [[nodiscard]] bool bottom() const { return position && *position == "bottom"; }
    [[nodiscard]] std::string mod() const;  // the mod's name, "" for the built-in strip
    [[nodiscard]] Json props() const;
    bool operator==(const Header&) const = default;
};

// layout.rs 258-282: how a layout wraps each pane of the tab it shows.
struct PaneFrame {
    std::optional<Header> header;
    Json background;  // null: none
    std::optional<std::string> shadow;
    std::optional<Line> border;
    std::optional<Line> focus;
    std::optional<float> radius;
    std::optional<float> gap;
    bool lone = false;
    bool operator==(const PaneFrame&) const = default;
};

// layout.rs 9-31: what a block shows.
struct Content {
    enum class Type { kTerminal, kMod, kTab, kAgent };
    Type type = Type::kMod;
    std::string value;  // the terminal, the mod's name, the tab ("active") or the agent ("focused")
    Json props;         // a mod's props (null: none)
    std::optional<PaneFrame> pane;  // a tab's pane frame
    bool operator==(const Content&) const = default;
};

// layout.rs 61-77: a block floating over a node, snapped to one of its edges.
struct Float {
    std::string id;
    Content content;
    Edge edge = Edge::kRight;
    Align align = Align::kCenter;
    std::optional<float> peek;
    bool hidden = false;
    bool operator==(const Float&) const = default;
};

enum class Kind { kBlock, kRow, kColumn, kStack };
enum class Axis { kRow, kColumn };

// layout.rs 327-400. Fields kept as JSON are only read by agent-ui or written back as they came
// (each normalized as herdr's serde types would read and write them).
struct Node {
    std::string id;
    Json cloned_from;  // {"name", "version"} or null
    Kind kind = Kind::kBlock;
    Content content;                    // a block's
    std::vector<Node> children;         // a row's, column's or stack's
    std::optional<std::string> active;  // a stack's shown child
    std::optional<Header> header;       // a stack's
    std::optional<Span> size;
    std::optional<float> min;
    std::optional<float> max;
    bool hidden = false;
    std::optional<std::string> title;
    Json background;  // a color (theme name or #rrggbb[aa]), {"gradient", "angle"} or {"image", "fit"}
    std::optional<Line> border;
    std::optional<float> radius;
    std::optional<std::string> shadow;
    std::optional<Padding> padding;
    Json focus;  // {"border": Line, "background": color}
    Json look;   // layout.rs 150-186, flattened: theme, colors, font, font_size, ... (an object)
    std::optional<float> gap;
    std::optional<Line> divider;
    std::optional<bool> movable;
    std::optional<bool> closable;
    std::optional<bool> resizable;
    std::vector<Float> floating;
    std::optional<std::string> titlebar;  // "system" or "none"
    Json traffic_lights;                  // [x, y]
    std::optional<std::string> preview;
    std::optional<std::string> terminal_menu;
    std::optional<std::string> toast;
    bool window_drag = false;
    bool window_blur = false;

    static Node block(std::string id, Content content);
    static Node container(std::string id, Axis axis, std::vector<Node> children);
    static Node stack(std::string id, std::vector<Node> children, std::optional<std::string> active = {});

    [[nodiscard]] bool is_block() const { return kind == Kind::kBlock; }
    [[nodiscard]] std::optional<Axis> axis() const;
    [[nodiscard]] const Content* block_content() const { return is_block() ? &content : nullptr; }
    [[nodiscard]] bool is_pane() const;                    // layout.rs 489-492: a terminal or a mod block
    [[nodiscard]] bool closable_or() const;                // default true
    [[nodiscard]] bool resizable_or() const;               // default true
    [[nodiscard]] std::optional<float> font_size() const;  // look.font_size
    [[nodiscard]] std::string mod() const;                 // a mod block's mod, else ""
    [[nodiscard]] std::string role() const;                // a mod block's props.role, else ""

    // layout.rs 502-583: tree walks.
    void fill_ids(const std::function<std::string()>& fresh);
    [[nodiscard]] std::vector<const Node*> walk() const;
    [[nodiscard]] const Node* find(std::string_view id) const;
    Node* find(std::string_view id);
    Float* find_float(std::string_view id);
    [[nodiscard]] const Float* find_float(std::string_view id) const;
    [[nodiscard]] std::optional<std::vector<std::string>> path_to(std::string_view id) const;
    [[nodiscard]] const Node* parent_of(std::string_view id) const;
    [[nodiscard]] std::vector<const Node*> terminals() const;
    [[nodiscard]] std::vector<const Node*> panes() const;
    [[nodiscard]] std::vector<const Node*> visible_blocks() const;
    [[nodiscard]] const Node* active_child() const;  // a stack's shown child

    // layout.rs 585-670: tree edits.
    std::optional<Node> remove(std::string_view id);
    std::optional<Node> remove_slot(std::string_view id);
    bool split(std::string_view target, Axis axis, Node fresh, std::string container_id);
    bool select(std::string_view stack, std::string_view child);
    bool swap(std::string_view a, std::string_view b);

    bool operator==(const Node& o) const;
};

// ---------------------------------------------------------------- JSON (serde's rules)
// Reads a layout as herdr's serde types do: the same keys and defaults; unknown keys are left
// out. Nothing on a mistake, with what and where in *error.
std::optional<Node> parse(const Json& j, std::string* error = nullptr);
std::optional<Node> parse_text(std::string_view text, std::string* error = nullptr);
Json to_json(const Node& n);
// layout_sync.rs 289-313: as the file holds it: pretty, whole numbers without ".0" (keys in
// order, as serde_json's map writes them).
std::string text_of(const Node& n);
void normalize(Json& v);

// ---------------------------------------------------------------- the shipped layouts
// herdr window.rs 160-166: main, terminals and win98 (defaults/layouts, kept verbatim in
// third_party/herdr/layouts). AgentDesk names main "workspace", as its settings always have.
std::vector<std::string> builtin_names();              // workspace, terminals, win98
std::string_view builtin_text(std::string_view name);  // "" when not one
std::string_view herdr_name(std::string_view name);    // workspace -> main
// window.rs 180-182: a layout as shipped (main for names herdr doesn't ship).
Node builtin(std::string_view name);

// AgentDesk's layout files before this engine: {"name", "sidebar", "details", "tab_bar",
// "status_bar", "theme", "style", "font_size"} and no tree. Read as the shipped layout of their
// style with those parts hidden and the look set (the file is not rewritten).
bool is_legacy(const Json& j);
Node from_legacy(const Json& j);

// ---------------------------------------------------------------- sizes
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    [[nodiscard]] float right() const { return x + w; }
    [[nodiscard]] float bottom() const { return y + h; }
    [[nodiscard]] bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    bool operator==(const Rect&) const = default;
};

// pane_size.rs 7-37.
const Line* node_border(const Node& n, bool focused);
std::pair<float, float> padding_of(const Node& n);  // (vertical, horizontal)
float header_height(const Header& h);               // window.rs 32: 30
float separator_width(const Node& n, float gap);
float weight(const Node& n);
// pane_size.rs 49-85: the room each child gets along the axis: px sizes first (within min and
// max), weights share what is left, a child its min or max holds is frozen and the rest share
// again (flexbox's rules, which herdr's window lays out by).
// whole: each px length times the zoom is rounded to a whole screen pixel (agent-ui draws at
// whole pixels, its px()); herdr leaves them fractional.
std::vector<float> shares(const std::vector<const Node*>& children, float available, float zoom,
                          bool whole = false);

struct Separator {
    std::string container;  // the row or column
    int index = 0;          // between its shown children index and index + 1
    Axis axis = Axis::kRow;
    Rect box;  // gap + divider
    Rect hit;  // the drag handle: HANDLE (6 px) or the box, centered (window.rs 4655-4659)
    bool resizable = true;
};

struct Placed {
    Rect outer;   // the slot
    Rect inner;   // inside the border and the padding
    Rect padded;  // inside the border: what floats and absolute children see
};

struct Placement {
    std::map<std::string, Placed> nodes;  // shown nodes by id
    std::vector<std::string> order;       // shown nodes, parents first (paint order)
    std::vector<Separator> separators;
    std::map<std::string, Rect> headers;  // a stack's header strip
    [[nodiscard]] const Placed* at(std::string_view id) const;
};

// The window's rect for every shown node of `root` in `area` (screen px; sizes in the layout are
// px at zoom 1). `focused`: the ids on the path to the focused block (their focus border).
Placement place(const Node& root, Rect area, float zoom, const std::set<std::string>& focused = {},
                bool whole = false);

// window.rs 4671-4723 drag_to: the separator `index` of `container` dragged to `pointer` (screen
// px along its axis), `a` and `b` its two children as placed now. Says whether a size changed.
bool drag(Node& root, std::string_view container, int index, Axis axis, const Placement& placed,
          float pointer, float zoom);

// ---------------------------------------------------------------- floating blocks
// layout.rs 732-751: dropped with its center at (x, y) in its node (w x h): the nearest edge
// (as shares of the node, so wide windows don't favor top and bottom) and the third along it.
std::pair<Edge, Align> snap(float x, float y, float w, float h);
// floats.rs 77-96: its top left in `owner` for a block of bw x bh, `margin` in from the edges
// (floats.rs 14: 12 px at zoom 1).
std::array<float, 2> float_home(Rect owner, float bw, float bh, Edge edge, Align align, float margin);
inline constexpr float kFloatMargin = 12.0f;

// ---------------------------------------------------------------- the person's clicks
// layout_sync.rs: sizes, shown/hidden, a stack's child, a node's background or font size, a pane
// frame's header height and borders, where a float sits, and the theme: kept in
// layouts/.state/<name>.json apart from the file, so an update or an edit of the file still
// applies, and an edit of a field wins over the click on it.
struct PaneClick {
    std::optional<float> header_height;
    std::optional<Line> border;
    std::optional<Line> focus;
    bool operator==(const PaneClick&) const = default;
    static std::optional<PaneClick> of(const Node& n);
    void apply(Node& n) const;
};

struct NodeState {
    std::optional<Span> size;
    std::optional<bool> hidden;
    std::optional<std::string> active;
    Json background;  // null: none
    std::optional<float> font_size;
    std::optional<PaneClick> pane;
    [[nodiscard]] bool empty() const;
    bool operator==(const NodeState&) const = default;
};

struct FloatState {
    std::optional<Edge> edge;
    std::optional<Align> align;
    std::optional<bool> hidden;
    [[nodiscard]] bool empty() const;
    bool operator==(const FloatState&) const = default;
};

struct State {
    std::map<std::string, NodeState> nodes;
    std::map<std::string, FloatState> floats;
    Json theme;  // null: none
    Json file;   // the layout file as it was when these were saved (null: unknown)

    [[nodiscard]] bool empty() const;
    void forget_since_saved(const Node& file);
    static State between(const Node& file, const Node& now);
    void apply(Node& root) const;
    void forget_edited(const Node& old_file, const Node& new_file);

    static State from_json(const Json& j);
    [[nodiscard]] Json to_json() const;
    bool operator==(const State&) const = default;
};

// layout_sync.rs 260-287: `now` with the click fields put back as `file` has them.
Node without_clicks(const Node& file, const Node& now);

}  // namespace agentdesk::ui::layout
