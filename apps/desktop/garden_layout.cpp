// Garden: a calm scene where each agent is a small creature of our own design whose pose says its
// state (working hops along, needs-you waves, done bounces then sits, idle sleeps, no status rests
// faded). Hover for details, click to work with it in your layout. Rules are in libs/ui garden.hpp.
#include "agentdesk/ui/garden.hpp"
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {
namespace gdn = ui::garden;

namespace {
constexpr float kPi = 3.14159265f;
constexpr auto kRefreshEvery = std::chrono::milliseconds(200);  // who is in the garden, their labels

// One creature, rebuilt (with its strings) at most every 200 ms; what changes every frame (state,
// pose) is read in place.
struct Critter {
    std::string id;
    int parent = -1;  // index in the list, -1: an agent of its own
    gdn::Variant look;
    ImU32 body = 0;
    std::string name;   // label, first line (already shortened to fit)
    std::string where;  // label, second line
    float name_w = 0, where_w = 0;
};

// Pose easing, per creature id (kept across refreshes).
struct PoseMemory {
    gdn::Pose pose = gdn::Pose::kRest;
    gdn::PoseShape from;   // what was shown when the pose last changed
    gdn::PoseShape shown;  // what was shown last frame
    double since = -1e9;   // real ms of that change
};

struct Flower {
    ImVec2 at;  // 0..1 in the field
    int kind = 0;
    float size = 1.0f;
};

struct GardenState {
    std::vector<Critter> critters;
    std::vector<Critter> spare;  // the list being built (swapped in; both keep their capacity)
    std::vector<int> parents;
    std::vector<gdn::Spot> spots;
    std::vector<Flower> flowers;
    gdn::Field field;
    ImVec2 field_size{};
    Clock::time_point refreshed{};
    std::map<std::string, PoseMemory> memory;
    double clock = 0;                          // the creatures' clock (ms), slower behind other windows
    double last_real = 0;                      // real ms of the last frame
    gdn::Motion motion = gdn::Motion::kStill;  // the liveliest pose drawn last frame
    bool shown = false;                        // drawn last frame
    bool crowded = false;                      // more than kCrowded agents
    // The hover card's text, built when the hovered creature changes and once a second.
    std::string hover_id;
    double hover_built = 0;
    std::string card_title, card_where, card_state, card_message;
    std::string menu_id;   // the creature whose right-click menu is open
    std::string back_tip;  // the bar's back button tooltip (with its key, when it has one)
};
GardenState g_garden;

ImU32 with_a(ImU32 c, float a) {
    const auto al = static_cast<float>((c >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a, 0.0f, 1.0f);
    return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(al) << IM_COL32_A_SHIFT);
}

// Multiplies the opacity of every vertex drawn since `from` (a faded creature without a second
// draw list or an allocation).
void fade_since(ImDrawList* dl, int from, float a) {
    if (a >= 0.999f)
        return;
    for (int i = from; i < dl->VtxBuffer.Size; ++i)
        dl->VtxBuffer[i].col = with_a(dl->VtxBuffer[i].col, a);
}

bool night() {
    return !g_pal->light;
}

// ---------------------------------------------------------------- the scene's colours
struct Sky {
    ImU32 top, low, ground_top, ground_low, hill_far, hill_near, path, ink;
};
Sky sky_colors() {
    const ImU32 grass = g_pal->done;
    Sky k{};
    if (night()) {
        k.top = mix(g_pal->bg, IM_COL32(6, 10, 26, 255), 0.45f);
        k.low = mix(g_pal->bg, g_pal->focus, 0.10f);
        k.hill_far = mix(mix(g_pal->bg, grass, 0.10f), IM_COL32(10, 14, 30, 255), 0.25f);
        k.ground_top = mix(g_pal->bg, grass, 0.17f);
        k.hill_near = k.ground_top;  // the near hill runs into the ground without a seam
        k.ground_low = mix(g_pal->bg, grass, 0.09f);
        k.path = mix(k.ground_top, g_pal->text, 0.10f);
    } else {
        k.top = mix(g_pal->bg, g_pal->info, 0.20f);
        k.low = mix(g_pal->bg, IM_COL32(255, 255, 255, 255), 0.55f);
        k.hill_far = mix(g_pal->bg, grass, 0.16f);
        k.ground_top = mix(g_pal->bg, grass, 0.28f);
        k.hill_near = k.ground_top;
        k.ground_low = mix(g_pal->bg, grass, 0.20f);
        k.path = mix(k.ground_top, IM_COL32(255, 255, 255, 255), 0.35f);
    }
    k.ink = IM_COL32(28, 30, 38, 255);
    return k;
}

// The horizon, as a share of the scene's height from its top; lower sky when the garden is crowded.
constexpr float kHorizon = 0.30f;
constexpr float kHorizonCrowded = 0.20f;
// More agents than this: the names lose their second line (the space) and the sky gives way.
constexpr std::size_t kCrowded = 10;

// A soft hill line across [x0, x1] at `y`, `rise` high, filled down to `bottom`.
void hill(ImDrawList* dl, float x0, float x1, float y, float rise, float bottom, ImU32 col, float shift) {
    const float w = x1 - x0;
    dl->PathLineTo(ImVec2(x0, bottom));
    dl->PathLineTo(ImVec2(x0, y));
    dl->PathBezierCubicCurveTo(ImVec2(x0 + w * (0.18f + shift), y - rise),
                               ImVec2(x0 + w * (0.32f + shift), y - rise),
                               ImVec2(x0 + w * 0.5f, y - rise * 0.35f), 24);
    dl->PathBezierCubicCurveTo(ImVec2(x0 + w * (0.66f - shift), y + rise * 0.1f),
                               ImVec2(x0 + w * (0.80f - shift), y - rise * 0.9f), ImVec2(x1, y - rise * 0.2f),
                               24);
    dl->PathLineTo(ImVec2(x1, bottom));
    dl->PathFillConcave(col);
}

// A four-pointed sparkle.
void sparkle(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    const float t = r * 0.28f;
    const ImVec2 pts[8] = {{c.x, c.y - r}, {c.x + t, c.y - t}, {c.x + r, c.y}, {c.x + t, c.y + t},
                           {c.x, c.y + r}, {c.x - t, c.y + t}, {c.x - r, c.y}, {c.x - t, c.y - t}};
    dl->AddConcavePolyFilled(pts, 8, col);
}

void flower(ImDrawList* dl, ImVec2 at, float s, int kind, const Sky& k) {
    static constexpr const char* kColors[] = {"ansi1", "ansi5", "ansi3", "ansi4"};
    const ImU32 stem = mix(k.ground_top, g_pal->done, 0.55f);
    if (kind == 4) {  // a tuft of grass
        for (int i = -1; i <= 1; ++i)
            dl->AddBezierQuadratic(
                at, ImVec2(at.x + static_cast<float>(i) * s * 0.2f, at.y - s * 0.6f),
                ImVec2(at.x + static_cast<float>(i) * s * 0.45f, at.y - s * (i == 0 ? 1.0f : 0.75f)), stem,
                std::max(1.0f, s * 0.09f), 8);
        return;
    }
    const ImU32 petal = mix(avatar_color(kColors[kind % 4]), k.ground_top, night() ? 0.35f : 0.15f);
    const ImVec2 head(at.x, at.y - s);
    dl->AddLine(at, head, stem, std::max(1.0f, s * 0.1f));
    dl->AddEllipseFilled(ImVec2(at.x + s * 0.16f, at.y - s * 0.45f), ImVec2(s * 0.16f, s * 0.07f), stem,
                         -0.5f, 10);
    for (int i = 0; i < 5; ++i) {
        const float a = static_cast<float>(i) * 2.0f * kPi / 5.0f - kPi / 2.0f;
        dl->AddCircleFilled(ImVec2(head.x + std::cos(a) * s * 0.2f, head.y + std::sin(a) * s * 0.2f),
                            s * 0.15f, petal, 12);
    }
    dl->AddCircleFilled(head, s * 0.11f, mix(avatar_color("ansi3"), IM_COL32(255, 255, 255, 255), 0.4f), 10);
}

// The sky, stars or sun, the hills and the ground; `field` gets the ground's area for creatures.
void draw_backdrop(ImDrawList* dl, ImVec2 a, ImVec2 b, const Sky& k, double clock, bool still,
                   float horizon) {
    const float w = b.x - a.x, h = b.y - a.y;
    const float hy = a.y + h * horizon;
    dl->AddRectFilledMultiColor(a, ImVec2(b.x, hy + px(2)), k.top, k.top, k.low, k.low);
    if (night()) {
        for (int i = 0; i < 34; ++i) {
            const std::uint32_t r =
                gdn::hash_id(std::string_view(reinterpret_cast<const char*>(&i), sizeof i));
            const float x = a.x + static_cast<float>(r % 1000u) / 1000.0f * w;
            const float y = a.y + static_cast<float>((r / 1000u) % 1000u) / 1000.0f * (hy - a.y) * 0.82f;
            const float tw = still ? 0.7f
                                   : 0.55f + 0.45f * std::sin(static_cast<float>(clock) * 0.0016f +
                                                              static_cast<float>(i) * 1.7f);
            const float rad = (r % 7u == 0 ? px(1.6f) : px(1.0f));
            dl->AddCircleFilled(
                ImVec2(x, y), rad,
                with_a(mix(g_pal->text, IM_COL32(255, 255, 255, 255), 0.4f), 0.25f + 0.45f * tw), 8);
        }
        const ImVec2 moon(a.x + w * 0.84f, a.y + (hy - a.y) * 0.34f);
        const float mr = std::clamp(h * 0.045f, px(12), px(26));
        for (int i = 3; i >= 1; --i)
            dl->AddCircleFilled(moon, mr * (1.0f + 0.45f * static_cast<float>(i)),
                                with_a(g_pal->text, 0.025f), 40);
        dl->AddCircleFilled(moon, mr, mix(g_pal->text, IM_COL32(255, 248, 225, 255), 0.5f), 40);
        dl->AddCircleFilled(ImVec2(moon.x - mr * 0.3f, moon.y - mr * 0.15f), mr * 0.18f, with_a(k.top, 0.18f),
                            16);
        dl->AddCircleFilled(ImVec2(moon.x + mr * 0.25f, moon.y + mr * 0.3f), mr * 0.12f, with_a(k.top, 0.15f),
                            16);
    } else {
        const ImVec2 sun(a.x + w * 0.84f, a.y + (hy - a.y) * 0.36f);
        const float sr = std::clamp(h * 0.05f, px(14), px(30));
        const ImU32 gold = mix(avatar_color("ansi3"), IM_COL32(255, 226, 120, 255), 0.75f);
        for (int i = 3; i >= 1; --i)
            dl->AddCircleFilled(sun, sr * (1.0f + 0.45f * static_cast<float>(i)), with_a(gold, 0.08f), 40);
        dl->AddCircleFilled(sun, sr, gold, 40);
        // Two soft clouds drifting very slowly.
        const float drift =
            still ? 0.0f : static_cast<float>(std::fmod(clock * 0.004, static_cast<double>(w) + px(200)));
        for (int i = 0; i < 2; ++i) {
            float cx = a.x + w * (0.22f + 0.38f * static_cast<float>(i)) +
                       drift * (0.6f + 0.3f * static_cast<float>(i));
            if (cx > b.x + px(80))
                cx -= w + px(200);
            const float cy = a.y + (hy - a.y) * (0.32f + 0.2f * static_cast<float>(i));
            const ImU32 c = with_a(IM_COL32(255, 255, 255, 255), 0.75f);
            dl->AddEllipseFilled(ImVec2(cx, cy), ImVec2(px(34), px(10)), c, 0, 24);
            dl->AddCircleFilled(ImVec2(cx - px(10), cy - px(6)), px(11), c, 20);
            dl->AddCircleFilled(ImVec2(cx + px(9), cy - px(9)), px(14), c, 20);
        }
    }
    hill(dl, a.x, b.x, hy + px(4), h * 0.07f, b.y, k.hill_far, 0.04f);
    hill(dl, a.x - w * 0.1f, b.x, hy + h * 0.05f, h * 0.05f, b.y, k.hill_near, -0.06f);
    dl->AddRectFilledMultiColor(ImVec2(a.x, hy + h * 0.08f), b, k.ground_top, k.ground_top, k.ground_low,
                                k.ground_low);
}

// ---------------------------------------------------------------- who is in the garden
bool garden_agent(App::State& s, const std::string& id, const ui::SessionView& v) {
    if (v.lost || v.process_state != "Running" || s.hidden.contains(id) || !is_agent(v))
        return false;
    if (s.ws.find(id).space >= 0)
        return true;
    // A sub-agent without a pane: shown beside its parent.
    return !v.parent.empty() && s.views_by_id.count(v.parent) != 0;
}

ImU32 space_color(App::State& s, const std::string& id) {
    std::string at = id;
    for (int hop = 0; hop < 4 && !at.empty(); ++hop) {
        if (const auto loc = s.ws.find(at); loc.space >= 0)
            return avatar_color(s.ws.spaces[static_cast<std::size_t>(loc.space)].color);
        const auto it = s.views_by_id.find(at);
        at = it == s.views_by_id.end() ? std::string() : it->second.parent;
    }
    return avatar_color("ansi6");
}

// `text` shortened with an ellipsis to fit `max_w` at `font`/`size`.
std::string fit(ImFont* font, float size, const std::string& text, float max_w, float* width) {
    float tw = font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str()).x;
    if (tw <= max_w) {
        *width = tw;
        return text;
    }
    std::string t = text;
    while (!t.empty()) {
        do  // drop one UTF-8 character
            t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80);
        const std::string e = t + "\xE2\x80\xA6";
        tw = font->CalcTextSizeA(size, FLT_MAX, 0, e.c_str()).x;
        if (tw <= max_w) {
            *width = tw;
            return e;
        }
    }
    *width = 0;
    return {};
}

