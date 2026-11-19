// The Files and Changes panels as drawn, the side panel card that holds one of them, and the
// details column.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// A faint, wrapped status line in a panel list ("No folder", "Loading…", errors).
void panel_note(App::State& s, const std::string& t) {
    ImGui::SetCursorPosX(px(14));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->faint));
    ImGui::PushFont(s.fonts.ui, 12);
    ImGui::TextWrapped("%s", t.c_str());
    ImGui::PopFont();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------- Files

// A message in place of the preview (binary, too large, unreadable): 12 px padding, dim UI text,
// wrapping.
void draw_preview_message(App::State& s, const std::string& message, ImVec2 area) {
    ImGui::BeginChild("##preview", area, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::PushFont(s.fonts.ui, 12);
    ImGui::Dummy(ImVec2(0, px(8)));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
    ImGui::SetCursorPosX(px(12));
    ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - px(12));
    ImGui::TextUnformatted(message.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::EndChild();
}

// The file tree as it draws: rows are 28 px, indented 18 px per level; a click on a folder
// toggles it, on a file opens the preview. The clicks are acted on after the panel mutex is
// released.
struct FileTree {
    App::State& s;
    SidePanels& p;
    ImDrawList* d;
    float rw;  // row width
    std::string toggle;
    std::string open;
};

char change_mark(const SidePanels& p, const std::string& rel) {
    std::string key = rel;
    for (auto& c : key)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto m = p.marks.find(key);
    return m != p.marks.end() ? m->second : 0;
}

void draw_file_rows(FileTree& t, const std::string& rel, int depth) {
    const auto it = t.p.listing.find(rel);
    if (it == t.p.listing.end() || depth > 40)
        return;
    App::State& s = t.s;
    ImDrawList* d = t.d;
    const float rw = t.rw;
    for (const auto& e : it->second) {
        const std::string child = rel.empty() ? e.name : rel + "/" + e.name;
        const ImVec2 a0 = ImGui::GetCursorScreenPos();
        const ImVec2 a(a0.x + px(6), a0.y);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(child.c_str());
        if (ImGui::InvisibleButton("##f", ImVec2(rw, px(28)))) {
            if (e.dir)
                t.toggle = child;
            else
                t.open = child;
        }
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hov)
            d->AddRectFilled(a, ImVec2(a.x + rw, a.y + px(28)), g_pal->hover, px(7));
        const float cy = a.y + px(14);
        float x = a.x + px(8) + static_cast<float>(depth) * px(18);
        const bool open_dir = e.dir && t.p.expanded.contains(child);
        if (e.dir)
            draw_icon(s, d, open_dir ? ui::icon::kChevronDown : ui::icon::kChevronRight, fpx(12),
                      ImVec2(x + px(6), cy), g_pal->faint);
        x += px(12) + px(7);
        draw_icon(s, d, e.dir ? ui::icon::kFolder : ui::icon::kFile, fpx(14), ImVec2(x + px(7), cy),
                  g_pal->dim);
        x += px(14) + px(7);
        const char mark = e.dir ? 0 : change_mark(t.p, child);
        const float right = a.x + rw - px(8) - (mark != 0 ? px(14) : 0.0f);
        text_clipped(d, s.fonts.ui, fpx(13), ImVec2(x, cy - fpx(13) * 0.6f), g_pal->text, e.name, right - x);
        if (mark != 0) {
            const char m[2] = {mark, 0};
            d->AddText(s.fonts.ui_medium, fpx(11), ImVec2(a.x + rw - px(8) - px(8), cy - fpx(11) * 0.6f),
                       mark_color(mark), m);
        }
        ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + px(28)));
        if (open_dir)
            draw_file_rows(t, child, depth + 1);
    }
}

