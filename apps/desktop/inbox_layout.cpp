// Inbox: a layout that lists every agent waiting for the person as a card (what it asks in its own
// report, how long it has waited, its folder's changes, the last lines of its screen) with quick
// answers, a reply field, Open and Snooze; agents that finished sit in a quieter list below. The
// agent opened from a card shows in the pane area beside the queue.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {
namespace ib = ui::inbox;
namespace mui = ui::mission;
namespace tt = ui::tokens;

namespace {
// Design sizes (px() scales them); spacing comes from tok.
constexpr float kIcon = 20.0f;
constexpr float kMonoSize = 11.0f;
constexpr float kLineH = 15.0f;
constexpr int kPreviewLines = 4;
constexpr int kMessageLines = 3;
constexpr float kButtonH = 28.0f;
constexpr float kReplyH = 34.0f;
constexpr float kDoneRowH = 56.0f;
constexpr float kSectionH = 28.0f;
constexpr float kStripe = 3.0f;
constexpr double kLeaveMs = 260.0;  // a card sliding out
constexpr double kSentMs = 6000.0;  // "Sent" stays on a card that still waits
constexpr double kZeroMs = 1100.0;  // the Inbox zero drawing's one-shot animation
constexpr auto kAnswersEvery = std::chrono::seconds(2);

struct Card {
    std::string id;
    bool done = false;
    ui::Program program;
    std::string title;
    std::string where;   // "space · detail"
    std::string branch;  // "" when not in git
    int changed = -1;    // uncommitted files; -1 unknown
    std::string message;
    std::string kind;
    long long waited_ms = 0;
    long long stamp = 0;  // when this wait started (for snoozing)
    bool unreachable = false;
    bool can_input = true;
    std::vector<ib::QuickAnswer> answers;
};

struct Queue {
    std::vector<Card> blocked;  // in card order, snoozed ones left out
    std::vector<Card> done;     // newest first
    int snoozed = 0;
    int working = 0;
    int idle = 0;
};

long long steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

ImGuiID card_key(const std::string& id) {
    return ImGui::GetID(("inbox-card/" + id).c_str());
}

// ui.json changed on disk: its inbox_answers are read again (at most every 2 s, only while the
// Inbox shows), so a person editing their quick answers sees them without a restart.
void reload_answers(App::State& s) {
    const auto now = Clock::now();
    if (now - s.inbox.answers_checked < kAnswersEvery)
        return;
    s.inbox.answers_checked = now;
    std::error_code ec;
    const auto path = data_dir() / L"ui.json";
    const auto mtime = std::filesystem::last_write_time(path, ec);
    if (ec || mtime == s.inbox.answers_mtime)
        return;
    const bool first = s.inbox.answers_mtime == std::filesystem::file_time_type{};
    s.inbox.answers_mtime = mtime;
    if (first)
        return;  // load_settings already read this version
    const Json j = Json::parse(read_text_file(path), nullptr, false);
    if (!j.is_object())
        return;
    s.settings.inbox_answer_errors.clear();
    s.settings.inbox_answers = ib::parse_answers(j.contains("inbox_answers") ? j.at("inbox_answers") : Json(),
                                                 &s.settings.inbox_answer_errors);
}

Queue collect(App::State& s) {
    Queue q;
    const AgentRoster roster = agent_roster(s);  // also stamps state changes (s.changed)
    for (const auto& id : roster.ids) {
        const Attention a = s.attention_of(id);
        q.working += a == Attention::kWorking ? 1 : 0;
        q.idle += a == Attention::kIdle ? 1 : 0;
    }
    const long long now_wall = now_ms();
    const long long now_steady = steady_ms();
    const auto cwds = s.session_cwds();
    std::vector<ib::Waiting> waiting;
    std::vector<ib::Waiting> finished;
    std::map<std::string, Card> cards;
    long long seq = 0;
    std::set<std::string> blocked_ids;
    for (const auto& sess : s.sessions) {
        const std::string& id = sess->id();
        ++seq;
        const auto vit = s.views_by_id.find(id);
        if (vit == s.views_by_id.end())
            continue;
        const auto& v = vit->second;
        const Attention a = s.attention_of(id);
        if (a != Attention::kBlocked && a != Attention::kDone)
            continue;
        Card c;
        c.id = id;
        c.done = a == Attention::kDone;
        c.program = program_of(v);
        c.title = s.title_of(v);
        if (const auto loc = s.ws.find(id); loc.space >= 0)
            c.where = s.ws.space_title(loc.space, cwds);
        else if (!v.cwd.empty())
            c.where = folder_name(v.cwd);
        if (const std::string d = agent_detail(s, id, v); !d.empty() && d != c.title)
            c.where += (c.where.empty() ? "" : " \xC2\xB7 ") + d;
        const auto mi = s.machine_of.find(id);
        const std::string machine = mi == s.machine_of.end() ? std::string() : mi->second;
        c.unreachable = !s.machine_connected(machine);
        c.can_input = sess->can_input() && !c.unreachable;
        if (!v.cwd.empty()) {
            // Only a waiting card says how many files changed: the count costs a git status, so the
            // finished rows do not ask for it (the same counts Pair's headers use).
            const GitInfo g = c.done ? s.git.get(v.cwd, machine) : s.git.with_changes(v.cwd, machine);
            if (g.repo) {
                c.branch = g.branch == "HEAD" ? std::string("detached") : g.branch;
                c.changed = g.changed_files;
            }
        }
        c.message = mui::clean_message(v.agent_message);
        c.kind = v.agent_kind;
        // How long: the persisted stamp of the blocked state when there is one (it survives a
        // restart of the window), else the moment this window saw the state change.
        long long age = -1;
        if (!c.done)
            if (const auto ch = s.changed.find(id);
                ch != s.changed.end() && ch->second.s == "blocked" && ch->second.at > 0) {
                age = std::max(0LL, now_wall - ch->second.at);
                c.stamp = ch->second.at;
            }
        if (age < 0) {
            const auto& at = s.styles.changes.changed_at;
            const auto it = at.find(id);
            age = it == at.end() ? 0 : std::max(0LL, now_steady - it->second);
            c.stamp = it == at.end() ? 0 : it->second;
        }
        c.waited_ms = age;
        if (!c.done) {
            const auto prog = c.program.key.empty() ? std::string("program") : c.program.key;
            c.answers = ib::answers_for(s.settings.inbox_answers, prog, c.kind);
            blocked_ids.insert(id);
            if (const auto z = s.inbox.snoozed.find(id); z != s.inbox.snoozed.end()) {
                if (ib::still_snoozed(z->second, c.stamp, c.message, c.kind)) {
                    ++q.snoozed;
                    continue;
                }
                s.inbox.snoozed.erase(z);
            }
            waiting.push_back({id, now_wall - age, seq});
        } else {
            finished.push_back({id, now_wall - age, seq});
        }
        cards.emplace(id, std::move(c));
    }
    // A snooze ends when the agent stops waiting: the next wait shows its card again.
    std::erase_if(s.inbox.snoozed, [&](const auto& kv) { return !blocked_ids.contains(kv.first); });
    for (const auto& id : ib::order(waiting, s.settings.inbox_order))
        q.blocked.push_back(std::move(cards.at(id)));
    for (const auto& id : ib::order(finished, "newest"))
        q.done.push_back(std::move(cards.at(id)));
    return q;
}

// ---------------------------------------------------------------- small pieces
ImU32 tail_color(const mui::TailSegment& seg) {
    if (seg.default_fg)
        return g_pal->term_fg;
    if (seg.ansi >= 0 && seg.ansi < 16 && g_pal->ansi != nullptr)
        return (*g_pal->ansi)[static_cast<std::size_t>(seg.ansi)];
    return IM_COL32((seg.rgb >> 16) & 0xFF, (seg.rgb >> 8) & 0xFF, seg.rgb & 0xFF, 255);
}

// The screen's last lines, display only (the state never comes from them).
void draw_preview(App::State& s, ImDrawList* dl, const Card& c, ImVec2 a, ImVec2 b) {
    dl->AddRectFilled(a, b, g_pal->term_bg, tok::radius_small());
    dl->AddRect(a, b, tok::divider(g_pal->term_bg), tok::radius_small());
    const ImVec2 in(a.x + tok::gap_s() + px(2), a.y + tok::gap_xs() + px(2));
    const ImVec2 out(b.x - tok::gap_s(), b.y - tok::gap_xs());
    const auto& t = screen_tail_of(s, c.id, kPreviewLines);  // shared with Stage (stage_layout.cpp)
    if (t.tail.empty()) {
        draw_skeleton_rows(dl, in, (out.x - in.x) * 0.8f, {0.7f, 0.45f}, px(kLineH), px(6));
        return;
    }
    const float fs = fpx(kMonoSize);
    const float cw = s.fonts.mono->CalcTextSizeA(fs, FLT_MAX, 0, "M").x;
    dl->PushClipRect(in, out, true);
    // Bottom-aligned: the newest line sits just above the lower edge, like the terminal itself.
    float y = out.y - static_cast<float>(t.tail.size()) * px(kLineH);
    for (const auto& line : t.tail) {
        for (const auto& seg : line.segments) {
            ImFont* f = seg.bold && s.fonts.mono_bold != nullptr ? s.fonts.mono_bold : s.fonts.mono;
            float x = in.x + static_cast<float>(seg.col) * cw;
            if (x > out.x)
                break;
            if (seg.wide)
                x += (2.0f * cw - f->CalcTextSizeA(fs, FLT_MAX, 0, seg.text.c_str()).x) * 0.5f;
            dl->AddText(f, fs, ImVec2(x, y + (px(kLineH) - fs) * 0.5f), tail_color(seg), seg.text.c_str());
        }
        y += px(kLineH);
    }
    dl->PopClipRect();
}

// Word-wrapped lines of `text`, at most `max_lines`; the last one ends in "…" when cut.
std::vector<std::string> wrap_lines(ImFont* font, float fs, const std::string& text, float width,
                                    int max_lines) {
    std::vector<std::string> out;
    const char* p = text.c_str();
    const char* end = p + text.size();
    while (p < end && static_cast<int>(out.size()) < max_lines) {
        if (static_cast<int>(out.size()) == max_lines - 1) {
            out.emplace_back(p, end);  // the rest: text_clipped adds the ellipsis
            break;
        }
        const char* e = font->CalcWordWrapPosition(fs, p, end, width);
        if (e == p)
            e = p + 1;
        out.emplace_back(p, e);
        p = e;
        while (p < end && *p == ' ')
            ++p;
    }
    return out;
}

enum class Look { kPrimary, kSecondary, kQuiet };

struct Button {
    std::string label;
    std::string key;  // a small key badge before the label ("1"), or ""
    Look look = Look::kSecondary;
    bool enabled = true;
    std::string tip;
};

float button_width(const App::State& s, const Button& b) {
    float w = tok::measure(s, tt::kCaption, b.label.c_str()).x + tok::gap_m() * 2;
    if (!b.key.empty())
        w += px(16) + tok::gap_xs() + px(2);
    return w;
}

// A compact button `kButtonH` high: the accent fill, a raised chip, or text until hovered.
bool draw_button(App::State& s, ImDrawList* dl, const char* id, ImVec2 p, const Button& b) {
    const float w = button_width(s, b);
    const float h = px(kButtonH);
    ImGui::SetCursorScreenPos(p);
    ImGui::BeginDisabled(!b.enabled);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && b.enabled;
    const bool held = ImGui::IsItemActive();
    ImGui::EndDisabled();
    const ImVec2 q(p.x + w, p.y + h);
    ImU32 fill = 0, ink = g_pal->text, edge = 0;
    switch (b.look) {
        case Look::kPrimary:
            fill = held  ? mix(g_pal->focus, g_pal->text, 0.12f)
                   : hov ? alpha(g_pal->focus, 225)
                         : g_pal->focus;
            ink = accent_foreground();
            break;
        case Look::kSecondary:
            fill = hov ? tok::hover(raised_color()) : raised_color();
            edge = tok::divider(g_pal->card);
            break;
        case Look::kQuiet:
            fill = hov ? tok::hover(g_pal->card) : 0;
            ink = hov ? g_pal->text : g_pal->dim;
            break;
    }
    if (!b.enabled) {
        ink = g_pal->faint;
        fill = b.look == Look::kQuiet ? 0 : alpha(raised_color(), 140);
    }
    if (fill != 0)
        dl->AddRectFilled(p, q, fill, tok::radius_small());
    if (edge != 0)
        dl->AddRect(p, q, edge, tok::radius_small());
    float x = p.x + tok::gap_m();
    const float cy = p.y + h * 0.5f;
    if (!b.key.empty()) {
        const ImVec2 ka(x, cy - px(8));
        const ImVec2 kb(x + px(16), cy + px(8));
        dl->AddRectFilled(
            ka, kb,
            b.look == Look::kPrimary ? alpha(ink, 0x38) : tok::divider(fill != 0 ? fill : g_pal->card),
            px(4));
        const ImVec2 ks = tok::measure(s, tt::kCaption, b.key.c_str());
        tok::text(s, dl, tt::kCaption, ImVec2(x + (px(16) - ks.x) * 0.5f, cy - ks.y * 0.5f),
                  b.look == Look::kPrimary ? ink : g_pal->dim, b.key.c_str());
        x += px(16) + tok::gap_xs() + px(2);
    }
    const ImVec2 ts = tok::measure(s, tt::kCaption, b.label.c_str());
    tok::text(s, dl, tt::kCaption, ImVec2(x, cy - ts.y * 0.5f), ink, b.label.c_str());
    if (hov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (!b.tip.empty())
            ImGui::SetTooltip("%s", b.tip.c_str());
    }
    return clicked && b.enabled;
}

// A pill: a tinted fill and its text, `cy` centred; returns its width.
float pill(App::State& s, ImDrawList* dl, float x, float cy, const std::string& text, ImU32 col) {
    const ImVec2 t = tok::measure(s, tt::kCaption, text.c_str());
    const float h = px(20);
    const float w = t.x + tok::gap_s() * 2;
    dl->AddRectFilled(ImVec2(x, cy - h * 0.5f), ImVec2(x + w, cy + h * 0.5f), alpha(col, 0x24), h * 0.5f);
    tok::text(s, dl, tt::kCaption, ImVec2(x + tok::gap_s(), cy - t.y * 0.5f), col, text.c_str());
    return w;
}

void send_bytes(App::State& s, const std::string& id, const std::string& bytes, bool enter, bool paste) {
    const auto it = s.by_id.find(id);
    if (it == s.by_id.end())
        return;
    if (!bytes.empty()) {
        if (paste)
            it->second->send(host::PasteInput{bytes});
        else
            it->second->send(host::RawInput{bytes});
    }
    if (enter)
        it->second->send(host::RawInput{"\r"});
    s.inbox.sent[id] = tok::now_ms();
}

// ---------------------------------------------------------------- measuring
struct Metrics {
    float pad, head, msg_line, preview, actions, reply;
};
Metrics metrics() {
    return {tok::gap_l(),       px(tt::kTitle.line) + px(2) + px(tt::kCaption.line),
            px(tt::kBody.line), static_cast<float>(kPreviewLines) * px(kLineH) + (tok::gap_xs() + px(2)) * 2,
            px(kButtonH),       px(kReplyH)};
}

int message_lines(App::State& s, const Card& c, float inner_w) {
    if (c.message.empty())
        return 1;
    return static_cast<int>(
        wrap_lines(tok::font(s, tt::kBody), tok::size(tt::kBody), c.message, inner_w, kMessageLines).size());
}

float card_height(App::State& s, const Card& c, float w, bool selected) {
    const Metrics m = metrics();
    const float inner = w - m.pad * 2 - px(kStripe);
    float h = m.pad + m.head + tok::gap_m() + static_cast<float>(message_lines(s, c, inner)) * m.msg_line +
              tok::gap_m() + m.preview + tok::gap_m() + m.actions + m.pad;
    if (selected)
        h += tok::gap_s() + m.reply;
    return h;
}

// ---------------------------------------------------------------- drawing a card
std::string files_text(int n) {
    if (n < 0)
        return {};
    if (n == 0)
        return "no changes";
    return std::to_string(n) + (n == 1 ? " file changed" : " files changed");
}

void draw_reply(App::State& s, ImDrawList* dl, const Card& c, ImVec2 a, float w) {
    auto& in = s.inbox;
    if (in.reply_for != c.id) {  // the text typed belongs to one card
        in.reply_for = c.id;
        in.reply[0] = '\0';
    }
    const float h = px(kReplyH);
    const float send_w = h;
    const float field_w = w - send_w - tok::gap_s();
    ImGui::SetCursorScreenPos(a);
    ImGui::SetNextItemWidth(field_w);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::gap_m(), (h - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tok::radius_small());
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(g_pal->bg));
    if (in.reply_focus_next) {
        ImGui::SetKeyboardFocusHere();
        in.reply_focus_next = false;
    }
    ImGui::BeginDisabled(!c.can_input);
    const std::string hint = c.can_input
                                 ? "Reply to " + c.title + "\xE2\x80\xA6"
                                 : std::string("Can't type here: another window controls this terminal");
    const bool enter = ImGui::InputTextWithHint("##reply", hint.c_str(), in.reply, sizeof in.reply,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    const bool active = ImGui::IsItemActive();
    ImGui::EndDisabled();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    in.reply_active = active;
    if (active)
        in.queue_focus = true;  // typing here is the queue's business
    const ImVec2 b(a.x + field_w, a.y + h);
    dl->AddRect(a, b, active ? tok::focus_ring(g_pal->bg) : tok::divider(g_pal->card), tok::radius_small(), 0,
                active ? px(1.5f) : 1.0f);
    const std::string text = in.reply;
    const bool has_text = text.find_first_not_of(" \t") != std::string::npos;
    if (active && !has_text) {
        const char* k = "Enter sends";
        const ImVec2 ks = tok::measure(s, tt::kCaption, k);
        tok::text(s, dl, tt::kCaption, ImVec2(b.x - tok::gap_m() - ks.x, a.y + (h - ks.y) * 0.5f),
                  g_pal->faint, k);
    }
    // The send button: an arrow in the accent colour when there is something to send.
    const ImVec2 sp(b.x + tok::gap_s(), a.y);
    ImGui::SetCursorScreenPos(sp);
    const bool can = has_text && c.can_input;
    ImGui::BeginDisabled(!can);
    const bool clicked = ImGui::InvisibleButton("##send", ImVec2(send_w, h));
    const bool hov = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    dl->AddRectFilled(sp, ImVec2(sp.x + send_w, sp.y + h),
                      can ? (hov ? alpha(g_pal->focus, 225) : g_pal->focus) : raised_color(),
                      tok::radius_small());
    const ImVec2 cc(sp.x + send_w * 0.5f, sp.y + h * 0.5f);
    const ImU32 ink = can ? accent_foreground() : g_pal->faint;
    const float u = px(1);
    dl->AddLine(ImVec2(cc.x - 5 * u, cc.y), ImVec2(cc.x + 5 * u, cc.y), ink, px(1.6f));
    dl->AddPolyline(std::array{ImVec2(cc.x + 1 * u, cc.y - 4 * u), ImVec2(cc.x + 5 * u, cc.y),
                               ImVec2(cc.x + 1 * u, cc.y + 4 * u)}
                        .data(),
                    3, ink, 0, px(1.6f));
    if (hov && can) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("Send with Enter");
    }
    if ((enter || clicked) && can) {
        send_bytes(s, c.id, text, true, true);
        in.reply[0] = '\0';
        if (enter)
            in.reply_focus_next = true;  // stay in the field for a follow-up
    }
}

