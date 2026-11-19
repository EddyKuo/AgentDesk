#pragma once
// Internal to agent-ui: the AgentDesk 98 layout's shared types and helpers (namespace
// detail::w98), used by the win98_*.cpp files and the commands and panes that call into them.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail::w98 {

// ------------------------------------------------------------ win98_kit.cpp
// The drawing kit, the shared values, the popup menus and the overlay windows. Uses the app's
// helpers from app_internal.hpp (run_command, create_session, program_info, ...).
//
// Pixels: everything is drawn in whole Win98 pixels, U(n) = n * scale (Small Fonts 1, Large
// Fonts 2) times the display's DPI scale; the app zoom does not change them. Layout px (padding,
// gaps) are px().

using namespace ::agentdesk::desktop::w98state;
namespace k98 = agentdesk::ui::win98;
namespace C = agentdesk::ui::win98::color;
using k98::Bevel;
using MenuItems = std::vector<MenuItem>;
W98& st(App::State& s);
ImU32 rgb(std::uint32_t c, int a = 255);
ImU32 hex(const std::string& h);
std::filesystem::path root_dir();
std::string home_dir();
long long now_ms();
k98::DateTime local_now();
const Json& shared(App::State& s, const std::string& key);
void set_shared(App::State& s, const std::string& key, Json v);
std::string shared_str(App::State& s, const std::string& key);
int scale(App::State& s);
float K(App::State& s);
float U(App::State& s, float n);
float U(App::State& s, int n);
float one(App::State& s);
float snap(float v);
// 0 -> 1 -> 0 over each period of the UI clock (blinking carets and badges, bobbing).
double triangle_wave(double period_ms);
// Without leading and trailing white space (std::isspace).
std::string trim_space(std::string t);
std::string app_state(App::State& s, const std::string& name);
void set_app(App::State& s, const std::string& name, const std::string& state);
void sfx(App::State& s, const std::string& name);
void ensure_fonts(App::State& s);
float terminal_font_size(App::State& s);
ImFont* layout_font(App::State& s, int which);  // 0 terminals, 1 interface, 2 interface bold
ImFont* face(App::State& s, bool bold);
float text_w(App::State& s, const std::string& t, bool bold = false, float size = 11);
float text_top(App::State& s, float top, float size = 11, float line = 13);
float label(App::State& s, ImDrawList* dl, float x, float top, const std::string& text, ImU32 col,
            bool bold = false, float size = 11, float line = 13, bool access = true, ImFont* font = nullptr);
// The common case: regular 11 px text in a 13 px line, "&" shown as is.
float plain_label(App::State& s, ImDrawList* dl, float x, float top, const std::string& text, ImU32 col);
// A 1 px underline under `w` px of regular text laid out at (x, top).
void underline(App::State& s, ImDrawList* dl, float x, float top, float w, ImU32 col);
float label_clipped(App::State& s, ImDrawList* dl, float x, float top, const std::string& text, ImU32 col,
                    float max_w, bool bold = false);
std::vector<std::string> wrap(App::State& s, const std::string& text, float width, bool bold = false,
                              float size = 11);
float lines(App::State& s, ImDrawList* dl, float x, float top, const std::vector<std::string>& ls, ImU32 col,
            bool bold = false);

// ---------------------------------------------------------------- boxes and bevels

struct Box {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    [[nodiscard]] float w() const { return x1 - x0; }
    [[nodiscard]] float h() const { return y1 - y0; }
    [[nodiscard]] ImVec2 a() const { return ImVec2(x0, y0); }
    [[nodiscard]] ImVec2 b() const { return ImVec2(x1, y1); }
    [[nodiscard]] Box inset(float t, float r, float b2, float l) const {
        return {x0 + l, y0 + t, x1 - r, y1 - b2};
    }
    [[nodiscard]] Box inset(float v) const { return inset(v, v, v, v); }
    [[nodiscard]] bool contains(ImVec2 p) const { return p.x >= x0 && p.x < x1 && p.y >= y0 && p.y < y1; }
};
Box box(ImVec2 a, ImVec2 size);
void fill(ImDrawList* dl, Box b, ImU32 c);
Box bevel(App::State& s, ImDrawList* dl, Box b, Bevel kind, std::optional<ImU32> bg = std::nullopt);
void etch(App::State& s, ImDrawList* dl, float x, float y, float len, bool vertical);
void gradient(ImDrawList* dl, Box b, ImU32 from, ImU32 to, bool down = false);
void dither(App::State& s, ImDrawList* dl, Box b);
void image(App::State& s, ImDrawList* dl, const std::filesystem::path& file, Box b,
           ImU32 tint = IM_COL32_WHITE, bool mono = false);
void picture(App::State& s, ImDrawList* dl, const std::string& rel, Box b, ImU32 tint = IM_COL32_WHITE);
void icon(App::State& s, ImDrawList* dl, const std::string& name, float size, ImVec2 at, int alpha = 255);
void brand_header(App::State& s, ImDrawList* dl, Box b);
void brand_splash(App::State& s, ImDrawList* dl, Box b);
void glyph(App::State& s, ImDrawList* dl, const std::string& name, float w, float h, ImVec2 at, ImU32 col);
void program_icon(App::State& s, ImDrawList* dl, const ui::Program& p, float size, ImVec2 at);

// ---------------------------------------------------------------- buttons

struct ButtonOpts {
    bool def = false;      // the thick outline
    bool toggled = false;  // stays in
    bool disabled = false;
    float pad_y = 0, pad_x = 4;
    bool on_down = false;  // acts on the press (onDown), not the click
    const char* tooltip = nullptr;
    ImU32 bg = 0;  // 0: the face color
};

