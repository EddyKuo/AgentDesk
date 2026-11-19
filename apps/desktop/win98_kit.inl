// Herdr 98, part 1: the drawing kit, the shared values, the popup menus and the overlay windows.
// Ported from herdr-app defaults/mods/win98 (commit 321c9a8, Apache-2.0): kit.js, menu.js; herdr's
// overlays (herdr.d.ts 395-425). Included by app.cpp inside its anonymous namespace, after
// App::State; uses app.cpp's helpers (run_command, create_session, program_info, ...).
//
// Pixels: kit.js 4-6 draws in whole Win98 pixels, u(n) = n * scale (Small Fonts 1, Large 2) at
// the display's scale; herdr's zoom does not change them. Layout px (padding, gaps) are px().

namespace w98 {

using namespace ::agentdesk::desktop::w98state;
namespace k98 = agentdesk::ui::win98;
namespace C = agentdesk::ui::win98::color;
using k98::Bevel;
using MenuItems = std::vector<MenuItem>;

void open_dialog(App::State& s, Dialog d);
void command(App::State& s, const std::string& cmd);
void sfx(App::State& s, const std::string& name);

W98& st(App::State& s) {
    return *s.win98;
}

ImU32 rgb(std::uint32_t c, int a = 255) {
    return IM_COL32((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, a);
}

ImU32 hex(const std::string& h) {
    if (h.size() != 7 || h[0] != '#')
        return IM_COL32(0, 0, 0, 255);
    return rgb(static_cast<std::uint32_t>(std::strtoul(h.c_str() + 1, nullptr, 16)));
}

std::filesystem::path root_dir() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path() / L"win98";
}

std::string home_dir() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    return n > 0 && n < MAX_PATH ? win::to_utf8(std::wstring(buf, n)) : std::string();
}

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

k98::DateTime local_now() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    k98::DateTime d;
    d.year = tm.tm_year + 1900;
    d.month = tm.tm_mon;
    d.day = tm.tm_mday;
    d.weekday = tm.tm_wday;
    d.hour = tm.tm_hour;
    d.minute = tm.tm_min;
    d.second = tm.tm_sec;
    std::tm copy = tm;
    d.utc_offset_minutes = static_cast<int>((_mkgmtime(&copy) - t) / 60);
    return d;
}

// ---------------------------------------------------------------- shared (herdr shared(), persisted)

std::filesystem::path shared_file() {
    return data_dir() / L"win98.json";
}

const Json& shared(App::State& s, const std::string& key) {
    W98& w = st(s);
    if (!w.shared_loaded) {
        w.shared_loaded = true;
        const auto j = Json::parse(read_text_file(shared_file()), nullptr, false);
        if (j.is_object())
            w.shared = j;
    }
    static const Json kNull;
    const auto it = w.shared.find(key);
    return it == w.shared.end() ? kNull : *it;
}

void set_shared(App::State& s, const std::string& key, Json v) {
    (void)shared(s, key);
    W98& w = st(s);
    if (v.is_null())
        w.shared.erase(key);
    else
        w.shared[key] = std::move(v);
    const auto tmp = std::filesystem::path(shared_file()).concat(L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << w.shared.dump(2, ' ', false, Json::error_handler_t::replace) << "\n";
    }
    ::MoveFileExW(tmp.c_str(), shared_file().c_str(), MOVEFILE_REPLACE_EXISTING);
}

std::string shared_str(App::State& s, const std::string& key) {
    const Json& j = shared(s, key);
    return j.is_string() ? j.get<std::string>() : std::string();
}

// kit.js 40-43.
int scale(App::State& s) {
    const Json& j = shared(s, "win98.scale");
    return k98::scale_of(j.is_number() ? j.get<int>() : 1);
}

// kit.js 45-51: one Win98 pixel on screen.
float K(App::State& s) {
    return static_cast<float>(scale(s)) * s.dpi;
}
float U(App::State& s, float n) {
    return n * K(s);
}
float U(App::State& s, int n) {
    return static_cast<float>(n) * K(s);
}
// One Win98 pixel, at least one screen pixel.
float one(App::State& s) {
    return std::max(1.0f, std::round(K(s)));
}
float snap(float v) {
    return std::round(v);
}

// kit.js 417-427: which game windows are open: {mines: "open" | "min", sol: ...}.
std::string app_state(App::State& s, const std::string& name) {
    const Json& a = shared(s, "win98.apps");
    return a.is_object() && a.contains(name) && a[name].is_string() ? a[name].get<std::string>()
                                                                    : std::string();
}
void set_app(App::State& s, const std::string& name, const std::string& state) {
    Json a = shared(s, "win98.apps");
    if (!a.is_object())
        a = Json::object();
    if (state.empty())
        a.erase(name);
    else
        a[name] = state;
    set_shared(s, "win98.apps", a);
}

// ---------------------------------------------------------------- sounds (kit.js 404-413)

void play_file(const std::filesystem::path& file) {
    static std::atomic<int> counter{0};
    std::thread([file, id = ++counter] {
        const std::wstring alias = L"agentdesk98_" + std::to_wstring(id);
        const std::wstring open = L"open \"" + file.native() + L"\" type mpegvideo alias " + alias;
        if (::mciSendStringW(open.c_str(), nullptr, 0, nullptr) == 0) {
            ::mciSendStringW((L"play " + alias + L" wait").c_str(), nullptr, 0, nullptr);
            ::mciSendStringW((L"close " + alias).c_str(), nullptr, 0, nullptr);
        }
    }).detach();
}

// Plays a Herdr 98 sound unless sounds are off (settings, or the speaker in the tray).
void sfx(App::State& s, const std::string& name) {
    if (!s.settings.sounds || s.config.automated)
        return;
    if (shared(s, "win98.mute") == Json(true))
        return;
    const auto f = root_dir() / L"sounds" / win::to_wide(name + ".mp3");
    std::error_code ec;
    if (std::filesystem::is_regular_file(f, ec))
        play_file(f);
}