// One blocked card. `a` its top-left, `w` its width; `k` 0..1 how far it has come in.
void draw_card(App::State& s, ImDrawList* dl, const Card& c, ImVec2 a, float w, bool selected, float k) {
    const Metrics m = metrics();
    const float h = card_height(s, c, w, selected);
    const ImVec2 b(a.x + w, a.y + h);
    const bool focused_ring = selected && s.inbox.queue_focus;
    ImGui::PushID(c.id.c_str());
    // The whole card selects it on a click (buttons drawn on top keep their own clicks).
    ImGui::SetCursorScreenPos(a);
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InvisibleButton("##card", ImVec2(w, h)))
        s.inbox.selected = c.id;
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        run_command(s, "inbox.open:" + c.id);
    const bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const int from = dl->VtxBuffer.Size;

    if (focused_ring)
        dl->AddRect(ImVec2(a.x - px(3), a.y - px(3)), ImVec2(b.x + px(3), b.y + px(3)),
                    tok::focus_ring(g_pal->sidebar), tok::radius_card() + px(3), 0, px(2));
    tok::card(dl, a, b, tok::radius_card(), 1, hov && !selected ? tok::hover(g_pal->card) : g_pal->card,
              selected && !focused_ring ? tok::selected_edge(g_pal->card) : 0);
    // The needs-you stripe down the left edge, as on the notes.
    dl->AddRectFilled(a, ImVec2(a.x + px(kStripe), b.y), g_pal->needs_you, tok::radius_card(),
                      ImDrawFlags_RoundCornersLeft);

    const float x0 = a.x + px(kStripe) + m.pad;
    const float x1 = b.x - m.pad;
    float y = a.y + m.pad;
    // Header: icon, name, waited; under it where it runs, its branch and changes, and the kind.
    const ImVec2 ip(x0, y + (m.head - px(kIcon)) * 0.5f);
    program_icon(dl, c.program, ip, px(kIcon), g_pal->text);
    const float tx = x0 + px(kIcon) + tok::gap_m() - px(2);
    const std::string ago = c.unreachable ? std::string("offline") : ib::waited(c.waited_ms);
    const ImVec2 as = tok::measure(s, tt::kCaption, ago.c_str());
    const bool long_wait = c.waited_ms >= 10LL * 60 * 1000;
    const ImU32 ago_col = c.unreachable ? g_pal->needs_you : long_wait ? g_pal->needs_you : g_pal->dim;
    // Right of the name: the kind of question as a pill, then how long it has waited.
    const float row_cy = y + px(tt::kTitle.line) * 0.5f;
    float right = x1 - as.x;
    tok::text(s, dl, tt::kCaption, ImVec2(right, row_cy - as.y * 0.5f), ago_col, ago.c_str());
    draw_icon(s, dl, ui::icon::kClock, fpx(12), ImVec2(right - px(9), row_cy), ago_col);
    right -= px(18) + tok::gap_s();
    if (const std::string kl = ib::kind_label(c.kind); !kl.empty()) {
        const float pw = tok::measure(s, tt::kCaption, kl.c_str()).x + tok::gap_s() * 2;
        right -= pw;
        pill(s, dl, right, row_cy, kl, g_pal->needs_you);
        right -= tok::gap_s();
    }
    text_clipped(dl, tok::font(s, tt::kTitle), tok::size(tt::kTitle),
                 ImVec2(tx, y + (px(tt::kTitle.line) - tok::size(tt::kTitle)) * 0.5f - px(1)), g_pal->text,
                 c.title, std::max(0.0f, right - tx));
    const float sub_y = y + px(tt::kTitle.line) + px(2);
    right = x1;
    std::string sub = c.where;
    if (!c.branch.empty())
        sub += (sub.empty() ? "" : "  \xC2\xB7  ") + c.branch;
    if (const std::string f = files_text(c.changed); !f.empty())
        sub += (sub.empty() ? "" : "  \xC2\xB7  ") + f;
    text_clipped(dl, tok::font(s, tt::kCaption), tok::size(tt::kCaption), ImVec2(tx, sub_y), g_pal->dim, sub,
                 std::max(0.0f, right - tx));
    y += m.head + tok::gap_m();

    // What it asks: the agent's own words from its report.
    const float inner = x1 - x0;
    if (c.message.empty()) {
        tok::text(s, dl, tt::kBody, ImVec2(x0, y + (m.msg_line - tok::size(tt::kBody)) * 0.5f), g_pal->faint,
                  "No message in its report \xE2\x80\x94 the screen below shows what it asks.");
        y += m.msg_line;
    } else {
        const auto lines =
            wrap_lines(tok::font(s, tt::kBody), tok::size(tt::kBody), c.message, inner, kMessageLines);
        for (const auto& line : lines) {
            text_clipped(dl, tok::font(s, tt::kBody), tok::size(tt::kBody),
                         ImVec2(x0, y + (m.msg_line - tok::size(tt::kBody)) * 0.5f), g_pal->text, line,
                         inner);
            y += m.msg_line;
        }
    }
    y += tok::gap_m();
    draw_preview(s, dl, c, ImVec2(x0, y), ImVec2(x1, y + m.preview));
    if (c.unreachable) {
        dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + m.preview), alpha(g_pal->card, 0xB0),
                          tok::radius_small());
        const char* t = "Machine unreachable";
        const ImVec2 ts = tok::measure(s, tt::kCaption, t);
        tok::text(s, dl, tt::kCaption, ImVec2((x0 + x1 - ts.x) * 0.5f, y + (m.preview - ts.y) * 0.5f),
                  g_pal->needs_you, t);
    }
    y += m.preview + tok::gap_m();

    // Actions: quick answers on the left (1-9), Snooze and Open on the right.
    float bx = x0;
    for (std::size_t i = 0; i < c.answers.size(); ++i) {
        const auto& q = c.answers[i];
        Button btn{q.label, std::to_string(i + 1), i == 0 ? Look::kPrimary : Look::kSecondary, c.can_input,
                   "Sends " + ib::describe_send(q.send, q.enter)};
        if (bx + button_width(s, btn) > x1 - px(160))
            break;  // no room: the rest stay on the keys
        if (draw_button(s, dl, ("##qa" + std::to_string(i)).c_str(), ImVec2(bx, y), btn)) {
            s.inbox.selected = c.id;
            run_command(s, "inbox.answer:" + std::to_string(i + 1) + ":" + c.id);
        }
        bx += button_width(s, btn) + tok::gap_s();
    }
    if (!selected) {
        const Button reply{"Reply", "R", c.answers.empty() ? Look::kPrimary : Look::kQuiet, c.can_input, ""};
        if (bx + button_width(s, reply) <= x1 - px(160) &&
            draw_button(s, dl, "##replybtn", ImVec2(bx, y), reply))
            run_command(s, "inbox.reply:" + c.id);
    }
    const Button open{"Open", "", Look::kSecondary, true, "Work in its terminal (Enter)"};
    const Button snooze{"Snooze", "", Look::kQuiet, true, "Hide until it asks something new (S)"};
    float rx = x1 - button_width(s, open);
    if (draw_button(s, dl, "##open", ImVec2(rx, y), open))
        run_command(s, "inbox.open:" + c.id);
    rx -= tok::gap_xs() + button_width(s, snooze);
    if (draw_button(s, dl, "##snooze", ImVec2(rx, y), snooze))
        run_command(s, "inbox.snooze:" + c.id);
    // "Sent" between the answers and the buttons while the agent has not moved on yet.
    if (const auto st = s.inbox.sent.find(c.id); st != s.inbox.sent.end()) {
        const double age = tok::now_ms() - st->second;
        if (age < kSentMs) {
            const char* t = "Sent";
            const ImVec2 ts = tok::measure(s, tt::kCaption, t);
            const float fx = rx - tok::gap_m() - ts.x;
            if (fx > bx) {
                const float fa = static_cast<float>(std::min(1.0, (kSentMs - age) / 600.0));
                draw_icon(s, dl, ui::icon::kCheck, fpx(12), ImVec2(fx - px(9), y + px(kButtonH) * 0.5f),
                          alpha(g_pal->done, static_cast<int>(255 * fa)));
                tok::text(s, dl, tt::kCaption, ImVec2(fx, y + (px(kButtonH) - ts.y) * 0.5f),
                          alpha(g_pal->done, static_cast<int>(255 * fa)), t);
            }
            s.inbox.animating = true;
        }
    }
    y += m.actions;
    if (selected)
        draw_reply(s, dl, c, ImVec2(x0, y + tok::gap_s()), inner);

    // Coming in: slides from the right and fades in.
    if (k < 1.0f) {
        const float dx = (1.0f - k) * px(tt::kSlideDistance) * 2.0f;
        for (int i = from; i < dl->VtxBuffer.Size; ++i) {
            dl->VtxBuffer[i].pos.x += dx;
            ImU32& col = dl->VtxBuffer[i].col;
            const auto av = static_cast<float>((col >> IM_COL32_A_SHIFT) & 0xFFu);
            col = (col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(av * k + 0.5f) << IM_COL32_A_SHIFT);
        }
    }
    ImGui::PopID();
}