struct Pressed {
    bool clicked = false;  // released over it (or pressed, with on_down)
    bool right = false;    // right button down on it
    bool hovered = false;
    bool held = false;
    bool double_click = false;
    Box content;  // where its face goes (moved 1 px while held)
};
extern bool g_area_hovered;
Pressed area(const char* id, Box b, bool on_down = false);
Pressed button(App::State& s, ImDrawList* dl, const char* id, Box b, const ButtonOpts& o = {});
Pressed text_button(App::State& s, ImDrawList* dl, const char* id, Box b, const std::string& text,
                    ButtonOpts o = {}, bool bold = false);

// ---------------------------------------------------------------- title bars, fields, menus, windows

struct Control {
    std::string kind;
    std::function<void()> fn;
    const char* tip = nullptr;
};
struct TitleEvents {
    bool click = false;
    bool double_click = false;
    bool right = false;
};
TitleEvents title_bar(App::State& s, ImDrawList* dl, const char* id, Box b, const std::string& title,
                      const std::function<void(ImVec2)>& draw_icon_at, bool active,
                      const std::vector<Control>& controls, bool clickable);
Box field(App::State& s, ImDrawList* dl, Box b, float pad_y = 2, float pad_x = 3, ImU32 bg = 0);
Box well(App::State& s, ImDrawList* dl, Box b, float pad_y = 1, float pad_x = 3);
int open_menu(App::State& s, MenuItems items, std::optional<ImVec2> at, bool start = false,
              std::string owner = {}, std::vector<int> parents = {}, std::optional<Box> above = std::nullopt);
void close_all_menus(App::State& s);
void raise(const std::string& name);
void keyboard_back(App::State& s);
void draw_menus(App::State& s);
// Begins a borderless, undecorated ImGui window for an overlay; the caller sets its position and
// size first and calls ImGui::End().
void begin_overlay(const std::string& name, ImGuiWindowFlags extra);
Window* find_window(App::State& s, int id);
Window* find_key(App::State& s, const std::string& key);
int open_window(App::State& s, std::unique_ptr<Window> win);
void close_window(App::State& s, int id);
void open_dialog(App::State& s, Dialog d);

// ------------------------------------------------------------ win98_dialogs.cpp
struct Task {
    std::string address;
    std::string name;
    ui::Program program;
    std::string status;
};

// A row of push buttons (75 x 23 unless given, gap 6), right-aligned (`end`) or centered.
// Returns the index pressed, or -1.
struct Btn {
    std::string label;
    bool def = false;
    bool disabled = false;
    float w = 75;
};
int buttons(App::State& s, ImDrawList* dl, float x0, float x1, float y, const std::vector<Btn>& list,
            bool end);
void dialog_keys(App::State& s, Window& w, Dialog& d);
ImVec2 render_dialog(App::State& s, Window& w, ImDrawList* dl, ImVec2 at);

// ------------------------------------------------------------ win98_chrome.cpp
std::string status_word(Attention a);
std::string space_status(App::State& s, const ui::Space& sp);
std::string user_name_now();
void select_space_id(App::State& s, const std::string& id);
void rename_space_id(App::State& s, const std::string& id, const std::string& name);
void new_tab(App::State& s, const std::string& name, int space = -1, const std::string& profile = {},
             const std::vector<std::string>& args = {});
void new_space(App::State& s, const std::string& name, const std::string& machine);
Dialog prompt(const std::string& title, const std::string& text, const std::string& icon,
              std::function<void(const std::string&, const std::string&)> cmd,
              std::optional<std::string> value = {}, bool machines = false);
Dialog message(const std::string& title, const std::string& text, const std::string& icon = {});
Dialog kind_dialog(const std::string& kind);
Dialog new_space_prompt(App::State& s);
Dialog new_tab_prompt(App::State& s);
Dialog rename_tab_prompt(App::State& s, const std::string& id, const std::string& current);
Dialog rename_space_prompt(App::State& s, const std::string& id, const std::string& current);
Dialog close_space_ask(App::State& s, int index);
// The badge icon for an agent status word ("" for none).
std::string status_badge(const std::string& status);
// Where a session lives: "<space> \ <tab>" (both empty when it is in no tab).
std::string session_place(App::State& s, const std::string& id,
                          const std::map<std::string, std::string>& cwds);
std::vector<Task> tasks(App::State& s);
void set_desktop(App::State& s, const std::string& c);
float pane_border(App::State& s);
float pane_header_h(App::State& s);
void pane_header_at(App::State& s, const std::string& id, ImVec2 a, ImVec2 b, bool focused);
void command(App::State& s, const std::string& c);
void boot(App::State& s, bool force);
long long app_age_seconds();
void taskbar(App::State& s, ImDrawList* dl, Box b);

// ------------------------------------------------------------ win98_desktop.cpp
void desktop(App::State& s, ImDrawList* dl, Box b);

// ------------------------------------------------------------ win98_games.cpp
k98::Mines::Options mines_options(App::State& s);
ImVec2 render_mines(App::State& s, Window& win, ImDrawList* dl, ImVec2 at);

// ---------------------------------------------------------------- Solitaire

using Sol = k98::Solitaire;
ImVec2 render_sol(App::State& s, Window& /*win*/, ImDrawList* dl, ImVec2 at);
ImVec2 render_sol_dialog(App::State& s, Window& win, ImDrawList* dl, ImVec2 at);

// ------------------------------------------------------------ win98_desktop.cpp
void preview(App::State& s, ImDrawList* dl, ImVec2 a, ImVec2 size, float rounding, ImU32 border);
void background(App::State& s, ImDrawList* dl, Box b);
void begin(App::State& s);
void fit(App::State& s, ui::layout::Node& root);
void overlays(App::State& s);
void hidden(App::State& s);
}  // namespace detail::w98

}  // namespace agentdesk::desktop
