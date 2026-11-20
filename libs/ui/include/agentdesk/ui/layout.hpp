#pragma once
// The layout engine (ADR-15) without any drawing: the tree of blocks and containers and its JSON,
// the rect each node gets in the window, separator drags, floating blocks, the person's clicks
// kept beside the layout file, and the shipped layouts. agent-ui draws the blocks (layout_view).
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
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

// Which side of its node a floating block sits on, and where along that side.
enum class Edge { kTop, kRight, kBottom, kLeft };
enum class Align { kStart, kCenter, kEnd };
[[nodiscard]] inline bool vertical(Edge e) {
    return e == Edge::kLeft || e == Edge::kRight;
}
const char* edge_name(Edge e);    // "top", "right", "bottom", "left"
const char* align_name(Align a);  // "start", "center", "end"

// A size along the parent's axis: {"px": n} or {"weight": n}.
struct Span {
    bool px = false;
    float value = 1.0f;
    static Span pixels(float v) { return {true, v}; }
    static Span weight(float v) { return {false, v}; }
    bool operator==(const Span&) const = default;
};

// A border or divider line: width 1 in the theme's "border" colour unless given.
struct Line {
    float width = 1.0f;
    std::string color = "border";
    bool operator==(const Line&) const = default;
};

// Padding: one number, or [vertical, horizontal].
struct Padding {
    float y = 0.0f;
    float x = 0.0f;
    bool axes = false;  // written as a pair
    bool operator==(const Padding&) const = default;
};

// A stack's or a pane's header.
struct Header {
    Json content;                         // "tabs" (the plain built-in strip) or {"mod": ..., "props": ...}
    std::optional<std::string> reveal;    // "always" (default) or "hover"
    std::optional<float> height;          // px, default 30
    std::optional<std::string> position;  // "top" (default) or "bottom"
    [[nodiscard]] bool hover() const { return reveal && *reveal == "hover"; }
    [[nodiscard]] bool bottom() const { return position && *position == "bottom"; }
    [[nodiscard]] std::string mod() const;  // the mod's name, "" for the built-in strip
    [[nodiscard]] Json props() const;
    bool operator==(const Header&) const = default;
};

// How a layout wraps each pane of the tab it shows.
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

// What a block shows.
struct Content {
    enum class Type { kTerminal, kMod, kTab, kAgent };
    Type type = Type::kMod;
    std::string value;  // the terminal, the mod's name, the tab ("active") or the agent ("focused")
    Json props;         // a mod's props (null: none)
    std::optional<PaneFrame> pane;  // a tab's pane frame
    bool operator==(const Content&) const = default;
};

// A block floating over a node, snapped to one of its edges.
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

// One node of the tree. Fields kept as JSON are only read by agent-ui or written back as they
// came (normalized the way parse() and to_json() read and write them).
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
    Json look;   // flattened look: theme, colors, font, font_size, ... (an object)
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
    [[nodiscard]] bool is_pane() const;                    // a terminal or a mod block
    [[nodiscard]] bool closable_or() const;                // default true
    [[nodiscard]] bool resizable_or() const;               // default true
    [[nodiscard]] std::optional<float> font_size() const;  // look.font_size
    [[nodiscard]] std::string mod() const;                 // a mod block's mod, else ""
    [[nodiscard]] std::string role() const;                // a mod block's props.role, else ""

    // Tree walks.
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

    // Tree edits.
    std::optional<Node> remove(std::string_view id);
    std::optional<Node> remove_slot(std::string_view id);
    bool split(std::string_view target, Axis axis, Node fresh, std::string container_id);
    bool select(std::string_view stack, std::string_view child);
    bool swap(std::string_view a, std::string_view b);

    bool operator==(const Node& o) const;
};

// ---------------------------------------------------------------- JSON
// Reads a layout file: fixed keys and defaults, unknown keys are dropped. Returns nothing on a
// mistake and puts what and where in *error.
std::optional<Node> parse(const Json& j, std::string* error = nullptr);
std::optional<Node> parse_text(std::string_view text, std::string* error = nullptr);
Json to_json(const Node& n);
// The text a layout file holds: pretty-printed, whole numbers without ".0", keys sorted.
std::string text_of(const Node& n);
void normalize(Json& v);

// ---------------------------------------------------------------- the shipped layouts
// main and terminals, compiled in verbatim from the shipped files, and AgentDesk's own mac.
// AgentDesk calls main "workspace", as its settings always have.
std::vector<std::string> builtin_names();              // workspace, terminals, mac, mission
std::string_view builtin_text(std::string_view name);  // "" when not one
std::string_view builtin_file_name(std::string_view name);    // file name of a built-in: workspace -> main
// A layout as shipped (main for a name that is not a built-in).
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

// Border, padding, header and separator sizes of a node.
const Line* node_border(const Node& n, bool focused);
std::pair<float, float> padding_of(const Node& n);  // (vertical, horizontal)
float header_height(const Header& h);               // default 30
float separator_width(const Node& n, float gap);
float weight(const Node& n);
// The room each child gets along the axis, flexbox style: px sizes first (within min and max),
// weights share what is left, a child held by its min or max is frozen and the rest share again.
// whole: each px length times the zoom is rounded to a whole screen pixel (agent-ui draws at
// whole pixels, its px()); otherwise lengths stay fractional.
std::vector<float> shares(const std::vector<const Node*>& children, float available, float zoom,
                          bool whole = false);

struct Separator {
    std::string container;  // the row or column
    int index = 0;          // between its shown children index and index + 1
    Axis axis = Axis::kRow;
    Rect box;  // gap + divider
    Rect hit;  // the drag handle: 6 px or the box if wider, centered
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

// The separator `index` of `container` dragged to `pointer` (screen px along its axis), its two
// neighbours as `placed` has them now. Says whether a size changed.
bool drag(Node& root, std::string_view container, int index, Axis axis, const Placement& placed,
          float pointer, float zoom);

// ---------------------------------------------------------------- floating blocks
// A float dropped with its center at (x, y) in its node (w x h): the nearest edge (measured as
// shares of the node, so wide windows don't favor top and bottom) and the third along it.
std::pair<Edge, Align> snap(float x, float y, float w, float h);
// A float's top left in `owner` for a block of bw x bh, `margin` in from the edges
// (kFloatMargin: 12 px at zoom 1).
std::array<float, 2> float_home(Rect owner, float bw, float bh, Edge edge, Align align, float margin);
inline constexpr float kFloatMargin = 12.0f;

// ---------------------------------------------------------------- the person's clicks
// Sizes, shown/hidden, a stack's child, a node's background or font size, a pane frame's header
// height and borders, where a float sits, and the theme: kept in
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

// `now` with the click fields put back as `file` has them.
Node without_clicks(const Node& file, const Node& now);

}  // namespace agentdesk::ui::layout