// ---------------------------------------------------------------- fonts (README.md 8-10)

void ensure_fonts(App::State& s) {
    W98& w = st(s);
    if (w.fonts_tried)
        return;
    w.fonts_tried = true;
    const auto fonts = root_dir() / L"fonts";
    w.font = ui::load_font_file((fonts / L"PixelatedMSSansSerif-Regular.ttf").native(), 11.0f, false);
    w.bold = ui::load_font_file((fonts / L"PixelatedMSSansSerif-Bold.ttf").native(), 11.0f, false);
    w.dos = ui::load_font_file((fonts / L"PxPlus_IBM_VGA_8x16.ttf").native(), 16.0f, true);
    if (w.font == nullptr)
        w.font = s.base_ui != nullptr ? s.base_ui : s.fonts.ui;
    if (w.bold == nullptr)
        w.bold = w.font;
    if (w.dos == nullptr)
        w.dos = s.fonts.mono;
    // mines.js 157: the numbers in Arial bold.
    if (const auto* f = find_family("Arial"))
        w.arial = ui::load_font_file(f->file_for(700), 14.0f, false);
    if (w.arial == nullptr)
        w.arial = w.bold;
    // The Solitaire cards name "DejaVu Serif" (sol-*.svg <text>); Windows has no DejaVu, so the
    // serif Windows ships stands in (what a renderer falls back to without the family).
    wchar_t windir[MAX_PATH]{};
    ::GetWindowsDirectoryW(windir, MAX_PATH);
    const std::filesystem::path wf = std::filesystem::path(windir) / L"Fonts";
    ui::ImageCache::add_svg_font("DejaVu Serif", false, wf / L"times.ttf");
    ui::ImageCache::add_svg_font("DejaVu Serif", true, wf / L"timesbd.ttf");
}

// kit.js 451-452, at Herdr's zoom (the terminal font size at zoom 1).
float terminal_font_size(App::State& s) {
    return k98::terminal_font_px(scale(s), s.settings.zoom);
}

ImFont* layout_font(App::State& s, int which) {
    ensure_fonts(s);
    return which == 0 ? st(s).dos : which == 1 ? st(s).font : st(s).bold;
}

// ---------------------------------------------------------------- text (kit.js 55-80)

ImFont* face(App::State& s, bool bold) {
    ensure_fonts(s);  // the layout may have switched in the middle of a frame
    return bold ? st(s).bold : st(s).font;
}

float text_w(App::State& s, const std::string& t, bool bold = false, float size = 11) {
    return face(s, bold)->CalcTextSizeA(U(s, size), FLT_MAX, 0, t.c_str()).x;
}

// The font's baseline in a line box of `line` Win98 px (CSS centers the text in its line).
float text_top(App::State& s, float top, float size = 11, float line = 13) {
    return snap(top + (U(s, line) - U(s, size)) * 0.5f);
}

// One line of text in a line box of `line` px at (x, top). "&File" underlines the F unless
// noAccess (kit.js 56-71). Returns its width.
float label(App::State& s, ImDrawList* dl, float x, float top, const std::string& text, ImU32 col,
            bool bold = false, float size = 11, float line = 13, bool access = true, ImFont* font = nullptr) {
    ImFont* f = font != nullptr ? font : face(s, bold);
    const float px_size = U(s, size);
    const float y = text_top(s, top, size, line);
    const int amp = access ? k98::access_index(text) : -1;
    const std::string shown = amp >= 0 ? k98::plain(text) : text;
    dl->AddText(f, px_size, ImVec2(snap(x), y), col, shown.c_str());
    if (amp >= 0) {
        const float a = f->CalcTextSizeA(px_size, FLT_MAX, 0, shown.c_str(), shown.c_str() + amp).x;
        std::size_t len = 1;
        while (amp + len < shown.size() &&
               (static_cast<unsigned char>(shown[static_cast<std::size_t>(amp) + len]) & 0xC0) == 0x80)
            ++len;
        const float b = f->CalcTextSizeA(px_size, FLT_MAX, 0, shown.c_str() + amp,
                                         shown.c_str() + amp + static_cast<int>(len))
                            .x;
        const float base = y + f->GetFontBaked(px_size)->Ascent;
        dl->AddRectFilled(ImVec2(snap(x + a), snap(base + 1)),
                          ImVec2(snap(x + a + b), snap(base + 1) + one(s)), col);
    }
    return f->CalcTextSizeA(px_size, FLT_MAX, 0, shown.c_str()).x;
}

// Text read bottom to top (the Start menu's banner): laid out level from (x, bottom), then turned
// a quarter left about that point, its top toward x. Returns its length.
float label_up(App::State& s, ImDrawList* dl, float x, float bottom, const std::string& text, ImU32 col, bool bold,
               float size, float line) {
    dl->PushClipRectFullScreen();  // level, it runs past the banner; turned, it is inside it
    const int first = dl->VtxBuffer.Size;
    const float w = label(s, dl, x, bottom, text, col, bold, size, line, false);
    for (int i = first; i < dl->VtxBuffer.Size; ++i) {
        ImVec2& p = dl->VtxBuffer[i].pos;
        p = ImVec2(x + (p.y - bottom), bottom - (p.x - x));
    }
    dl->PopClipRect();
    return w;
}

// A truncated line (GPUI truncate: an ellipsis).
float label_clipped(App::State& s, ImDrawList* dl, float x, float top, const std::string& text, ImU32 col,
                    float max_w, bool bold = false) {
    return text_clipped(dl, face(s, bold), U(s, 11), ImVec2(snap(x), text_top(s, top)), col, text, max_w);
}

