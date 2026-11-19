// AgentDesk 98 games: Mines and Solitaire.
#include "win98_internal.hpp"

namespace agentdesk::desktop {
namespace detail::w98 {

// ---------------------------------------------------------------- Mines (mines.js)

k98::Mines::Options mines_options(App::State& s) {
    k98::Mines::Options o;
    const Json& j = shared(s, "win98.mines");
    if (j.is_object()) {
        // win98.json is the person's file: a value of another type keeps the default.
        o.level = safe::string_or(j, "level", o.level);
        o.marks = safe::bool_or(j, "marks", o.marks);
        o.color = safe::bool_or(j, "color", o.color);
        o.sound = safe::bool_or(j, "sound", o.sound);
        if (const auto b = j.find("best"); b != j.end() && b->is_object())
            for (const auto& [k, v] : b->items())
                if (const auto secs = safe::int_in(v, 0, 999))  // mines.js: the clock stops at 999
                    o.best[k] = *secs;
    }
    return o;
}

namespace {
void save_mines(App::State& s) {
    const auto& o = st(s).mines->options();
    set_shared(s, "win98.mines",
               Json{{"level", o.level},
                    {"marks", o.marks},
                    {"color", o.color},
                    {"sound", o.sound},
                    {"best", o.best}});
}

void mines_request(App::State& s, const std::string& cmd) {
    W98& w = st(s);
    if (!w.mines)
        return;
    if (cmd == "exit") {
        set_app(s, "mines", "");
        w.mines->reset();
        return;
    }
    if (cmd == "best") {
        open_dialog(s, message("Fastest Mine Sweepers", w.mines->best_text()));
        return;
    }
    w.mines->request(cmd);
    if (std::exchange(w.mines->saved, false))
        save_mines(s);
}

// mines.js 125-136.
MenuItems mines_menu(App::State& s, bool help) {
    if (help)
        return {{"&Contents", "", "", "", [&s] { run_command(s, "app.help"); }},
                {.sep = true},
                {"&About Mines...", "", "", "", [&s] {
                     open_dialog(s, message("About Mines",
                                            "Mines\nAgentDesk 98\n\nFind all the mines without uncovering "
                                            "one.\nRight-click to mark a mine. "
                                            "Double-click a number to clear its neighbors."));
                 }}};
    const auto& o = st(s).mines->options();
    auto item = [&s](const std::string& name, const std::string& cmd) {
        return MenuItem{name, "", "", "", [&s, cmd] { mines_request(s, cmd); }};
    };
    MenuItems m;
    MenuItem n = item("&New", "new");
    n.shortcut = "F2";
    m.push_back(n);
    m.push_back({.sep = true});
    for (const char* level : {"beginner", "intermediate", "expert"}) {
        std::string label = std::string("&") + static_cast<char>(std::toupper(level[0])) + (level + 1);
        MenuItem it = item(label, level);
        it.radio = o.level == level;
        m.push_back(it);
    }
    m.push_back({.sep = true});
    MenuItem marks = item("&Marks (?)", "marks");
    marks.checked = o.marks;
    MenuItem col = item("&Color", "color");
    col.checked = o.color;
    MenuItem snd = item("&Sound", "sound");
    snd.checked = o.sound;
    m.push_back(marks);
    m.push_back(col);
    m.push_back(snd);
    m.push_back({.sep = true});
    m.push_back(item("&Best Times...", "best"));
    m.push_back({.sep = true});
    m.push_back(item("E&xit", "exit"));
    return m;
}

// kit.js 295-308 win(): the frame, the title bar, the menu bar, the body. Returns the inside box.
Box win_frame(App::State& s, ImDrawList* dl, ImVec2 at, float width, float height) {
    return bevel(s, dl, box(at, ImVec2(width, height)), Bevel::kWindow, rgb(C::kFace)).inset(one(s));
}

// mines.js 15-22.
void counter(App::State& s, ImDrawList* dl, ImVec2 at, int value) {
    const std::string chars = k98::counter_chars(value);
    const float w = U(s, 11) * 3 + U(s, 2) * 2 + U(s, 1) * 2 + 2 * one(s);
    const Box in = bevel(s, dl, box(at, ImVec2(w, U(s, 23) + 2 * one(s))), Bevel::kWell, rgb(0x000000))
                       .inset(0, U(s, 1), 0, U(s, 1));
    float x = in.x0;
    for (const char ch : chars) {
        const auto lit = k98::segments_lit(ch);
        for (int i = 0; i < 7; ++i) {
            const auto& r = k98::segment_rects()[static_cast<std::size_t>(i)];
            fill(dl,
                 box(ImVec2(x + U(s, static_cast<float>(r[0])), in.y0 + U(s, static_cast<float>(r[1]))),
                     ImVec2(U(s, static_cast<float>(r[2])), U(s, static_cast<float>(r[3])))),
                 lit[static_cast<std::size_t>(i)] ? rgb(0xff0000) : rgb(0x650000));
        }
        x += U(s, 11) + U(s, 2);
    }
}

void mines_art(App::State& s, ImDrawList* dl, const std::string& name, float size, ImVec2 at) {
    image(s, dl, root_dir() / L"icons" / win::to_wide("mines-" + name + ".svg"),
          box(at, ImVec2(U(s, size), U(s, size))));
}

}  // namespace

ImVec2 render_mines(App::State& s, Window& win, ImDrawList* dl, ImVec2 at) {
    W98& w = st(s);
    auto& m = *w.mines;
    const long long now = now_ms();
    if (m.tick(now))
        (void)0;
    const float width = U(s, static_cast<float>(m.cols() * 16 + 22));
    const float board_h = U(s, static_cast<float>(m.rows() * 16)) + 6 * one(s);
    const float body_h = U(s, 5) + U(s, 33) + 2 * one(s) + U(s, 6) + board_h + U(s, 5);
    const float height = 3 * one(s) + U(s, 18) + U(s, 1) + U(s, 18) + U(s, 1) + body_h + 3 * one(s);
    const Box in = win_frame(s, dl, at, width, height).inset(one(s));
    title_bar(
        s, dl, "##mines-title", Box{in.x0, in.y0, in.x1, in.y0 + U(s, 18)}, "Mines",
        [&s, dl](ImVec2 p) { icon(s, dl, "mines", 16, p); }, true,
        {{"min", [&s] { set_app(s, "mines", "min"); }, "Minimize"},
         {"close",
          [&s] {
              set_app(s, "mines", "");
              st(s).mines->reset();
          },
          "Close"}},
        false);
    // The menu bar (18): Game, Help; a label stays pressed (a well) while its menu is open.
    float mx = in.x0;
    const float my = in.y0 + U(s, 18) + U(s, 1);
    for (int i = 0; i < 2; ++i) {
        const char* name = i == 0 ? "&Game" : "&Help";
        const std::string owner = "mines-" + std::to_string(i);
        const bool open = shared_str(s, "win98.menu.owner") == owner;
        const float bw = text_w(s, k98::plain(name)) + U(s, 12) + (open ? 2 * one(s) : 0.0f);
        const Box mb = box(ImVec2(mx, my), ImVec2(bw, U(s, 18)));
        ImGui::PushID(i);
        const Pressed p = area("##mm", mb, true);
        ImGui::PopID();
        const Box li = open ? bevel(s, dl, mb, Bevel::kWell) : mb;
        label(s, dl, li.x0 + U(s, 6), li.y0 + (li.h() - U(s, 13)) * 0.5f, name, rgb(C::kText));
        if (p.clicked)
            open_menu(s, mines_menu(s, i == 1), ImVec2(mb.x0, mb.y1), false, owner);
        mx += bw;
    }
    // The body: p 5, gap 6: the panel (a well, 33 tall) and the board.
    const float bx = in.x0 + U(s, 5);
    float by = my + U(s, 18) + U(s, 1) + U(s, 5);
    const float bwid = in.x1 - U(s, 5) - bx;
    {
        const Box pb = box(ImVec2(bx, by), ImVec2(bwid, U(s, 33) + 2 * one(s)));
        const Box pi = bevel(s, dl, pb, Bevel::kWell).inset(0, U(s, 5), 0, U(s, 5));
        const float cw = U(s, 11) * 3 + U(s, 2) * 2 + U(s, 1) * 2 + 2 * one(s);
        const float ch = U(s, 23) + 2 * one(s);
        counter(s, dl, ImVec2(pi.x0, snap(pi.y0 + (pi.h() - ch) * 0.5f)), m.mines_left());
        counter(s, dl, ImVec2(pi.x1 - cw, snap(pi.y0 + (pi.h() - ch) * 0.5f)), m.seconds());
        ButtonOpts o;
        o.pad_x = o.pad_y = 0;
        const Pressed f =
            button(s, dl, "##mines-face",
                   box(ImVec2(pi.x0 + (pi.w() - U(s, 26)) * 0.5f, pi.y0 + (pi.h() - U(s, 26)) * 0.5f),
                       ImVec2(U(s, 26), U(s, 26))),
                   o);
        mines_art(s, dl, "face-" + m.face(), 17,
                  ImVec2(snap(f.content.x0 + (f.content.w() - U(s, 17)) * 0.5f),
                         snap(f.content.y0 + (f.content.h() - U(s, 17)) * 0.5f)));
        if (f.clicked)
            m.reset();
        by = pb.y1 + U(s, 6);
    }
    const Box board =
        box(ImVec2(bx, by), ImVec2(U(s, static_cast<float>(m.cols() * 16)) + 6 * one(s), board_h));
    const Box cells = bevel(s, dl, board, Bevel::kPressed, rgb(C::kShadow)).inset(one(s));
    const bool lost = m.state() == k98::Mines::State::kLost;
    const bool playing = m.state() == k98::Mines::State::kReady || m.state() == k98::Mines::State::kPlaying;
    int hovered = -1;
    int active = -1;
    std::string sound;
    for (int i = 0; i < m.cols() * m.rows(); ++i) {
        const int cx = i % m.cols(), cy = i / m.cols();
        const Box cb = box(ImVec2(cells.x0 + U(s, static_cast<float>(cx * 16)),
                                  cells.y0 + U(s, static_cast<float>(cy * 16))),
                           ImVec2(U(s, 16), U(s, 16)));
        ImGui::PushID(i);
        const Pressed p = area("##cell", cb);
        ImGui::PopID();
        if (p.hovered)
            hovered = i;
        if (p.held)
            active = i;
        const auto& c = m.cells()[static_cast<std::size_t>(i)];
        if (p.clicked) {
            if (c.open && p.double_click)
                sound = m.chord(i, now);
            else
                sound = m.reveal(i, now);
            m.held = -1;
        }
        if (p.right)
            m.mark(i);
    }
    // mouse down held = the cell (left or middle), cleared when the pointer leaves it.
    if (active >= 0 && playing)
        m.held = active == hovered ? active : -1;
    else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) && hovered >= 0)
        m.held = hovered;
    else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        m.held = active >= 0 ? m.held : -1;
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle) && hovered >= 0) {
        sound = m.chord(hovered, now);
        m.held = -1;
    }
    for (int i = 0; i < m.cols() * m.rows(); ++i) {
        const auto& c = m.cells()[static_cast<std::size_t>(i)];
        const int cx = i % m.cols(), cy = i / m.cols();
        const Box cb = box(ImVec2(cells.x0 + U(s, static_cast<float>(cx * 16)),
                                  cells.y0 + U(s, static_cast<float>(cy * 16))),
                           ImVec2(U(s, 16), U(s, 16)));
        const bool exposed = c.open || (lost && c.mine && c.mark != 1);
        const bool down = m.held == i && c.mark != 1 && !exposed;
        fill(dl, cb, i == m.boom() ? rgb(0xff0000) : rgb(C::kFace));
        Box content;
        if (exposed || down) {
            fill(dl, Box{cb.x0, cb.y0, cb.x1, cb.y0 + one(s)}, rgb(C::kShadow));
            fill(dl, Box{cb.x0, cb.y0, cb.x0 + one(s), cb.y1}, rgb(C::kShadow));
            content = cb.inset(one(s), 0, 0, one(s));
        } else {
            const float t = 2 * one(s);
            fill(dl, Box{cb.x0, cb.y0, cb.x1, cb.y0 + t}, rgb(C::kHi));
            fill(dl, Box{cb.x0, cb.y0, cb.x0 + t, cb.y1}, rgb(C::kHi));
            fill(dl, Box{cb.x1 - t, cb.y0 + t, cb.x1, cb.y1}, rgb(C::kShadow));
            fill(dl, Box{cb.x0 + t, cb.y1 - t, cb.x1, cb.y1}, rgb(C::kShadow));
            content = cb.inset(t, 0, 0, t);
        }
        auto center_art = [&](const std::string& name) {
            mines_art(s, dl, name, 16,
                      ImVec2(snap(content.x0 + (content.w() - U(s, 16)) * 0.5f),
                             snap(content.y0 + (content.h() - U(s, 16)) * 0.5f)));
        };
        if (lost && c.mark == 1 && !c.mine) {
            center_art("wrong");
        } else if (c.mark == 1) {
            center_art("flag");
        } else if (exposed && c.mine) {
            center_art("mine");
        } else if (!exposed && c.mark == 2) {
            const float tw = text_w(s, "?", true);
            label(s, dl, content.x0 + (content.w() - tw) * 0.5f, content.y0 + (content.h() - U(s, 13)) * 0.5f,
                  "?", rgb(C::kText), true, 11, 13, false);
        } else if (c.open && c.n != 0) {
            const std::string t = std::to_string(c.n);
            const float tw = w.arial->CalcTextSizeA(U(s, 14), FLT_MAX, 0, t.c_str()).x;
            label(s, dl, content.x0 + (content.w() - tw) * 0.5f, content.y0 + (content.h() - U(s, 14)) * 0.5f,
                  t, m.options().color ? rgb(k98::mines_number_color(c.n)) : rgb(C::kText), true, 14, 14,
                  false, w.arial);
        }
    }
    area("##mines-own", Box{in.x0, my, in.x1, in.y1});  // own(): the menu bar and body are not for dragging
    if (!sound.empty())
        sfx(s, sound);
    if (std::exchange(m.saved, false))
        save_mines(s);
    (void)win;
    return ImVec2(width, snap(height));
}