// One finished agent: a quiet row with a green stripe, Open and Dismiss.
void draw_done_row(App::State& s, ImDrawList* dl, const Card& c, ImVec2 a, float w, bool selected, float k) {
    const float h = px(kDoneRowH);
    const ImVec2 b(a.x + w, a.y + h);
    const int from = dl->VtxBuffer.Size;
    ImGui::PushID(c.id.c_str());
    ImGui::SetCursorScreenPos(a);
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InvisibleButton("##row", ImVec2(w, h)))
        s.inbox.selected = c.id;
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        run_command(s, "inbox.open:" + c.id);
    const bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const ImU32 fill = selected ? tok::selected(g_pal->sidebar) : hov ? tok::hover(g_pal->sidebar) : 0;
    if (fill != 0)
        dl->AddRectFilled(a, b, fill, tok::radius_card());
    dl->AddRect(
        a, b,
        selected && s.inbox.queue_focus ? tok::focus_ring(g_pal->sidebar) : tok::divider(g_pal->sidebar),
        tok::radius_card(), 0, selected && s.inbox.queue_focus ? px(1.5f) : 1.0f);
    dl->AddRectFilled(a, ImVec2(a.x + px(kStripe), b.y), g_pal->done, tok::radius_card(),
                      ImDrawFlags_RoundCornersLeft);
    const float x0 = a.x + px(kStripe) + tok::gap_m();
    program_icon(dl, c.program, ImVec2(x0, a.y + (h - px(kIcon)) * 0.5f), px(kIcon), g_pal->text);
    const float tx = x0 + px(kIcon) + tok::gap_m() - px(2);
    const Button open{"Open", "", Look::kSecondary, true, "Work in its terminal (Enter)"};
    const Button dismiss{"Dismiss", "", Look::kQuiet, true, "Mark as seen (S)"};
    float rx = b.x - tok::gap_m() - button_width(s, open);
    const float by = a.y + (h - px(kButtonH)) * 0.5f;
    if (draw_button(s, dl, "##open", ImVec2(rx, by), open))
        run_command(s, "inbox.open:" + c.id);
    rx -= tok::gap_xs() + button_width(s, dismiss);
    if (draw_button(s, dl, "##dismiss", ImVec2(rx, by), dismiss))
        run_command(s, "inbox.dismiss:" + c.id);
    const float max_w = std::max(0.0f, rx - tok::gap_m() - tx);
    const float ty = a.y + (h - px(tt::kBody.line) - px(tt::kCaption.line)) * 0.5f;
    text_clipped(dl, tok::font(s, tt::kBody), tok::size(tt::kBody), ImVec2(tx, ty + px(1)), g_pal->text,
                 c.title, max_w);
    std::string line =
        "Finished " + (c.waited_ms < 10'000 ? std::string("just now") : ib::waited(c.waited_ms) + " ago");
    if (!c.message.empty())
        line += "  \xC2\xB7  " + c.message;
    else if (!c.where.empty())
        line += "  \xC2\xB7  " + c.where;
    text_clipped(dl, tok::font(s, tt::kCaption), tok::size(tt::kCaption),
                 ImVec2(tx, ty + px(tt::kBody.line) + px(1)), g_pal->dim, line, max_w);
    if (k < 1.0f) {
        const float dx = (1.0f - k) * px(tt::kSlideDistance) * 2.0f;
        for (int i = from; i < dl->VtxBuffer.Size; ++i) {
            dl->VtxBuffer[i].pos.x += dx;
            ImU32& col = dl->VtxBuffer[i].col;
            const auto av = static_cast<float>((col >> IM_COL32_A_SHIFT) & 0xFFu);
            col = (col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(av * k + 0.5f) << IM_COL32_A_SHIFT);
        }
    }
    ImGui::PopID();
}