void refresh(App::State& s, GardenState& g, ImVec2 field) {
    g.spare.clear();
    std::vector<std::pair<std::string, std::string>> born;  // (created_at, id); ISO times sort as text
    for (const auto& sess : s.sessions) {
        const std::string& id = sess->id();
        const auto it = s.views_by_id.find(id);
        if (it != s.views_by_id.end() && garden_agent(s, id, it->second))
            born.emplace_back(it->second.created_at, id);
    }
    // Oldest first: creatures keep their places when states change.
    std::stable_sort(born.begin(), born.end(),
                     [](const auto& x, const auto& y) { return x.first < y.first; });
    for (const auto& [at, id] : born) {
        Critter c;
        c.id = id;
        c.look = gdn::variant_for(id);
        c.body = space_color(s, id);
        g.spare.push_back(std::move(c));
    }
    g.parents.assign(g.spare.size(), -1);
    for (std::size_t i = 0; i < g.spare.size(); ++i) {
        const std::string& parent = s.views_by_id[g.spare[i].id].parent;
        if (parent.empty())
            continue;
        for (std::size_t j = 0; j < g.spare.size(); ++j)
            if (g.spare[j].id == parent)
                g.parents[i] = static_cast<int>(j);
    }
    std::swap(g.critters, g.spare);
    g.crowded = static_cast<std::size_t>(std::count(g.parents.begin(), g.parents.end(), -1)) > kCrowded;
    g.field = gdn::place(g.parents, field.x, field.y, g.spots, px(68), g.crowded ? px(24) : px(40));
    g.field_size = field;
    // Labels, shortened to a cell.
    const auto cwds = s.session_cwds();
    const float max_w = std::max(px(40), g.field.cell_w - px(12));
    for (std::size_t i = 0; i < g.critters.size(); ++i) {
        Critter& c = g.critters[i];
        if (g.parents[i] >= 0)
            continue;  // sub-agents: the hover card says who they are
        const auto& v = s.views_by_id[c.id];
        std::string name = agent_detail(s, c.id, v);
        if (name.empty())
            name = s.title_of(v);
        std::string where;
        if (const auto loc = s.ws.find(c.id); loc.space >= 0)
            where = s.ws.space_title(loc.space, cwds);
        c.name = fit(tok::font(s, tok::t::kCaption), tok::size(tok::t::kCaption), name, max_w, &c.name_w);
        c.where = fit(s.fonts.ui, tok::size(tok::t::kCaption), where, max_w, &c.where_w);
    }
    // Flowers and tufts where no creature stands (the same every time for the same field).
    g.flowers.clear();
    for (int i = 0; i < 60 && g.flowers.size() < 22; ++i) {
        const std::uint32_t r =
            gdn::hash_id(std::string_view(reinterpret_cast<const char*>(&i), sizeof i)) ^ 0x9e3779b9u;
        const ImVec2 at(0.03f + 0.94f * static_cast<float>(r % 997u) / 997.0f,
                        0.04f + 0.92f * static_cast<float>((r / 997u) % 991u) / 991.0f);
        // An empty garden keeps the middle free for its sign and button.
        bool clear = !g.critters.empty() || std::abs(at.x - 0.5f) > 0.2f || at.y < 0.2f || at.y > 0.8f;
        for (std::size_t k = 0; k < g.spots.size() && clear; ++k) {
            const auto& sp = g.spots[k];
            const float dx = at.x * field.x - sp.x, dy = at.y * field.y - sp.y;
            // The creature, its little path and its two-line name under it.
            const float half = std::max(sp.size * 1.1f, std::min(g.field.cell_w * 0.42f, px(90)));
            clear = std::abs(dx) > half || dy < -sp.size * 1.1f || dy > px(48) + sp.size * 0.15f;
        }
        if (clear)
            g.flowers.push_back({at, static_cast<int>((r >> 9) % 5u),
                                 0.7f + 0.5f * static_cast<float>((r >> 13) % 100u) / 100.0f});
    }
    for (auto it = g.memory.begin(); it != g.memory.end();)
        it = std::any_of(g.critters.begin(), g.critters.end(),
                         [&](const Critter& c) { return c.id == it->first; })
                 ? std::next(it)
                 : g.memory.erase(it);
    g.back_tip = "Back to your layout";
    if (const std::string chord = s.keymap.chord_for("garden.toggle"); !chord.empty())
        g.back_tip += "  " + chord;
    g.refreshed = Clock::now();
}