void draw_files_panel(App::State& s, ImVec2 pos, ImVec2 size) {
    auto& p = *s.panels;
    std::string machine;
    const std::string root = panel_folder(s, &machine);
    p.track_files(root, machine);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    std::unique_lock lock(p.mutex);
    bool back = false;
    const bool previewing = !p.preview_path.empty();
    panel_header(s, dl, pos, size.x, "Files", previewing ? p.preview_path : folder_name(root), previewing,
                 &back);
    const float top = pos.y + px(42) + 1;
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 1, top));
    if (previewing) {
        const ImVec2 area(size.x - 2, pos.y + size.y - top - px(4));
        if (!p.preview_message.empty() && p.preview_text.empty())
            draw_preview_message(s, p.preview_message, area);
        else
            draw_code_preview(s, p, area);
        lock.unlock();
        if (back)
            p.close_preview();
        return;
    }
    ImGui::BeginChild("##tree", ImVec2(size.x - 2, pos.y + size.y - top - px(4)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    FileTree tree{s, p, ImGui::GetWindowDrawList(), ImGui::GetContentRegionAvail().x - px(12), {}, {}};
    ImGui::Dummy(ImVec2(0, px(6)));
    draw_file_rows(tree, "", 0);
    if (root.empty())
        panel_note(s, "No folder");
    else if (!p.files_error.empty())
        panel_note(s, p.files_error);
    else if (!p.listing.count(""))
        panel_note(s, "Loading files\xE2\x80\xA6");
    else if (p.listing[""].empty())
        panel_note(s, "No files");
    if (p.files_limited)
        panel_note(s, "File list limited to 500 entries per folder and 5,000 total");
    ImGui::EndChild();
    lock.unlock();
    if (!tree.toggle.empty())
        p.toggle_folder(tree.toggle);
    if (!tree.open.empty())
        p.open_file(tree.open);
}

// ---------------------------------------------------------------- Changes

// Header (42 px): "Changes", a "<n> files" badge, and +added −deleted at the right; all three
// show even at zero.
void draw_changes_header(App::State& s, ImDrawList* dl, const SidePanels& p, ImVec2 pos, ImVec2 size) {
    const float hh = px(42);
    const float cy = pos.y + hh * 0.5f;
    float x = pos.x + px(14);
    const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(13), FLT_MAX, 0, "Changes");
    dl->AddText(s.fonts.ui_bold, fpx(13), ImVec2(x, cy - t.y * 0.5f), g_pal->text, "Changes");
    x += t.x + px(8);
    int adds = 0, dels = 0;
    for (const auto& c : p.changes) {
        adds += c.add;
        dels += c.del;
    }
    const std::string badge = std::to_string(p.changes.size()) + " files";
    const ImVec2 bt = s.fonts.ui->CalcTextSizeA(fpx(10), FLT_MAX, 0, badge.c_str());
    dl->AddRect(ImVec2(x, cy - bt.y * 0.5f - px(1)), ImVec2(x + bt.x + px(10), cy + bt.y * 0.5f + px(1)),
                g_pal->border, px(4));
    dl->AddText(s.fonts.ui, fpx(10), ImVec2(x + px(5), cy - bt.y * 0.5f), g_pal->dim, badge.c_str());
    float rx = pos.x + size.x - px(14);
    const std::string del = "\xE2\x88\x92" + std::to_string(dels);
    const std::string add = "+" + std::to_string(adds);
    rx -= s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, del.c_str()).x;
    dl->AddText(s.fonts.ui, fpx(11), ImVec2(rx, cy - fpx(11) * 0.6f), g_pal->needs_you, del.c_str());
    rx -= px(6) + s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, add.c_str()).x;
    dl->AddText(s.fonts.ui, fpx(11), ImVec2(rx, cy - fpx(11) * 0.6f), g_pal->done, add.c_str());
    dl->AddLine(ImVec2(pos.x, pos.y + hh), ImVec2(pos.x + size.x, pos.y + hh), g_pal->border);
}