namespace {
void sol_card(App::State& s, ImDrawList* dl, int c, bool up, bool selected, ImVec2 at, int deck) {
    const std::string file = up ? std::string(selected ? "select-" : "") + std::to_string(Sol::suit(c)) +
                                      "-" + std::to_string(Sol::rank(c))
                                : "back-" + std::to_string(deck);
    image(s, dl, root_dir() / L"img" / win::to_wide("sol-" + file + ".svg"),
          box(at, ImVec2(U(s, Sol::kCW), U(s, Sol::kCH))));
}

// The card back: 0 or 1 (the two the Deck dialog offers); anything else in win98.json is the first.
int sol_deck(App::State& s) {
    return safe::int_in(shared(s, "win98.sol.deck"), 0, 1).value_or(0);
}

void sol_command(App::State& s, const std::string& cmd) {
    W98& w = st(s);
    if (!w.sol)
        return;
    if (cmd == "options" || cmd == "deck") {
        auto win = std::make_unique<Window>();
        win->role = "sol-dialog";
        win->sol_dialog = cmd;
        win->modal = true;
        win->draw = w.sol->draw;
        win->scoring = w.sol->scoring;
        win->deck = sol_deck(s);
        open_window(s, std::move(win));
        return;
    }
    if (cmd == "exit") {
        set_app(s, "sol", "");
        return;
    }
    if (cmd == "help") {
        open_dialog(s, message("Solitaire Help",
                               "Build each suit from Ace to King in the four foundations. Build the columns "
                               "down in alternating "
                               "colors. Only a King can fill an empty column. Click or drag cards to move "
                               "them. Double-click a card "
                               "to send it home."));
        return;
    }
    if (cmd == "about") {
        open_dialog(s, message("About Solitaire",
                               "Solitaire\nAgentDesk 98\n\nKlondike. A little patience goes a long way."));
        return;
    }
    w.sol->command(cmd);
    w.sol_press.reset();
    w.sol_drag.reset();
}

}  // namespace