// A card on its way out: its outline, name and line, sliding left and fading.
void draw_ghost(App::State& s, ImDrawList* dl, const App::State::InboxState::Ghost& g, float x,
                float list_top, float w) {
    const double t = (tok::now_ms() - g.since) / kLeaveMs;
    const float k = tt::ease_out(static_cast<float>(std::clamp(t, 0.0, 1.0)));
    const float dx = -k * px(24);
    const ImVec2 a(x + dx, list_top + g.top);
    const ImVec2 b(a.x + w, a.y + g.h);
    const int from = dl->VtxBuffer.Size;
    tok::card(dl, a, b, tok::radius_card(), g.done ? 0 : 1, g.done ? g_pal->sidebar : g_pal->card);
    dl->AddRectFilled(a, ImVec2(a.x + px(kStripe), b.y), g.done ? g_pal->done : g_pal->needs_you,
                      tok::radius_card(), ImDrawFlags_RoundCornersLeft);
    const float x0 = a.x + px(kStripe) + tok::gap_l();
    program_icon(dl, g.program, ImVec2(x0, a.y + tok::gap_l()), px(kIcon), g_pal->text);
    tok::text(s, dl, tt::kTitle, ImVec2(x0 + px(kIcon) + tok::gap_s(), a.y + tok::gap_l()), g_pal->text,
              g.title.c_str());
    const float fade = 1.0f - k;
    for (int i = from; i < dl->VtxBuffer.Size; ++i) {
        ImU32& col = dl->VtxBuffer[i].col;
        const auto av = static_cast<float>((col >> IM_COL32_A_SHIFT) & 0xFFu);
        col = (col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(av * fade + 0.5f) << IM_COL32_A_SHIFT);
    }
}