// One changed file (28 px): its status letter, the folder (faint) and name, and +added −deleted
// at the right. Returns true when clicked.
bool draw_change_row(App::State& s, ImDrawList* d, const SidePanels& p, const SidePanels::Change& c,
                     float rw) {
    const ImVec2 a0 = ImGui::GetCursorScreenPos();
    const ImVec2 a(a0.x + px(6), a0.y);
    ImGui::SetCursorScreenPos(a);
    ImGui::PushID(c.path.c_str());
    const bool clicked = ImGui::InvisibleButton("##c", ImVec2(rw, px(28)));
    const bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    if (c.path == p.open_diff)
        d->AddRectFilled(a, ImVec2(a.x + rw, a.y + px(28)), g_pal->selected, px(7));
    else if (hov)
        d->AddRectFilled(a, ImVec2(a.x + rw, a.y + px(28)), g_pal->hover, px(7));
    const float ry = a.y + px(14);
    const char letter[2] = {c.letter, 0};
    d->AddText(s.fonts.ui_medium, fpx(11), ImVec2(a.x + px(8) + px(3), ry - fpx(11) * 0.6f),
               mark_color(c.letter), letter);
    const float cx = a.x + px(8) + px(14) + px(8);
    const std::string add = c.add > 0 ? "+" + std::to_string(c.add) : std::string();
    const std::string del = c.del > 0 ? "\xE2\x88\x92" + std::to_string(c.del) : std::string();
    float rx = a.x + rw - px(8);
    auto count = [&](const std::string& text, ImU32 col) {
        if (text.empty())
            return;
        rx -= s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, text.c_str()).x;
        d->AddText(s.fonts.ui, fpx(11), ImVec2(rx, ry - fpx(11) * 0.6f), col, text.c_str());
        rx -= px(6);
    };
    count(del, g_pal->needs_you);
    count(add, g_pal->done);
    const auto slash = c.path.find_last_of('/');
    const std::string dir = slash == std::string::npos ? std::string() : c.path.substr(0, slash + 1);
    const std::string name = slash == std::string::npos ? c.path : c.path.substr(slash + 1);
    const float name_w = std::min(px(260), s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, name.c_str()).x);
    const float dir_room = std::max(0.0f, rx - cx - name_w - px(4));
    const float dw = dir.empty() ? 0.0f
                                 : text_clipped(d, s.fonts.ui, fpx(13), ImVec2(cx, ry - fpx(13) * 0.6f),
                                                g_pal->faint, dir, dir_room);
    text_clipped(d, s.fonts.ui, fpx(13), ImVec2(cx + dw, ry - fpx(13) * 0.6f), g_pal->text, name,
                 rx - cx - dw);
    ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + px(28)));
    return clicked;
}

// The list of changed files; returns the path clicked, if any.
std::string draw_changes_list(App::State& s, const SidePanels& p, const std::string& cwd, ImVec2 at,
                              ImVec2 size) {
    std::string select;
    ImGui::SetCursorScreenPos(at);
    ImGui::BeginChild("##changes", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float rw = ImGui::GetContentRegionAvail().x - px(12);
    ImGui::Dummy(ImVec2(0, px(6)));
    for (const auto& c : p.changes)
        if (draw_change_row(s, d, p, c, rw))
            select = c.path;
    if (cwd.empty())
        panel_note(s, "No folder");
    else if (p.not_repo)
        panel_note(s, "Not a git repository");
    else if (!p.changes_error.empty())
        panel_note(s, p.changes_error);
    else if (p.changes.empty())
        panel_note(s, p.repo_top.empty() ? "Loading changes\xE2\x80\xA6" : "No uncommitted changes");
    ImGui::EndChild();
    return select;
}

// The diff: unified, one line-number column, the +/- prefix dropped, added and deleted rows tinted
// green and red at 9 % (0x18), in the terminal font at 11.
void draw_diff_rows(App::State& s, const SidePanels& p) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::PushFont(s.fonts.mono, 11);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    const float rh = px(20);
    std::size_t longest = 0;
    for (const auto& r : p.diff)
        longest = std::max(longest, r.text.size());
    const float width = std::max(px(300), static_cast<float>(longest + 1) * px(7) + px(50));
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(p.diff.size()), rh);
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const auto& r = p.diff[static_cast<std::size_t>(i)];
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(width, rh));
            const ImVec2 b(a.x + std::max(width, ImGui::GetWindowWidth()), a.y + rh);
            const float ty = a.y + (rh - fpx(11)) * 0.5f;
            if (r.kind == SidePanels::DiffRow::kHunk) {
                d->AddRectFilled(a, b, raised_color());
                d->AddText(s.fonts.mono, fpx(11), ImVec2(a.x + px(12), ty), g_pal->faint, r.text.c_str());
                continue;
            }
            if (r.kind == SidePanels::DiffRow::kAdd)
                d->AddRectFilled(a, b, alpha(g_pal->done, 0x18));
            else if (r.kind == SidePanels::DiffRow::kDel)
                d->AddRectFilled(a, b, alpha(g_pal->needs_you, 0x18));
            if (r.kind != SidePanels::DiffRow::kOther) {
                const std::string n = std::to_string(r.number);
                const float nw = s.fonts.mono->CalcTextSizeA(fpx(11), FLT_MAX, 0, n.c_str()).x;
                d->AddText(s.fonts.mono, fpx(11), ImVec2(a.x + px(8) + px(30) - nw, ty), g_pal->faint,
                           n.c_str());
            }
            d->AddText(s.fonts.mono, fpx(11), ImVec2(a.x + px(8) + px(30) + px(10), ty), g_pal->text,
                       r.text.c_str());
        }
    }
    ImGui::PopStyleVar();
    ImGui::PopFont();
}

