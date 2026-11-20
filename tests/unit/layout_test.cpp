// Tests for agentdesk::ui::layout: reading and writing layouts, tree edits, sizes, separator
// drags, floats and the click state kept beside the file.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "agentdesk/ui/layout.hpp"

#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

using namespace agentdesk::ui::layout;

namespace {

Node node(const char* text) {
    std::string error;
    auto n = parse_text(text, &error);
    EXPECT_TRUE(n.has_value()) << error;
    return n ? *n : Node{};
}

Node node(const Json& j) {
    std::string error;
    auto n = parse(j, &error);
    EXPECT_TRUE(n.has_value()) << error;
    return n ? *n : Node{};
}

Node round_trip(const Node& n) {
    return node(Json::parse(text_of(n)));
}

Node term(const std::string& id) {
    return Node::block(id, Content{Content::Type::kTerminal, "t-" + id, {}, {}});
}

std::vector<std::string> ids(const std::vector<const Node*>& v) {
    std::vector<std::string> out;
    for (const Node* n : v)
        out.push_back(n->id);
    return out;
}

float w(const Node& n, const char* id) {
    const Node* x = n.find(id);
    return x != nullptr && x->size && !x->size->px ? x->size->value : 1.0f;
}

}  // namespace

TEST(Layout, FloatsSnapToTheNearestEdgeAndThird) {
    EXPECT_EQ(snap(10, 400, 1200, 800), std::pair(Edge::kLeft, Align::kCenter));
    EXPECT_EQ(snap(1190, 20, 1200, 800), std::pair(Edge::kRight, Align::kStart));
    EXPECT_EQ(snap(600, 5, 1200, 800), std::pair(Edge::kTop, Align::kCenter));
    EXPECT_EQ(snap(1000, 790, 1200, 800), std::pair(Edge::kBottom, Align::kEnd));
    EXPECT_EQ(snap(-50, 900, 1200, 800).second, Align::kEnd);
}

TEST(Layout, FloatsRoundTrip) {
    const Node n = node(R"({ "id": "root", "type": "block", "content": {"agent": "focused"},
      "floating": [ { "id": "team", "content": {"mod": "team"}, "edge": "left" } ] })");
    EXPECT_EQ(n.content.type, Content::Type::kAgent);
    EXPECT_EQ(n.content.value, "focused");
    EXPECT_EQ(n.floating[0].edge, Edge::kLeft);
    EXPECT_EQ(n.floating[0].align, Align::kCenter);
    EXPECT_EQ(round_trip(n), n);
}

TEST(Layout, NodesCarryLooksBackgroundsPaddingAndShadows) {
    const Node n = node(
        R"({ "id": "root", "type": "column", "theme": "ink", "font": "Berkeley Mono", "font_size": 13, "font_weight": 300,
      "terminal_opacity": 0.85, "padding": [16, 12], "shadow": "lg",
      "background": { "gradient": ["#123a6e", "#2a6db3"], "angle": 160 },
      "children": [ { "id": "a", "type": "block", "content": { "mod": "x" }, "background": "surface", "padding": 8 },
                    { "id": "b", "type": "block", "content": { "mod": "y" }, "background": { "image": "bg/sky.jpg" } } ] })");
    EXPECT_EQ(n.look["theme"], "ink");
    EXPECT_EQ(n.font_size(), 13.0f);
    EXPECT_FLOAT_EQ(n.look["terminal_opacity"].get<float>(), 0.85f);
    EXPECT_EQ(n.look["font_weight"], 300);
    EXPECT_EQ(padding_of(n), std::pair(16.0f, 12.0f));
    EXPECT_EQ(n.background["angle"], 160);
    EXPECT_EQ(n.find("a")->background, "surface");
    EXPECT_EQ(n.find("b")->background["fit"], "cover");
    EXPECT_EQ(round_trip(n), n);
}