// ---------------------------------------------------------------- empty states
// AgentDesk's own Inbox zero drawing: an empty tray, a check that pops in above it and a few
// sparks flying out once. `t` 0..1 through the animation (1: the still picture).
void draw_inbox_zero_art(ImDrawList* dl, ImVec2 c, float side, double t) {
    const float u = side / 96.0f;
    auto P = [&](float x, float y) { return ImVec2(c.x + (x - 48.0f) * u, c.y + (y - 48.0f) * u); };
    const ImU32 line = mix(g_pal->bg, g_pal->text, g_pal->light ? 0.38f : 0.42f);
    const ImU32 fill = mix(g_pal->bg, g_pal->text, g_pal->light ? 0.05f : 0.07f);
    const float w = std::max(1.0f, 1.6f * u);
    // The tray: a shallow box with a dip in its front edge.
    dl->AddRectFilled(P(18, 58), P(78, 80), fill, 6 * u);
    const ImVec2 tray[] = {P(18, 62), P(34, 62), P(38, 68), P(58, 68), P(62, 62), P(78, 62)};
    dl->AddPolyline(tray, 6, line, 0, w);
    dl->AddRect(P(18, 54), P(78, 80), line, 6 * u, 0, w);
    // The check: a disc scaling up with a little overshoot, the tick drawn after it.
    const auto seg = [&](double from, double to) { return std::clamp((t - from) / (to - from), 0.0, 1.0); };
    const double pop = seg(0.0, 0.38);
    const double back = 1.70158;
    const double ob = pop <= 0 ? 0.0 : 1.0 + (back + 1) * std::pow(pop - 1, 3) + back * std::pow(pop - 1, 2);
    const float r = 15.0f * static_cast<float>(ob);
    const ImVec2 cc = P(48, 32);
    if (r > 0.2f) {
        dl->AddCircleFilled(cc, r * u, alpha(g_pal->done, 0x2E), 40);
        dl->AddCircle(cc, r * u, g_pal->done, 40, w);
    }
    const double tick = seg(0.25, 0.55);
    if (tick > 0) {
        const ImVec2 p0 = P(41, 32), p1 = P(46, 37), p2 = P(56, 26);
        const float k = static_cast<float>(tick);
        if (k <= 0.4f) {
            const float f = k / 0.4f;
            dl->AddLine(p0, ImVec2(p0.x + (p1.x - p0.x) * f, p0.y + (p1.y - p0.y) * f), g_pal->done,
                        w * 1.4f);
        } else {
            const float f = (k - 0.4f) / 0.6f;
            const ImVec2 pts[] = {p0, p1, ImVec2(p1.x + (p2.x - p1.x) * f, p1.y + (p2.y - p1.y) * f)};
            dl->AddPolyline(pts, 3, g_pal->done, 0, w * 1.4f);
        }
    }
    // Sparks: six short strokes flying out and fading, in the theme's own accents.
    const double fly = seg(0.3, 1.0);
    if (fly > 0 && fly < 1) {
        const ImU32 cols[] = {g_pal->focus,   g_pal->done,  g_pal->info,
                              g_pal->working, g_pal->focus, g_pal->done};
        const float e = static_cast<float>(1.0 - std::pow(1.0 - fly, 3));
        const int a = static_cast<int>(255 * (1.0 - fly));
        for (int i = 0; i < 6; ++i) {
            const float ang = -2.6f + static_cast<float>(i) * 0.43f;  // fanned out above the check
            const float r0 = (20.0f + 12.0f * e) * u;
            const float r1 = r0 + (5.0f * (1.0f - e) + 1.5f) * u;
            const ImVec2 d(std::cos(ang), std::sin(ang));
            dl->AddLine(ImVec2(cc.x + d.x * r0, cc.y + d.y * r0), ImVec2(cc.x + d.x * r1, cc.y + d.y * r1),
                        alpha(cols[i], a), w * 1.2f);
        }
    }
}

// A few dots and their counts: "● 3 working  ○ 2 idle".
void draw_summary(App::State& s, ImDrawList* dl, float cx, float y, int working, int idle) {
    const std::string text = ib::summary(working, idle);
    const ImVec2 ts = tok::measure(s, tt::kBody, text.c_str());
    const float dot = working > 0 ? px(8) + tok::gap_s() : 0.0f;
    const float x = cx - (ts.x + dot) * 0.5f;
    if (working > 0)
        dl->AddCircle(ImVec2(x + px(4), y + ts.y * 0.5f), px(3.5f), g_pal->working, 16, px(1.5f));
    tok::text(s, dl, tt::kBody, ImVec2(x + dot, y), g_pal->dim, text.c_str());
}