// ---------------------------------------------------------------- one creature
struct Drawn {
    ImVec2 head;   // the body's centre this frame
    float facing;  // -1 left, +1 right
};

Drawn draw_critter(ImDrawList* dl, const Critter& c, ImVec2 home, float S, const gdn::PoseShape& p,
                   double clock, float range, bool still, const Sky& k, int hidden_kids, App::State& s) {
    const float ph = c.look.phase * 2.0f * kPi;
    const auto t = static_cast<float>(clock);
    // Travel along the little path, and which way it faces.
    float dx = 0.0f, facing = (c.look.seed & 1u) != 0 ? 1.0f : -1.0f;
    if (p.travel > 0.01f && !still) {
        const float u = t * 0.0011f + ph;
        dx = std::sin(u) * range * p.travel;
        facing = std::cos(u) >= 0 ? 1.0f : -1.0f;
    }
    // Hops: Working trots in small hops; Done's bounce is higher.
    float lift = 0.0f, squash = 0.0f, landing_age = 1.0f;
    if (p.hop > 0.01f) {
        const float rate = p.travel > 0.5f ? 0.0105f : 0.0068f;
        const float amp = (p.travel > 0.5f ? 0.13f : 0.26f) * S;
        const float u = still ? 0.45f * kPi : t * rate + ph * 3.0f;
        const float sv = std::abs(std::sin(u));
        lift = sv * amp * p.hop;
        squash = (1.0f - std::min(1.0f, sv * 4.0f)) * 0.08f * p.hop;
        landing_age = std::fmod(u, kPi) / kPi;  // 0 right after a landing
    }
    const float breathe = still ? 0.0f : std::sin(t * 0.0021f + ph) * 0.025f * (0.4f + p.sit);
    const ImVec2 feet(home.x + dx, home.y);
    const float rx = 0.42f * S * c.look.width * (1.0f + 0.05f * p.sit + squash);
    const float ry = 0.38f * S * (1.0f - 0.12f * p.sit - squash + breathe);
    const ImVec2 bc(feet.x, feet.y - lift - ry - 0.06f * S * (1.0f - p.sit));
    const float look = facing * (1.0f - p.front);

    const ImU32 body = mix(c.body, c.look.tone >= 0 ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 255),
                           std::abs(c.look.tone));
    const ImU32 shade = mix(body, IM_COL32(0, 0, 0, 255), 0.22f);
    const ImU32 light = mix(body, IM_COL32(255, 255, 255, 255), 0.45f);
    const ImU32 ink = k.ink;

    // Shadow on the ground (smaller while in the air), dust puffs behind after a landing.
    dl->AddEllipseFilled(ImVec2(feet.x, feet.y), ImVec2(rx * (1.0f - lift / S), 0.08f * S),
                         IM_COL32(0, 0, 0, night() ? 70 : 34), 0, 24);
    if (p.travel > 0.5f && !still && landing_age < 0.6f) {
        const float age = landing_age / 0.6f;
        for (int i = 0; i < 3; ++i) {
            const float off = (0.30f + 0.16f * static_cast<float>(i)) * S;
            dl->AddCircleFilled(ImVec2(feet.x - facing * off * (0.8f + age * 0.5f),
                                       feet.y - 0.04f * S - age * 0.08f * S * static_cast<float>(i)),
                                (0.04f + 0.03f * age) * S * (1.0f - 0.2f * static_cast<float>(i)),
                                with_a(k.path, (1.0f - age) * 0.8f), 10);
        }
    }
    // Ears and tail go behind the body.
    const ImVec2 top(bc.x + look * 0.04f * S, bc.y - ry * 0.86f);
    switch (c.look.ears) {
        case gdn::Ears::kRound:
            for (float side : {-1.0f, 1.0f}) {
                const ImVec2 e(top.x + side * rx * 0.55f, top.y + 0.02f * S);
                dl->AddCircleFilled(e, 0.12f * S, body, 20);
                dl->AddCircleFilled(e, 0.065f * S, mix(light, IM_COL32(255, 160, 170, 255), 0.35f), 16);
            }
            break;
        case gdn::Ears::kPointy:
            for (float side : {-1.0f, 1.0f}) {
                const ImVec2 b0(top.x + side * rx * 0.25f, top.y + 0.10f * S);
                const ImVec2 b1(top.x + side * rx * 0.80f, top.y + 0.14f * S);
                const ImVec2 tip(top.x + side * rx * 0.70f, top.y - 0.20f * S);
                dl->AddTriangleFilled(b0, b1, tip, body);
                dl->AddTriangleFilled(ImVec2(b0.x + side * 0.03f * S, b0.y),
                                      ImVec2(b1.x - side * 0.05f * S, b1.y - 0.02f * S),
                                      ImVec2(tip.x - side * 0.01f * S, tip.y + 0.08f * S),
                                      mix(light, IM_COL32(255, 160, 170, 255), 0.3f));
            }
            break;
        case gdn::Ears::kAntenna: {
            const float bob = still ? 0.0f : std::sin(t * 0.006f + ph) * 0.03f * S * (0.3f + p.hop);
            const ImVec2 base(top.x + 0.06f * S, top.y + 0.06f * S);
            const ImVec2 tip(top.x + 0.20f * S + look * 0.04f * S, top.y - 0.26f * S + bob);
            dl->AddBezierQuadratic(base, ImVec2(base.x - 0.02f * S, top.y - 0.16f * S), tip, shade,
                                   std::max(1.5f, 0.035f * S), 12);
            dl->AddCircleFilled(tip, 0.06f * S, mix(g_pal->focus, IM_COL32(255, 255, 255, 255), 0.35f), 14);
            break;
        }
        case gdn::Ears::kSprout: {
            const ImU32 leaf = mix(g_pal->done, IM_COL32(255, 255, 255, 255), night() ? 0.15f : 0.0f);
            const ImVec2 base(top.x, top.y + 0.06f * S);
            const ImVec2 stem(top.x + 0.01f * S, top.y - 0.12f * S);
            dl->AddLine(base, stem, mix(leaf, IM_COL32(0, 0, 0, 255), 0.2f), std::max(1.5f, 0.03f * S));
            dl->AddEllipseFilled(ImVec2(stem.x - 0.08f * S, stem.y - 0.02f * S),
                                 ImVec2(0.09f * S, 0.045f * S), leaf, 0.5f, 14);
            dl->AddEllipseFilled(ImVec2(stem.x + 0.08f * S, stem.y - 0.04f * S),
                                 ImVec2(0.09f * S, 0.045f * S), leaf, -0.5f, 14);
            break;
        }
    }
    const float back = -(look != 0.0f ? (look > 0 ? 1.0f : -1.0f) : (facing));
    switch (c.look.tail) {
        case gdn::Tail::kCurl: {
            const ImVec2 s0(bc.x + back * rx * 0.85f, bc.y + ry * 0.35f);
            dl->AddBezierCubic(s0, ImVec2(s0.x + back * 0.30f * S, s0.y + 0.02f * S),
                               ImVec2(s0.x + back * 0.30f * S, s0.y - 0.24f * S),
                               ImVec2(s0.x + back * 0.12f * S, s0.y - 0.18f * S), shade,
                               std::max(2.0f, 0.07f * S), 16);
            break;
        }
        case gdn::Tail::kPuff:
            dl->AddCircleFilled(ImVec2(bc.x + back * rx * 0.92f, bc.y + ry * 0.45f), 0.11f * S, light, 16);
            break;
        case gdn::Tail::kNone:
            break;
    }
    // Feet (stepping while trotting, forward while sitting).
    for (float side : {-1.0f, 1.0f}) {
        const float step =
            p.travel > 0.5f && !still
                ? std::max(0.0f, std::sin(t * 0.021f + ph + (side > 0 ? kPi : 0.0f))) * 0.05f * S
                : 0.0f;
        const ImVec2 f(feet.x + side * rx * 0.48f + look * 0.04f * S * p.sit,
                       feet.y - lift * 0.6f - 0.04f * S - step);
        dl->AddEllipseFilled(f, ImVec2(0.12f * S, 0.06f * S), shade, 0, 14);
    }
    // Body: a darker underside, the body, its sheen, the belly.
    dl->AddEllipseFilled(ImVec2(bc.x, bc.y + 0.03f * S), ImVec2(rx, ry), shade, 0, 40);
    dl->AddEllipseFilled(bc, ImVec2(rx, ry * 0.96f), body, 0, 40);
    dl->AddEllipseFilled(ImVec2(bc.x - rx * 0.35f, bc.y - ry * 0.45f), ImVec2(rx * 0.32f, ry * 0.18f),
                         with_a(IM_COL32(255, 255, 255, 255), 0.18f), -0.35f, 20);
    dl->AddEllipseFilled(ImVec2(bc.x + look * 0.06f * S, bc.y + ry * 0.38f), ImVec2(rx * 0.55f, ry * 0.42f),
                         with_a(light, 0.75f), 0, 28);
    if (c.look.spots) {
        dl->AddCircleFilled(ImVec2(bc.x + back * rx * 0.55f, bc.y - ry * 0.35f), 0.05f * S,
                            with_a(light, 0.7f), 12);
        dl->AddCircleFilled(ImVec2(bc.x + back * rx * 0.36f, bc.y - ry * 0.55f), 0.035f * S,
                            with_a(light, 0.7f), 12);
    }
    // Face: eyes (open dots, closed lines, or smiling arcs), cheeks, a small mouth.
    const ImVec2 face(bc.x + look * 0.13f * S, bc.y - ry * 0.12f);
    const float gap = 0.15f * S, er = std::max(1.2f, 0.048f * S);
    for (float side : {-1.0f, 1.0f}) {
        const ImVec2 e(face.x + side * gap, face.y);
        if (p.happy > 0.5f && p.eyes > 0.5f) {
            dl->PathArcTo(ImVec2(e.x, e.y + er * 0.5f), er * 1.05f, kPi * 1.15f, kPi * 1.85f, 10);
            dl->PathStroke(ink, 0, std::max(1.2f, er * 0.55f));
        } else if (p.eyes < 0.5f) {
            dl->PathArcTo(ImVec2(e.x, e.y - er * 0.4f), er * 1.0f, kPi * 0.15f, kPi * 0.85f, 10);
            dl->PathStroke(ink, 0, std::max(1.2f, er * 0.5f));
        } else {
            dl->AddEllipseFilled(e, ImVec2(er, er * (0.6f + 0.4f * p.eyes)), ink, 0, 14);
            dl->AddCircleFilled(ImVec2(e.x + er * 0.35f, e.y - er * 0.35f), er * 0.35f,
                                IM_COL32(255, 255, 255, 230), 8);
        }
        dl->AddEllipseFilled(ImVec2(e.x + side * 0.07f * S, e.y + 0.08f * S), ImVec2(0.05f * S, 0.03f * S),
                             IM_COL32(255, 120, 140, 90), 0, 12);
    }
    if (p.wave > 0.5f) {  // a small "o": calling you
        dl->AddEllipseFilled(ImVec2(face.x, face.y + 0.09f * S), ImVec2(0.03f * S, 0.035f * S), ink, 0, 10);
    } else if (p.eyes > 0.5f || p.happy > 0.5f) {
        dl->PathArcTo(ImVec2(face.x, face.y + 0.06f * S), 0.04f * S, kPi * 0.15f, kPi * 0.85f, 8);
        dl->PathStroke(ink, 0, std::max(1.0f, 0.02f * S));
    }
    // The waving arm (resting at the side otherwise).
    {
        // Angles from the shoulder: down along the side at rest, up and out while waving.
        const float rest = 1.25f, up = -1.05f + (still ? 0.0f : 0.42f * std::sin(t * 0.012f + ph));
        const float ang = rest + (up - rest) * p.wave;
        const float side = p.wave > 0.05f ? 1.0f : -facing;
        const ImVec2 sh(bc.x + side * rx * 0.82f, bc.y + ry * 0.05f);
        const ImVec2 hand(sh.x + side * std::cos(ang) * 0.24f * S, sh.y + std::sin(ang) * 0.24f * S);
        const float thick = std::max(2.0f, 0.09f * S);
        if (p.wave > 0.05f || p.sit < 0.5f) {
            dl->AddLine(sh, hand, body, thick);
            dl->AddCircleFilled(hand, thick * 0.62f, body, 12);
        }
    }
    // The "!" bubble: needs you.
    if (p.bubble > 0.02f) {
        const float pop = std::min(1.0f, p.bubble * 1.15f);
        const float bob = still ? 0.0f : std::sin(t * 0.005f + ph) * 0.03f * S;
        const float bw = std::max(px(14), 0.36f * S) * pop, bh = std::max(px(16), 0.40f * S) * pop;
        const ImVec2 at(bc.x + rx * 0.55f, bc.y - ry - 0.30f * S + bob);
        const ImVec2 ba(at.x - bw * 0.5f, at.y - bh * 0.5f), bb(at.x + bw * 0.5f, at.y + bh * 0.5f);
        dl->AddTriangleFilled(ImVec2(ba.x + bw * 0.2f, bb.y - px(2)), ImVec2(ba.x + bw * 0.5f, bb.y - px(2)),
                              ImVec2(ba.x + bw * 0.05f, bb.y + bh * 0.28f), g_pal->needs_you);
        dl->AddRectFilled(ba, bb, g_pal->needs_you, bw * 0.38f);
        const float fs = bh * 0.72f;
        const ImVec2 ts = s.fonts.ui_bold->CalcTextSizeA(fs, FLT_MAX, 0, "!");
        dl->AddText(s.fonts.ui_bold, fs, ImVec2(at.x - ts.x * 0.5f, at.y - ts.y * 0.5f), accent_foreground(),
                    "!");
    }
    // Done's sparkle.
    if (p.sparkle > 0.02f) {
        const ImU32 col = mix(g_pal->done, IM_COL32(255, 255, 255, 255), 0.45f);
        const float tw = still ? 1.0f : 0.75f + 0.25f * std::sin(t * 0.009f + ph);
        sparkle(dl, ImVec2(bc.x + rx * 1.05f, bc.y - ry * 1.05f), 0.13f * S * tw, with_a(col, p.sparkle));
        sparkle(dl, ImVec2(bc.x - rx * 1.0f, bc.y - ry * 0.55f), 0.08f * S * (1.6f - tw),
                with_a(col, p.sparkle * 0.85f));
        sparkle(dl, ImVec2(bc.x + rx * 0.35f, bc.y - ry * 1.55f), 0.06f * S, with_a(col, p.sparkle * 0.6f));
    }
    // Sleep: "z z" drifting up.
    if (p.zzz > 0.02f) {
        for (int i = 0; i < 2; ++i) {
            const float u = still ? 0.35f + 0.4f * static_cast<float>(i)
                                  : static_cast<float>(std::fmod(
                                        clock * 0.00045 + static_cast<double>(c.look.phase) + 0.5 * i, 1.0));
            const float fs = std::max(fpx(10), (0.20f + 0.08f * static_cast<float>(i)) * S);
            const ImVec2 at(bc.x + rx * (0.65f + 0.5f * u) + static_cast<float>(i) * 0.06f * S,
                            bc.y - ry * (0.9f + 1.1f * u));
            const float a = p.zzz * std::sin(u * kPi);
            dl->AddText(s.fonts.ui_bold, fs, at, with_a(mix(g_pal->text, k.top, 0.25f), a), "z");
        }
    }
    // Sub-agents past the two shown: "+n" beside the parent.
    if (hidden_kids > 0) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "+%d", hidden_kids);
        const float fs = tok::size(tok::t::kCaption);
        const ImVec2 ts = tok::measure(s, tok::t::kCaption, buf);
        const ImVec2 at(bc.x + rx * 1.9f, feet.y - ts.y - px(2));
        dl->AddRectFilled(ImVec2(at.x - px(5), at.y - px(1)),
                          ImVec2(at.x + ts.x + px(5), at.y + ts.y + px(1)), with_a(g_pal->card, 0.85f),
                          px(8));
        (void)fs;
        tok::text(s, dl, tok::t::kCaption, at, g_pal->dim, buf);
    }
    return {bc, facing};
}