TEST(Layout, SuggestedThemesAreReadAndWrittenBack) {
    const Node n = node(R"({ "id": "root", "type": "block", "content": { "mod": "x" },
      "suggested_themes": { "dark": "mac-dusk", "light": "mac-dawn" } })");
    EXPECT_EQ(n.look["suggested_themes"]["dark"], "mac-dusk");
    EXPECT_EQ(n.look["suggested_themes"]["light"], "mac-dawn");
    EXPECT_FALSE(n.look.contains("theme"));  // a suggestion, not a forced theme
    EXPECT_EQ(round_trip(n), n);
    std::string error;
    EXPECT_FALSE(parse_text(R"({ "id": "root", "type": "block", "content": { "mod": "x" },
      "suggested_themes": "mac-dusk" })",
                            &error));
    EXPECT_NE(error.find("suggested_themes"), std::string::npos);
}

TEST(Layout, TabBlocksSayHowTheirPanesAreFramed) {
    const Node n = node(R"({ "id": "main", "type": "block", "content": { "tab": "active",
      "pane": { "header": { "content": { "mod": "pane" }, "height": 28, "reveal": "hover", "position": "bottom" },
                "radius": 10, "gap": 8, "lone": true, "border": { "width": 1, "color": "border" } } } })");
    ASSERT_EQ(n.content.type, Content::Type::kTab);
    ASSERT_TRUE(n.content.pane.has_value());
    const PaneFrame& f = *n.content.pane;
    EXPECT_TRUE(f.header->hover() && f.header->bottom() && f.lone);
    EXPECT_EQ(f.radius, 10.0f);
    EXPECT_EQ(f.gap, 8.0f);
    EXPECT_EQ(f.header->height, 28.0f);
    EXPECT_EQ(f.header->mod(), "pane");
    EXPECT_EQ(round_trip(n), n);
}

TEST(Layout, DocumentsParseWithTypesSizesAndLooks) {
    const Node n = node(
        R"({ "id": "root", "type": "row", "divider": {"width": 1, "color": "border"}, "window_blur": true,
      "children": [
        { "id": "sidebar", "type": "column", "size": {"px": 260},
          "children": [ { "id": "spaces", "type": "block", "content": {"mod": "spaces"} },
                        { "id": "agents", "type": "block", "content": {"mod": "agents"} } ] },
        { "id": "main", "type": "block", "content": {"tab": "active"} } ] })");
    EXPECT_EQ(n.find("sidebar")->size, Span::pixels(260));
    EXPECT_TRUE(n.window_blur && !n.find("sidebar")->window_blur);
    EXPECT_EQ(n.find("main")->content.type, Content::Type::kTab);
    EXPECT_FALSE(n.find("main")->content.pane.has_value());
    EXPECT_EQ(round_trip(n), n);
}

TEST(Layout, SplitsReuseASameAxisParentAndWrapOtherwise) {
    Node n = Node::stack("tab", {term("a")});
    EXPECT_TRUE(n.split("a", Axis::kRow, term("b"), "r1"));
    EXPECT_EQ(n.find("r1")->children.size(), 2u);
    EXPECT_TRUE(n.split("b", Axis::kRow, term("c"), "unused"));
    EXPECT_EQ(n.find("r1")->children.size(), 3u);
    EXPECT_TRUE(n.split("c", Axis::kColumn, term("d"), "c1"));
    EXPECT_EQ(n.find("c1")->children[0].id, "c");
    EXPECT_EQ(n.find("c1")->children[1].id, "d");
    EXPECT_EQ(ids(n.terminals()), (std::vector<std::string>{"a", "b", "c", "d"}));
}

TEST(Layout, ClosingASplitGivesItsShareBackToThePaneItCameFrom) {
    Node a = term("a");
    a.size = Span::weight(0.7f);
    Node b = term("b");
    b.size = Span::weight(0.3f);
    Node n = Node::container("tab", Axis::kRow, {a, b});
    n.split("b", Axis::kRow, term("c"), "unused");
    n.remove("c");
    EXPECT_FLOAT_EQ(w(n, "a"), 0.7f);
    EXPECT_FLOAT_EQ(w(n, "b"), 0.3f);
    n.split("a", Axis::kRow, term("d"), "unused");
    n.remove("a");
    EXPECT_FLOAT_EQ(w(n, "d"), 0.7f);
    EXPECT_FLOAT_EQ(w(n, "b"), 0.3f);
}