// The text's lines (dialog.js 40-44: one per "\n", an empty one keeps its height), wrapped at
// `width` as GPUI wraps a div's text.
std::vector<std::string> wrap(App::State& s, const std::string& text, float width, bool bold = false,
                              float size = 11) {
    std::vector<std::string> out;
    std::stringstream in(text);
    for (std::string para; std::getline(in, para, '\n');) {
        if (width <= 0 || text_w(s, para, bold, size) <= width) {
            out.push_back(para);
            continue;
        }
        std::string line;
        std::size_t i = 0;
        while (i < para.size()) {
            std::size_t j = para.find(' ', i);
            if (j == std::string::npos)
                j = para.size();
            std::string word = para.substr(i, j - i);
            const std::string tryline = line.empty() ? word : line + " " + word;
            if (text_w(s, tryline, bold, size) <= width) {
                line = tryline;
                i = j + 1;
                continue;
            }
            if (!line.empty()) {
                out.push_back(line);
                line.clear();
            }
            // A word wider than the line breaks between characters (GPUI's line wrapper).
            while (text_w(s, word, bold, size) > width && word.size() > 1) {
                std::size_t n = 0;
                while (n < word.size()) {
                    std::size_t next = n + 1;
                    while (next < word.size() && (static_cast<unsigned char>(word[next]) & 0xC0) == 0x80)
                        ++next;
                    if (n > 0 && text_w(s, word.substr(0, next), bold, size) > width)
                        break;
                    n = next;
                }
                out.push_back(word.substr(0, n));
                word.erase(0, n);
            }
            line = word;
            i = j + 1;
        }
        out.push_back(line);
    }
    if (!text.empty() && text.back() == '\n')
        out.emplace_back();
    if (out.empty())
        out.emplace_back();
    return out;
}

// Draws lines from the top; returns the height (13 px each).
float lines(App::State& s, ImDrawList* dl, float x, float top, const std::vector<std::string>& ls, ImU32 col,
            bool bold = false) {
    float y = top;
    for (const auto& l : ls) {
        label(s, dl, x, y, l, col, bold, 11, 13, false);
        y += U(s, 13);
    }
    return y - top;
}

// ---------------------------------------------------------------- bevels (kit.js 82-137)

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

Box box(ImVec2 a, ImVec2 size) {
    return {snap(a.x), snap(a.y), snap(a.x + size.x), snap(a.y + size.y)};
}

void fill(ImDrawList* dl, Box b, ImU32 c) {
    dl->AddRectFilled(b.a(), b.b(), c);
}

// One bevel: per layer, outside in, the right and bottom edges in one color (the full height and
// width) and the top and left edges in the other. Returns the inside.
Box bevel(App::State& s, ImDrawList* dl, Box b, Bevel kind, std::optional<ImU32> bg = std::nullopt) {
    const float o = one(s);
    for (const auto& l : k98::bevel_layers(kind)) {
        dl->AddRectFilled(ImVec2(b.x0, b.y0), ImVec2(b.x1 - o, b.y0 + o), rgb(l.tl));
        dl->AddRectFilled(ImVec2(b.x0, b.y0), ImVec2(b.x0 + o, b.y1 - o), rgb(l.tl));
        dl->AddRectFilled(ImVec2(b.x1 - o, b.y0), ImVec2(b.x1, b.y1), rgb(l.rb));
        dl->AddRectFilled(ImVec2(b.x0, b.y1 - o), ImVec2(b.x1, b.y1), rgb(l.rb));
        b = b.inset(o);
    }
    if (bg)
        fill(dl, b, *bg);
    return b;
}

// kit.js 176-181: an etched line, 2 px.
void etch(App::State& s, ImDrawList* dl, float x, float y, float len, bool vertical) {
    const float o = one(s);
    if (vertical) {
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + o, y + len), rgb(C::kShadow));
        dl->AddRectFilled(ImVec2(x + o, y), ImVec2(x + 2 * o, y + len), rgb(C::kHi));
    } else {
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + len, y + o), rgb(C::kShadow));
        dl->AddRectFilled(ImVec2(x, y + o), ImVec2(x + len, y + 2 * o), rgb(C::kHi));
    }
}

// kit.js 192-195: a left-to-right gradient (angle 90), or top to bottom (180).
void gradient(ImDrawList* dl, Box b, ImU32 from, ImU32 to, bool down = false) {
    if (down)
        dl->AddRectFilledMultiColor(b.a(), b.b(), from, from, to, to);
    else
        dl->AddRectFilledMultiColor(b.a(), b.b(), from, to, to, from);
}

// kit.js 197-200: a 1 px dither (Win98's checked buttons): white on every other Win98 pixel.
void dither(App::State& s, ImDrawList* dl, Box b) {
    const float o = one(s);
    dl->PushClipRect(b.a(), b.b(), true);
    for (float y = std::floor(b.y0 / o) * o; y < b.y1; y += o) {
        const int row = static_cast<int>(std::round(y / o));
        for (float x = std::floor(b.x0 / o) * o + static_cast<float>(row & 1) * o; x < b.x1; x += 2 * o)
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + o, y + o), rgb(C::kHi));
    }
    dl->PopClipRect();
}

// ---------------------------------------------------------------- icons (kit.js 202-225)

void image(App::State& s, ImDrawList* dl, const std::filesystem::path& file, Box b,
           ImU32 tint = IM_COL32_WHITE, bool mono = false) {
    const auto img = st(s).images.svg(file, static_cast<int>(b.w()), static_cast<int>(b.h()), mono);
    if (img.ok())
        dl->AddImage(img.tex, b.a(), b.b(), ImVec2(0, 0), ImVec2(1, 1), tint);
}

void picture(App::State& s, ImDrawList* dl, const std::string& rel, Box b, ImU32 tint = IM_COL32_WHITE) {
    const auto img = st(s).images.picture(root_dir() / win::to_wide(rel));
    if (img.ok())
        dl->AddImage(img.tex, b.a(), b.b(), ImVec2(0, 0), ImVec2(1, 1), tint);
}