// The whole queue is empty: the drawing, "Inbox zero", the summary and snoozed cards to show.
void draw_zero(App::State& s, ImDrawList* dl, ImVec2 pos, ImVec2 size, const Queue& q) {
    auto& in = s.inbox;
    if (in.zero_since < 0)
        in.zero_since = tok::now_ms();
    double t = tok::reduce_motion() ? 1.0 : (tok::now_ms() - in.zero_since) / kZeroMs;
    if (t < 1.0)
        tok::keep_animating();
    t = std::clamp(t, 0.0, 1.0);
    const float art = px(96);
    const float title_h = fpx(18) * 1.3f;
    const float total = art + tok::gap_l() + title_h + tok::gap_xs() + px(tt::kBody.line) + tok::gap_l() +
                        px(tt::kBody.line) + (q.snoozed > 0 ? tok::gap_l() + px(20) : 0.0f);
    const float cx = pos.x + size.x * 0.5f;
    float y = pos.y + std::max(tok::gap_xl(), (size.y - total) * 0.42f);
    draw_inbox_zero_art(dl, ImVec2(cx, y + art * 0.5f), art, t);
    y += art + tok::gap_l();
    // The words rise into place with the check.
    const float rise = 1.0f - tt::ease_out(static_cast<float>(std::clamp((t - 0.15) / 0.5, 0.0, 1.0)));
    const int text_a = static_cast<int>(255 * (1.0f - rise));
    const char* title = "Inbox zero";
    const ImVec2 tsz = s.fonts.ui_bold->CalcTextSizeA(fpx(18), FLT_MAX, 0, title);
    dl->AddText(s.fonts.ui_bold, fpx(18), ImVec2(cx - tsz.x * 0.5f, y + rise * px(6)),
                alpha(g_pal->text, text_a), title);
    y += title_h + tok::gap_xs();
    const char* hint = "Nothing needs you right now.";
    const ImVec2 hs = tok::measure(s, tt::kBody, hint);
    tok::text(s, dl, tt::kBody, ImVec2(cx - hs.x * 0.5f, y + rise * px(6)), alpha(g_pal->dim, text_a), hint);
    y += px(tt::kBody.line) + tok::gap_l();
    draw_summary(s, dl, cx, y, q.working, q.idle);
    y += px(tt::kBody.line);
    if (q.snoozed > 0) {
        const std::string link = std::to_string(q.snoozed) +
                                 (q.snoozed == 1 ? " snoozed card" : " snoozed cards") + " \xE2\x80\x94 show";
        const ImVec2 ls = tok::measure(s, tt::kCaption, link.c_str());
        const ImVec2 p(cx - ls.x * 0.5f, y + tok::gap_l());
        ImGui::SetCursorScreenPos(p);
        if (ImGui::InvisibleButton("##unsnooze", ImVec2(ls.x, ls.y + px(4))))
            run_command(s, "inbox.unsnooze");
        const bool hov = ImGui::IsItemHovered();
        tok::text(s, dl, tt::kCaption, p, g_pal->focus, link.c_str());
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            tok::hline(dl, p.x, p.x + ls.x, p.y + ls.y + px(1), g_pal->focus);
        }
    }
}

// Nothing waits, but some finished: a slim "all clear" line above the Done list.
float draw_clear_banner(App::State& s, ImDrawList* dl, ImVec2 a, float w, const Queue& q) {
    const float h = px(64);
    tok::card(dl, a, ImVec2(a.x + w, a.y + h), tok::radius_card(), 0, g_pal->sidebar,
              tok::divider(g_pal->sidebar));
    tok::draw_art(dl, tok::Art::kAllDone, ImVec2(a.x + tok::gap_l() + px(20), a.y + h * 0.5f), px(40));
    const float tx = a.x + tok::gap_l() + px(40) + tok::gap_m();
    tok::text(s, dl, tt::kTitle, ImVec2(tx, a.y + h * 0.5f - px(tt::kTitle.line)), g_pal->text,
              "Nothing needs you");
    std::string sub = ib::summary(q.working, q.idle);
    if (q.snoozed > 0)
        sub += "  \xC2\xB7  " + std::to_string(q.snoozed) + " snoozed";
    tok::text(s, dl, tt::kCaption, ImVec2(tx, a.y + h * 0.5f + px(2)), g_pal->dim, sub.c_str());
    return h;
}

float draw_section(App::State& s, ImDrawList* dl, ImVec2 a, float w, const char* title, int count,
                   ImU32 col) {
    const float h = px(kSectionH);
    const std::string t = std::string(title) + "  " + std::to_string(count);
    dl->AddCircleFilled(ImVec2(a.x + px(4), a.y + h * 0.5f), px(3.5f), col);
    tok::text(s, dl, tt::kCaption,
              ImVec2(a.x + px(4) + tok::gap_s() + px(2), a.y + (h - px(tt::kCaption.line)) * 0.5f),
              g_pal->dim, t.c_str());
    (void)w;
    return h;
}

// ---------------------------------------------------------------- keys
// The queue's keys, only while it holds the keyboard: nothing (a terminal, a field) is active, no
// popup is open and no modifier is down, so a person typing in a terminal never loses a key.
void queue_keys(App::State& s, const std::vector<const Card*>& items) {
    auto& in = s.inbox;
    const ImGuiIO& io = ImGui::GetIO();
    if (!in.queue_focus || items.empty() || io.WantTextInput || ImGui::IsAnyItemActive() || io.KeyCtrl ||
        io.KeyAlt || io.KeySuper || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        return;
    int cur = -1;
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i]->id == in.selected)
            cur = static_cast<int>(i);
    const int n = static_cast<int>(items.size());
    auto pick = [&](int i) {
        if (i >= 0 && i < n)
            in.selected = items[static_cast<std::size_t>(i)]->id;
    };
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_J))
        pick(ib::move_selection(cur, n, 1));
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_K))
        pick(ib::move_selection(cur, n, -1));
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
        pick(0);
    if (ImGui::IsKeyPressed(ImGuiKey_End, false))
        pick(n - 1);
    if (cur < 0)
        return;
    const Card& c = *items[static_cast<std::size_t>(cur)];
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
        run_command(s, "inbox.open:" + c.id);
    if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false))
        run_command(s, (c.done ? "inbox.dismiss:" : "inbox.snooze:") + c.id);
    if (c.done && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_D, false)))
        run_command(s, "inbox.dismiss:" + c.id);
    if (c.done)
        return;
    if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_R, false))
        run_command(s, "inbox.reply:" + c.id);
    for (int k = 1; k <= 9 && k <= static_cast<int>(c.answers.size()); ++k)
        if (!io.KeyShift && (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + k), false) ||
                             ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_Keypad0 + k), false)))
            run_command(s, "inbox.answer:" + std::to_string(k) + ":" + c.id);
}