TEST(Layout, RemovingCollapsesSingleChildrenButKeepsStacksAndTheRoot) {
    Node tab = Node::container("t1", Axis::kRow, {term("a"), term("b")});
    Node n = Node::stack("tabs", {tab, term("solo")}, "solo");
    n.remove("b");
    EXPECT_EQ(n.children[0].id, "t1");
    EXPECT_EQ(n.children[0].children.size(), 1u);
    n.remove("solo");
    EXPECT_EQ(n.active_child()->id, "t1");
    n.split("a", Axis::kColumn, term("c"), "c1");
    n.remove("c");
    EXPECT_EQ(n.find("t1")->children[0].id, "a");
    n.remove("t1");
    EXPECT_EQ(n.kind, Kind::kStack);
    EXPECT_TRUE(n.children.empty());
    Node root = Node::container("ctx", Axis::kRow, {term("x"), term("y")});
    root.remove("y");
    EXPECT_EQ(root.id, "ctx");
    EXPECT_EQ(root.children.size(), 1u);
}

TEST(Layout, RemovingACompositionSlotKeepsAuthoredContainers) {
    Node panel = Node::container("panel", Axis::kColumn, {term("panel-body")});
    panel.hidden = true;
    panel.size = Span::pixels(368);
    Node root = Node::container("work-body", Axis::kRow, {term("coordinator"), term("main"), panel});
    EXPECT_TRUE(root.remove_slot("coordinator").has_value());
    EXPECT_EQ(root.find("coordinator"), nullptr);
    EXPECT_EQ(*root.find("panel"), panel);
    EXPECT_FALSE(root.remove_slot("missing").has_value());
}

TEST(Layout, StacksSelectAndHiddenChildrenFallBack) {
    Node n = Node::stack("s", {term("a"), term("b")});
    EXPECT_TRUE(n.select("s", "b"));
    EXPECT_EQ(ids(n.visible_blocks()), (std::vector<std::string>{"b"}));
    n.find("b")->hidden = true;
    EXPECT_EQ(n.active_child()->id, "a");
    EXPECT_FALSE(n.select("s", "zzz"));
}

TEST(Layout, SwappingKeepsSizesWithPlaces) {
    Node n = Node::container("r", Axis::kRow, {term("a"), term("b"), term("c")});
    n.find("a")->size = Span::pixels(100);
    EXPECT_TRUE(n.swap("a", "c"));
    EXPECT_EQ(n.children[0].id, "c");
    EXPECT_EQ(n.children[1].id, "b");
    EXPECT_EQ(n.children[2].id, "a");
    EXPECT_EQ(n.children[0].size, Span::pixels(100));
}

TEST(Layout, MistakesSayWhatAndWhere) {
    std::string error;
    EXPECT_FALSE(parse_text(R"({"id": "root", "type": "grid", "children": []})", &error));
    EXPECT_NE(error.find("grid"), std::string::npos);
    EXPECT_FALSE(parse_text(R"({"id": "root", "type": "row"})", &error));
    EXPECT_NE(error.find("children"), std::string::npos);
    EXPECT_FALSE(parse_text(
        R"({"id": "r", "type": "row", "children": [{"id": "x", "type": "block", "content": {"nope": 1}}]})",
        &error));
    EXPECT_NE(error.find("children[0](x)"), std::string::npos);
    EXPECT_FALSE(parse_text(R"({"type": "block", "content": {"mod": "a"}, "size": {"pct": 3}})", &error));
    EXPECT_FALSE(parse_text("{", &error));
    // `context` is the old (0.1.0) word for `tab`; unknown keys are left out.
    const Node n = node(R"({"type": "block", "content": {"context": "active"}, "colour": "red"})");
    EXPECT_EQ(n.content.type, Content::Type::kTab);
    EXPECT_FALSE(to_json(n).contains("colour"));
}