void draw_diff(App::State& s, const SidePanels& p, ImVec2 at, ImVec2 size) {
    ImGui::SetCursorScreenPos(at);
    ImGui::BeginChild("##diff", size, ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);
    if (!p.diff_message.empty() && p.diff.empty()) {
        ImGui::SetCursorPos(ImVec2(px(14), px(10)));
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->faint));
        ImGui::TextUnformatted(p.diff_message.c_str());
        ImGui::PopStyleColor();
    } else if (p.diff.empty() && !p.changes.empty()) {
        ImGui::SetCursorPos(ImVec2(px(14), px(10)));
        ImGui::TextDisabled("Select a file to view its changes");
    } else {
        draw_diff_rows(s, p);
    }
    ImGui::EndChild();
}

// Footer: the commit message box and Commit (git add -A, git commit -m), with the last commit's
// note under them. Returns true when a commit was asked for (Enter or the button) and is allowed.
bool draw_commit_footer(App::State& s, ImDrawList* dl, SidePanels& p, ImVec2 pos, ImVec2 size,
                        float footer_h) {
    const float fy = pos.y + size.y - footer_h + px(8);
    dl->AddLine(ImVec2(pos.x, fy - px(8)), ImVec2(pos.x + size.x, fy - px(8)), g_pal->border);
    const float bw =
        s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, p.committing ? "Committing\xE2\x80\xA6" : "Commit").x +
        px(28);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + px(10), fy));
    ImGui::SetNextItemWidth(size.x - px(20) - bw - px(8));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), (px(32) - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(8));
    // Esc leaves the box and keeps the message. ImGui reverts the edit on Esc, so the text from
    // before the key is put back.
    static std::string typed;
    const bool esc = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    bool commit = ImGui::InputTextWithHint("##commitmsg", "Commit message", p.commit_msg, sizeof p.commit_msg,
                                           ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemActive())
        typed = p.commit_msg;
    else if (ImGui::IsItemDeactivated() && esc)
        std::snprintf(p.commit_msg, sizeof p.commit_msg, "%s", typed.c_str());
    const std::string msg = p.commit_msg;
    const bool can = !msg.empty() && msg.find_first_not_of(" \n") != std::string::npos &&
                     !p.repo_top.empty() && !p.committing && !p.changes_loading;
    ImGui::SameLine(0, px(8));
    ImGui::BeginDisabled(!can);
    ImGui::PushStyleColor(ImGuiCol_Button, v4(can ? g_pal->text : raised_color()));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(can ? g_pal->card : g_pal->faint));
    commit |=
        ImGui::Button(p.committing ? "Committing\xE2\x80\xA6##commit" : "Commit##commit", ImVec2(bw, px(32)));
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();
    ImGui::PopStyleVar(2);
    if (!p.commit_note.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(pos.x + px(12), fy + px(36)));
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->needs_you));
        ImGui::TextUnformatted(p.commit_note.c_str());
        ImGui::PopStyleColor();
    }
    return commit && can;
}