// A pixel-art icon: 32 px art (name.svg) for big sizes, 16 px (name-sm.svg), 9 px badges.
void icon(App::State& s, ImDrawList* dl, const std::string& name, float size, ImVec2 at, int alpha = 255) {
    const std::string file = size >= 24 ? name : size >= 12 ? name + "-sm" : name + "-xs";
    image(s, dl, root_dir() / L"icons" / win::to_wide(file + ".svg"), box(at, ImVec2(U(s, size), U(s, size))),
          IM_COL32(255, 255, 255, alpha));
}

// AgentDesk 98's name: the Start icon, "AgentDesk" and an orange "98" (the Welcome header's), at
// the given size, starting at x; returns its width. measure: only the width.
float brand_name(App::State& s, ImDrawList* dl, float x, float top, float size, bool measure = false) {
    const float line = size * 26.0f / 22.0f;
    const float ic = std::round(size * 32.0f / 22.0f);
    const float gap = U(s, size * 6.0f / 22.0f);
    const float name_w = text_w(s, "AgentDesk", true, size), nine_w = text_w(s, "98", false, size);
    const float w = U(s, ic) + gap + name_w + gap + nine_w;
    if (measure)
        return w;
    icon(s, dl, "start", ic, ImVec2(x, snap(top)));
    const float ty = top + U(s, ic) - U(s, line);
    x += U(s, ic) + gap;
    x += label(s, dl, x, ty, "AgentDesk", rgb(C::kText), true, size, line, false) + gap;
    label(s, dl, x, ty, "98", rgb(0xd07000), false, size, line, false);
    return w;
}

// The About box's picture (art/generate.ts 120: white to #a4d2ee, the name at 32).
void brand_header(App::State& s, ImDrawList* dl, Box b) {
    gradient(dl, b, rgb(0xffffff), rgb(0xa4d2ee));
    brand_name(s, dl, b.x0 + U(s, 12), b.y0 + (b.h() - U(s, 46)) * 0.5f, 32);
}

// The start-up and shut-down screen: the clouds, the name centered at twice the size.
void brand_splash(App::State& s, ImDrawList* dl, Box b) {
    picture(s, dl, "img/clouds.png", b);
    const float w = brand_name(s, dl, 0, 0, 44, true);
    brand_name(s, dl, snap(b.x0 + (b.w() - w) * 0.5f), snap(b.y0 + b.h() * 0.4f - U(s, 64) * 0.5f), 44);
}

// A one-color SVG of the mod (svg(...).text_color()): ctl-*, check, arrows.
void glyph(App::State& s, ImDrawList* dl, const std::string& name, float w, float h, ImVec2 at, ImU32 col) {
    image(s, dl, root_dir() / L"icons" / win::to_wide(name + ".svg"), box(at, ImVec2(U(s, w), U(s, h))), col,
          true);
}

// kit.js 208-225 programIcon: from the app's program table (herdr programs.json, programs_of in
// app.cpp); shells, the generic shell entry and programs without an icon are MS-DOS. A tinted
// glyph sits on its tile (navy without one), 4 px smaller, in its color (white without one).
void program_icon(App::State& s, ImDrawList* dl, const ui::Program& p, float size, ImVec2 at) {
    if (p.shell || p.key == "shell" || p.icon.empty()) {
        icon(s, dl, "dos", size, at);
        return;
    }
    const auto file = program_icon_file(p);
    const Box b = box(at, ImVec2(U(s, size), U(s, size)));
    if (p.tint) {
        fill(dl, b, p.tile.empty() ? rgb(C::kNavy) : hex(p.tile));
        image(s, dl, file, b.inset(U(s, 2)), p.color.empty() ? rgb(C::kWhite) : hex(p.color), true);
    } else {
        image(s, dl, file, b);
    }
}

// ---------------------------------------------------------------- buttons (kit.js 139-174)

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

// Some control was under the pointer (reset by the caller; a focused terminal keeps an ImGui item
// active, which makes ImGui::IsAnyItemHovered() always true).
bool g_area_hovered = false;

// The press handling of an area: an invisible ImGui button.
Pressed area(const char* id, Box b, bool on_down = false) {
    Pressed p;
    ImGui::SetCursorScreenPos(b.a());
    const bool released = ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, b.w()), std::max(1.0f, b.h())),
                                                 ImGuiButtonFlags_MouseButtonLeft);
    // on_mouse_down: the press itself; on_click: released over it.
    p.clicked = on_down ? ImGui::IsItemActivated() : released;
    p.hovered = ImGui::IsItemHovered();
    g_area_hovered = g_area_hovered || p.hovered;
    p.held = ImGui::IsItemActive();
    p.right = p.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    p.double_click = p.clicked && ImGui::GetIO().MouseClickedLastCount[0] >= 2;
    p.content = b;
    return p;
}

Pressed button(App::State& s, ImDrawList* dl, const char* id, Box b, const ButtonOpts& o = {}) {
    Pressed p = o.disabled ? Pressed{} : area(id, b, o.on_down);
    if (o.disabled) {
        ImGui::SetCursorScreenPos(b.a());
        ImGui::Dummy(ImVec2(std::max(1.0f, b.w()), std::max(1.0f, b.h())));
        p.content = b;
    }
    if (o.disabled || o.toggled)
        p.clicked = p.clicked && !o.disabled;
    const bool pressed_look = o.toggled || (p.held && !o.toggled && !o.disabled);
    Box face = b;
    if (o.def) {
        const float t = one(s);
        dl->AddRect(face.a(), face.b(), rgb(C::kDark), 0, 0, t);
        face = face.inset(t);
    }
    Box in =
        bevel(s, dl, face, pressed_look ? Bevel::kPressed : Bevel::kRaised, o.bg != 0 ? o.bg : rgb(C::kFace));
    in = in.inset(U(s, o.pad_y), U(s, o.pad_x), U(s, o.pad_y), U(s, o.pad_x));
    if (p.held && !o.toggled && !o.disabled)  // kit.js 118: the face moves 1 px down and right
        in = in.inset(one(s), -one(s), -one(s), one(s));
    p.content = in;
    if (o.tooltip != nullptr && p.hovered)
        ImGui::SetTooltip("%s", o.tooltip);
    return p;
}