// The shipped layouts parse, name every node, and match the layout files they are built from.
TEST(Layout, ShippedLayoutsParseAndNameEveryNode) {
    for (const auto& name : builtin_names()) {
        std::string error;
        const auto n = parse_text(builtin_text(name), &error);
        ASSERT_TRUE(n.has_value()) << name << ": " << error;
        for (const Node* x : n->walk())
            EXPECT_FALSE(x->id.empty()) << name;
        // herdr-app's files are kept in third_party; AgentDesk's own (mac, mission, review) beside
        // the engine.
        const bool own = name == "mac" || name == "mission" || name == "review";
        const std::string dir = own ? AGENTDESK_OWN_LAYOUTS_DIR : AGENTDESK_PRESET_LAYOUTS_DIR;
        std::ifstream in(dir + "/" + std::string(builtin_file_name(name)) + ".json", std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        std::string file = ss.str();
        std::erase(file, '\r');  // a checkout may have CRLF; the compiled-in text is LF
        EXPECT_EQ(file, builtin_text(name)) << name;
    }
    const Node main = builtin("workspace");
    EXPECT_EQ(main.find("sidebar")->size, Span::pixels(268));
    EXPECT_TRUE(main.find("panel")->hidden);
    EXPECT_EQ(main.find("panel-body")->active, "files");
    EXPECT_EQ(builtin_names(),
              (std::vector<std::string>{"workspace", "terminals", "mac", "mission", "review"}));
    EXPECT_EQ(builtin("nope"), main);
    EXPECT_EQ(builtin("win98"), main);  // a name no longer built in falls back to the workspace
}

TEST(Layout, OldAgentDeskFilesReadAsTheShippedLayoutOfTheirStyle) {
    const Json old = Json::parse(
        R"({"name": "mine", "sidebar": false, "status_bar": false, "theme": "ember", "font_size": 99})");
    ASSERT_TRUE(is_legacy(old));
    EXPECT_FALSE(is_legacy(to_json(builtin("workspace"))));
    const Node n = from_legacy(old);
    EXPECT_TRUE(n.find("sidebar")->hidden);
    EXPECT_TRUE(n.find("status")->hidden);
    EXPECT_FALSE(n.find("tabs")->hidden);
    EXPECT_EQ(n.look["theme"], "ember");
    EXPECT_EQ(n.font_size(), 72.0f);
    EXPECT_EQ(from_legacy(Json::parse(R"({"style": "win98"})")), builtin("workspace"));
    EXPECT_NE(from_legacy(Json::parse(R"({"style": "terminals"})")).find("tabs"), nullptr);
    EXPECT_EQ(from_legacy(Json::parse(R"({"style": "terminals"})")).find("tabs")->mod(), "terminals-bar");
}

TEST(LayoutSize, PxFirstThenWeightsShareWhatIsLeftWithinMinAndMax) {
    Node a = term("a");
    a.size = Span::pixels(100);
    Node b = term("b");
    Node c = term("c");
    c.size = Span::weight(3);
    auto s = shares({&a, &b, &c}, 500, 1);
    EXPECT_FLOAT_EQ(s[0], 100);
    EXPECT_FLOAT_EQ(s[1], 100);
    EXPECT_FLOAT_EQ(s[2], 300);
    // b held at its max: c keeps its grow factor of the original siblings (3/4 of the 400 px
    // free), as flexbox gives it, not all of what b left.
    b.max = 50.0f;
    s = shares({&a, &b, &c}, 500, 1);
    EXPECT_FLOAT_EQ(s[1], 50);
    EXPECT_FLOAT_EQ(s[2], 300);
    // b held at its min.
    b.max.reset();
    b.min = 300.0f;
    s = shares({&a, &b, &c}, 500, 1);
    EXPECT_FLOAT_EQ(s[1], 300);
    EXPECT_FLOAT_EQ(s[2], 100);
    // Zoom scales px and limits.
    s = shares({&a}, 500, 2);
    EXPECT_FLOAT_EQ(s[0], 200);
}

// The window as main.json lays it out (the sizes agent-ui drew before this engine).
TEST(LayoutSize, MainPlacesSidebarTabsBodyAndStatus) {
    Node main = builtin("workspace");
    const Placement p = place(main, Rect{0, 0, 1400, 900}, 1.0f);
    EXPECT_EQ(p.at("sidebar")->outer, (Rect{0, 0, 268, 900}));
    EXPECT_EQ(p.at("brand")->outer, (Rect{0, 0, 268, 46}));
    EXPECT_EQ(p.at("sidebar-footer")->outer, (Rect{0, 831, 268, 69}));
    EXPECT_EQ(p.at("spaces")->outer, (Rect{0, 46, 268, 392.5f}));
    EXPECT_EQ(p.at("work")->outer, (Rect{269, 0, 1131, 900}));
    EXPECT_EQ(p.at("tabs")->outer, (Rect{269, 0, 1131, 42}));
    EXPECT_EQ(p.at("status")->outer, (Rect{269, 872, 1131, 28}));
    EXPECT_EQ(p.at("main")->outer, (Rect{277, 50, 1115, 814}));
    EXPECT_EQ(p.at("panel"), nullptr);
    // The panel shown: 368 px beside the panes, 8 px apart.
    main.find("panel")->hidden = false;
    const Placement q = place(main, Rect{0, 0, 1400, 900}, 1.0f);
    EXPECT_EQ(q.at("main")->outer, (Rect{277, 50, 739, 814}));
    EXPECT_EQ(q.at("panel")->outer, (Rect{1024, 50, 368, 814}));
    EXPECT_EQ(q.at("files")->outer, q.at("panel")->outer);
    // Separators: the 1 px divider after the sidebar drags; inside the sidebar column they don't.
    bool sidebar_edge = false;
    for (const auto& s : q.separators) {
        if (s.container == "root") {
            sidebar_edge = true;
            EXPECT_EQ(s.box, (Rect{268, 0, 1, 900}));
            EXPECT_EQ(s.hit, (Rect{265.5f, 0, 6, 900}));
            EXPECT_TRUE(s.resizable);
        }
        if (s.container == "sidebar" || s.container == "work")
            EXPECT_FALSE(s.resizable);
    }
    EXPECT_TRUE(sidebar_edge);
}

TEST(LayoutSize, StacksGiveTheirHeaderItsStripAndTheBodyTheRest) {
    const Node n = node(
        R"({"id": "s", "type": "stack", "active": "b", "header": {"content": "tabs", "position": "bottom"},
        "children": [{"id": "a", "type": "block", "content": {"mod": "x"}}, {"id": "b", "type": "block", "content": {"mod": "y"}}]})");
    const Placement p = place(n, Rect{0, 0, 200, 100}, 1.0f);
    EXPECT_EQ(p.headers.at("s"), (Rect{0, 70, 200, 30}));
    EXPECT_EQ(p.at("b")->outer, (Rect{0, 0, 200, 70}));
    EXPECT_EQ(p.at("a"), nullptr);
}