// ---------------------------------------------------------------- hover card and menu
const char* state_word(Attention a) {
    switch (a) {
        case Attention::kBlocked:
            return "Needs you";
        case Attention::kWorking:
            return "Working";
        case Attention::kDone:
            return "Done";
        case Attention::kIdle:
            return "Idle";
        case Attention::kNone:
            break;
    }
    return "No status";
}

ImU32 state_color(Attention a) {
    switch (a) {
        case Attention::kBlocked:
            return g_pal->needs_you;
        case Attention::kWorking:
            return g_pal->working;
        case Attention::kDone:
            return g_pal->done;
        case Attention::kIdle:
            return g_pal->idle;
        case Attention::kNone:
            break;
    }
    return g_pal->faint;
}

void build_card(App::State& s, GardenState& g, const std::string& id) {
    const auto& v = s.views_by_id[id];
    g.hover_id = id;
    g.hover_built = tok::now_ms();
    g.card_title = agent_detail(s, id, v);
    const std::string program = program_of(v).name;
    if (g.card_title.empty())
        g.card_title = s.title_of(v);
    g.card_where = program;
    if (const auto loc = s.ws.find(id); loc.space >= 0)
        g.card_where += " \xC2\xB7 " + s.ws.space_title(loc.space, s.session_cwds());
    else if (!v.parent.empty())
        g.card_where += " \xC2\xB7 sub-agent";
    const Attention a = s.attention_of(id);
    g.card_state = state_word(a);
    if (const auto ch = s.changed.find(id); ch != s.changed.end() && ch->second.at > 0)
        g.card_state += " \xC2\xB7 for " + ui::focus::short_ago(now_ms() - ch->second.at);
    g.card_message = ui::mission::clean_message(v.agent_message);
    if (g.card_message.empty() && a == Attention::kBlocked && !v.agent_kind.empty()) {
        g.card_message = v.agent_kind;
        g.card_message[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(g.card_message[0])));
    }
}