// ---------------------------------------------------------------- the bar
// The two-part order switch (Newest | Oldest), 26 px high; returns its left edge.
float order_switch(App::State& s, ImDrawList* dl, float right, float cy) {
    const char* keys[] = {"newest", "oldest"};
    const char* labels[] = {"Newest", "Oldest"};
    const char* tips[] = {"Newest waiting first", "Longest waiting first"};
    const float sw = px(60), h = px(24);
    const float w = sw * 2 + px(4);
    const ImVec2 a(right - w, cy - (h + px(4)) * 0.5f);
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h + px(4)), raised_color(), tok::radius_small() + px(1));
    for (int i = 0; i < 2; ++i) {
        const ImVec2 p(a.x + px(2) + sw * static_cast<float>(i), a.y + px(2));
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##order", ImVec2(sw, h)))
            run_command(s, std::string("inbox.order:") + keys[i]);
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hov)
            ImGui::SetTooltip("%s", tips[i]);
        const bool on = s.settings.inbox_order == keys[i];
        if (on || hov)
            dl->AddRectFilled(p, ImVec2(p.x + sw, p.y + h),
                              on ? tok::selected(raised_color()) : tok::hover(raised_color()),
                              tok::radius_small());
        const ImVec2 t = tok::measure(s, tt::kCaption, labels[i]);
        tok::text(s, dl, tt::kCaption, ImVec2(p.x + (sw - t.x) * 0.5f, p.y + (h - t.y) * 0.5f),
                  on ? g_pal->text : g_pal->dim, labels[i]);
    }
    return a.x;
}

}  // namespace

void inbox_entered(App::State& s) {
    auto& in = s.inbox;
    in.queue_focus = true;
    in.take_keyboard = true;
    in.scroll = 0;
    in.shown.clear();
    in.leaving.clear();
    for (auto& [id, vs] : s.views)
        vs.focus_next = false;  // the queue takes the keys, not the terminal beside it
}

void draw_inbox_bar(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + w, pos.y + h);
    dl->AddRectFilled(pos, end, g_pal->sidebar);
    tok::hline(dl, pos.x, end.x, end.y - 1.0f, g_pal->border);
    const float cy = pos.y + h * 0.5f;
    const float x = clear_of_traffic_lights(s, pos.x + px(14), pos.y, end.y, px(14));
    float right = end.x - px(10);
    if (windows_window_buttons(s)) {
        draw_window_buttons(s, right, pos.y, h);
        right -= px(26) * 3 + px(4) + px(8);
    }
    right = layout_bar_buttons(s, right, cy, "inbox.toggle") - tok::gap_m();
    right = order_switch(s, dl, right, cy) - tok::gap_m();
    const Queue q = s.host->connected() ? collect(s) : Queue{};
    const char* title = "Inbox";
    const ImVec2 t = tok::measure(s, tt::kTitle, title);
    tok::text(s, dl, tt::kTitle, ImVec2(x, cy - t.y * 0.5f), g_pal->text, title);
    float px_left = x + t.x + tok::gap_m();
    if (!q.blocked.empty()) {
        const std::string n =
            std::to_string(q.blocked.size()) + (q.blocked.size() == 1 ? " needs you" : " need you");
        px_left += pill(s, dl, px_left, cy, n, g_pal->needs_you) + tok::gap_s();
    }
    if (!q.done.empty())
        px_left +=
            pill(s, dl, px_left, cy, std::to_string(q.done.size()) + " done", g_pal->done) + tok::gap_s();
    if (q.snoozed > 0) {
        const std::string z = std::to_string(q.snoozed) + " snoozed";
        const ImVec2 zs = tok::measure(s, tt::kCaption, z.c_str());
        if (px_left + zs.x < right) {
            ImGui::SetCursorScreenPos(ImVec2(px_left, cy - zs.y * 0.5f));
            const bool clicked = ImGui::InvisibleButton("##snoozed", zs);
            const bool hov = ImGui::IsItemHovered();
            tok::text(s, dl, tt::kCaption, ImVec2(px_left, cy - zs.y * 0.5f), hov ? g_pal->text : g_pal->dim,
                      z.c_str());
            if (hov) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("Show the snoozed cards again");
            }
            if (clicked)
                run_command(s, "inbox.unsnooze");
        }
    }
    window_drag_area(s, pos, end);
}

void draw_inbox_queue(App::State& s, ImVec2 pos, ImVec2 size) {
    auto& in = s.inbox;
    in.animating = false;
    // Not drawn last frame: the Inbox just came up (by any route: a key, the palette, a Settings
    // card), so the queue starts with the keyboard.
    const int frame = ImGui::GetFrameCount();
    if (in.drawn_frame != frame - 1 && in.drawn_frame != frame)
        inbox_entered(s);
    in.drawn_frame = frame;
    reload_answers(s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + size.x, pos.y + size.y);
    if (in.take_keyboard) {
        ui::adapter::clear_keyboard();
        in.take_keyboard = false;
    }
    // Who has the keys: a click in the queue gives them to it, a click anywhere else takes them.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        const bool inside = ImGui::IsMouseHoveringRect(pos, end, false);
        in.queue_focus = inside;
        if (inside)
            ui::adapter::clear_keyboard();
    }
    if (!s.host->connected()) {
        StateCard card;
        card.title =
            s.machine_loading("") ? "Connecting to agent-host\xE2\x80\xA6" : "agent-host is not running";
        card.kind = s.machine_loading("") ? CardKind::kLoading : CardKind::kError;
        card.hint = "The Inbox lists the agents agent-host runs that need you.";
        if (!s.machine_loading("")) {
            card.detail = s.host->last_error();
            card.buttons = {{"Retry", "", [&s] { s.host->request_start_host(); }}};
        }
        draw_state_card(s, pos, size, card);
        return;
    }
    const Queue q = collect(s);
    // The shared screen-tail cache forgets terminals that closed.
    std::erase_if(s.screen_tails, [&](const auto& kv) { return !s.views_by_id.contains(kv.first); });
    std::erase_if(in.sent, [&](const auto& kv) { return tok::now_ms() - kv.second > kSentMs; });

    std::vector<const Card*> items;
    for (const auto& c : q.blocked)
        items.push_back(&c);
    for (const auto& c : q.done)
        items.push_back(&c);
    // The selection: kept on its card; when that card went, the one that took its place.
    int sel = -1;
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i]->id == in.selected)
            sel = static_cast<int>(i);
    if (sel < 0) {
        sel = in.selected_at < 0 ? (items.empty() ? -1 : 0)
                                 : ib::after_removal(in.selected_at, static_cast<int>(items.size()));
        in.selected = sel >= 0 ? items[static_cast<std::size_t>(sel)]->id : std::string();
    }
    const std::string before = in.selected;
    queue_keys(s, items);
    in.selected_at = -1;
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i]->id == in.selected)
            in.selected_at = static_cast<int>(i);
    const bool moved = in.selected != before;
    if (in.reply_for != in.selected)
        in.reply_active = false;

    if (q.blocked.empty() && q.done.empty()) {
        draw_zero(s, dl, pos, size, q);
        in.shown.clear();
        return;
    }
    in.zero_since = -1;

    // Lay out: target tops relative to the list, then ease each card toward its target.
    const float pad_x = tok::gap_l();
    const float w = std::max(px(200), size.x - pad_x * 2);
    const float x = pos.x + pad_x;
    struct Slot {
        const Card* card;
        float top;
        float h;
        bool selected;
    };
    std::vector<Slot> slots;
    float y = tok::gap_m();
    float banner_y = -1;
    float done_head = -1;
    float blocked_head = -1;
    if (q.blocked.empty()) {
        banner_y = y;
        y += px(64) + tok::gap_l();
    } else {
        blocked_head = y;
        y += px(kSectionH) + tok::gap_xs();
        for (const auto& c : q.blocked) {
            const bool selected = c.id == in.selected;
            const float h = card_height(s, c, w, selected);
            slots.push_back({&c, y, h, selected});
            y += h + tok::gap_m();
        }
    }
    if (!q.done.empty()) {
        y += tok::gap_s();
        done_head = y;
        y += px(kSectionH) + tok::gap_xs();
        for (const auto& c : q.done) {
            slots.push_back({&c, y, px(kDoneRowH), c.id == in.selected});
            y += px(kDoneRowH) + tok::gap_s();
        }
    }
    const float content_h = y + tok::gap_m();

    // Scrolling: the wheel over the queue, and the keyboard's card kept in view.
    const float max_scroll = std::max(0.0f, content_h - size.y);
    if (ImGui::IsMouseHoveringRect(pos, end) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        in.scroll -= ImGui::GetIO().MouseWheel * px(80);
    if (moved)
        for (const auto& sl : slots)
            if (sl.selected) {
                if (sl.top - tok::gap_m() < in.scroll)
                    in.scroll = sl.top - tok::gap_m();
                else if (sl.top + sl.h + tok::gap_m() > in.scroll + size.y)
                    in.scroll = sl.top + sl.h + tok::gap_m() - size.y;
            }
    in.scroll = std::clamp(in.scroll, 0.0f, max_scroll);
    const float list_top = pos.y - in.scroll;

    // Ghosts: cards that were here last frame and are not now slide out where they were.
    std::map<std::string, App::State::InboxState::Ghost> now_shown;
    for (const auto& sl : slots)
        now_shown[sl.card->id] = {
            sl.card->id, sl.card->title, sl.card->message, sl.card->program, sl.card->done, sl.top, sl.h, 0};
    if (!tok::reduce_motion())
        for (const auto& [id, g] : in.shown)
            if (!now_shown.contains(id)) {
                auto ghost = g;
                ghost.top = in.top.count(id) ? in.top[id] : g.top;
                ghost.since = tok::now_ms();
                in.leaving.push_back(std::move(ghost));
                tok::forget(card_key(id));
            }
    in.shown = std::move(now_shown);
    std::erase_if(in.leaving, [](const auto& g) { return tok::now_ms() - g.since > kLeaveMs; });
    std::erase_if(in.top, [&](const auto& kv) { return !in.shown.contains(kv.first); });

    ImGui::PushClipRect(pos, end, true);
    for (const auto& g : in.leaving) {
        draw_ghost(s, dl, g, x, list_top, w);
        tok::keep_animating();
    }
    if (blocked_head >= 0)
        draw_section(s, dl, ImVec2(x, list_top + blocked_head), w, "NEEDS YOU",
                     static_cast<int>(q.blocked.size()), g_pal->needs_you);
    if (banner_y >= 0)
        draw_clear_banner(s, dl, ImVec2(x, list_top + banner_y), w, q);
    if (done_head >= 0)
        draw_section(s, dl, ImVec2(x, list_top + done_head), w, "DONE", static_cast<int>(q.done.size()),
                     g_pal->done);
    const float dt = ImGui::GetIO().DeltaTime;
    for (const auto& sl : slots) {
        float& cur = in.top.try_emplace(sl.card->id, sl.top).first->second;
        if (tok::reduce_motion() || std::abs(cur - sl.top) < 0.5f) {
            cur = sl.top;
        } else {
            cur += (sl.top - cur) * std::min(1.0f, dt * 14.0f);
            tok::keep_animating();
        }
        const float k = tok::appear(card_key(sl.card->id), tt::kNormalMs, 1e9);
        const ImVec2 a(x, list_top + cur);
        if (a.y > end.y || a.y + sl.h < pos.y)
            continue;  // out of view
        if (sl.card->done)
            draw_done_row(s, dl, *sl.card, a, w, sl.selected, k);
        else
            draw_card(s, dl, *sl.card, a, w, sl.selected, k);
    }
    ImGui::PopClipRect();
    if (max_scroll > 0) {  // a thin bar at the right edge
        const float thumb = std::max(px(24), size.y * size.y / content_h);
        const float t = in.scroll / max_scroll;
        const float ty = pos.y + (size.y - thumb) * t;
        dl->AddRectFilled(ImVec2(end.x - px(6), ty), ImVec2(end.x - px(2), ty + thumb),
                          alpha(g_pal->dim, 110), px(2));
    }
    // The waiting times tick over with the frame loop's own once-a-second wakeup.
}