TEST(LayoutSize, BordersAndPaddingInsetTheInside) {
    const Node n = node(R"({"id": "r", "type": "row", "padding": [8, 10], "gap": 4, "border": {"width": 2},
        "children": [{"id": "a", "type": "block", "content": {"mod": "x"}}, {"id": "b", "type": "block", "content": {"mod": "y"}}]})");
    const Placement p = place(n, Rect{0, 0, 200, 100}, 1.0f);
    EXPECT_EQ(p.at("r")->padded, (Rect{2, 2, 196, 96}));
    EXPECT_EQ(p.at("r")->inner, (Rect{12, 10, 176, 80}));
    EXPECT_EQ(p.at("a")->outer, (Rect{12, 10, 86, 80}));
    EXPECT_EQ(p.at("b")->outer, (Rect{102, 10, 86, 80}));
}

TEST(LayoutDrag, APxSideTakesTheDragAndWeightsSplitTheirSum) {
    Node main = builtin("workspace");
    Placement p = place(main, Rect{0, 0, 1400, 900}, 1.0f);
    EXPECT_TRUE(drag(main, "root", 0, Axis::kRow, p, 300, 1.0f));
    EXPECT_EQ(main.find("sidebar")->size, Span::pixels(300));
    // Its max (480) holds.
    p = place(main, Rect{0, 0, 1400, 900}, 1.0f);
    drag(main, "root", 0, Axis::kRow, p, 900, 1.0f);
    EXPECT_EQ(main.find("sidebar")->size, Span::pixels(480));
    // Two weights: the boundary moves, their sum stays.
    p = place(main, Rect{0, 0, 1400, 900}, 1.0f);
    const Placed* spaces = p.at("spaces");
    EXPECT_TRUE(drag(main, "sidebar-lists", 0, Axis::kColumn, p, spaces->outer.y + 200, 1.0f));
    EXPECT_NEAR(w(main, "spaces") + w(main, "agents"), 2.0f, 1e-5);
    EXPECT_NEAR(w(main, "spaces"), 2.0f * 200 / 785, 1e-5);
    // Not resizable: nothing moves.
    EXPECT_FALSE(drag(main, "work", 0, Axis::kColumn, p, 500, 1.0f));
}