// The card beside a hovered creature: name, program and space, state and how long, last message.
void draw_card(App::State& s, GardenState& g, const std::string& id, ImVec2 anchor, float S, ImVec2 lo,
               ImVec2 hi) {
    if (g.hover_id != id || tok::now_ms() - g.hover_built > 1000.0)
        build_card(s, g, id);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float w = px(240), pad = tok::gap_m();
    const auto& v = s.views_by_id[id];
    const Attention a = s.attention_of(id);
    // Height: title, where, state, up to two lines of message, the hint.
    const float wrap = w - pad * 2;
    const float msg_h =
        g.card_message.empty()
            ? 0.0f
            : std::min(
                  s.fonts.ui->CalcTextSizeA(tok::size(tok::t::kBody), FLT_MAX, wrap, g.card_message.c_str())
                      .y,
                  tok::size(tok::t::kBody) * 2.6f) +
                  tok::gap_s();
    const float h = pad + px(20) + px(16) + tok::gap_s() + px(18) + msg_h + tok::gap_s() + px(16) + pad;
    float x = anchor.x + S * 0.7f;
    if (x + w > hi.x)
        x = anchor.x - S * 0.7f - w;
    x = std::clamp(x, lo.x + px(4), hi.x - w - px(4));
    const float y = std::clamp(anchor.y - h * 0.5f, lo.y + px(4), hi.y - h - px(4));
    const float in = tok::appear(ImGui::GetID("garden-card"), ui::tokens::kFastMs);
    const ImVec2 ca(x, y + (1.0f - in) * px(4)), cb(ca.x + w, ca.y + h);
    const int from = dl->VtxBuffer.Size;
    tok::card(dl, ca, cb, tok::radius_card(), 1);
    float cy = ca.y + pad;
    program_icon(dl, program_of(v), ImVec2(ca.x + pad, cy + px(2)), px(16), g_pal->text);
    text_clipped(dl, tok::font(s, tok::t::kTitle), tok::size(tok::t::kTitle), ImVec2(ca.x + pad + px(22), cy),
                 g_pal->text, g.card_title, wrap - px(22));
    cy += px(20);
    text_clipped(dl, s.fonts.ui, tok::size(tok::t::kCaption), ImVec2(ca.x + pad, cy), g_pal->dim,
                 g.card_where, wrap);
    cy += px(16) + tok::gap_s();
    dl->AddCircleFilled(ImVec2(ca.x + pad + px(4), cy + px(8)), px(4), state_color(a), 12);
    tok::text(s, dl, tok::t::kBody, ImVec2(ca.x + pad + px(14), cy),
              a == Attention::kBlocked ? g_pal->needs_you : g_pal->text, g.card_state.c_str());
    cy += px(18);
    if (!g.card_message.empty()) {
        cy += tok::gap_s();
        dl->PushClipRect(ImVec2(ca.x, cy), ImVec2(cb.x, cy + msg_h - tok::gap_s()), true);
        dl->AddText(s.fonts.ui, tok::size(tok::t::kBody), ImVec2(ca.x + pad, cy), g_pal->dim,
                    g.card_message.c_str(), nullptr, wrap);
        dl->PopClipRect();
        cy += msg_h - tok::gap_s();
    }
    cy += tok::gap_s();
    tok::hline(dl, ca.x + pad, cb.x - pad, cy, tok::divider());
    tok::text(s, dl, tok::t::kCaption, ImVec2(ca.x + pad, cy + px(4)), g_pal->faint,
              "Click to open \xC2\xB7 right-click for more");
    fade_since(dl, from, in);
}