// A push button with a label, centered (kit.js 150-151).
Pressed text_button(App::State& s, ImDrawList* dl, const char* id, Box b, const std::string& text,
                    ButtonOpts o = {}, bool bold = false) {
    Pressed p = button(s, dl, id, b, o);
    const float tw = text_w(s, k98::plain(text), bold);
    label(s, dl, p.content.x0 + (p.content.w() - tw) * 0.5f, p.content.y0 + (p.content.h() - U(s, 13)) * 0.5f,
          text, rgb(o.disabled ? C::kDisabled : C::kText), bold);
    return p;
}

// kit.js 229-239: a title bar's little button, 16 x 14, its glyph 8 x 7.
bool control(App::State& s, ImDrawList* dl, const char* id, const std::string& kind, ImVec2 at,
             const char* tip) {
    static const std::map<std::string, std::string> kGlyph = {{"min", "ctl-min"},
                                                              {"max", "ctl-max"},
                                                              {"restore", "ctl-restore"},
                                                              {"close", "ctl-close"},
                                                              {"help", "ctl-help"}};
    ButtonOpts o;
    o.pad_x = o.pad_y = 0;
    o.tooltip = tip;
    const Pressed p = button(s, dl, id, box(at, ImVec2(U(s, 16), U(s, 14))), o);
    const float gw = U(s, 8), gh = U(s, 7);
    glyph(s, dl, kGlyph.at(kind), 8, 7,
          ImVec2(snap(p.content.x0 + (p.content.w() - gw) * 0.5f),
                 snap(p.content.y0 + (p.content.h() - gh) * 0.5f)),
          rgb(C::kText));
    return p.clicked;
}

// ---------------------------------------------------------------- title bars (kit.js 241-289)

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

// The bar: pl 3, pr 2, gap 3; the gradient (navy to blue when active, grays when not); the icon,
// the bold title (truncated), the buttons (close 2 px apart from the others).
TitleEvents title_bar(App::State& s, ImDrawList* dl, const char* id, Box b, const std::string& title,
                      const std::function<void(ImVec2)>& draw_icon_at, bool active,
                      const std::vector<Control>& controls, bool clickable) {
    TitleEvents ev;
    ImGui::PushID(id);
    gradient(dl, b, active ? rgb(C::kNavy) : rgb(C::kInactive), active ? rgb(C::kBlue) : rgb(C::kInactive2));
    // The buttons first: they win the press over the bar.
    float cw = 0;
    for (std::size_t i = 0; i < controls.size(); ++i)
        cw += U(s, 16) + (controls[i].kind == "close" && i > 0 ? U(s, 2) : 0.0f);
    float x = b.x1 - U(s, 2) - cw;
    const float cy = snap(b.y0 + (b.h() - U(s, 14)) * 0.5f);
    for (std::size_t i = 0; i < controls.size(); ++i) {
        if (controls[i].kind == "close" && i > 0)
            x += U(s, 2);
        if (control(s, dl, ("##ctl-" + controls[i].kind).c_str(), controls[i].kind, ImVec2(snap(x), cy),
                    controls[i].tip) &&
            controls[i].fn)
            controls[i].fn();
        x += U(s, 16);
    }
    float tx = b.x0 + U(s, 3);
    if (draw_icon_at) {
        draw_icon_at(ImVec2(snap(tx), snap(b.y0 + (b.h() - U(s, 16)) * 0.5f)));
        tx += U(s, 16) + U(s, 3);
    }
    const float right = b.x1 - U(s, 2) - cw - (controls.empty() ? 0.0f : U(s, 3));
    label_clipped(s, dl, tx, b.y0 + (b.h() - U(s, 13)) * 0.5f, title,
                  active ? rgb(C::kTitle) : rgb(C::kInactiveText), right - tx, true);
    if (clickable) {
        const Pressed p = area("##bar", b);
        ev.click = p.clicked && !p.double_click;
        ev.double_click = p.double_click;
        ev.right = p.right;
    }
    ImGui::PopID();
    return ev;
}

// kit.js 310-318: a sunken white field and a status bar panel.
Box field(App::State& s, ImDrawList* dl, Box b, float pad_y = 2, float pad_x = 3, ImU32 bg = 0) {
    return bevel(s, dl, b, Bevel::kField, bg != 0 ? bg : rgb(C::kWhite))
        .inset(U(s, pad_y), U(s, pad_x), U(s, pad_y), U(s, pad_x));
}
Box well(App::State& s, ImDrawList* dl, Box b, float pad_y = 1, float pad_x = 3) {
    return bevel(s, dl, b, Bevel::kWell).inset(U(s, pad_y), U(s, pad_x), U(s, pad_y), U(s, pad_x));
}

// ---------------------------------------------------------------- menus (menu.js, kit.js 322-371)

Menu* find_menu(App::State& s, int id) {
    for (auto& m : st(s).menus)
        if (m.id == id)
            return &m;
    return nullptr;
}

