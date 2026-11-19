// AgentDesk 98 dialogs (Run, Shut Down, Display, Find, ...).
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "win98_internal.hpp"

namespace agentdesk::desktop {
namespace detail::w98 {

// Each kind is drawn top-down in Win98 px; the window's height is whatever its body took.

namespace {
// The title of a dialog that does not set its own.
std::string kind_title(const std::string& kind) {
    static const std::map<std::string, std::string> kT = {{"run", "Run"},
                                                          {"shutdown", "Shut Down AgentDesk"},
                                                          {"logoff", "Log Off AgentDesk"},
                                                          {"about", "About AgentDesk 98"},
                                                          {"taskman", "Close Program"},
                                                          {"display", "Display Properties"},
                                                          {"datetime", "Date/Time Properties"},
                                                          {"welcome", "Welcome to AgentDesk 98"},
                                                          {"mycomputer", "My Computer"},
                                                          {"network", "Network Neighborhood"},
                                                          {"bin", "Recycle Bin"}};
    const auto it = kT.find(kind);
    return it == kT.end() ? std::string("AgentDesk") : it->second;
}

}  // namespace

int buttons(App::State& s, ImDrawList* dl, float x0, float x1, float y, const std::vector<Btn>& list,
            bool end) {
    float total = 0;
    for (const auto& b : list)
        total += U(s, b.w);
    total += U(s, 6) * static_cast<float>(list.size() > 0 ? list.size() - 1 : 0);
    float x = end ? x1 - total : x0 + (x1 - x0 - total) * 0.5f;
    int hit = -1;
    for (std::size_t i = 0; i < list.size(); ++i) {
        ButtonOpts o;
        o.def = list[i].def;
        o.disabled = list[i].disabled;
        ImGui::PushID(static_cast<int>(i));
        if (text_button(s, dl, "##btn", box(ImVec2(x, y), ImVec2(U(s, list[i].w), U(s, 23))), list[i].label,
                        o)
                .clicked)
            hit = static_cast<int>(i);
        ImGui::PopID();
        x += U(s, list[i].w) + U(s, 6);
    }
    return hit;
}

namespace {
// The text box: the text (selected while fresh) and a blinking caret.
void input(App::State& s, ImDrawList* dl, Box b, const Dialog& d) {
    const Box in = field(s, dl, b);
    dl->PushClipRect(in.a(), in.b(), true);
    const float top = in.y0 + (in.h() - U(s, 13)) * 0.5f;
    float x = in.x0;
    if (d.fresh && !d.input.empty()) {
        const float w = text_w(s, d.input);
        fill(dl, Box{x, snap(top), snap(x + w), snap(top + U(s, 13))}, rgb(C::kSelect));
        x += plain_label(s, dl, x, top, d.input, rgb(C::kSelectText));
    } else {
        x += plain_label(s, dl, x, top, d.input, rgb(C::kText));
    }
    // The caret fades in and out over 1060 ms.
    const int a = static_cast<int>(255.0 * triangle_wave(1060.0));
    fill(dl, Box{snap(x), snap(top), snap(x) + one(s), snap(top + U(s, 13))}, rgb(C::kText, a));
    dl->PopClipRect();
}

// A radio button and its label.
bool radio(App::State& s, ImDrawList* dl, const char* id, float x, float y, const std::string& text,
           bool on) {
    const float w = U(s, 12) + U(s, 6) + text_w(s, k98::plain(text));
    const Pressed p = area(id, box(ImVec2(x, y), ImVec2(w, U(s, 13))));
    image(s, dl, root_dir() / L"icons" / (on ? L"radio-on.svg" : L"radio-off.svg"),
          box(ImVec2(x, y + (U(s, 13) - U(s, 12)) * 0.5f), ImVec2(U(s, 12), U(s, 12))));
    label(s, dl, x + U(s, 18), y, text, rgb(C::kText));
    return p.clicked;
}

// A check box (a 13 px field) and its label.
bool check(App::State& s, ImDrawList* dl, const char* id, float x, float y, const std::string& text,
           bool on) {
    const float w = U(s, 13) + U(s, 6) + text_w(s, k98::plain(text));
    const Pressed p = area(id, box(ImVec2(x, y), ImVec2(w, U(s, 13))));
    const Box in = bevel(s, dl, box(ImVec2(x, y), ImVec2(U(s, 13), U(s, 13))), Bevel::kField, rgb(C::kWhite));
    if (on)
        glyph(s, dl, "check", 7, 7,
              ImVec2(snap(in.x0 + (in.w() - U(s, 7)) * 0.5f), snap(in.y0 + (in.h() - U(s, 7)) * 0.5f)),
              rgb(C::kText));
    label(s, dl, x + U(s, 19), y, text, rgb(C::kText));
    return p.clicked;
}

void close_dialog(App::State& /*s*/, Window& w) {
    w.closing = true;
}

// The Run box's history, the last command line first.
std::vector<std::string> run_history(App::State& s) {
    std::vector<std::string> hist;
    for (const auto& h : shared(s, "win98.run"))
        if (h.is_string())
            hist.push_back(h.get<std::string>());
    return hist;
}

// Steps the prompt's computer (this one, then each remote machine) by `dir`, wrapping.
void pick_machine(App::State& s, Dialog& d, int dir) {
    std::vector<std::string> all = {""};
    for (const auto& m : s.settings.machines)
        all.push_back(m.name);
    const auto it = std::find(all.begin(), all.end(), d.machine);
    const int i = it == all.end() ? 0 : static_cast<int>(it - all.begin());
    const int n = static_cast<int>(all.size());
    d.machine = all[static_cast<std::size_t>((i + dir + n) % n)];
}

// Runs a command line in a new tab, and the shell stays afterwards: PowerShell -NoExit -Command,
// cmd /K, or a POSIX shell's -c "<cmd>; exec $SHELL".
void run_it(App::State& s, Window& w, Dialog& d) {
    const std::string cmd = trim_space(d.input);
    if (cmd.empty())
        return;
    set_shared(s, "win98.run", k98::run_history(run_history(s), cmd));
    close_dialog(s, w);
    const std::string shell = pick_shell(s.settings.shell_profile, s.host->profiles());
    std::vector<std::string> args;
    if (shell == "cmd")
        args = {"/K", cmd};
    else if (shell == "pwsh" || shell == "powershell")
        args = {"-NoExit", "-Command", cmd};
    else
        args = {"-c", cmd + "\nexec \"${SHELL:-sh}\""};
    new_tab(s, k98::run_name(cmd), -1, shell, args);
}

// Shut Down's choice: stand by, shut down, restart, or "Restart in DOS mode" (the Terminals layout).
void shutdown_choice(App::State& s, Window& w, Dialog& d) {
    const int c = d.choice;
    close_dialog(s, w);
    if (c == 0) {
        // Stand by: every tab of the shown space to the taskbar.
        if (auto* sp = s.ws.active())
            for (int i = 0; i < static_cast<int>(sp->tabs.size()); ++i)
                if (!sp->tabs[static_cast<std::size_t>(i)].settled)
                    s.ws.settle_tab(s.ws.active_space, i, true);
        s.doc_dirty = true;
        s.doc_dirty_at = Clock::now();
    } else if (c == 1 || c == 2) {
        Dialog scr;
        scr.kind = "screen";
        scr.mode = c == 1 ? "shutdown" : "restart";
        open_dialog(s, std::move(scr));
    } else {
        run_command(s, "layout:terminals");
    }
}

// Close Program's End Task: kills the selected terminal.
void end_task(App::State& s, Dialog& d) {
    const auto list = tasks(s);
    if (d.sel < 0 || d.sel >= static_cast<int>(list.size()))
        return;
    run_command(s, "terminal.kill:" + list[static_cast<std::size_t>(d.sel)].address);
    sfx(s, "chord");
    d.sel = std::max(0, d.sel - 1);
}

// OK (or Enter), per kind.
void ok(App::State& s, Window& w, Dialog& d) {
    if (d.kind == "prompt") {
        const std::string name = trim_space(d.input);
        if (name.empty())
            return;
        close_dialog(s, w);
        if (d.command)
            d.command(name, d.machines ? d.machine : std::string());
    } else if (d.kind == "run") {
        run_it(s, w, d);
    } else if (d.kind == "confirm") {
        close_dialog(s, w);
        for (const auto& b : d.buttons)
            if (b.def) {
                if (b.act)
                    b.act();
                return;
            }
        if (!d.buttons.empty() && d.buttons.front().act)
            d.buttons.front().act();
    } else if (d.kind == "shutdown") {
        shutdown_choice(s, w, d);
    } else if (d.kind == "logoff") {
        close_dialog(s, w);
        run_command(s, "layout:workspace");
    } else if (d.kind == "taskman") {
        end_task(s, d);
    } else {
        close_dialog(s, w);
    }
}

// OK and Cancel, the first two buttons of most dialogs.
void ok_or_cancel(App::State& s, Window& w, Dialog& d, int hit) {
    if (hit == 0)
        ok(s, w, d);
    else if (hit == 1)
        close_dialog(s, w);
}

void append_utf8(std::string& out, unsigned u) {
    if (u < 0x80) {
        out += static_cast<char>(u);
    } else if (u < 0x800) {
        out += static_cast<char>(0xC0 | (u >> 6));
        out += static_cast<char>(0x80 | (u & 0x3F));
    } else if (u < 0x10000) {
        out += static_cast<char>(0xE0 | (u >> 12));
        out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (u & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (u >> 18));
        out += static_cast<char>(0x80 | ((u >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (u & 0x3F));
    }
}

// Removes the last UTF-8 character.
void erase_last_char(std::string& t) {
    if (t.empty())
        return;
    std::size_t n = t.size() - 1;
    while (n > 0 && (static_cast<unsigned char>(t[n]) & 0xC0) == 0x80)
        --n;
    t.resize(n);
}

// Removes the last word and the spaces after it (nothing when there is no word).
void erase_last_word(std::string& t) {
    auto space = [&](std::size_t i) { return std::isspace(static_cast<unsigned char>(t[i])) != 0; };
    std::size_t e = t.size();
    while (e > 0 && space(e - 1))
        --e;
    const std::size_t word_end = e;
    while (e > 0 && !space(e - 1))
        --e;
    if (e < word_end)
        t.resize(e);
}

// The text box's keys (prompt and Run). While the text is fresh (selected), typing replaces it
// and Backspace clears it; any handled key but Ctrl+A ends the selection.
void edit_keys(App::State& s, Dialog& d) {
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    const ImGuiIO& io = ImGui::GetIO();
    bool handled = true;
    bool select_all = false;
    if (pressed(ImGuiKey_Backspace)) {
        if (d.fresh)
            d.input.clear();
        else
            erase_last_char(d.input);
    } else if (io.KeyCtrl && pressed(ImGuiKey_U)) {
        d.input.clear();
    } else if (io.KeyCtrl && pressed(ImGuiKey_W)) {
        erase_last_word(d.input);
    } else if (io.KeyCtrl && pressed(ImGuiKey_A)) {
        d.fresh = true;
        select_all = true;
    } else if (!io.KeyCtrl && !io.KeySuper && !io.InputQueueCharacters.empty()) {
        std::string typed;
        for (const ImWchar c : io.InputQueueCharacters)
            if (c >= 32)
                append_utf8(typed, static_cast<unsigned>(c));
        if (typed.empty())
            handled = false;
        else
            d.input = (d.fresh ? std::string() : d.input) + typed;
    } else if (pressed(ImGuiKey_LeftArrow) && !s.settings.machines.empty() && d.machines) {
        pick_machine(s, d, -1);
    } else if (pressed(ImGuiKey_RightArrow) && !s.settings.machines.empty() && d.machines) {
        pick_machine(s, d, 1);
    } else {
        handled = false;
    }
    if (handled && !select_all)
        d.fresh = false;
}

}  // namespace

// The keys of the dialog that has the keyboard.
void dialog_keys(App::State& s, Window& w, Dialog& d) {
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    if (d.kind == "screen")
        return;
    if (pressed(ImGuiKey_Escape)) {
        close_dialog(s, w);
        return;
    }
    if ((pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) && d.kind != "welcome") {
        ok(s, w, d);
        return;
    }
    if (d.kind == "prompt" || d.kind == "run") {
        edit_keys(s, d);
    } else if (d.kind == "shutdown" || d.kind == "taskman") {
        const int n = d.kind == "shutdown" ? 4 : static_cast<int>(tasks(s).size());
        if (pressed(ImGuiKey_DownArrow)) {
            if (d.kind == "shutdown")
                d.choice = (d.choice + 1) % n;
            else
                d.sel = std::min(n - 1, d.sel + 1);
        } else if (pressed(ImGuiKey_UpArrow)) {
            if (d.kind == "shutdown")
                d.choice = (d.choice + n - 1) % n;
            else
                d.sel = std::max(0, d.sel - 1);
        }
    }
}

namespace {
// ---------------------------------------------------------------- the kinds

// A prompt: an icon, the text, a text box and, when asked, a choice of computer.
float render_prompt(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    const float x0 = b.x0 + U(s, 10), x1 = b.x1 - U(s, 10);
    float y = b.y0 + U(s, 10);
    icon(s, dl, d.icon.empty() ? "folder" : d.icon, 32, ImVec2(x0, y));
    const float cx = x0 + U(s, 32) + U(s, 12);
    const auto ls = wrap(s, d.text, x1 - cx);
    const float th = lines(s, dl, cx, y, ls, rgb(C::kText));
    input(s, dl, box(ImVec2(cx, y + th + U(s, 8)), ImVec2(x1 - cx, U(s, 23))), d);
    y += std::max(U(s, 32), th + U(s, 8) + U(s, 23)) + U(s, 10);
    if (d.machines && !s.settings.machines.empty()) {
        // "Computer:" and a radio per computer, wrapped, gap 10.
        const float px0 = x0 + U(s, 44);
        label(s, dl, px0, y, "Computer:", rgb(C::kText));
        y += U(s, 13) + U(s, 4);
        std::vector<std::string> names = {""};
        for (const auto& m : s.settings.machines)
            names.push_back(m.name);
        float x = px0;
        for (std::size_t i = 0; i < names.size(); ++i) {
            const std::string t = names[i].empty() ? std::string("This computer") : names[i];
            const float rw = U(s, 18) + text_w(s, t);
            if (x > px0 && x + rw > x1) {
                x = px0;
                y += U(s, 13) + U(s, 10);
            }
            ImGui::PushID(static_cast<int>(i));
            if (radio(s, dl, "##m", x, y, t, d.machine == names[i]))
                d.machine = names[i];
            ImGui::PopID();
            x += rw + U(s, 10);
        }
        y += U(s, 13) + U(s, 10);
    }
    ok_or_cancel(s, w, d, buttons(s, dl, x0, x1, y, {{"OK", true}, {"Cancel"}}, true));
    return y + U(s, 23) + U(s, 10) - b.y0;
}

// A confirmation or a message: an optional icon, the text and a centered row of buttons.
float render_confirm(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    const float x0 = b.x0 + U(s, 12), x1 = b.x1 - U(s, 12);
    float y = b.y0 + U(s, 12);
    float tx = x0;
    if (!d.icon.empty()) {
        icon(s, dl, d.icon, 32, ImVec2(x0, y));
        tx += U(s, 32) + U(s, 14);
    }
    const float th = U(s, 6) + lines(s, dl, tx, y + U(s, 6), wrap(s, d.text, x1 - tx), rgb(C::kText));
    y += std::max(d.icon.empty() ? 0.0f : U(s, 32), th) + U(s, 14);
    std::vector<Button> bs = d.buttons;
    if (bs.empty())
        bs.push_back({"OK", true, nullptr});
    std::vector<Btn> list;
    for (const auto& x : bs)
        list.push_back({x.label, x.def});
    const int hit = buttons(s, dl, x0, x1, y, list, false);
    if (hit >= 0) {
        close_dialog(s, w);
        if (bs[static_cast<std::size_t>(hit)].act)
            bs[static_cast<std::size_t>(hit)].act();
    }
    return y + U(s, 23) + U(s, 12) - b.y0;
}

// Run: the text box with its history button.
float render_run(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    const float x0 = b.x0 + U(s, 10), x1 = b.x1 - U(s, 10);
    float y = b.y0 + U(s, 10);
    icon(s, dl, "run", 32, ImVec2(x0, y));
    const float tx = x0 + U(s, 32) + U(s, 12);
    const float th = lines(
        s, dl, tx, y,
        wrap(s, "Type the name of a program, and AgentDesk\nwill open it for you in a new window.", x1 - tx),
        rgb(C::kText));
    y += std::max(U(s, 32), th) + U(s, 12);
    // "&Open:" in 32 px, gap 8, the text box and the history button (16 x 21).
    label(s, dl, x0, y + (U(s, 23) - U(s, 13)) * 0.5f, "&Open:", rgb(C::kText));
    const float fx = x0 + U(s, 32) + U(s, 8);
    const float bw = U(s, 16);
    input(s, dl, box(ImVec2(fx, y), ImVec2(x1 - bw - fx, U(s, 23))), d);
    {
        ButtonOpts o;
        o.pad_x = o.pad_y = 0;
        const Pressed p = button(
            s, dl, "##hist", box(ImVec2(x1 - bw, y + (U(s, 23) - U(s, 21)) * 0.5f), ImVec2(bw, U(s, 21))), o);
        glyph(s, dl, "arrow-down", 7, 4,
              ImVec2(snap(p.content.x0 + (p.content.w() - U(s, 7)) * 0.5f),
                     snap(p.content.y0 + (p.content.h() - U(s, 4)) * 0.5f)),
              rgb(C::kText));
        if (p.clicked) {
            // Steps through the history, starting after the current text.
            const std::vector<std::string> hist = run_history(s);
            const auto it = std::find(hist.begin(), hist.end(), d.input);
            const int i = it == hist.end() ? -1 : static_cast<int>(it - hist.begin());
            d.input = hist.empty() ? std::string()
                                   : hist[static_cast<std::size_t>(
                                         (i + 1) % std::max<int>(1, static_cast<int>(hist.size())))];
            d.fresh = true;
        }
    }
    y += U(s, 23) + U(s, 12);
    ok_or_cancel(s, w, d,
                 buttons(s, dl, x0, x1, y, {{"OK", true}, {"Cancel"}, {"&Browse...", false, true}}, true));
    return y + U(s, 23) + U(s, 10) - b.y0;
}

// Shut Down: four choices.
float render_shutdown(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    const float x0 = b.x0 + U(s, 10), x1 = b.x1 - U(s, 10);
    float y = b.y0 + U(s, 10);
    icon(s, dl, "shutdown", 32, ImVec2(x0, y));
    const float cx = x0 + U(s, 32) + U(s, 14);
    float cy = y;
    plain_label(s, dl, cx, cy, "What do you want the computer to do?", rgb(C::kText));
    cy += U(s, 13);
    static const char* const kOpts[] = {"&Stand by", "Sh&ut down", "&Restart", "Restart in &DOS mode"};
    for (int i = 0; i < 4; ++i) {
        cy += U(s, 8);
        ImGui::PushID(i);
        if (radio(s, dl, "##sd", cx, cy, kOpts[i], d.choice == i))
            d.choice = i;
        ImGui::PopID();
        cy += U(s, 13);
    }
    y += std::max(U(s, 32), cy - y) + U(s, 12);
    const int hit = buttons(s, dl, x0, x1, y, {{"OK", true}, {"Cancel"}, {"&Help"}}, false);
    ok_or_cancel(s, w, d, hit);
    if (hit == 2)
        run_command(s, "app.help");
    return y + U(s, 23) + U(s, 10) - b.y0;
}

// Log Off.
float render_logoff(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    const float x0 = b.x0 + U(s, 12), x1 = b.x1 - U(s, 12);
    float y = b.y0 + U(s, 12);
    icon(s, dl, "logoff", 32, ImVec2(x0, y));
    plain_label(s, dl, x0 + U(s, 32) + U(s, 14), y + (U(s, 32) - U(s, 13)) * 0.5f,
                "Are you sure you want to log off?", rgb(C::kText));
    y += U(s, 32) + U(s, 14);
    ok_or_cancel(s, w, d, buttons(s, dl, x0, x1, y, {{"&Yes", true}, {"&No"}}, false));
    return y + U(s, 23) + U(s, 12) - b.y0;
}

// About: the brand picture and a few lines of (mostly playful) system information.
float render_about(App::State& s, Window& w, Dialog& /*d*/, ImDrawList* dl, Box b) {
    const float x0 = b.x0 + U(s, 10), x1 = b.x1 - U(s, 10);
    float y = b.y0 + U(s, 10);
    brand_header(s, dl, box(ImVec2(x0, y), ImVec2(U(s, 356), U(s, 80))));
    y += U(s, 80) + U(s, 10);
    icon(s, dl, "computer", 32, ImVec2(x0, y));
    const float cx = x0 + U(s, 32) + U(s, 14);
    int terms = 0, agents = 0;
    for (const auto& [id, v] : s.views_by_id) {
        ++terms;
        agents += is_agent(v) ? 1 : 0;
    }
    float cy = y;
    auto row = [&](const std::string& t, float indent = 0) {
        plain_label(s, dl, cx + U(s, indent), cy, t, rgb(C::kText));
        cy += U(s, 13) + U(s, 2);
    };
    row("AgentDesk 98");
    row(std::string("Version ") + AGENTDESK_VERSION_STRING + " Second Edition");
    row("Copyright \xC2\xA9 2026 AgentDesk");
    cy += U(s, 6) + U(s, 2);
    row("This product is licensed to:");
    row(user_name_now(), 12);
    row("Personal Use Edition", 12);
    cy += U(s, 6) + U(s, 2);
    etch(s, dl, cx, cy, x1 - cx, false);
    cy += U(s, 2) + U(s, 2);
    cy += U(s, 4) + U(s, 2);
    row("Terminals running:  " + std::to_string(terms));
    row("Agents at work:  " + std::to_string(agents));
    row("agent-host:  ?");
    row("Physical memory available to agents:  64.0 MB RAM");
    cy -= U(s, 2);
    y = std::max(y + U(s, 32), cy) + U(s, 10);
    if (buttons(s, dl, x0, x1, y, {{"OK", true}}, true) == 0)
        close_dialog(s, w);
    return y + U(s, 23) + U(s, 10) - b.y0;
}

// Close Program: every running terminal, End Task and Shut Down.
float render_taskman(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    const auto list = tasks(s);
    if (d.sel >= static_cast<int>(list.size()))
        d.sel = std::max(0, static_cast<int>(list.size()) - 1);
    const float x0 = b.x0 + U(s, 10), x1 = b.x1 - U(s, 10);
    float y = b.y0 + U(s, 10);
    const Box fb = box(ImVec2(x0, y), ImVec2(x1 - x0, U(s, 150) + U(s, 2) + U(s, 4)));
    const Box in = field(s, dl, fb, 1, 1);
    dl->PushClipRect(in.a(), in.b(), true);
    float ry = in.y0;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        const auto& t = list[static_cast<std::size_t>(i)];
        const Box r{in.x0, ry, in.x1, ry + U(s, 16)};
        ImGui::PushID(i);
        const Pressed p = area("##task", r);
        ImGui::PopID();
        const bool on = i == d.sel;
        if (on)
            fill(dl, r, rgb(C::kSelect));
        const ImU32 fg = on ? rgb(C::kSelectText) : rgb(C::kText);
        float x = r.x0 + U(s, 2);
        program_icon(s, dl, t.program, 16, ImVec2(x, r.y0));
        x += U(s, 16) + U(s, 4);
        const float aw = text_w(s, t.address);
        plain_label(s, dl, r.x1 - U(s, 2) - aw, r.y0 + U(s, 1.5f), t.address,
                    on ? rgb(C::kSelectText) : rgb(C::kGray));
        label_clipped(s, dl, x, r.y0 + U(s, 1.5f), t.name + (t.status.empty() ? "" : " (" + t.status + ")"),
                      fg, r.x1 - U(s, 2) - aw - U(s, 4) - x);
        if (p.clicked) {
            d.sel = i;
            if (p.double_click) {
                close_dialog(s, w);
                run_command(s, "agent.focus:" + t.address);
            }
        }
        ry += U(s, 16);
    }
    dl->PopClipRect();
    y = fb.y1 + U(s, 10);
    y += lines(s, dl, x0, y,
               wrap(s,
                    "WARNING: Pressing CTRL+ALT+DEL again will restart your\ncomputer. You will lose unsaved "
                    "information in all\nprograms that are running.",
                    x1 - x0),
               rgb(C::kText)) +
         U(s, 10);
    const int hit = buttons(s, dl, x0, x1, y, {{"&End Task", true}, {"Sh&ut Down"}, {"Cancel"}}, false);
    if (hit == 0) {
        end_task(s, d);
    } else if (hit == 1) {
        close_dialog(s, w);
        Dialog sd;
        sd.kind = "shutdown";
        open_dialog(s, std::move(sd));
    } else if (hit == 2) {
        close_dialog(s, w);
    }
    return y + U(s, 23) + U(s, 10) - b.y0;
}

// ---------------------------------------------------------------- Display Properties

constexpr const char* kDisplayTabs[] = {"Background", "Effects", "Settings"};

struct DisplayTab {
    Box b;
    bool on;
    int i;
};

// The tab row, laid out at the bottom of a 20 px row from x; the chosen tab is 1 px lower, over
// the page frame's top edge.
std::vector<DisplayTab> display_tab_boxes(App::State& s, float x, float y, int page) {
    const float row_h = U(s, 20);
    std::vector<DisplayTab> tabs;
    for (int i = 0; i < 3; ++i) {
        const bool on = i == page;
        const float tw = text_w(s, kDisplayTabs[i]) + U(s, on ? 16 : 12) + U(s, 3);
        const float th = U(s, 13) + U(s, on ? 6 : 4) + U(s, 1);
        const float top = y + row_h - th + (on ? U(s, 1) : 0.0f);
        tabs.push_back({box(ImVec2(x, top), ImVec2(tw, th)), on, i});
        x += tw;
    }
    return tabs;
}

// Tabs: top, left and right edges white, an inner right edge dark; the face. A click picks the page.
void display_tabs(App::State& s, ImDrawList* dl, const std::vector<DisplayTab>& tabs, Dialog& d) {
    const float o = one(s);
    for (const auto& t : tabs) {
        const Box tb = t.b;
        if (t.on)
            fill(dl, Box{tb.x0, tb.y0, tb.x1, tb.y1 + o}, rgb(C::kFace));
        dl->AddRectFilled(ImVec2(tb.x0, tb.y0), ImVec2(tb.x1, tb.y0 + o), rgb(C::kHi));
        dl->AddRectFilled(ImVec2(tb.x0, tb.y0), ImVec2(tb.x0 + o, tb.y1), rgb(C::kHi));
        dl->AddRectFilled(ImVec2(tb.x1 - o, tb.y0), ImVec2(tb.x1, tb.y1), rgb(C::kHi));
        const Box inner{tb.x0 + o, tb.y0 + o, tb.x1 - o, tb.y1};
        fill(dl, inner, rgb(C::kFace));
        dl->AddRectFilled(ImVec2(inner.x1 - o, inner.y0), ImVec2(inner.x1, inner.y1), rgb(C::kDark));
        label(s, dl, inner.x0 + U(s, t.on ? 8 : 6), inner.y0 + U(s, t.on ? 3 : 2), kDisplayTabs[t.i],
              rgb(C::kText));
        ImGui::PushID(t.i);
        if (area("##dtab", tb).clicked)
            d.page = t.i;
        ImGui::PopID();
    }
}

// Background: the monitor showing the desktop, and the list of wallpapers.
void display_background(App::State& s, ImDrawList* dl, Box content) {
    const std::string desktop =
        shared_str(s, "win98.desktop").empty() ? std::string("#008080") : shared_str(s, "win98.desktop");
    float cy = content.y0;
    const float mw = U(s, 184), mh = U(s, 170);
    const float mx = snap(content.x0 + (content.w() - mw) * 0.5f);
    picture(s, dl, "img/monitor.png", box(ImVec2(mx, cy), ImVec2(mw, mh)));
    const Box scr = box(ImVec2(mx + U(s, 17), cy + U(s, 16)), ImVec2(U(s, 152), U(s, 112)));
    fill(dl, scr, desktop == "clouds" ? hex("#6fa8dc") : hex(desktop));
    if (desktop == "clouds")
        picture(s, dl, "img/clouds.png", scr);
    cy += mh + U(s, 8);
    label(s, dl, content.x0, cy, "&Wallpaper:", rgb(C::kText));
    cy += U(s, 13) + U(s, 8);
    static const std::pair<const char*, const char*> kColors[] = {
        {"(None)", "#008080"}, {"Black", "#000000"},  {"Navy", "#000080"},
        {"Plum", "#5c3566"},   {"Forest", "#004000"}, {"Clouds", "clouds"}};
    const Box lb = box(ImVec2(content.x0, cy), ImVec2(content.w(), U(s, 16) * 6 + U(s, 2) + U(s, 4)));
    const Box li = field(s, dl, lb, 1, 1);
    float ry = li.y0;
    for (const auto& [n, c] : kColors) {
        const Box r{li.x0, ry, li.x1, ry + U(s, 16)};
        ImGui::PushID(n);
        const Pressed p = area("##bg", r);
        ImGui::PopID();
        const bool on = desktop == c;
        if (on)
            fill(dl, r, rgb(C::kSelect));
        plain_label(s, dl, r.x0 + U(s, 2), r.y0 + U(s, 1.5f), n, on ? rgb(C::kSelectText) : rgb(C::kText));
        if (p.clicked)
            set_desktop(s, c);
        ry += U(s, 16);
    }
}

// Effects: sounds and the Office Assistant.
void display_effects(App::State& s, ImDrawList* dl, Box content) {
    const float px0 = content.x0 + U(s, 6), px1 = content.x1 - U(s, 6);
    float cy = content.y0 + U(s, 6);
    const bool mute = shared(s, "win98.mute") == Json(true);
    if (check(s, dl, "##snd", px0, cy, "Play &sounds (startup, dialogs, games)", !mute))
        set_shared(s, "win98.mute", !mute);
    cy += U(s, 13) + U(s, 10);
    const bool clip = shared_str(s, "win98.clippy") != "off";
    if (check(s, dl, "##clippy", px0, cy, "Show the &Office Assistant", clip))
        set_shared(s, "win98.clippy", clip ? "off" : "on");
    cy += U(s, 13) + U(s, 10);
    etch(s, dl, px0, cy, px1 - px0, false);
    cy += U(s, 2) + U(s, 10);
    lines(s, dl, px0, cy,
          wrap(s, "Sounds also follow AgentDesk's own\nsetting (Control Panel > Alerts).", px1 - px0),
          rgb(C::kGray));
}

// Settings: Small or Large Fonts (the Win98 pixel scale).
void display_settings(App::State& s, ImDrawList* dl, Box content) {
    const float px0 = content.x0 + U(s, 6), px1 = content.x1 - U(s, 6);
    float cy = content.y0 + U(s, 6);
    label(s, dl, px0, cy, "Font size:", rgb(C::kText));
    cy += U(s, 13) + U(s, 10);
    const int sc = scale(s);
    if (radio(s, dl, "##s1", px0, cy, "&Small Fonts (pixel perfect)", sc == 1))
        set_shared(s, "win98.scale", 1);
    cy += U(s, 13) + U(s, 10);
    if (radio(s, dl, "##s2", px0, cy, "&Large Fonts (every pixel doubled)", sc == 2))
        set_shared(s, "win98.scale", 2);
    cy += U(s, 13) + U(s, 10);
    etch(s, dl, px0, cy, px1 - px0, false);
    cy += U(s, 2) + U(s, 10);
    char zoom[32];
    std::snprintf(zoom, sizeof zoom, "%g", s.settings.zoom);
    lines(
        s, dl, px0, cy,
        wrap(s,
             std::string("AgentDesk's zoom is ") + zoom + "x; AgentDesk 98 draws\nwhole pixels at any zoom.",
             px1 - px0),
        rgb(C::kGray));
}

// Display Properties: Background, Effects and Settings pages.
float render_display(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    const float x0 = b.x0 + U(s, 8), x1 = b.x1 - U(s, 8);
    float y = b.y0 + U(s, 8);
    const auto tabs = display_tab_boxes(s, x0 + U(s, 4), y, d.page);
    // The page frame first; the chosen tab is drawn over its top edge.
    const float fy = y + U(s, 20);
    // Padding 10, at least 300 tall; the Background page is taller (monitor 170, gap 8, label 13,
    // gap 8, the list of six 16 px rows in a field).
    const float page_h =
        d.page == 0 ? std::max(U(s, 300), U(s, 20 + 170 + 8 + 13 + 8 + 16 * 6 + 6)) : U(s, 300);
    const Box pb{x0, fy, x1, snap(fy + 2 * one(s) + page_h + 2 * one(s))};
    bevel(s, dl, pb, Bevel::kWindow, rgb(C::kFace));
    const Box content{pb.x0 + 2 * one(s) + U(s, 10), fy + 2 * one(s) + U(s, 10),
                      pb.x1 - 2 * one(s) - U(s, 10), fy};
    if (d.page == 0)
        display_background(s, dl, content);
    else if (d.page == 1)
        display_effects(s, dl, content);
    else
        display_settings(s, dl, content);
    display_tabs(s, dl, tabs, d);
    y = pb.y1 + U(s, 10);
    const int hit = buttons(s, dl, x0, x1, y, {{"OK", true}, {"Cancel"}, {"&Apply", false, true}}, true);
    if (hit == 0 || hit == 1)
        close_dialog(s, w);
    return y + U(s, 23) + U(s, 8) - b.y0;
}

// Date/Time Properties: the month, the year, the calendar, the clock face and the time zone.
float render_datetime(App::State& s, Window& w, Dialog& /*d*/, ImDrawList* dl, Box b) {
    const k98::DateTime d = local_now();
    const float x0 = b.x0 + U(s, 10), x1 = b.x1 - U(s, 10);
    float y = b.y0 + U(s, 10);
    // Left: the month and year, then the calendar.
    const float fh = U(s, 13) + U(s, 4) + U(s, 4);
    {
        const Box m = box(ImVec2(x0, y), ImVec2(U(s, 80) + U(s, 6) + U(s, 4), fh));
        const Box mi = field(s, dl, m);
        plain_label(s, dl, mi.x0, mi.y0, k98::month_name(d.month), rgb(C::kText));
        const Box yb = box(ImVec2(m.x1 + U(s, 6), y), ImVec2(U(s, 40) + U(s, 6) + U(s, 4), fh));
        const Box yi = field(s, dl, yb);
        plain_label(s, dl, yi.x0, yi.y0, std::to_string(d.year), rgb(C::kText));
    }
    const auto weeks = k98::calendar(d.year, d.month);
    const float cal_w = U(s, 22) * 7 + U(s, 4);
    const float cal_h = U(s, 13) + U(s, 16) * static_cast<float>(weeks.size()) + U(s, 4);
    const float cal_y = y + fh + U(s, 6);
    {
        const Box ci = field(s, dl, box(ImVec2(x0, cal_y), ImVec2(cal_w, cal_h)), 0, 0);
        fill(dl, ci, rgb(C::kWhite));
        fill(dl, Box{ci.x0, ci.y0, ci.x1, ci.y0 + U(s, 13)}, rgb(C::kGray));
        static const char* const kDays[] = {"S", "M", "T", "W", "T", "F", "S"};
        for (int i = 0; i < 7; ++i) {
            const float cx = ci.x0 + U(s, 22) * static_cast<float>(i);
            plain_label(s, dl, cx + (U(s, 22) - text_w(s, kDays[i])) * 0.5f, ci.y0, kDays[i], rgb(C::kWhite));
        }
        float ry = ci.y0 + U(s, 13);
        for (const auto& wk : weeks) {
            for (int i = 0; i < 7; ++i) {
                const int n = wk[static_cast<std::size_t>(i)];
                const Box cell =
                    box(ImVec2(ci.x0 + U(s, 22) * static_cast<float>(i), ry), ImVec2(U(s, 22), U(s, 16)));
                const bool today = n == d.day;
                if (today)
                    fill(dl, cell, rgb(C::kSelect));
                if (n != 0) {
                    const std::string t = std::to_string(n);
                    plain_label(s, dl, cell.x0 + (cell.w() - text_w(s, t)) * 0.5f, cell.y0 + U(s, 1.5f), t,
                                today ? rgb(C::kSelectText) : rgb(C::kText));
                }
            }
            ry += U(s, 16);
        }
    }
    const float left_h = fh + U(s, 6) + cal_h;
    // Right: the clock face and the time.
    const float rx = x0 + std::max(cal_w, U(s, 90) + U(s, 6) + U(s, 50) + U(s, 4) * 2) + U(s, 12);
    const float rw = U(s, 90) + U(s, 6) + U(s, 4);
    const float face_x = snap(rx + (rw - U(s, 96)) * 0.5f);
    for (const auto& dot : k98::clock_face(d))
        fill(dl,
             box(ImVec2(face_x + U(s, static_cast<float>(dot.x)), y + U(s, static_cast<float>(dot.y))),
                 ImVec2(U(s, static_cast<float>(dot.size)), U(s, static_cast<float>(dot.size)))),
             rgb(dot.color));
    {
        const Box ti = field(s, dl, box(ImVec2(rx, y + U(s, 96) + U(s, 6)), ImVec2(rw, fh)));
        const std::string t = k98::time_text(d);
        plain_label(s, dl, ti.x0 + (ti.w() - text_w(s, t)) * 0.5f, ti.y0, t, rgb(C::kText));
    }
    const float right_h = U(s, 96) + U(s, 6) + fh;
    y += std::max(left_h, right_h) + U(s, 10);
    plain_label(s, dl, x0, y, "Time zone: " + k98::time_zone_text(d), rgb(C::kText));
    y += U(s, 13) + U(s, 10);
    if (buttons(s, dl, x0, x1, y, {{"OK", true}, {"Cancel"}}, true) >= 0)
        close_dialog(s, w);
    return y + U(s, 23) + U(s, 10) - b.y0;
}

// An Explorer-like window body: icons in a 200 px field, a status bar.
struct ExItem {
    std::string label;
    std::string icon;
    std::function<void()> act;
};
float explorer(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b, const std::vector<ExItem>& items,
               const std::string& status) {
    const Box fb = box(b.a(), ImVec2(b.w(), U(s, 200) + U(s, 4)));
    const Box in = field(s, dl, fb, 0, 0);
    dl->PushClipRect(in.a(), in.b(), true);
    if (items.empty()) {
        plain_label(s, dl, in.x0 + U(s, 10), in.y0 + U(s, 10), "This folder is empty.", rgb(C::kGray));
    } else {
        float x = in.x0 + U(s, 8), y = in.y0 + U(s, 8);
        float row_h = 0;
        std::function<void()> run;
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const auto& it = items[static_cast<std::size_t>(i)];
            const auto ls = wrap(s, it.label, U(s, 74));
            const float ih = U(s, 32) + U(s, 2) + U(s, 13) * static_cast<float>(ls.size());
            if (x > in.x0 + U(s, 8) && x + U(s, 76) > in.x1 - U(s, 8)) {
                x = in.x0 + U(s, 8);
                y += row_h + U(s, 8);
                row_h = 0;
            }
            ImGui::PushID(i);
            const Pressed p = area("##ex", box(ImVec2(x, y), ImVec2(U(s, 76), ih)));
            ImGui::PopID();
            icon(s, dl, it.icon, 32, ImVec2(snap(x + (U(s, 76) - U(s, 32)) * 0.5f), y));
            float ly = y + U(s, 32) + U(s, 2);
            for (const auto& l : ls) {
                const float lw = text_w(s, l);
                const float lx = snap(x + (U(s, 76) - lw) * 0.5f);
                if (i == d.sel)
                    fill(dl, Box{lx - U(s, 1), ly, lx + lw + U(s, 1), ly + U(s, 13)}, rgb(C::kSelect));
                plain_label(s, dl, lx, ly, l, i == d.sel ? rgb(C::kSelectText) : rgb(C::kText));
                ly += U(s, 13);
            }
            if (p.clicked) {
                d.sel = i;
                if (it.act)
                    run = it.act;
            }
            row_h = std::max(row_h, ih);
            x += U(s, 76) + U(s, 8);
        }
        if (run) {
            close_dialog(s, w);
            run();
        }
    }
    dl->PopClipRect();
    // The status bar: pt 2, gap 2: the count, then "My Computer" (120).
    const float sy = fb.y1 + U(s, 2);
    const float sh = U(s, 13) + U(s, 2) + U(s, 2);
    const Box right = box(ImVec2(b.x1 - U(s, 120), sy), ImVec2(U(s, 120), sh));
    const Box left = box(ImVec2(b.x0, sy), ImVec2(right.x0 - U(s, 2) - b.x0, sh));
    const Box li = well(s, dl, left);
    plain_label(s, dl, li.x0, li.y0, status, rgb(C::kText));
    const Box ri = well(s, dl, right);
    icon(s, dl, "computer", 16, ImVec2(ri.x0, snap(ri.y0 + (ri.h() - U(s, 16)) * 0.5f)));
    plain_label(s, dl, ri.x0 + U(s, 20), ri.y0, "My Computer", rgb(C::kText));
    return sy + sh - b.y0;
}

// My Computer: a drive per space, Control Panel and Printers.
float render_mycomputer(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    static const char* const kLetters = "CDEFGHIJKLMNOPQRSTUVWXYZ";
    std::vector<ExItem> items;
    const auto cwds = s.session_cwds();
    for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i) {
        const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
        const char letter = i < 24 ? kLetters[i] : 'Z';
        const std::string id = sp.id;
        items.push_back({s.ws.space_title(i, cwds) + " (" + std::string(1, letter) + ":)",
                         sp.machine.empty() ? "drive" : "netdrive", [&s, id] { select_space_id(s, id); }});
    }
    items.push_back({"Control Panel", "control", [&s] { run_command(s, "app.settings"); }});
    items.push_back(
        {"Printers", "printer", [&s] {
             open_dialog(
                 s,
                 message("Printers",
                         "There are no printers installed.\nPrinting a terminal is a choice, not a feature.",
                         "error"));
         }});
    return explorer(s, w, d, dl, b, items, std::to_string(items.size()) + " object(s)");
}

// Network Neighborhood: this computer and each remote machine; opening one makes a space there.
float render_network(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    std::vector<ExItem> items;
    items.push_back({"Entire Network", "network", nullptr});
    items.push_back(
        {"This computer", "computer", [&s] {
             open_dialog(s, prompt("New Space", "Type a name for the new space:", "folder",
                                   [&s](const std::string& n, const std::string&) { new_space(s, n, ""); }));
         }});
    for (const auto& m : s.settings.machines) {
        const std::string name = m.name;
        items.push_back({name, "computer", [&s, name] {
                             open_dialog(s,
                                         prompt("New Space on " + name,
                                                "Type a name for the new space on " + name + ":", "netdrive",
                                                [&s, name](const std::string& n, const std::string&) {
                                                    new_space(s, n, name);
                                                }));
                         }});
    }
    return explorer(s, w, d, dl, b, items, std::to_string(items.size()) + " object(s)");
}

// Recycle Bin: always empty.
float render_bin(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    float h = explorer(s, w, d, dl, b, {}, "0 object(s)");
    h += U(s, 8);
    if (buttons(s, dl, b.x0, b.x1, b.y0 + h, {{"&Empty Recycle Bin", false, false, 120}}, true) == 0)
        sfx(s, "recycle");
    return h + U(s, 23);
}

// ---------------------------------------------------------------- Welcome

struct WelcomePage {
    const char* icon;
    const char* title;
    const char* text;
    const char* go;  // the page's button
    std::function<void()> act;
};

std::vector<WelcomePage> welcome_pages(App::State& s) {
    return {
        {"programs", "Begin an Agent",
         "AgentDesk runs your coding agents in windows,\nlike it's 1998 and the future at once.\n\nStart one "
         "from Start > Programs > Agents,\nor open the Run box and type its name.",
         "&Run...",
         [&s] {
             Dialog r = kind_dialog("run");
             r.value = "claude";
             open_dialog(s, std::move(r));
         }},
        {"help", "Discover AgentDesk 98",
         "The taskbar is your tabs. The folders on\nthe desktop are your spaces. Each window\nis a pane: "
         "double-click its title bar to\nmaximize it, or use _ to minimize the tab.\n\nCtrl+Esc opens the "
         "Start menu.",
         "&Help", [&s] { run_command(s, "app.help"); }},
        {"mines", "Play Mines",
         "Your agent is thinking.\nYou've earned a break.\n\nSolitaire is on the desktop too.", "&Play",
         [&s] { set_app(s, "mines", "open"); }},
        {"taskman", "Maintain Your Computer",
         "See every program AgentDesk runs, and end\nthe ones you don't need anymore.\n\nCtrl+Shift+Esc "
         "opens it "
         "anytime.",
         "&Open", [&s] { open_dialog(s, kind_dialog("taskman")); }},
        {"logoff", "Leave AgentDesk 98",
         "Miss the future? Log Off takes you\nback to AgentDesk's normal layout.\nYour terminals keep "
         "running.",
         "&Log Off", [&s] { open_dialog(s, kind_dialog("logoff")); }},
    };
}

// The header: white to light blue, "Welcome to AgentDesk 98" at 22 px beside the Start icon.
void welcome_header(App::State& s, ImDrawList* dl, Box hb) {
    gradient(dl, hb, rgb(0xffffff), rgb(0x6aa6e8));
    float x = hb.x0 + U(s, 16);
    const float base = hb.y0 + (hb.h() - U(s, 32)) * 0.5f;
    icon(s, dl, "start", 32, ImVec2(x, snap(base)));
    x += U(s, 32) + U(s, 6);
    const float ty = base + U(s, 32) - U(s, 26);
    x += label(s, dl, x, ty, "Welcome to", rgb(C::kNavy), false, 22, 26, false) + U(s, 6);
    x += label(s, dl, x, ty, "AgentDesk", rgb(C::kText), true, 22, 26, false) + U(s, 6);
    label(s, dl, x, ty, "98", rgb(0xd07000), false, 22, 26, false);
}

// The contents (navy): one row per page; hovering a row chooses it, clicking runs its action.
void welcome_contents(App::State& s, ImDrawList* dl, Box left, const std::vector<WelcomePage>& pages,
                      Dialog& d, std::function<void()>& run) {
    fill(dl, left, rgb(0x000080));
    float ly = left.y0 + U(s, 8);
    label(s, dl, left.x0 + U(s, 8), ly, "CONTENTS", rgb(0xffff80), true, 11, 13, false);
    ly += U(s, 13) + U(s, 4) + U(s, 2);
    for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
        const WelcomePage& pg = pages[static_cast<std::size_t>(i)];
        const Box r{left.x0 + U(s, 8), ly, left.x1 - U(s, 8), ly + U(s, 32) + U(s, 6)};
        ImGui::PushID(i);
        const Pressed p = area("##wl", r);
        ImGui::PopID();
        if (p.hovered && d.sel != i)
            d.sel = i;
        const ImU32 fg = i == d.sel ? rgb(0xffff80) : rgb(C::kWhite);
        icon(s, dl, pg.icon, 32, ImVec2(r.x0 + U(s, 3), r.y0 + U(s, 3)));
        const float tx = r.x0 + U(s, 3) + U(s, 32) + U(s, 8);
        const float ty = r.y0 + (r.h() - U(s, 13)) * 0.5f;
        const float tw = plain_label(s, dl, tx, ty, pg.title, fg);
        if (i == d.sel)
            underline(s, dl, tx, ty, tw, fg);
        if (p.clicked)
            run = pg.act;
        ly = r.y1 + U(s, 2);
    }
}

// The chosen page (white): its title, its text and its button at the foot.
void welcome_page(App::State& s, ImDrawList* dl, Box right, const WelcomePage& pg,
                  std::function<void()>& run) {
    fill(dl, right, rgb(C::kWhite));
    const float px0 = right.x0 + U(s, 14), px1 = right.x1 - U(s, 14);
    float y = right.y0 + U(s, 14);
    label(s, dl, px0, y, pg.title, rgb(C::kNavy), true, 16, 20, false);
    y += U(s, 20) + U(s, 10);
    lines(s, dl, px0, y, wrap(s, pg.text, px1 - px0), rgb(C::kText));
    const float by = right.y1 - U(s, 14) - U(s, 23);
    ImGui::PushID("go");
    if (buttons(s, dl, px0, px1, by, {{pg.go, true}}, true) == 0)
        run = pg.act;
    ImGui::PopID();
}

// Welcome: a contents list on the left, the chosen page on the right, and a footer.
float render_welcome(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    const auto pages = welcome_pages(s);
    const int n = static_cast<int>(pages.size());
    if (d.sel < 0 || d.sel >= n)
        d.sel = 0;
    const int shown = d.sel;  // the page drawn this frame, even if hovering picks another
    std::function<void()> run;
    const Box hb{b.x0, b.y0, b.x1, snap(b.y0 + U(s, 56))};
    welcome_header(s, dl, hb);
    const Box body{b.x0, hb.y1, b.x1, snap(hb.y1 + U(s, 230))};
    const Box left{body.x0, body.y0, snap(body.x0 + U(s, 190)), body.y1};
    welcome_contents(s, dl, left, pages, d, run);
    welcome_page(s, dl, Box{left.x1, body.y0, body.x1, body.y1}, pages[static_cast<std::size_t>(shown)], run);
    // The footer: "show at startup" and Close.
    etch(s, dl, b.x0, body.y1, b.w(), false);
    const float fy = body.y1 + 2 * one(s);
    const float row_h = U(s, 23);
    const bool show = shared_str(s, "win98.welcome") != "off";
    if (check(s, dl, "##wl-show", b.x0 + U(s, 8), fy + U(s, 8) + (row_h - U(s, 13)) * 0.5f,
              "&Show this screen each time AgentDesk 98 starts", show))
        set_shared(s, "win98.welcome", show ? "off" : "on");
    ImGui::PushID("close");
    if (buttons(s, dl, b.x0, b.x1 - U(s, 8), fy + U(s, 8), {{"&Close"}}, true) == 0)
        close_dialog(s, w);
    ImGui::PopID();
    if (run) {
        close_dialog(s, w);
        run();
    }
    return fy + U(s, 8) + row_h + U(s, 8) - b.y0;
}

// The timers of the full-window boot, shut down and restart screens.
void screen_timers(App::State& s, Window& w, Dialog& d) {
    const auto since = Clock::now() - d.opened;
    using std::chrono::milliseconds;
    if (d.mode == "boot") {
        if (since >= milliseconds(3400)) {
            close_dialog(s, w);
            if (shared_str(s, "win98.welcome") != "off") {
                Dialog wl;
                wl.kind = "welcome";
                open_dialog(s, std::move(wl));
            }
        }
    } else if (d.mode == "shutdown") {
        if (d.stage == 0 && since >= milliseconds(2600)) {
            d.stage = 1;
            d.stage_at = Clock::now();
        } else if (d.stage == 1 && Clock::now() - d.stage_at >= milliseconds(1600)) {
            close_dialog(s, w);
            run_command(s, "window.close");
        }
    } else {
        if (d.stage == 0 && since >= milliseconds(1200)) {
            d.stage = 1;
            d.stage_at = Clock::now();
            sfx(s, "startup");
        } else if (d.stage == 1 && Clock::now() - d.stage_at >= milliseconds(3200)) {
            close_dialog(s, w);
        }
    }
}

void render_screen(App::State& s, Dialog& d, ImDrawList* dl, Box b) {
    if (d.mode == "shutdown" && d.stage == 1) {
        fill(dl, b, rgb(0x000000));
        ImFont* f = st(s).dos;
        const float size = U(s, 32), line = U(s, 40);
        const char* const kLines[] = {"It's now safe to turn off", "your computer."};
        float y = b.y0 + (b.h() - line * 2) * 0.5f + (line - size) * 0.5f;
        for (const char* l : kLines) {
            const float tw = f->CalcTextSizeA(size, FLT_MAX, 0, l).x;
            dl->AddText(f, size, ImVec2(snap(b.x0 + (b.w() - tw) * 0.5f), snap(y)), rgb(0xff9c00), l);
            y += line;
        }
        return;
    }
    const std::string msg =
        d.mode == "shutdown" || d.stage == 0 ? "AgentDesk is shutting down..." : "Starting AgentDesk 98...";
    fill(dl, b, rgb(0x000000));
    brand_splash(s, dl, b);
    const float tw = text_w(s, msg, true, 14);
    label(s, dl, b.x0 + (b.w() - tw) * 0.5f, b.y1 - U(s, 40) - U(s, 14), msg, rgb(C::kWhite), true, 14, 14,
          false);
}

// A dialog body: draws the kind's contents in `b` and returns the height it took.
using BodyFn = float (*)(App::State&, Window&, Dialog&, ImDrawList*, Box);

const std::map<std::string, BodyFn> kBodies = {
    {"prompt", render_prompt},     {"confirm", render_confirm},       {"message", render_confirm},
    {"run", render_run},           {"shutdown", render_shutdown},     {"logoff", render_logoff},
    {"about", render_about},       {"taskman", render_taskman},       {"display", render_display},
    {"datetime", render_datetime}, {"mycomputer", render_mycomputer}, {"network", render_network},
    {"bin", render_bin},           {"welcome", render_welcome},
};

}  // namespace