std::optional<std::string> inbox_command(App::State& s, const std::string& name, const std::string& arg) {
    auto& in = s.inbox;
    // "<n>:<id>" or "<id>": the card a command is for (the selected one when none is named).
    auto target = [&](const std::string& a) { return a.empty() ? in.selected : a; };
    if (name == "inbox.toggle") {
        const bool shown = s.layout().style == "inbox" && !s.settings.focus_on;
        if (std::string err = toggle_layout(s, "inbox"); !err.empty())
            return err;
        if (!shown)
            inbox_entered(s);
        return std::string();
    }
    if (name == "inbox.order") {
        s.settings.inbox_order = ib::order_name(arg);
        save_settings(s.settings);
        return std::string();
    }
    if (name == "inbox.unsnooze") {
        in.snoozed.clear();
        return std::string();
    }
    if (name == "inbox.open") {
        const std::string id = target(arg);
        const auto it = s.views_by_id.find(id);
        if (it == s.views_by_id.end())
            return "no terminal \"" + id + "\"";
        in.selected = id;
        in.queue_focus = false;
        if (s.ws.find(id).space < 0 && !it->second.parent.empty()) {
            s.peek = id;  // a sub-agent without a pane: a look in the peek window
            s.views[id].focus_next = true;
            return std::string();
        }
        if (s.ws.unsettle_session(id))
            s.doc_changed();
        s.focus(id);
        return std::string();
    }
    if (name == "inbox.snooze") {
        const std::string id = target(arg);
        const auto it = s.views_by_id.find(id);
        if (it == s.views_by_id.end())
            return "no terminal \"" + id + "\"";
        if (s.attention_of(id) != Attention::kBlocked)
            return std::string("only an agent that needs you can be snoozed");
        long long stamp = 0;
        if (const auto ch = s.changed.find(id);
            ch != s.changed.end() && ch->second.s == "blocked" && ch->second.at > 0)
            stamp = ch->second.at;
        else if (const auto at = s.styles.changes.changed_at.find(id);
                 at != s.styles.changes.changed_at.end())
            stamp = at->second;
        in.snoozed[id] = {stamp, mui::clean_message(it->second.agent_message), it->second.agent_kind};
        return std::string();
    }
    if (name == "inbox.dismiss") {
        const std::string id = target(arg);
        if (!s.views_by_id.count(id))
            return "no terminal \"" + id + "\"";
        s.attention.seen(id);  // done -> idle, as looking at it would
        return std::string();
    }
    if (name == "inbox.reply") {
        const std::string id = target(arg);
        if (!s.views_by_id.count(id))
            return "no terminal \"" + id + "\"";
        in.selected = id;
        in.queue_focus = true;
        in.reply_focus_next = true;
        return std::string();
    }
    if (name == "inbox.answer") {
        // "<n>" or "<n>:<id>": the card's n-th quick answer (1-9).
        const auto colon = arg.find(':');
        const std::string num = arg.substr(0, colon);
        const std::string id = target(colon == std::string::npos ? std::string() : arg.substr(colon + 1));
        const auto it = s.views_by_id.find(id);
        if (it == s.views_by_id.end())
            return "no terminal \"" + id + "\"";
        int n = 0;
        if (num.size() == 1 && num[0] >= '1' && num[0] <= '9')
            n = num[0] - '0';
        if (n == 0)
            return std::string("inbox.answer needs a number 1-9");
        const ui::Program p = program_of(it->second);
        const auto answers = ib::answers_for(s.settings.inbox_answers, p.key.empty() ? "program" : p.key,
                                             it->second.agent_kind);
        if (n > static_cast<int>(answers.size()))
            return "no quick answer " + num + " for " + p.name;
        const auto sit = s.by_id.find(id);
        if (sit == s.by_id.end() || !sit->second->can_input())
            return std::string("this terminal does not take input here");
        const auto& qa = answers[static_cast<std::size_t>(n - 1)];
        send_bytes(s, id, qa.send, qa.enter, false);
        return std::string();
    }
    return std::nullopt;
}

}  // namespace detail
}  // namespace agentdesk::desktop