// kit.js 356-371: opens a popup menu at `at` (the pointer when not given), or above a box (the
// Start menu over its button). Returns its id.
int open_menu(App::State& s, MenuItems items, std::optional<ImVec2> at, bool start = false,
              std::string owner = {}, std::vector<int> parents = {},
              std::optional<Box> above = std::nullopt) {
    W98& w = st(s);
    std::vector<k98::MenuRowText> rows;
    for (const auto& it : items)
        rows.push_back({it.label, it.shortcut, it.sep});
    const auto size = k98::menu_size(rows, start);
    Menu m;
    m.id = w.next_id++;
    m.items = std::move(items);
    m.size = ImVec2(snap(U(s, size.w)), snap(U(s, size.h)));
    ImVec2 p = at.value_or(ImGui::GetIO().MousePos);
    if (above)
        p = ImVec2(above->x0, std::max(0.0f, above->y0 - m.size.y));
    // Kept inside the window, as a popup is.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    p.x = std::clamp(p.x, vp->WorkPos.x, std::max(vp->WorkPos.x, vp->WorkPos.x + vp->WorkSize.x - m.size.x));
    p.y = std::clamp(p.y, vp->WorkPos.y, std::max(vp->WorkPos.y, vp->WorkPos.y + vp->WorkSize.y - m.size.y));
    m.pos = ImVec2(snap(p.x), snap(p.y));
    m.start = start;
    m.opened_frame = ImGui::GetFrameCount();
    m.owner = std::move(owner);
    m.parents = std::move(parents);
    // menu.js 237: a top menu starts the list over.
    if (m.parents.empty())
        w.menus.clear();
    if (!m.owner.empty())
        set_shared(s, "win98.menu.owner", m.owner);
    w.menus.push_back(std::move(m));
    return w.menus.back().id;
}

void release_owner(App::State& s, const Menu& m) {
    if (!m.owner.empty() && shared_str(s, "win98.menu.owner") == m.owner)
        set_shared(s, "win98.menu.owner", nullptr);
}

// menu.js 216-223: closes a menu and every menu opened from it.
void close_tree(App::State& s, int id) {
    W98& w = st(s);
    std::erase_if(w.menus, [&](const Menu& m) {
        const bool doomed =
            m.id == id || std::find(m.parents.begin(), m.parents.end(), id) != m.parents.end();
        if (doomed)
            release_owner(s, m);
        return doomed;
    });
    for (auto& m : w.menus)
        if (m.sub_id == id) {
            m.sub_id = 0;
            m.sub_index = -1;
        }
}

// menu.js 261-268: a choice closes the whole chain.
void close_all_menus(App::State& s) {
    W98& w = st(s);
    for (const auto& m : w.menus)
        release_owner(s, m);
    w.menus.clear();
    if (!shared(s, "win98.start").is_null())
        set_shared(s, "win98.start", nullptr);
}

// menu.js 277-290: a submenu to the right of its row (at the pointer when opened by a key).
void open_sub(App::State& s, int menu_id, int i, std::optional<Box> row) {
    Menu* m = find_menu(s, menu_id);
    if (m == nullptr || i < 0 || i >= static_cast<int>(m->items.size()))
        return;
    if (m->sub_id != 0 && m->sub_index == i)
        return;
    if (m->sub_id != 0) {
        close_tree(s, m->sub_id);
        m = find_menu(s, menu_id);
        m->sub_id = 0;
        m->sub_index = -1;
    }
    const MenuItem& it = m->items[static_cast<std::size_t>(i)];
    if (it.sub.empty() || it.disabled)
        return;
    std::optional<ImVec2> at;
    if (row)
        at = ImVec2(row->x1 + U(s, 2), row->y0 - U(s, k98::MenuMetrics::kFrame));
    std::vector<int> parents = m->parents;
    parents.push_back(menu_id);
    MenuItems sub = it.sub;
    const int id = open_menu(s, std::move(sub), at, false, {}, parents);
    if (Menu* again = find_menu(s, menu_id)) {
        again->sub_id = id;
        again->sub_index = i;
    }
}

// menu.js 270-275.
void choose(App::State& s, const MenuItem& it) {
    if (it.disabled || it.sep || !it.sub.empty())
        return;
    const auto act = it.act;
    close_all_menus(s);
    if (act)
        act();
}

// menu.js 245-251: Escape (and Left) closes this menu and what hangs off it.
void escape_menu(App::State& s, int menu_id) {
    Menu* m = find_menu(s, menu_id);
    if (m == nullptr)
        return;
    const bool top = m->parents.empty();
    close_tree(s, menu_id);
    if (!shared(s, "win98.start").is_null() && top)
        set_shared(s, "win98.start", nullptr);
}

// menu.js 292-301: the next row that is not a separator.
void move_menu(Menu& m, int d) {
    const int n = static_cast<int>(m.items.size());
    if (n == 0)
        return;
    int i = m.at;
    for (int k = 0; k < n; ++k) {
        i = (i + d + n) % n;
        if (!m.items[static_cast<std::size_t>(i)].sep)
            break;
    }
    m.at = i;
}

// menu.js 303-336: the keys of the menu opened last.
void menu_keys(App::State& s, int menu_id) {
    Menu* m = find_menu(s, menu_id);
    if (m == nullptr)
        return;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    if (pressed(ImGuiKey_DownArrow)) {
        move_menu(*m, 1);
    } else if (pressed(ImGuiKey_UpArrow)) {
        move_menu(*m, -1);
    } else if (pressed(ImGuiKey_RightArrow)) {
        if (m->at >= 0 && !m->items[static_cast<std::size_t>(m->at)].sub.empty())
            open_sub(s, menu_id, m->at, std::nullopt);
    } else if (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_Escape)) {
        escape_menu(s, menu_id);
    } else if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter) || pressed(ImGuiKey_Space)) {
        if (m->at >= 0) {
            if (!m->items[static_cast<std::size_t>(m->at)].sub.empty())
                open_sub(s, menu_id, m->at, std::nullopt);
            else
                choose(s, MenuItem(m->items[static_cast<std::size_t>(m->at)]));
        }
    } else {
        for (const ImWchar c : ImGui::GetIO().InputQueueCharacters) {
            if (c >= 128 || c == ' ')
                continue;
            Menu* mm = find_menu(s, menu_id);
            if (mm == nullptr)
                break;
            int found = -1;
            for (int i = 0; i < static_cast<int>(mm->items.size()) && found < 0; ++i)
                if (!mm->items[static_cast<std::size_t>(i)].sep &&
                    k98::access_matches(mm->items[static_cast<std::size_t>(i)].label, static_cast<char>(c)))
                    found = i;
            if (found < 0)
                continue;
            mm->at = found;
            if (!mm->items[static_cast<std::size_t>(found)].sub.empty())
                open_sub(s, menu_id, found, std::nullopt);
            else
                choose(s, MenuItem(mm->items[static_cast<std::size_t>(found)]));
            break;
        }
    }
}