// Dragging squeezed layout blocks never crashes or leaves sizes out of range.
TEST(LayoutDrag, DraggingSqueezedLayoutBlocksNeverCrashes) {
    auto block = [](const char* id, float h) {
        return Json{
            {"id", id}, {"type", "block"}, {"size", {{"px", h}}}, {"content", {{"mod", "drag-probe"}}}};
    };
    const Json layout = {
        {"id", "root"},
        {"type", "row"},
        {"children",
         Json::array(
             {{{"id", "sidebar"},
               {"type", "column"},
               {"size", {{"px", 268}}},
               {"resizable", false},
               {"children",
                Json::array(
                    {block("brand", 46),
                     block("button", 52),
                     {{"id", "lists"},
                      {"type", "column"},
                      {"divider", {{"width", 1}, {"color", "border"}}},
                      {"children",
                       Json::array({block("heading", 34),
                                    {{"id", "stack"},
                                     {"type", "stack"},
                                     {"children", Json::array({{{"id", "list"},
                                                                {"type", "block"},
                                                                {"content", {{"mod", "drag-probe"}}}}})}},
                                    block("zero1", 0),
                                    block("zero2", 0),
                                    block("zero3", 0),
                                    block("zero4", 0),
                                    block("zero5", 0)})}},
                     block("pendientes", 200),
                     block("vistas", 46)})}}})}};
    for (const float height : {800.0f, 380.0f, 344.0f, 300.0f}) {
        for (const auto& [container, count] : {std::pair{"sidebar", 4}, std::pair{"lists", 6}}) {
            for (int index = 0; index < count; ++index) {
                for (const float y : {-2000.0f, 0.0f, 2000.0f}) {
                    Node root = node(layout);
                    const Placement p = place(root, Rect{0, 0, 900, height}, 1.0f);
                    const Node* parent = root.find(container);
                    const auto& a = parent->children[static_cast<std::size_t>(index)];
                    const auto& b = parent->children[static_cast<std::size_t>(index) + 1];
                    const float total = p.at(a.id)->outer.h + p.at(b.id)->outer.h;
                    const Json before = to_json(*parent);
                    drag(root, container, index, Axis::kColumn, p, y, 1.0f);
                    const Node* after = root.find(container);
                    if (std::string(container) == "sidebar" || total <= 48.0f) {
                        EXPECT_EQ(to_json(*after), before) << container << index << " " << height;
                    } else {
                        for (int k = index; k <= index + 1; ++k) {
                            const auto& s = after->children[static_cast<std::size_t>(k)].size;
                            if (s && s->px) {
                                EXPECT_GE(s->value, 0.0f);
                                EXPECT_LE(s->value, total);
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST(LayoutFloat, HomeSitsMarginInFromItsEdge) {
    const Rect owner{100, 50, 800, 600};
    EXPECT_EQ(float_home(owner, 80, 100, Edge::kLeft, Align::kEnd, 12), (std::array<float, 2>{112, 538}));
    EXPECT_EQ(float_home(owner, 80, 100, Edge::kTop, Align::kCenter, 12), (std::array<float, 2>{460, 62}));
    EXPECT_EQ(float_home(owner, 80, 100, Edge::kRight, Align::kStart, 12), (std::array<float, 2>{808, 62}));
}

TEST(LayoutSync, BackgroundFontSizeAndPaneFrameSizesAreClicksNotStructureEdits) {
    const Node file = node(Json::parse(R"({
        "id":"root", "type":"column", "font_size":16, "background":"#008080",
        "children":[{"id":"main", "type":"block", "content":{"tab":"active","pane":{
            "header":{"content":{"mod":"terminals-pane"},"height":18},
            "border":{"width":3,"color":"#c0c0c0"}, "focus":{"width":3,"color":"#c0c0c0"}
        }}}]})"));
    Node now = file;
    now.background = "#123456";
    now.look["font_size"] = 20.0;
    PaneFrame& p = *now.find("main")->content.pane;
    p.header->height = 25.0f;
    p.border->width = 4.0f;
    p.focus->width = 4.0f;
    EXPECT_EQ(without_clicks(file, now), file);
    const State state = State::between(file, now);
    Node shown = file;
    state.apply(shown);
    EXPECT_EQ(shown, now);
    shown.find("main")->content = Content{Content::Type::kMod, "other", {}, {}};
    EXPECT_NE(without_clicks(file, shown), file);
}

TEST(LayoutSync, ClicksAreKeptApartAndAnEditWins) {
    const Node file = node(R"({ "id": "root", "type": "row", "children": [
        { "id": "side", "type": "block", "size": { "px": 240 }, "content": { "mod": "spaces" } },
        { "id": "panel", "type": "stack", "hidden": true, "active": "a", "children": [
            { "id": "a", "type": "block", "content": { "mod": "files" } },
            { "id": "b", "type": "block", "content": { "mod": "changes" } } ] } ] })");
    Node now = file;
    now.find("side")->size = Span::pixels(300);
    now.find("panel")->hidden = false;
    EXPECT_TRUE(now.select("panel", "b"));
    const State s = State::between(file, now);
    EXPECT_EQ(s.nodes.size(), 2u);
    EXPECT_EQ(without_clicks(file, now), file);
    Node again = file;
    s.apply(again);
    EXPECT_EQ(again, now);
    Node edited = file;
    edited.find("side")->size = Span::pixels(200);
    State s2 = s;
    s2.forget_edited(file, edited);
    EXPECT_EQ(s2.nodes.count("side"), 0u);
    EXPECT_EQ(s2.nodes.count("panel"), 1u);
    // The state file round-trips.
    EXPECT_EQ(State::from_json(s.to_json()), s);
}

TEST(LayoutSync, AnEditMadeWhileClosedBeatsAnOlderClick) {
    const Node file = node(R"({ "id": "root", "type": "row", "children": [
        { "id": "side", "type": "block", "size": { "px": 240 }, "content": { "mod": "spaces" } },
        { "id": "main", "type": "block", "size": { "px": 500 }, "content": { "tab": "active" } } ] })");
    Node now = file;
    now.find("side")->size = Span::pixels(300);
    now.find("main")->size = Span::pixels(600);
    State s = State::between(file, now);
    s.file = to_json(file);
    s = State::from_json(s.to_json());
    Node edited = file;
    edited.find("side")->size = Span::pixels(200);
    s.forget_since_saved(edited);
    Node shown = edited;
    s.apply(shown);
    EXPECT_EQ(shown.find("side")->size, Span::pixels(200));
    EXPECT_EQ(shown.find("main")->size, Span::pixels(600));
}

TEST(LayoutSync, FloatsAndThemeAreClicksToo) {
    const Node file = node(R"({ "id": "root", "type": "row", "children": [
        { "id": "main", "type": "block", "content": { "tab": "active" },
          "floating": [ { "id": "helper", "content": { "mod": "team" }, "edge": "left", "align": "end" } ] } ] })");
    Node now = file;
    now.find_float("helper")->edge = Edge::kRight;
    now.find_float("helper")->align = Align::kStart;
    now.look["theme"] = "midnight";
    const State s = State::between(file, now);
    EXPECT_EQ(s.floats.at("helper").edge, Edge::kRight);
    EXPECT_FALSE(s.floats.at("helper").hidden.has_value());
    EXPECT_EQ(s.theme, "midnight");
    Node shown = file;
    State::from_json(s.to_json()).apply(shown);
    EXPECT_EQ(shown, now);
    EXPECT_EQ(without_clicks(file, now), file);
}

TEST(LayoutText, WholeNumbersAreWrittenWithoutAFraction) {
    const std::string t = text_of(builtin("workspace"));
    EXPECT_NE(t.find("\"px\": 268\n"), std::string::npos);
    EXPECT_EQ(t.find("268.0"), std::string::npos);
    EXPECT_EQ(t.back(), '\n');
}