// Top to bottom: header, the list of changed files, an optional "first 1,000" row, the diff of
// the selected file, the commit footer. The list and the diff split the room evenly; the list is
// at least 44 px and at most its rows (28 px each plus 12).
void draw_changes_panel(App::State& s, ImVec2 pos, ImVec2 size) {
    auto& p = *s.panels;
    std::string machine;
    const std::string cwd = panel_folder(s, &machine);
    p.track_changes(cwd, machine);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    std::unique_lock lock(p.mutex);
    draw_changes_header(s, dl, p, pos, size);
    const float hh = px(42);
    const float footer_h = px(32) + px(16) + (p.commit_note.empty() ? 0.0f : px(18));
    const float body_top = pos.y + hh + 1;
    const float limited_h = p.changes_more ? px(22) : 0.0f;
    const float body_h = pos.y + size.y - footer_h - body_top - limited_h;
    const float list_h = std::clamp(body_h * 0.5f, px(44),
                                    std::max(px(44), static_cast<float>(p.changes.size()) * px(28) + px(12)));
    const std::string select =
        draw_changes_list(s, p, cwd, ImVec2(pos.x + 1, body_top), ImVec2(size.x - 2, list_h));
    dl->AddLine(ImVec2(pos.x, body_top + list_h), ImVec2(pos.x + size.x, body_top + list_h), g_pal->border);
    if (p.changes_more) {  // its own row under the list: 12 px in, 4 px down, 11 px faint
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(pos.x + px(12), body_top + list_h + 1 + px(4)), g_pal->faint,
                    "Showing the first 1,000 changed files");
    }
    const float diff_top = body_top + list_h + 1 + limited_h;
    draw_diff(s, p, ImVec2(pos.x + 1, diff_top),
              ImVec2(size.x - 2, std::max(px(20), pos.y + size.y - footer_h - diff_top)));
    const bool commit = draw_commit_footer(s, dl, p, pos, size, footer_h);
    lock.unlock();
    if (!select.empty())
        p.select_change(select);
    if (commit)
        p.commit();
}

}  // namespace

// The right panel: one card, Files or Changes (only one shows at a time).
void draw_side_panel(App::State& s, ImVec2 pos, ImVec2 size) {
    auto* sp = s.ws.active();
    if (sp == nullptr || sp->panel.empty())
        return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->card, px(10));
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->border, px(10));
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushID(sp->panel.c_str());
    ImGui::BeginChild(
        "##sidepanel", size, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    if (sp->panel == "files")
        draw_files_panel(s, pos, size);
    else
        draw_changes_panel(s, pos, size);
    ImGui::EndChild();
    ImGui::PopID();
}

// ---------------------------------------------------------------- details column

namespace {
// A "key   value" line: the key dim, the value wrapped in its colour from 92 px.
void detail_line(const char* k, const std::string& value, ImU32 color) {
    ImGui::TextDisabled("%s", k);
    ImGui::SameLine(px(92));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(color));
    ImGui::TextWrapped("%s", value.c_str());
    ImGui::PopStyleColor();
}

// What the focused terminal is: agent state and source, process, program, folder, labels.
void draw_detail_lines(App::State& s, const std::string& id, const ui::SessionView& v) {
    const Attention att = s.attention_of(id);
    detail_line("Agent", v.agent_state + (att == Attention::kDone ? "  (finished, not seen)" : ""),
                attention_color(att));
    if (v.agent_state == "Unknown" && v.last_agent_state != "Unknown")
        detail_line("Last known", v.last_agent_state, g_pal->dim);
    detail_line("Source", v.agent_source.empty() ? std::string("none") : v.agent_source, g_pal->dim);
    if (!v.agent_message.empty())
        detail_line("Message", v.agent_message, g_pal->text);
    ImGui::Separator();
    std::string proc = v.lost ? std::string("Lost") : v.process_state;
    if (v.pid != 0)
        proc += "  \xC2\xB7  pid " + std::to_string(v.pid);
    detail_line("Process", proc, g_pal->text);
    if (!v.program.empty())
        detail_line("Running", v.program, g_pal->dim);
    if (v.exit_code) {
        char buf[40];
        std::snprintf(buf, sizeof buf, "%u (0x%08X)", *v.exit_code, *v.exit_code);
        detail_line("Exit code", buf, g_pal->text);
    }
    if (v.remaining_processes > 0)
        detail_line("Cleaning", std::to_string(v.remaining_processes) + " child processes", g_pal->working);
    if (s.machine_of.count(id) && !s.machine_of[id].empty())
        detail_line("Machine", s.machine_of[id], g_pal->info);
    detail_line("Folder", display_path(v.cwd), g_pal->dim);
    if (const auto b = ui::git_branch(v.cwd); !b.empty())
        detail_line("Branch", b, g_pal->dim);
    if (const auto age = elapsed_since(v.created_at); !age.empty())
        detail_line("Started", age + " ago", g_pal->dim);
    detail_line("Input", v.is_controller ? std::string("this window") : std::string("read-only"), g_pal->dim);
    if (!v.parent.empty())
        detail_line("Started by",
                    s.views_by_id.count(v.parent) ? s.pane_title(v.parent) : v.parent.substr(0, 8),
                    g_pal->dim);
    std::string labels;
    for (const auto& [k, val] : v.labels)
        labels += (labels.empty() ? "" : "  ") + k + "=" + val;
    if (!labels.empty())
        detail_line("Labels", labels, g_pal->dim);
}