// menu.js 338-386: one row. Start menu rows are 34 px with 32 px icons.
void menu_row(App::State& s, ImDrawList* dl, int menu_id, int i, Box r, bool start) {
    Menu* m = find_menu(s, menu_id);
    const MenuItem it = m->items[static_cast<std::size_t>(i)];
    if (it.sep) {
        const float px_ = U(s, start ? 2 : 1);
        etch(s, dl, r.x0 + px_, snap(r.y0 + (r.h() - 2 * one(s)) * 0.5f), r.w() - 2 * px_, false);
        return;
    }
    ImGui::PushID(i);
    const Pressed p = area("##mi", r);
    ImGui::PopID();
    m = find_menu(s, menu_id);
    if (m == nullptr)
        return;
    // on_mouse_move: the row under the pointer is the highlighted one; its submenu opens.
    if (p.hovered && (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0)) {
        if (!(m->at == i && (m->sub_index == i || it.sub.empty()))) {
            m->at = i;
            open_sub(s, menu_id, i, r);
            m = find_menu(s, menu_id);
            if (m == nullptr)
                return;
        }
    }
    const bool on = i == m->at && !it.disabled;
    const ImU32 fg = it.disabled ? rgb(C::kDisabled) : on ? rgb(C::kSelectText) : rgb(C::kText);
    if (on)
        fill(dl, r, rgb(C::kSelect));
    float x = r.x0;
    if (start) {
        const float lw = U(s, 40);
        if (!it.icon.empty())
            icon(s, dl, it.icon, 32,
                 ImVec2(snap(x + (lw - U(s, 32)) * 0.5f), snap(r.y0 + (r.h() - U(s, 32)) * 0.5f)));
        else if (!it.img.empty())
            image(s, dl, root_dir() / win::to_wide(it.img),
                  box(ImVec2(x + (lw - U(s, 32)) * 0.5f, r.y0 + (r.h() - U(s, 32)) * 0.5f),
                      ImVec2(U(s, 32), U(s, 32))));
        x += lw;
    } else {
        const float lw = U(s, 22);
        const float cx = x + lw * 0.5f, cy = r.y0 + r.h() * 0.5f;
        if (it.checked)
            glyph(s, dl, "check", 7, 7, ImVec2(snap(cx - U(s, 3.5f)), snap(cy - U(s, 3.5f))), fg);
        else if (it.radio)
            dl->AddCircleFilled(ImVec2(cx, cy), U(s, 3), fg, 12);
        else if (!it.icon.empty())
            icon(s, dl, it.icon, 16, ImVec2(snap(cx - U(s, 8)), snap(cy - U(s, 8))));
        else if (!it.img.empty())
            image(s, dl, root_dir() / win::to_wide(it.img),
                  box(ImVec2(cx - U(s, 8), cy - U(s, 8)), ImVec2(U(s, 16), U(s, 16))));
        x += lw;
    }
    const float arrow_w = U(s, start ? 16 : 18);
    const float sc_w = it.shortcut.empty() ? 0.0f : text_w(s, it.shortcut);
    label(s, dl, x, r.y0 + (r.h() - U(s, 13)) * 0.5f, it.label, fg, it.bold);
    if (!it.shortcut.empty())
        label(s, dl, r.x1 - arrow_w - sc_w, r.y0 + (r.h() - U(s, 13)) * 0.5f, it.shortcut, fg, false, 11, 13,
              false);
    if (!it.sub.empty())
        glyph(
            s, dl, "arrow-right", 4, 7,
            ImVec2(snap(r.x1 - arrow_w + (arrow_w - U(s, 4)) * 0.5f), snap(r.y0 + (r.h() - U(s, 7)) * 0.5f)),
            fg);
    if (p.clicked) {
        if (!it.sub.empty())
            open_sub(s, menu_id, i, std::nullopt);
        else
            choose(s, it);
    }
}

// Puts a Herdr 98 window above the others, unless one of AgentDesk's own dialogs (an ImGui popup:
// Settings, the palette) is open over everything.
void raise(const std::string& name) {
    if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        ui::adapter::bring_to_front(name.c_str());
}

// When a menu or a dialog closes, the keyboard goes to the dialog on top (dialogs focus
// themselves, dialog.js 50-51), else back to the terminal.
void keyboard_back(App::State& s) {
    W98& w = st(s);
    for (auto it = w.windows.rbegin(); it != w.windows.rend(); ++it)
        if (!(*it)->closing && ((*it)->role == "dialog" || (*it)->role == "sol-dialog")) {
            if (w.focus != (*it)->id) {
                w.focus = (*it)->id;
                (*it)->want_focus = true;
            }
            return;
        }
    w.focus = 0;
    s.focus_view(s.ws.focused_session());
}