void draw_menu(App::State& s, const std::string& id) {
    if (!begin_menu("##gardenctx"))
        return;
    const bool has_pane = s.ws.find(id).space >= 0;
    if (menu_row(s, ui::icon::kZoom, "Open"))
        run_command(s, "garden.open:" + id);
    if (menu_row(s, ui::icon::kSearch, "Peek"))
        run_command(s, "agent.peek:" + id);
    if (menu_row(s, ui::icon::kEdit, "Rename", s.keymap.chord_for("pane.rename"), has_pane)) {
        s.ws.focus(id);
        run_command(s, "pane.rename");
    }
    menu_separator();
    if (menu_row(s, ui::icon::kClose, "Close pane", s.keymap.chord_for("pane.close"), has_pane)) {
        s.ws.focus(id);
        run_command(s, "pane.close");
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- empty garden
// The sign's board is 46 px high, its posts reach kPostH px below it.
constexpr float kSignH = 46.0f;
constexpr float kPostH = 18.0f;

void draw_sign(App::State& s, ImDrawList* dl, ImVec2 c, const Sky& k) {
    const char* line = "No agents yet \xE2\x80\x94 start one with +";
    const ImVec2 ts = tok::measure(s, tok::t::kBody, line);
    const float bw = ts.x + tok::gap_xl() * 2, bh = px(kSignH);
    const ImU32 wood =
        mix(mix(avatar_color("ansi3"), IM_COL32(120, 80, 50, 255), 0.5f), g_pal->bg, night() ? 0.35f : 0.15f);
    const ImU32 wood_dark = mix(wood, IM_COL32(0, 0, 0, 255), 0.25f);
    const ImU32 face = mix(g_pal->card, wood, 0.16f);
    // Two posts into the grass, their shadow, then the board.
    for (float side : {-1.0f, 1.0f}) {
        const float x = c.x + side * bw * 0.32f;
        dl->AddRectFilled(ImVec2(x - px(4), c.y - px(4)), ImVec2(x + px(4), c.y + bh * 0.5f + px(kPostH)),
                          wood_dark, px(2));
    }
    dl->AddEllipseFilled(ImVec2(c.x, c.y + bh * 0.5f + px(kPostH)), ImVec2(bw * 0.42f, px(4)),
                         IM_COL32(0, 0, 0, night() ? 60 : 30), 0, 24);
    const ImVec2 a(c.x - bw * 0.5f, c.y - bh * 0.5f), b(c.x + bw * 0.5f, c.y + bh * 0.5f);
    dl->AddRectFilled(ImVec2(a.x, a.y + px(2)), ImVec2(b.x, b.y + px(3)), wood_dark, px(9));
    dl->AddRectFilled(a, b, wood, px(9));
    dl->AddRectFilled(ImVec2(a.x + px(4), a.y + px(4)), ImVec2(b.x - px(4), b.y - px(4)), face, px(6));
    tok::text(s, dl, tok::t::kBody, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), g_pal->text, line);
    (void)k;
}

bool draw_unavailable(App::State& s, ImVec2 pos, ImVec2 size) {
    if (s.host->connected())
        return false;
    StateCard card;
    card.framed = true;
    if (s.machine_loading("")) {
        card.kind = CardKind::kLoading;
        card.title = "Connecting to agent-host\xE2\x80\xA6";
    } else {
        card.kind = CardKind::kError;
        card.title = "agent-host is not running";
        card.hint = "The garden shows the agents agent-host runs.";
        card.detail = s.host->last_error();
        card.detail_color = g_pal->needs_you;
        card.buttons = {{"Retry", "", [&s] { s.host->request_start_host(); }}};
    }
    const ImVec2 cs(std::min(size.x - px(32), px(420)), std::min(size.y - px(32), px(220)));
    draw_state_card(s, ImVec2(pos.x + (size.x - cs.x) * 0.5f, pos.y + (size.y - cs.y) * 0.5f), cs, card);
    return true;
}

int count_of(App::State& s, const GardenState& g, Attention a) {
    int n = 0;
    for (std::size_t i = 0; i < g.critters.size(); ++i)  // agents, as the sidebar counts them
        n += g.parents[i] < 0 && s.attention_of(g.critters[i].id) == a ? 1 : 0;
    return n;
}
}  // namespace

// ---------------------------------------------------------------- blocks
void draw_garden_bar(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + w, pos.y + h);
    const Sky k = sky_colors();
    dl->AddRectFilled(pos, end, k.top);
    tok::hline(dl, pos.x, end.x, end.y - 1.0f, with_a(tok::divider(k.top), 0.5f));
    const float cy = pos.y + h * 0.5f;
    float x = clear_of_traffic_lights(s, pos.x + tok::gap_l(), pos.y, end.y, px(14));
    float right = end.x - px(10);
    if (windows_window_buttons(s)) {
        draw_window_buttons(s, right, pos.y, h);
        right -= px(26) * 3 + px(4) + px(8);
    }
    // Right: back to your layout, Settings, new.
    const float b = px(28);
    right -= b;
    if (icon_button(s, "##garden-back", ui::icon::kLayers, ImVec2(right, cy - b * 0.5f), b, fpx(15),
                    g_garden.back_tip.empty() ? "Back to your layout" : g_garden.back_tip.c_str(),
                    g_pal->dim))
        run_command(s, "garden.toggle");
    right -= b + px(2);
    if (icon_button(s, "##garden-settings", ui::icon::kSettings, ImVec2(right, cy - b * 0.5f), b, fpx(15),
                    "Settings", g_pal->dim))
        run_command(s, "app.settings");
    right -= b + px(2);
    if (icon_button(s, "##garden-new", ui::icon::kPlus, ImVec2(right, cy - b * 0.5f), b, fpx(15), "New",
                    g_pal->dim))
        ImGui::OpenPopup("##newmenu");
    draw_new_menu(s);
    // Left: a small sprout and the name, then the counts that matter.
    const float leaf = px(7);
    const ImU32 green = g_pal->done;
    dl->AddLine(ImVec2(x + leaf, cy + px(6)), ImVec2(x + leaf, cy - px(1)), green, px(1.6f));
    dl->AddEllipseFilled(ImVec2(x + leaf - px(4), cy - px(3)), ImVec2(px(4.5f), px(2.4f)), green, 0.55f, 14);
    dl->AddEllipseFilled(ImVec2(x + leaf + px(4), cy - px(5)), ImVec2(px(4.5f), px(2.4f)), green, -0.55f, 14);
    x += leaf * 2 + tok::gap_s();
    const ImVec2 ts = tok::measure(s, tok::t::kTitle, "Garden");
    tok::text(s, dl, tok::t::kTitle, ImVec2(x, cy - ts.y * 0.5f), g_pal->text, "Garden");
    x += ts.x + tok::gap_l();
    if (s.host->connected()) {
        struct Count {
            Attention a;
            const char* one;
            const char* many;
        };
        static constexpr Count kCounts[] = {{Attention::kBlocked, "needs you", "need you"},
                                            {Attention::kWorking, "working", "working"},
                                            {Attention::kDone, "done", "done"}};
        char buf[48];
        for (const auto& c : kCounts) {
            const int n = count_of(s, g_garden, c.a);
            if (n <= 0)
                continue;
            std::snprintf(buf, sizeof buf, "%d %s", n, n == 1 ? c.one : c.many);
            const ImVec2 t = tok::measure(s, tok::t::kCaption, buf);
            if (x + t.x + px(24) > right - tok::gap_l())
                break;
            const ImU32 col = state_color(c.a);
            const ImVec2 pa(x, cy - px(11)), pb(x + px(18) + t.x + px(10), cy + px(11));
            dl->AddRectFilled(pa, pb, with_a(col, 0.16f), px(11));
            dl->AddCircleFilled(ImVec2(x + px(10), cy), px(3.5f), col, 12);
            tok::text(s, dl, tok::t::kCaption, ImVec2(x + px(18), cy - t.y * 0.5f),
                      c.a == Attention::kBlocked ? col : g_pal->text, buf);
            x = pb.x + tok::gap_s();
        }
    }
    window_drag_area(s, pos, end);
}