ImVec2 render_sol(App::State& s, Window& /*win*/, ImDrawList* dl, ImVec2 at) {
    W98& w = st(s);
    Sol& g = *w.sol;
    // sol.js 371-375: the options from shared; the clock runs while the window is open.
    const Json& jd = shared(s, "win98.sol.draw");
    const Json& js = shared(s, "win98.sol.scoring");
    if (const auto draw = safe::int_in(jd, 1, 3); draw == 1 || draw == 3)  // Draw One / Draw Three only
        g.draw = *draw;
    if (js.is_boolean())
        g.scoring = js.get<bool>();
    const int deck = sol_deck(s);
    const auto now = Clock::now();
    if (g.running && !g.won) {
        while (now - w.sol_tick >= std::chrono::seconds(1)) {
            if (w.sol_tick == Clock::time_point{}) {
                w.sol_tick = now;
                break;
            }
            w.sol_tick += std::chrono::seconds(1);
            g.tick_second();
        }
    } else {
        w.sol_tick = now;
    }
    if (g.animating) {
        while (now - w.sol_anim >= std::chrono::milliseconds(33)) {
            if (w.sol_anim == Clock::time_point{} || now - w.sol_anim > std::chrono::seconds(1)) {
                w.sol_anim = now;
                g.animate();
                break;
            }
            w.sol_anim += std::chrono::milliseconds(33);
            if (!g.animate())
                break;
        }
    }
    if (!g.sound.empty())
        sfx(s, std::exchange(g.sound, {}));
    const float o = one(s);
    const float width = U(s, 602);
    const float height =
        3 * o + U(s, 18) + U(s, 1) + U(s, 19) + U(s, 1) + U(s, Sol::kH) + U(s, 1) + U(s, 19) + 3 * o;
    const Box in = win_frame(s, dl, at, width, height).inset(o);
    title_bar(
        s, dl, "##sol-title", Box{in.x0, in.y0, in.x1, in.y0 + U(s, 18)}, "Solitaire",
        [&s, dl](ImVec2 p) { icon(s, dl, "sol", 16, p); }, true,
        {{"min", [&s] { set_app(s, "sol", "min"); }, "Minimize"},
         {"close", [&s] { set_app(s, "sol", ""); }, "Close"}},
        false);
    // The menu (19 tall, gap 2): Game, Help, pressed while open.
    float mx = in.x0;
    const float my = in.y0 + U(s, 18) + U(s, 1);
    for (const char* name : {"Game", "Help"}) {
        const bool open = w.sol_menu == name && shared_str(s, "win98.menu.owner") == "sol";
        const std::string lab = std::string("&") + name;
        const float bw = text_w(s, name) + U(s, 12) + (open ? 4 * o : 0.0f);
        const Box mb = box(ImVec2(mx, my), ImVec2(bw, U(s, 19)));
        ImGui::PushID(name);
        const Pressed p = area("##sm", mb, true);
        ImGui::PopID();
        const Box li = open ? bevel(s, dl, mb, Bevel::kPressed) : mb;
        label(s, dl, li.x0 + U(s, 6), li.y0 + U(s, 1) + (li.h() - U(s, 2) - U(s, 13)) * 0.5f, lab,
              rgb(C::kText));
        if (p.clicked) {
            w.sol_menu = name;
            MenuItems items;
            if (std::string(name) == "Game") {
                const std::pair<const char*, const char*> entries[] = {
                    {"&Deal", "deal"},          {nullptr, nullptr}, {"&Undo", "undo"}, {"&Deck...", "deck"},
                    {"&Options...", "options"}, {nullptr, nullptr}, {"E&xit", "exit"}};
                for (const auto& [l, c] : entries) {
                    if (l == nullptr) {
                        items.push_back({.sep = true});
                        continue;
                    }
                    const std::string cmd = c;
                    MenuItem it{l, "", "", "", [&s, cmd] { sol_command(s, cmd); }};
                    it.disabled = cmd == "undo" && !g.undo;
                    items.push_back(it);
                }
            } else {
                items = {{"&Contents", "", "", "", [&s] { sol_command(s, "help"); }},
                         {"&About Solitaire...", "", "", "", [&s] { sol_command(s, "about"); }}};
            }
            open_menu(s, std::move(items), ImVec2(mb.x0, mb.y1), false, "sol");
        }
        mx += bw + U(s, 2);
    }
    // The table: 596 x 355, green; positions in Win98 px from its corner.
    const Box table = box(ImVec2(in.x0, my + U(s, 19) + U(s, 1)), ImVec2(U(s, Sol::kW), U(s, Sol::kH)));
    fill(dl, table, rgb(0x008000));
    dl->PushClipRect(table.a(), table.b(), true);
    auto P = [&](float x, float y) { return ImVec2(snap(table.x0 + U(s, x)), snap(table.y0 + U(s, y))); };
    auto spot = [&](float x, float y) {
        const Box b = box(P(x, y), ImVec2(U(s, Sol::kCW), U(s, Sol::kCH)));
        dl->AddRect(b.a(), b.b(), rgb(0x004000), U(s, 3), 0, o);
        return b;
    };
    // Hit targets, in drawing order (the last one under the pointer takes the press).
    struct Hit {
        Box b;
        int what;  // 0 stock, 1 card, 2 home spot, 3 tab spot
        Sol::Loc loc;
        bool up = true;
    };
    std::vector<Hit> hits;
    const bool dragging = w.sol_drag.has_value() && w.sol_press.has_value();
    // The stock: a card back, or the turn-over arrow.
    {
        const Box b = spot(12, 10);
        if (!g.g.stock.empty()) {
            sol_card(s, dl, 0, false, false, b.a(), deck);
        } else {
            ImFont* f = face(s, false);
            const char* arrow = "\xE2\x86\xBB";
            const float sz = U(s, 32);
            const ImVec2 t = f->CalcTextSizeA(sz, FLT_MAX, 0, arrow);
            dl->AddText(f, sz, ImVec2(snap(b.x0 + (b.w() - t.x) * 0.5f), snap(b.y0 + (b.h() - t.y) * 0.5f)),
                        rgb(0x004000), arrow);
        }
        hits.push_back({b, 0, {}});
    }
    auto add = [&](const Sol::Loc& loc, int c, float x, float y, bool up = true) {
        if (dragging && w.sol_press->loc.type == loc.type && w.sol_press->loc.i == loc.i &&
            loc.n >= w.sol_press->loc.n)
            return;
        const bool selected = g.pick && g.pick->type == loc.type && g.pick->i == loc.i && loc.n >= g.pick->n;
        sol_card(s, dl, c, up, selected, P(x, y), deck);
        hits.push_back({box(P(x, y), ImVec2(U(s, Sol::kCW), U(s, Sol::kCH))), 1, loc, up});
    };
    const int visible = std::min(g.draw, static_cast<int>(g.g.waste.size()));
    const int wn = static_cast<int>(g.g.waste.size());
    for (int j = wn - visible; j < wn; ++j)
        add({Sol::Pile::kWaste, 0, j}, g.g.waste[static_cast<std::size_t>(j)],
            static_cast<float>(94 + (j - wn + visible) * 15), 10);
    for (int i = 0; i < 4; ++i) {
        const auto& p = g.g.home[static_cast<std::size_t>(i)];
        const Sol::Loc loc{Sol::Pile::kHome, i, static_cast<int>(p.size()) - 1};
        hits.push_back({spot(static_cast<float>(258 + i * 82), 10), 2, loc});
        if (!p.empty())
            add(loc, p.back(), static_cast<float>(258 + i * 82), 10);
    }
    for (int i = 0; i < 7; ++i) {
        const auto& p = g.g.tab[static_cast<std::size_t>(i)];
        float y = 126;
        hits.push_back({spot(static_cast<float>(12 + i * 82), y), 3, {Sol::Pile::kTab, i, 0}});
        const float step = g.tab_step(i);
        for (int n = 0; n < static_cast<int>(p.size()); ++n) {
            add({Sol::Pile::kTab, i, n}, p[static_cast<std::size_t>(n)].c, static_cast<float>(12 + i * 82), y,
                p[static_cast<std::size_t>(n)].up);
            y += p[static_cast<std::size_t>(n)].up ? std::max(4.0f, step) : 7.0f;
        }
    }
    if (dragging) {
        const auto cards = g.cards(w.sol_press->loc);
        for (std::size_t i = 0; i < cards.size(); ++i)
            sol_card(s, dl, cards[i], true, false,
                     P(w.sol_drag->x, w.sol_drag->y + static_cast<float>(i) * 18), deck);
    }
    for (const auto& f : g.trails)
        sol_card(s, dl, f.c, true, false, P(f.x, f.y), deck);
    dl->PopClipRect();
    // The pointer on the table (sol.js 384-399, 401-407).
    const Pressed tp = area("##sol-table", table);
    const ImVec2 mp = ImGui::GetIO().MousePos;
    const ImVec2 pt((mp.x - table.x0) / K(s), (mp.y - table.y0) / K(s));
    if (tp.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const Hit* hit = nullptr;
        for (const auto& h : hits)
            if (h.b.contains(mp))
                hit = &h;
        if (hit == nullptr) {
            g.animating = false;
        } else if (hit->what == 0) {
            g.stock();
        } else {
            const Sol::Down r = g.down(hit->loc);
            if (r == Sol::Down::kPick)
                w.sol_press =
                    W98::SolPress{hit->loc, mp, ImVec2((mp.x - hit->b.x0) / K(s), (mp.y - hit->b.y0) / K(s))};
            if (hit->what == 1 && hit->up && ImGui::GetIO().MouseClickedCount[0] >= 2) {
                // on_click with click_count 2: send it home.
                w.sol_press.reset();
                g.pick.reset();
                g.foundation(hit->loc);
            }
        }
    }
    if (w.sol_press && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!tp.hovered && !w.sol_drag) {
            w.sol_press.reset();  // on_hover(false)
        } else if (w.sol_drag ||
                   std::abs(mp.x - w.sol_press->at.x) + std::abs(mp.y - w.sol_press->at.y) > 4 * s.dpi) {
            w.sol_drag = ImVec2(pt.x - w.sol_press->grab.x, pt.y - w.sol_press->grab.y);
            g.pick.reset();
        }
    }
    if (w.sol_press && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const Sol::Loc from = w.sol_press->loc;
        if (w.sol_drag) {
            if (const auto to = Sol::target(pt.x, pt.y))
                g.move(from, *to);
            g.pick.reset();
        } else {
            g.pick = g.pick && *g.pick == from ? std::nullopt : std::optional<Sol::Loc>(from);
        }
        w.sol_press.reset();
        w.sol_drag.reset();
    }
    area("##sol-own",
         Box{in.x0, my, in.x1, in.y1});  // own(): the menu and the status bar are not for dragging
    // The status bar: the message, the score, the time.
    const float sy = table.y1 + U(s, 1);
    const float sh = U(s, 19);
    const Box tbx = box(ImVec2(in.x1 - U(s, 95), sy), ImVec2(U(s, 95), sh));
    const Box sbx = box(ImVec2(tbx.x0 - U(s, 2) - U(s, 95), sy), ImVec2(U(s, 95), sh));
    const Box mbx{in.x0, sy, sbx.x0 - U(s, 2), sy + sh};
    const Box mi = well(s, dl, mbx);
    label(s, dl, mi.x0, mi.y0 + (mi.h() - U(s, 13)) * 0.5f, g.won ? "You won!" : "", rgb(C::kText), false, 11,
          13, false);
    const Box si = well(s, dl, sbx);
    label(s, dl, si.x0, si.y0 + (si.h() - U(s, 13)) * 0.5f,
          "Score: " + std::to_string(g.scoring ? g.g.score : 0), rgb(C::kText), false, 11, 13, false);
    const Box ti = well(s, dl, tbx);
    label(s, dl, ti.x0, ti.y0 + (ti.h() - U(s, 13)) * 0.5f, "Time: " + std::to_string(g.seconds),
          rgb(C::kText), false, 11, 13, false);
    return ImVec2(width, snap(height));
}