// menu.js 388-426: every open menu, the last one opened on top with the keyboard.
void draw_menus(App::State& s) {
    W98& w = st(s);
    if (w.menus.empty()) {
        if (w.menu_focus != 0) {  // the keyboard back to the terminal
            w.menu_focus = 0;
            keyboard_back(s);
        }
        return;
    }
    // on_mouse_down_out: a press beside a menu closes it (popup overlays).
    for (int b = 0; b < 3; ++b) {
        if (!ImGui::IsMouseClicked(b))
            continue;
        const ImVec2 mp = ImGui::GetIO().MousePos;
        std::vector<int> doomed;
        for (const auto& m : w.menus)
            if (!box(m.pos, m.size).contains(mp) && m.opened_frame != ImGui::GetFrameCount())
                doomed.push_back(m.id);
        for (const int id : doomed) {
            Menu* m = find_menu(s, id);
            if (m == nullptr)
                continue;
            if (m->start && !shared(s, "win98.start").is_null())
                set_shared(s, "win98.start", nullptr);
            const Menu copy = *m;
            release_owner(s, copy);
            std::erase_if(w.menus, [&](const Menu& x) { return x.id == id; });
            for (auto& x : w.menus)
                if (x.sub_id == id) {
                    x.sub_id = 0;
                    x.sub_index = -1;
                }
        }
        if (w.menus.empty()) {
            keyboard_back(s);
            return;
        }
    }
    const std::vector<int> ids = [&] {
        std::vector<int> v;
        for (const auto& m : w.menus)
            v.push_back(m.id);
        return v;
    }();
    const int last = ids.back();
    for (const int id : ids) {
        Menu* m = find_menu(s, id);
        if (m == nullptr)
            continue;
        const std::string name = "##w98menu" + std::to_string(id);
        ImGui::SetNextWindowPos(m->pos);
        ImGui::SetNextWindowSize(m->size);
        // menu.js 229-230: a menu takes the keyboard when it opens.
        if (id == last && w.menu_focus != id) {
            ImGui::SetNextWindowFocus();
            w.menu_focus = id;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
        ImGui::Begin(name.c_str(), nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(3);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Box all = box(m->pos, m->size);
        const bool start = m->start;
        Box in = bevel(s, dl, all, Bevel::kWindow, rgb(C::kFace)).inset(one(s));
        if (start) {
            // The banner: 21 px, navy with a black-to-navy gradient, "AgentDesk 98" read upward at the foot.
            const Box ban{in.x0, in.y0, in.x0 + U(s, k98::MenuMetrics::kBanner), in.y1};
            fill(dl, ban, rgb(C::kNavy));
            gradient(dl, ban, rgb(0x000000), rgb(C::kNavy), true);
            {
                const float bottom = ban.y1 - U(s, 3);
                const float l = label_up(s, dl, ban.x0, bottom, "AgentDesk", rgb(C::kWhite), true, 16,
                                         static_cast<float>(k98::MenuMetrics::kBanner));
                label_up(s, dl, ban.x0, bottom - l - U(s, 6), "98", rgb(0xdfdfdf), false, 16,
                         static_cast<float>(k98::MenuMetrics::kBanner));
            }
            in.x0 = ban.x1;
        }
        float y = in.y0;
        const std::size_t n = m->items.size();
        for (std::size_t i = 0; i < n; ++i) {
            m = find_menu(s, id);
            if (m == nullptr || i >= m->items.size())
                break;
            const float h = U(s, m->items[i].sep ? k98::MenuMetrics::kSep
                                 : start         ? k98::MenuMetrics::kStartRow
                                                 : k98::MenuMetrics::kRow);
            menu_row(s, dl, id, static_cast<int>(i), Box{in.x0, snap(y), in.x1, snap(y + h)}, start);
            y += h;
        }
        if (id == last && find_menu(s, id) != nullptr && ImGui::IsWindowFocused())
            menu_keys(s, id);
        ImGui::End();
        raise(name);
    }
    if (w.menus.empty())
        keyboard_back(s);
}

// ---------------------------------------------------------------- overlay windows

Window* find_window(App::State& s, int id) {
    for (auto& w : st(s).windows)
        if (w->id == id)
            return w.get();
    return nullptr;
}

Window* find_key(App::State& s, const std::string& key) {
    for (auto& w : st(s).windows)
        if (w->key == key && !w->closing)
            return w.get();
    return nullptr;
}

int open_window(App::State& s, std::unique_ptr<Window> win) {
    W98& w = st(s);
    win->id = w.next_id++;
    if (!win->key.empty()) {
        if (const auto it = w.dropped.find(win->key); it != w.dropped.end()) {
            win->pos = it->second;
            win->placed = true;
        }
    }
    const int id = win->id;
    w.windows.push_back(std::move(win));
    w.focus = id;
    return id;
}

void close_window(App::State& s, int id) {
    if (Window* w = find_window(s, id))
        w->closing = true;
}

// kit.js 375-386: a dialog, a window dragged by its title, its width by kind.
void open_dialog(App::State& s, Dialog d) {
    auto win = std::make_unique<Window>();
    win->role = "dialog";
    if (d.width <= 0 || d.width == 340)
        d.width = k98::dialog_width(d.kind);
    d.opened = Clock::now();
    if (d.kind == "screen")
        win->modal = true;
    // dialog.js 52-64: the text box starts with the value (Run: the last line run); the sounds.
    if (d.value)
        d.input = *d.value;
    if (d.kind == "run" && !d.value) {
        const Json& hist = shared(s, "win98.run");
        d.input = hist.is_array() && !hist.empty() && hist[0].is_string() ? hist[0].get<std::string>()
                                                                          : std::string();
    }
    if (d.kind == "confirm" || d.kind == "message")
        sfx(s, d.icon == "error" ? "chord" : "ding");
    if (d.kind == "shutdown" || d.kind == "logoff")
        sfx(s, "ding");
    if (d.kind == "screen" && d.mode == "boot") {  // dialog.js 86-88: straight to "Starting"
        d.stage = 1;
        sfx(s, "startup");
    }
    if (d.kind == "screen" && d.mode == "shutdown")
        sfx(s, "shutdown");
    win->dialog = std::make_unique<Dialog>(std::move(d));
    open_window(s, std::move(win));
}

}  // namespace w98