// The terminals this one started; a click focuses one with a pane and peeks at one without.
void draw_detail_sub_agents(App::State& s, const std::string& id) {
    std::vector<std::string> subs;
    for (const auto& [cid, cv] : s.views_by_id)
        if (cv.parent == id)
            subs.push_back(cid);
    if (subs.empty())
        return;
    ImGui::TextDisabled("Sub-agents");
    for (const auto& c : subs) {
        ImGui::PushID(c.c_str());
        if (ImGui::SmallButton(s.pane_title(c).c_str()))
            run_command(s, s.ws.find(c).space < 0 ? "agent.peek:" + c : "agent.focus:" + c);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", s.views_by_id[c].agent_state.c_str());
        ImGui::PopID();
    }
}

// Ctrl+C and Stop while running, Force stop while stopping, Restart when ended, Take control when
// another window has input. All disabled while the host is disconnected.
void draw_detail_actions(App::State& s, const std::string& id, const ui::SessionView& v, bool connected) {
    const bool running = v.process_state == "Running";
    ImGui::BeginDisabled(!connected);
    if (running && ImGui::Button("Ctrl+C"))
        run_command(s, "session.interrupt");
    if (running) {
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
            run_command(s, "session.stop");
    }
    if (v.process_state == "Stopping") {
        ImGui::TextDisabled(v.still_running_after_interrupt ? "Still running after interrupt"
                                                            : "Stopping...");
        ImGui::PushStyleColor(ImGuiCol_Button, v4(alpha(g_pal->needs_you, 200)));
        if (ImGui::Button("Force stop"))
            run_command(s, "session.force_stop");
        ImGui::PopStyleColor();
    }
    if (!running && v.process_state != "Stopping" && ImGui::Button("Restart here"))
        run_command(s, "session.restart");
    if (running && !v.is_controller && ImGui::Button("Take control"))
        if (auto r = s.by_id.find(id); r != s.by_id.end())
            r->second->take_control();
    ImGui::EndDisabled();
}

}  // namespace

void draw_details(App::State& s, ImVec2 pos, ImVec2 size, bool connected) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->card, px(12));
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->border, px(12));
    ImGui::SetCursorScreenPos(ImVec2(pos.x + px(16), pos.y + px(14)));
    ImGui::BeginChild("##details", ImVec2(size.x - px(32), size.y - px(28)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    const std::string id = s.ws.focused_session();
    const auto it = s.views_by_id.find(id);
    if (it == s.views_by_id.end()) {
        ImGui::TextDisabled("No session focused");
        ImGui::EndChild();
        return;
    }
    const auto& v = it->second;
    ImGui::PushFont(s.fonts.ui_bold, 16.0f);
    ImGui::TextUnformatted(s.pane_title(id).c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s", program_of(v).name.c_str());
    ImGui::Dummy(ImVec2(0, px(4)));
    draw_detail_lines(s, id, v);
    draw_detail_sub_agents(s, id);
    draw_mod_panels(s, "details");
    ImGui::Separator();
    draw_detail_actions(s, id, v, connected);
    ImGui::EndChild();
}
}  // namespace detail

}  // namespace agentdesk::desktop