// A dialog's frame (the title bar with Help, unless Explorer-like, and Close) and its body.
// Returns the window's size.
ImVec2 render_dialog(App::State& s, Window& w, ImDrawList* dl, ImVec2 at) {
    Dialog& d = *w.dialog;
    if (d.kind == "screen") {
        screen_timers(s, w, d);
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        render_screen(s, d, dl, box(vp->WorkPos, vp->WorkSize));
        return vp->WorkSize;
    }
    const float width = snap(U(s, d.width));
    const bool explorerish = d.kind == "mycomputer" || d.kind == "network" || d.kind == "bin";
    static const std::map<std::string, std::string> kIcon = {
        {"mycomputer", "computer"}, {"network", "network"}, {"bin", "bin"},
        {"display", "display"},     {"run", "run"},         {"taskman", "taskman"},
        {"datetime", "clock"}};
    const auto ic = kIcon.find(d.kind);
    ImDrawListSplitter split;
    split.Split(dl, 2);
    split.SetCurrentChannel(dl, 1);
    const float o = one(s);
    const Box frame_in{at.x + 3 * o, at.y + 3 * o, at.x + width - 3 * o, at.y + 100000};
    // The title bar (18), a 1 px gap, the body.
    std::vector<Control> controls;
    if (!explorerish)
        controls.push_back({"help", [] {}, "Help"});
    controls.push_back({"close", [&s, &w] { close_dialog(s, w); }, "Close"});
    title_bar(
        s, dl, "##dlg-title", Box{frame_in.x0, frame_in.y0, frame_in.x1, frame_in.y0 + U(s, 18)},
        d.title.empty() ? kind_title(d.kind) : d.title,
        ic == kIcon.end() ? std::function<void(ImVec2)>()
                          : [&s, dl, name = ic->second](ImVec2 p) { icon(s, dl, name, 16, p); },
        true, controls, false);
    const Box body{frame_in.x0, frame_in.y0 + U(s, 18) + U(s, 1), frame_in.x1, frame_in.y1};
    float h = 0;
    if (const auto body_fn = kBodies.find(d.kind); body_fn != kBodies.end()) {
        h = body_fn->second(s, w, d, dl, body);
    } else {
        plain_label(s, dl, body.x0 + U(s, 10), body.y0 + U(s, 10), "Unknown dialog " + d.kind, rgb(C::kText));
        h = U(s, 33);
    }
    const float height = snap(body.y0 + h - at.y + 3 * o);
    split.SetCurrentChannel(dl, 0);
    bevel(s, dl, box(at, ImVec2(width, height)), Bevel::kWindow, rgb(C::kFace));
    split.Merge(dl);
    return ImVec2(width, height);
}
}  // namespace detail::w98

}  // namespace agentdesk::desktop