// sol.js 345-365: Options and Select Card Back (a dialog over the window).
ImVec2 render_sol_dialog(App::State& s, Window& win, ImDrawList* dl, ImVec2 at) {
    const float o = one(s);
    const float width = U(s, 336);
    ImDrawListSplitter split;
    split.Split(dl, 2);
    split.SetCurrentChannel(dl, 1);
    const Box in{at.x + 3 * o, at.y + 3 * o, at.x + width - 3 * o, at.y + 100000};
    title_bar(s, dl, "##sold-title", Box{in.x0, in.y0, in.x1, in.y0 + U(s, 18)},
              win.sol_dialog == "options" ? "Options" : "Select Card Back", nullptr, true,
              {{"close", [&s, &win] { close_window(s, win.id); }, nullptr}}, false);
    const float x0 = in.x0 + U(s, 15), x1 = in.x1 - U(s, 15);
    float y = in.y0 + U(s, 18) + U(s, 1) + U(s, 15);
    auto choice = [&](const std::string& text, bool selected) {
        const std::string t = std::string(selected ? "\xE2\x97\x89" : "\xE2\x97\x8B") + "  " + text;
        const Pressed p = area(("##opt-" + text).c_str(), box(ImVec2(x0, y), ImVec2(x1 - x0, U(s, 23))));
        label(s, dl, x0, y + U(s, 5), t, rgb(C::kText), false, 11, 13, false);
        y += U(s, 23) + U(s, 4);
        return p.clicked;
    };
    if (win.sol_dialog == "options") {
        label(s, dl, x0, y, "Draw", rgb(C::kText), true, 11, 13, false);
        y += U(s, 13) + U(s, 4);
        for (const int n : {1, 3})
            if (choice(n == 1 ? "Draw One" : "Draw Three", win.draw == n))
                win.draw = n;
        y += U(s, 8);
        label(s, dl, x0, y, "Scoring", rgb(C::kText), true, 11, 13, false);
        y += U(s, 13) + U(s, 4);
        for (const bool v : {true, false})
            if (choice(v ? "Standard" : "None", win.scoring == v))
                win.scoring = v;
    } else {
        label(s, dl, x0, y, "Select a card back:", rgb(C::kText), false, 11, 13, false);
        y += U(s, 13) + U(s, 4);
        float x = x0;
        for (const int n : {0, 1}) {
            const Box b = box(ImVec2(x, y),
                              ImVec2(U(s, Sol::kCW) + U(s, 6) + U(s, 4), U(s, Sol::kCH) + U(s, 6) + U(s, 4)));
            ImGui::PushID(n);
            const Pressed p = area("##deck", b);
            ImGui::PopID();
            dl->AddRect(b.a(), b.b(), win.deck == n ? rgb(0x000080) : rgb(0xc0c0c0), 0, 0, U(s, 2));
            sol_card(s, dl, 0, false, false, ImVec2(b.x0 + U(s, 5), b.y0 + U(s, 5)), n);
            if (p.clicked)
                win.deck = n;
            x = b.x1 + U(s, 14);
            if (n == 1)
                y = b.y1 + U(s, 4);
        }
    }
    y += U(s, 12) - U(s, 4);
    const int hit = buttons(s, dl, x0, x1, y, {{"OK", true}, {"Cancel"}}, true);
    if (hit == 0) {
        set_shared(s, "win98.sol.draw", win.draw);
        set_shared(s, "win98.sol.scoring", win.scoring);
        set_shared(s, "win98.sol.deck", win.deck);
        close_window(s, win.id);
    } else if (hit == 1) {
        close_window(s, win.id);
    }
    const float height = snap(y + U(s, 23) + U(s, 15) + 3 * o - at.y);
    split.SetCurrentChannel(dl, 0);
    bevel(s, dl, box(at, ImVec2(width, height)), Bevel::kWindow, rgb(C::kFace));
    split.Merge(dl);
    return ImVec2(width, height);
}
}  // namespace detail::w98

}  // namespace agentdesk::desktop