void draw_garden_scene(App::State& s, ImVec2 pos, ImVec2 size) {
    GardenState& g = g_garden;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + size.x, pos.y + size.y);
    const bool still = tok::reduce_motion();
    const bool focused = s.window_focused;
    // The creatures' clock: frames land on slots of the pace in use, so the frames drawn for one
    // wake show the same instant; behind other windows it runs slower.
    const int interval = gdn::frame_interval_ms(focused, still, g.motion);
    double real = tok::now_ms();
    if (interval > 0)
        real -= std::fmod(real, static_cast<double>(interval));
    const double dt = g.shown ? std::clamp(real - g.last_real, 0.0, 300.0) : 0.0;
    g.clock += dt * static_cast<double>(gdn::clock_speed(focused));
    g.last_real = real;
    g.shown = true;

    const Sky k = sky_colors();
    ImGui::PushClipRect(pos, end, true);
    const float horizon = g.crowded ? kHorizonCrowded : kHorizon;
    draw_backdrop(dl, pos, end, k, g.clock, still, horizon);
    // The field: the ground below the hills, with margins.
    const float hy = pos.y + size.y * horizon;
    const ImVec2 fa(pos.x + tok::gap_xl(), hy + size.y * 0.06f);
    const ImVec2 fb(end.x - tok::gap_xl(), end.y - tok::gap_s());
    const ImVec2 field(std::max(1.0f, fb.x - fa.x), std::max(1.0f, fb.y - fa.y));
    if (draw_unavailable(s, pos, size)) {
        g.motion = gdn::Motion::kStill;
        ImGui::PopClipRect();
        return;
    }
    if (Clock::now() - g.refreshed >= kRefreshEvery || std::abs(field.x - g.field_size.x) > 0.5f ||
        std::abs(field.y - g.field_size.y) > 0.5f)
        refresh(s, g, field);
    for (const auto& f : g.flowers)
        flower(dl, ImVec2(fa.x + f.at.x * field.x, fa.y + f.at.y * field.y), px(13) * f.size, f.kind, k);
    if (g.critters.empty()) {
        g.motion = gdn::Motion::kStill;
        const ImVec2 c(pos.x + size.x * 0.5f, fa.y + field.y * 0.42f);
        draw_sign(s, dl, c, k);
        // One way in, right under the sign.
        const std::string label = with_chord(s, "Start a program\xE2\x80\xA6", "agent.new");
        const ImVec2 bs = settings_button_size(s, label.c_str());
        ImGui::SetCursorScreenPos(
            ImVec2(c.x - bs.x * 0.5f, c.y + px(kSignH) * 0.5f + px(kPostH) + tok::gap_l()));
        ImGui::PushStyleColor(ImGuiCol_Button, v4(g_pal->focus));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              v4(mix(g_pal->focus, IM_COL32(255, 255, 255, 255), 0.12f)));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, v4(mix(g_pal->focus, IM_COL32(0, 0, 0, 255), 0.1f)));
        ImGui::PushStyleColor(ImGuiCol_Text, v4(accent_foreground()));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tok::radius_small());
        if (ImGui::Button(label.c_str(), bs))
            run_command(s, "agent.new");
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        ImGui::PopClipRect();
        return;
    }

    // Poses, easing from the last one when a state changed.
    const long long wall = now_ms();
    gdn::Motion motion = gdn::Motion::kStill;
    int hovered = -1;
    ImVec2 hovered_at{};
    float hovered_size = 0;
    // Back rows first, sub-agents (a little behind) before their parent.
    for (int row = 0; row < std::max(1, g.field.rows); ++row) {
        for (int pass = 0; pass < 2; ++pass) {
            for (std::size_t i = 0; i < g.critters.size(); ++i) {
                const gdn::Spot& sp = g.spots[i];
                const bool sub = g.parents[i] >= 0;
                if (sp.row != row || sub != (pass == 0) || !sp.shown)
                    continue;
                const Critter& c = g.critters[i];
                const Attention att = sub ? ui::kid_status(s.attention_of(c.id)) : s.attention_of(c.id);
                long long since = 0;
                if (const auto ch = s.changed.find(c.id); ch != s.changed.end() && ch->second.at > 0)
                    since = wall - ch->second.at;
                const gdn::Pose pose = gdn::pose_for(att, since);
                PoseMemory& m = g.memory[c.id];
                const double now = tok::now_ms();
                if (m.since < -1e8) {  // first sight: no easing in
                    m.pose = pose;
                    m.shown = gdn::shape_of(pose);
                    m.from = m.shown;
                    m.since = now - gdn::kPoseEaseMs;
                } else if (m.pose != pose) {
                    m.from = m.shown;
                    m.pose = pose;
                    m.since = now;
                }
                const float u =
                    still ? 1.0f
                          : static_cast<float>(std::clamp((now - m.since) / gdn::kPoseEaseMs, 0.0, 1.0));
                m.shown = gdn::mix(m.from, gdn::shape_of(pose), ui::tokens::ease_in_out(u));
                if (u < 1.0f)
                    motion = gdn::Motion::kLively;
                motion = std::max(motion, gdn::motion_of(pose));
                const ImVec2 home(fa.x + sp.x, fa.y + sp.y);
                const float S = sp.size;
                // A little path under a creature at work.
                const float range = std::min(g.field.cell_w * 0.17f, S * 0.75f) * (sub ? 0.7f : 1.0f);
                if (m.shown.travel > 0.02f) {
                    for (int d = -4; d <= 4; ++d)
                        dl->AddEllipseFilled(
                            ImVec2(home.x + static_cast<float>(d) * (range + S * 0.3f) / 4.0f,
                                   home.y + px(1)),
                            ImVec2(std::max(1.5f, S * 0.035f), std::max(1.0f, S * 0.018f)),
                            with_a(k.path, 0.9f * m.shown.travel), 0, 8);
                }
                const int from = dl->VtxBuffer.Size;
                const int hidden = sub ? 0 : sp.hidden_kids;
                const Drawn d = draw_critter(dl, c, home, S, m.shown, g.clock, range, still, k, hidden, s);
                fade_since(dl, from, m.shown.alpha);
                (void)d;
                // Its name under it (agents only).
                if (!sub) {
                    const float ly = home.y + px(8);
                    tok::text(s, dl, tok::t::kCaption, ImVec2(home.x - c.name_w * 0.5f, ly),
                              with_a(g_pal->text, 0.5f + 0.5f * m.shown.alpha), c.name.c_str());
                    if (!c.where.empty() && !g.crowded)
                        dl->AddText(s.fonts.ui, tok::size(tok::t::kCaption),
                                    ImVec2(home.x - c.where_w * 0.5f, ly + px(15)),
                                    night() ? g_pal->dim : mix(g_pal->dim, g_pal->text, 0.25f),
                                    c.where.c_str());
                }
                // Hover, click and right-click on its spot (where it lives, so a hopping one is
                // easy to hit).
                const float hw = std::max(S * 0.6f, range + S * 0.45f);
                const ImVec2 ha(home.x - hw, home.y - S * 1.15f);
                const ImVec2 hb(home.x + hw, home.y + (sub ? px(4) : px(36)));
                ImGui::SetCursorScreenPos(ha);
                ImGui::PushID(c.id.c_str());
                const bool clicked = ImGui::InvisibleButton(
                    "##critter", ImVec2(hb.x - ha.x, hb.y - ha.y),
                    ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
                const bool hov = ImGui::IsItemHovered();
                const bool right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
                if (right)
                    g.menu_id = c.id;
                draw_menu(s, c.id);
                ImGui::PopID();
                if (hov) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    hovered = static_cast<int>(i);
                    hovered_at = ImVec2(home.x, home.y - S * 0.5f);
                    hovered_size = S;
                }
                if (clicked && !right && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
                    run_command(s, "garden.open:" + c.id);
            }
        }
    }
    g.motion = motion;
    ImGui::PopClipRect();
    if (hovered >= 0 && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        draw_card(s, g, g.critters[static_cast<std::size_t>(hovered)].id, hovered_at, hovered_size, pos, end);
    else
        tok::forget(ImGui::GetID("garden-card"));
}

void garden_layout_preview(ImDrawList* d, ImVec2 a, ImVec2 size) {
    const ImVec2 b(a.x + size.x, a.y + size.y);
    const Sky k = sky_colors();
    const float hy = a.y + size.y * 0.42f;
    d->PushClipRect(a, b, true);
    d->AddRectFilled(a, b, k.ground_top, px(6));
    d->AddRectFilledMultiColor(ImVec2(a.x, a.y + px(3)), ImVec2(b.x, hy), k.top, k.top, k.low, k.low);
    d->AddRectFilled(a, ImVec2(b.x, a.y + px(6)), k.top, px(6), ImDrawFlags_RoundCornersTop);
    d->AddEllipseFilled(ImVec2(a.x + size.x * 0.3f, hy + px(2)), ImVec2(size.x * 0.45f, px(7)), k.hill_near,
                        0, 24);
    d->AddEllipseFilled(ImVec2(a.x + size.x * 0.82f, hy + px(3)), ImVec2(size.x * 0.35f, px(6)), k.hill_far,
                        0, 24);
    d->AddRectFilled(ImVec2(a.x, hy + px(3)), ImVec2(b.x, b.y - px(5)), k.ground_top);
    if (night())
        for (int i = 0; i < 7; ++i)
            d->AddCircleFilled(ImVec2(a.x + size.x * (0.08f + 0.13f * static_cast<float>(i)),
                                      a.y + px(5) + static_cast<float>((i * 7) % 5) * px(3)),
                               px(0.8f), with_a(g_pal->text, 0.6f), 6);
    d->AddCircleFilled(ImVec2(a.x + size.x * 0.86f, a.y + px(10)), px(4),
                       night() ? mix(g_pal->text, IM_COL32(255, 248, 225, 255), 0.5f)
                               : mix(avatar_color("ansi3"), IM_COL32(255, 236, 170, 255), 0.6f),
                       16);
    // Three creatures: one needs you, one works, one sleeps.
    static constexpr const char* kTints[] = {"ansi4", "ansi3", "ansi5"};
    for (int i = 0; i < 3; ++i) {
        const ImVec2 c(a.x + size.x * (0.22f + 0.28f * static_cast<float>(i)),
                       b.y - px(14) - static_cast<float>(i % 2) * px(4));
        const ImU32 body = avatar_color(kTints[i]);
        d->AddEllipseFilled(ImVec2(c.x, c.y + px(6)), ImVec2(px(7), px(1.5f)), IM_COL32(0, 0, 0, 50), 0, 12);
        d->AddCircleFilled(ImVec2(c.x - px(4), c.y - px(5)), px(2.5f), body, 10);
        d->AddCircleFilled(ImVec2(c.x + px(4), c.y - px(5)), px(2.5f), body, 10);
        d->AddEllipseFilled(c, ImVec2(px(7), px(6)), body, 0, 20);
        d->AddCircleFilled(ImVec2(c.x - px(2.2f), c.y - px(1)), px(0.9f), k.ink, 6);
        d->AddCircleFilled(ImVec2(c.x + px(2.2f), c.y - px(1)), px(0.9f), k.ink, 6);
        if (i == 0) {
            d->AddRectFilled(ImVec2(c.x + px(5), c.y - px(16)), ImVec2(c.x + px(11), c.y - px(9)),
                             g_pal->needs_you, px(2));
            d->AddLine(ImVec2(c.x + px(8), c.y - px(15)), ImVec2(c.x + px(8), c.y - px(12)),
                       IM_COL32(255, 255, 255, 255), px(1));
        }
    }
    d->PopClipRect();
    d->AddRect(a, b, g_pal->border, px(6));
}

long long garden_wakeup_ms(const App::State& s) {
    constexpr long long kNone = 1000;
    if (s.settings.focus_on || s.layout().style != "garden" || !g_garden.shown)
        return kNone;
    const int interval = gdn::frame_interval_ms(s.window_focused, tok::reduce_motion(), g_garden.motion);
    if (interval <= 0)
        return kNone;
    return gdn::ms_to_next_frame(static_cast<long long>(tok::now_ms()), interval);
}

std::optional<std::string> exec_garden_command(App::State& s, const std::string& name,
                                               const std::string& arg) {
    if (name == "garden.toggle")
        return toggle_layout(s, "garden");
    if (name == "garden.open") {
        const auto it = s.views_by_id.find(arg);
        if (it == s.views_by_id.end())
            return "no terminal \"" + arg + "\"";
        if (s.ws.find(arg).space < 0 && !it->second.parent.empty()) {
            // A sub-agent without a pane: a look in the peek window; the garden stays.
            s.peek = arg;
            s.views[arg].focus_next = true;
            return std::string();
        }
        if (s.ws.unsettle_session(arg))
            s.doc_changed();
        s.focus(arg);
        if (s.layout().style == "garden" && !s.settings.focus_on) {
            if (std::string err = toggle_layout(s, "garden"); !err.empty())
                return err;
            std::string how = "Layout: Garden in the palette";
            if (const std::string chord = s.keymap.chord_for("garden.toggle"); !chord.empty())
                how = chord;
            else if (const std::string pal = s.keymap.chord_for("app.palette"); !pal.empty())
                how += " (" + pal + ")";
            s.toast("Back to the garden", how, "", g_pal->info, 3);
        }
        return std::string();
    }
    return std::nullopt;
}

}  // namespace detail
}  // namespace agentdesk::desktop
