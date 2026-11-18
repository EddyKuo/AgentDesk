// Herdr 98, part 2: the dialogs (herdr-app defaults/mods/win98/dialog.js, commit 321c9a8).
// Each kind is drawn top-down in Win98 px from dialog.js; the window's height is what it took.

namespace w98 {

void new_tab(App::State& s, const std::string& name, int space = -1, const std::string& profile = {},
             const std::vector<std::string>& args = {});
void new_space(App::State& s, const std::string& name, const std::string& machine);
void select_space_id(App::State& s, const std::string& id);
Dialog prompt(const std::string& title, const std::string& text, const std::string& icon,
              std::function<void(const std::string&, const std::string&)> cmd,
              std::optional<std::string> value = {}, bool machines = false);
Dialog message(const std::string& title, const std::string& text, const std::string& icon = {});
std::string user_name_now();
struct Task {
    std::string address;
    std::string name;
    std::string program;
    std::string status;
};
std::vector<Task> tasks(App::State& s);
std::string status_word(Attention a);

// dialog.js 26-38.
std::string kind_title(const std::string& kind) {
    static const std::map<std::string, std::string> kT = {{"run", "Run"},
                                                          {"shutdown", "Shut Down Herdr"},
                                                          {"logoff", "Log Off Herdr"},
                                                          {"about", "About Herdr 98"},
                                                          {"taskman", "Close Program"},
                                                          {"display", "Display Properties"},
                                                          {"datetime", "Date/Time Properties"},
                                                          {"welcome", "Welcome to Herdr 98"},
                                                          {"mycomputer", "My Computer"},
                                                          {"network", "Network Neighborhood"},
                                                          {"bin", "Recycle Bin"}};
    const auto it = kT.find(kind);
    return it == kT.end() ? std::string("Herdr") : it->second;
}

// dialog.js 263-273: 75 x 23 buttons in a row, gap 6, at the end or in the middle. Returns the
// index pressed, or -1.
struct Btn {
    std::string label;
    bool def = false;
    bool disabled = false;
    float w = 75;
};
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

// dialog.js 275-285: the text box: the text (selected while fresh) and a blinking caret.
void input(App::State& s, ImDrawList* dl, Box b, const Dialog& d) {
    const Box in = field(s, dl, b);
    dl->PushClipRect(in.a(), in.b(), true);
    const float top = in.y0 + (in.h() - U(s, 13)) * 0.5f;
    float x = in.x0;
    if (d.fresh && !d.input.empty()) {
        const float w = text_w(s, d.input);
        fill(dl, Box{x, snap(top), snap(x + w), snap(top + U(s, 13))}, rgb(C::kSelect));
        x += label(s, dl, x, top, d.input, rgb(C::kSelectText), false, 11, 13, false);
    } else {
        x += label(s, dl, x, top, d.input, rgb(C::kText), false, 11, 13, false);
    }
    // loop("opacity", {from: 0, to: 1, period: 1060})
    const double t = std::fmod(ImGui::GetTime() * 1000.0, 1060.0) / 1060.0;
    const int a = static_cast<int>(255.0 * (t < 0.5 ? t * 2 : 2 - t * 2));
    fill(dl, Box{snap(x), snap(top), snap(x) + one(s), snap(top + U(s, 13))}, rgb(C::kText, a));
    dl->PopClipRect();
}

// dialog.js 287-299: a radio button and its label.
bool radio(App::State& s, ImDrawList* dl, const char* id, float x, float y, const std::string& text,
           bool on) {
    const float w = U(s, 12) + U(s, 6) + text_w(s, k98::plain(text));
    const Pressed p = area(id, box(ImVec2(x, y), ImVec2(w, U(s, 13))));
    image(s, dl, root_dir() / L"icons" / (on ? L"radio-on.svg" : L"radio-off.svg"),
          box(ImVec2(x, y + (U(s, 13) - U(s, 12)) * 0.5f), ImVec2(U(s, 12), U(s, 12))));
    label(s, dl, x + U(s, 18), y, text, rgb(C::kText));
    return p.clicked;
}

// dialog.js 301-316: a check box (a 13 px field) and its label.
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

// dialog.js 185-188.
void pick_machine(App::State& s, Dialog& d, int dir) {
    std::vector<std::string> all = {""};
    for (const auto& m : s.settings.machines)
        all.push_back(m.name);
    const auto it = std::find(all.begin(), all.end(), d.machine);
    const int i = it == all.end() ? 0 : static_cast<int>(it - all.begin());
    const int n = static_cast<int>(all.size());
    d.machine = all[static_cast<std::size_t>((i + dir + n) % n)];
}

// dialog.js 221-229: a command line in a new tab, then the shell stays (herdr: sh -c "<cmd>;
// exec $SHELL"). On Windows the shell runs it and stays: PowerShell -NoExit -Command, cmd /K.
void run_it(App::State& s, Window& w, Dialog& d) {
    std::string cmd = d.input;
    while (!cmd.empty() && std::isspace(static_cast<unsigned char>(cmd.back())))
        cmd.pop_back();
    while (!cmd.empty() && std::isspace(static_cast<unsigned char>(cmd.front())))
        cmd.erase(cmd.begin());
    if (cmd.empty())
        return;
    std::vector<std::string> hist;
    for (const auto& h : shared(s, "win98.run"))
        if (h.is_string())
            hist.push_back(h.get<std::string>());
    set_shared(s, "win98.run", k98::run_history(hist, cmd));
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

// dialog.js 231-245.
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

// dialog.js 253-259.
void end_task(App::State& s, Dialog& d) {
    const auto list = tasks(s);
    if (d.sel < 0 || d.sel >= static_cast<int>(list.size()))
        return;
    run_command(s, "terminal.kill:" + list[static_cast<std::size_t>(d.sel)].address);
    sfx(s, "chord");
    d.sel = std::max(0, d.sel - 1);
}

// dialog.js 190-219.
void ok(App::State& s, Window& w, Dialog& d) {
    if (d.kind == "prompt") {
        std::string name = d.input;
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
            name.pop_back();
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
            name.erase(name.begin());
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

// dialog.js 154-183: the keys of the dialog that has the keyboard.
void dialog_keys(App::State& s, Window& w, Dialog& d) {
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    const ImGuiIO& io = ImGui::GetIO();
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
        bool handled = true;
        bool select_all = false;
        if (pressed(ImGuiKey_Backspace)) {
            if (d.fresh)
                d.input.clear();
            else if (!d.input.empty()) {
                std::size_t n = d.input.size() - 1;
                while (n > 0 && (static_cast<unsigned char>(d.input[n]) & 0xC0) == 0x80)
                    --n;
                d.input.resize(n);
            }
        } else if (io.KeyCtrl && pressed(ImGuiKey_U)) {
            d.input.clear();
        } else if (io.KeyCtrl && pressed(ImGuiKey_W)) {
            // replace(/\S+\s*$/, "")
            std::size_t e = d.input.size();
            while (e > 0 && std::isspace(static_cast<unsigned char>(d.input[e - 1])))
                --e;
            while (e > 0 && !std::isspace(static_cast<unsigned char>(d.input[e - 1])))
                --e;
            if (e < d.input.size() && (e == d.input.size() || true)) {
                const bool had_word = [&] {
                    for (std::size_t i = e; i < d.input.size(); ++i)
                        if (!std::isspace(static_cast<unsigned char>(d.input[i])))
                            return true;
                    return false;
                }();
                if (had_word)
                    d.input.resize(e);
            }
        } else if (io.KeyCtrl && pressed(ImGuiKey_A)) {
            d.fresh = true;
            select_all = true;
        } else if (!io.KeyCtrl && !io.KeySuper && !io.InputQueueCharacters.empty()) {
            std::string typed;
            for (const ImWchar c : io.InputQueueCharacters) {
                if (c < 32)
                    continue;
                const auto u = static_cast<unsigned>(c);
                if (u < 0x80) {
                    typed += static_cast<char>(u);
                } else if (u < 0x800) {
                    typed += static_cast<char>(0xC0 | (u >> 6));
                    typed += static_cast<char>(0x80 | (u & 0x3F));
                } else if (u < 0x10000) {
                    typed += static_cast<char>(0xE0 | (u >> 12));
                    typed += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
                    typed += static_cast<char>(0x80 | (u & 0x3F));
                } else {
                    typed += static_cast<char>(0xF0 | (u >> 18));
                    typed += static_cast<char>(0x80 | ((u >> 12) & 0x3F));
                    typed += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
                    typed += static_cast<char>(0x80 | (u & 0x3F));
                }
            }
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

// ---------------------------------------------------------------- the kinds

// dialog.js 333-360.
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
    const int hit = buttons(s, dl, x0, x1, y, {{"OK", true}, {"Cancel"}}, true);
    if (hit == 0)
        ok(s, w, d);
    else if (hit == 1)
        close_dialog(s, w);
    return y + U(s, 23) + U(s, 10) - b.y0;
}

// dialog.js 362-386 (confirm and message).
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

// dialog.js 388-423.
float render_run(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    const float x0 = b.x0 + U(s, 10), x1 = b.x1 - U(s, 10);
    float y = b.y0 + U(s, 10);
    icon(s, dl, "run", 32, ImVec2(x0, y));
    const float tx = x0 + U(s, 32) + U(s, 12);
    const float th = lines(
        s, dl, tx, y,
        wrap(s, "Type the name of a program, and Herdr will\nopen it for you in a new window.", x1 - tx),
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
            std::vector<std::string> hist;
            for (const auto& h : shared(s, "win98.run"))
                if (h.is_string())
                    hist.push_back(h.get<std::string>());
            const auto it = std::find(hist.begin(), hist.end(), d.input);
            const int i = it == hist.end() ? -1 : static_cast<int>(it - hist.begin());
            d.input = hist.empty() ? std::string()
                                   : hist[static_cast<std::size_t>(
                                         (i + 1) % std::max<int>(1, static_cast<int>(hist.size())))];
            d.fresh = true;
        }
    }
    y += U(s, 23) + U(s, 12);
    const int hit = buttons(s, dl, x0, x1, y, {{"OK", true}, {"Cancel"}, {"&Browse...", false, true}}, true);
    if (hit == 0)
        ok(s, w, d);
    else if (hit == 1)
        close_dialog(s, w);
    return y + U(s, 23) + U(s, 10) - b.y0;
}

// dialog.js 425-449.
float render_shutdown(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    const float x0 = b.x0 + U(s, 10), x1 = b.x1 - U(s, 10);
    float y = b.y0 + U(s, 10);
    icon(s, dl, "shutdown", 32, ImVec2(x0, y));
    const float cx = x0 + U(s, 32) + U(s, 14);
    float cy = y;
    label(s, dl, cx, cy, "What do you want the computer to do?", rgb(C::kText), false, 11, 13, false);
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
    if (hit == 0)
        ok(s, w, d);
    else if (hit == 1)
        close_dialog(s, w);
    else if (hit == 2)
        run_command(s, "app.help");
    return y + U(s, 23) + U(s, 10) - b.y0;
}

// dialog.js 451-457.
float render_logoff(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    const float x0 = b.x0 + U(s, 12), x1 = b.x1 - U(s, 12);
    float y = b.y0 + U(s, 12);
    icon(s, dl, "logoff", 32, ImVec2(x0, y));
    label(s, dl, x0 + U(s, 32) + U(s, 14), y + (U(s, 32) - U(s, 13)) * 0.5f,
          "Are you sure you want to log off?", rgb(C::kText), false, 11, 13, false);
    y += U(s, 32) + U(s, 14);
    const int hit = buttons(s, dl, x0, x1, y, {{"&Yes", true}, {"&No"}}, false);
    if (hit == 0)
        ok(s, w, d);
    else if (hit == 1)
        close_dialog(s, w);
    return y + U(s, 23) + U(s, 12) - b.y0;
}

// dialog.js 459-493.
float render_about(App::State& s, Window& w, Dialog& /*d*/, ImDrawList* dl, Box b) {
    const float x0 = b.x0 + U(s, 10), x1 = b.x1 - U(s, 10);
    float y = b.y0 + U(s, 10);
    picture(s, dl, "img/about.png", box(ImVec2(x0, y), ImVec2(U(s, 356), U(s, 80))));
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
        label(s, dl, cx + U(s, indent), cy, t, rgb(C::kText), false, 11, 13, false);
        cy += U(s, 13) + U(s, 2);
    };
    row("Herdr 98");
    row(std::string("Version ") + AGENTDESK_VERSION_STRING + " Second Edition");
    row("Copyright \xC2\xA9 1981-2026 The Herd Corp.");
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
    row("herdr-core:  ?");
    row("Physical memory available to agents:  64.0 MB RAM");
    cy -= U(s, 2);
    y = std::max(y + U(s, 32), cy) + U(s, 10);
    if (buttons(s, dl, x0, x1, y, {{"OK", true}}, true) == 0)
        close_dialog(s, w);
    return y + U(s, 23) + U(s, 10) - b.y0;
}

// dialog.js 495-542.
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
        label(s, dl, r.x1 - U(s, 2) - aw, r.y0 + U(s, 1.5f), t.address,
              on ? rgb(C::kSelectText) : rgb(C::kGray), false, 11, 13, false);
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

void set_desktop(App::State& s, const std::string& c);

// dialog.js 544-653.
float render_display(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    const float x0 = b.x0 + U(s, 8), x1 = b.x1 - U(s, 8);
    float y = b.y0 + U(s, 8);
    static const char* const kTabs[] = {"Background", "Effects", "Settings"};
    // The tab row: items at the bottom; the chosen one 1 px lower, over the frame's edge.
    const float row_h = U(s, 20);
    float tx = x0 + U(s, 4);
    struct TabBox {
        Box b;
        bool on;
        int i;
    };
    std::vector<TabBox> tabs;
    for (int i = 0; i < 3; ++i) {
        const bool on = i == d.page;
        const float tw = text_w(s, kTabs[i]) + U(s, on ? 16 : 12) + U(s, 3);
        const float th = U(s, 13) + U(s, on ? 6 : 4) + U(s, 1);
        const float top = y + row_h - th + (on ? U(s, 1) : 0.0f);
        tabs.push_back({box(ImVec2(tx, top), ImVec2(tw, th)), on, i});
        tx += tw;
    }
    // The page frame first; the chosen tab is drawn over its top edge.
    const float fy = y + row_h;
    // div().p(10).min_h(300): the Background page is taller (monitor 170, gap 8, label 13, gap 8,
    // the list of six 16 px rows in a field).
    const float page_h =
        d.page == 0 ? std::max(U(s, 300), U(s, 20 + 170 + 8 + 13 + 8 + 16 * 6 + 6)) : U(s, 300);
    const Box pb{x0, fy, x1, snap(fy + 2 * one(s) + page_h + 2 * one(s))};
    bevel(s, dl, pb, Bevel::kWindow, rgb(C::kFace));
    const Box content{pb.x0 + 2 * one(s) + U(s, 10), fy + 2 * one(s) + U(s, 10),
                      pb.x1 - 2 * one(s) - U(s, 10), fy};
    const std::string desktop =
        shared_str(s, "win98.desktop").empty() ? std::string("#008080") : shared_str(s, "win98.desktop");
    float cy = content.y0;
    if (d.page == 0) {
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
            label(s, dl, r.x0 + U(s, 2), r.y0 + U(s, 1.5f), n, on ? rgb(C::kSelectText) : rgb(C::kText),
                  false, 11, 13, false);
            if (p.clicked)
                set_desktop(s, c);
            ry += U(s, 16);
        }
        cy = lb.y1;
    } else if (d.page == 1) {
        const float px0 = content.x0 + U(s, 6), px1 = content.x1 - U(s, 6);
        cy += U(s, 6);
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
        cy += lines(s, dl, px0, cy,
                    wrap(s, "Sounds also follow Herdr's own setting\n(Control Panel > Alerts).", px1 - px0),
                    rgb(C::kGray));
        cy += U(s, 6);
    } else {
        const float px0 = content.x0 + U(s, 6), px1 = content.x1 - U(s, 6);
        cy += U(s, 6);
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
        cy += lines(
            s, dl, px0, cy,
            wrap(s, std::string("Herdr's zoom is ") + zoom + "x; Herdr 98 draws whole\npixels at any zoom.",
                 px1 - px0),
            rgb(C::kGray));
        cy += U(s, 6);
    }
    (void)cy;
    // Tabs: top, left and right edges white, an inner right edge dark; the face.
    for (const auto& t : tabs) {
        const float o = one(s);
        Box tb = t.b;
        if (t.on)
            fill(dl, Box{tb.x0, tb.y0, tb.x1, tb.y1 + o}, rgb(C::kFace));
        dl->AddRectFilled(ImVec2(tb.x0, tb.y0), ImVec2(tb.x1, tb.y0 + o), rgb(C::kHi));
        dl->AddRectFilled(ImVec2(tb.x0, tb.y0), ImVec2(tb.x0 + o, tb.y1), rgb(C::kHi));
        dl->AddRectFilled(ImVec2(tb.x1 - o, tb.y0), ImVec2(tb.x1, tb.y1), rgb(C::kHi));
        const Box inner{tb.x0 + o, tb.y0 + o, tb.x1 - o, tb.y1};
        fill(dl, inner, rgb(C::kFace));
        dl->AddRectFilled(ImVec2(inner.x1 - o, inner.y0), ImVec2(inner.x1, inner.y1), rgb(C::kDark));
        label(s, dl, inner.x0 + U(s, t.on ? 8 : 6), inner.y0 + U(s, t.on ? 3 : 2), kTabs[t.i], rgb(C::kText));
        ImGui::PushID(t.i);
        if (area("##dtab", tb).clicked)
            d.page = t.i;
        ImGui::PopID();
    }
    y = pb.y1 + U(s, 10);
    const int hit = buttons(s, dl, x0, x1, y, {{"OK", true}, {"Cancel"}, {"&Apply", false, true}}, true);
    if (hit == 0 || hit == 1)
        close_dialog(s, w);
    return y + U(s, 23) + U(s, 8) - b.y0;
}

// dialog.js 655-709 and 927-958.
float render_datetime(App::State& s, Window& w, Dialog& /*d*/, ImDrawList* dl, Box b) {
    const k98::DateTime d = local_now();
    const float x0 = b.x0 + U(s, 10), x1 = b.x1 - U(s, 10);
    float y = b.y0 + U(s, 10);
    // Left: the month and year, then the calendar.
    const float fh = U(s, 13) + U(s, 4) + U(s, 4);
    {
        const Box m = box(ImVec2(x0, y), ImVec2(U(s, 80) + U(s, 6) + U(s, 4), fh));
        const Box mi = field(s, dl, m);
        label(s, dl, mi.x0, mi.y0, k98::month_name(d.month), rgb(C::kText), false, 11, 13, false);
        const Box yb = box(ImVec2(m.x1 + U(s, 6), y), ImVec2(U(s, 40) + U(s, 6) + U(s, 4), fh));
        const Box yi = field(s, dl, yb);
        label(s, dl, yi.x0, yi.y0, std::to_string(d.year), rgb(C::kText), false, 11, 13, false);
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
            label(s, dl, cx + (U(s, 22) - text_w(s, kDays[i])) * 0.5f, ci.y0, kDays[i], rgb(C::kWhite), false,
                  11, 13, false);
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
                    label(s, dl, cell.x0 + (cell.w() - text_w(s, t)) * 0.5f, cell.y0 + U(s, 1.5f), t,
                          today ? rgb(C::kSelectText) : rgb(C::kText), false, 11, 13, false);
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
        label(s, dl, ti.x0 + (ti.w() - text_w(s, t)) * 0.5f, ti.y0, t, rgb(C::kText), false, 11, 13, false);
    }
    const float right_h = U(s, 96) + U(s, 6) + fh;
    y += std::max(left_h, right_h) + U(s, 10);
    label(s, dl, x0, y, "Time zone: " + k98::time_zone_text(d), rgb(C::kText), false, 11, 13, false);
    y += U(s, 13) + U(s, 10);
    if (buttons(s, dl, x0, x1, y, {{"OK", true}, {"Cancel"}}, true) >= 0)
        close_dialog(s, w);
    return y + U(s, 23) + U(s, 10) - b.y0;
}

// dialog.js 711-755: an Explorer-ish window: icons in a 200 px field, a status bar.
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
        label(s, dl, in.x0 + U(s, 10), in.y0 + U(s, 10), "This folder is empty.", rgb(C::kGray), false, 11,
              13, false);
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
                label(s, dl, lx, ly, l, i == d.sel ? rgb(C::kSelectText) : rgb(C::kText), false, 11, 13,
                      false);
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
    label(s, dl, li.x0, li.y0, status, rgb(C::kText), false, 11, 13, false);
    const Box ri = well(s, dl, right);
    icon(s, dl, "computer", 16, ImVec2(ri.x0, snap(ri.y0 + (ri.h() - U(s, 16)) * 0.5f)));
    label(s, dl, ri.x0 + U(s, 20), ri.y0, "My Computer", rgb(C::kText), false, 11, 13, false);
    return sy + sh - b.y0;
}

// dialog.js 757-767.
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

// dialog.js 769-780.
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

// dialog.js 871-876.
float render_bin(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    float h = explorer(s, w, d, dl, b, {}, "0 object(s)");
    h += U(s, 8);
    if (buttons(s, dl, b.x0, b.x1, b.y0 + h, {{"&Empty Recycle Bin", false, false, 120}}, true) == 0)
        sfx(s, "recycle");
    return h + U(s, 23);
}

// dialog.js 782-869.
float render_welcome(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {
    struct Item {
        const char* icon;
        const char* title;
        const char* text;
        const char* go;
        std::function<void()> act;
    };
    const Item items[] = {
        {"programs", "Begin an Agent",
         "Herdr runs your coding agents in windows,\nlike it's 1998 and the future at once.\n\nStart one "
         "from Start > Programs > Agents,\nor open the Run box and type its name.",
         "&Run...",
         [&s] {
             Dialog r;
             r.kind = "run";
             r.value = "claude";
             open_dialog(s, std::move(r));
         }},
        {"help", "Discover Herdr 98",
         "The taskbar is your tabs. The folders on\nthe desktop are your spaces. Each window\nis a pane: "
         "double-click its title bar to\nmaximize it, or use _ to minimize the tab.\n\nCtrl+Esc opens the "
         "Start menu.",
         "&Help", [&s] { run_command(s, "app.help"); }},
        {"mines", "Play Mines",
         "Your agent is thinking.\nYou've earned a break.\n\nSolitaire is on the desktop too.", "&Play",
         [&s] { set_app(s, "mines", "open"); }},
        {"taskman", "Maintain Your Computer",
         "See every program Herdr runs, and end\nthe ones you don't need anymore.\n\nCtrl+Shift+Esc opens it "
         "anytime.",
         "&Open",
         [&s] {
             Dialog t;
             t.kind = "taskman";
             open_dialog(s, std::move(t));
         }},
        {"logoff", "Leave Herdr 98",
         "Miss the future? Log Off takes you\nback to Herdr's normal layout.\nYour terminals keep running.",
         "&Log Off",
         [&s] {
             Dialog l;
             l.kind = "logoff";
             open_dialog(s, std::move(l));
         }},
    };
    const int n = static_cast<int>(std::size(items));
    if (d.sel < 0 || d.sel >= n)
        d.sel = 0;
    const Item& it = items[d.sel];
    std::function<void()> run;
    // The header: 56 px, white to light blue, "Welcome to Herdr 98" at 22 px.
    const Box hb{b.x0, b.y0, b.x1, snap(b.y0 + U(s, 56))};
    gradient(dl, hb, rgb(0xffffff), rgb(0x6aa6e8));
    {
        float x = hb.x0 + U(s, 16);
        const float base = hb.y0 + (hb.h() - U(s, 32)) * 0.5f;
        icon(s, dl, "start", 32, ImVec2(x, snap(base)));
        x += U(s, 32) + U(s, 6);
        const float ty = base + U(s, 32) - U(s, 26);
        x += label(s, dl, x, ty, "Welcome to", rgb(C::kNavy), false, 22, 26, false) + U(s, 6);
        x += label(s, dl, x, ty, "Herdr", rgb(C::kText), true, 22, 26, false) + U(s, 6);
        label(s, dl, x, ty, "98", rgb(0xd07000), false, 22, 26, false);
    }
    // The contents (190 px, navy) and the page (white).
    const Box body{b.x0, hb.y1, b.x1, snap(hb.y1 + U(s, 230))};
    const Box left{body.x0, body.y0, snap(body.x0 + U(s, 190)), body.y1};
    fill(dl, left, rgb(0x000080));
    float ly = left.y0 + U(s, 8);
    label(s, dl, left.x0 + U(s, 8), ly, "CONTENTS", rgb(0xffff80), true, 11, 13, false);
    ly += U(s, 13) + U(s, 4) + U(s, 2);
    for (int i = 0; i < n; ++i) {
        const Box r{left.x0 + U(s, 8), ly, left.x1 - U(s, 8), ly + U(s, 32) + U(s, 6)};
        ImGui::PushID(i);
        const Pressed p = area("##wl", r);
        ImGui::PopID();
        if (p.hovered && d.sel != i)
            d.sel = i;
        const ImU32 fg = i == d.sel ? rgb(0xffff80) : rgb(C::kWhite);
        icon(s, dl, items[i].icon, 32, ImVec2(r.x0 + U(s, 3), r.y0 + U(s, 3)));
        const float tx = r.x0 + U(s, 3) + U(s, 32) + U(s, 8);
        const float ty = r.y0 + (r.h() - U(s, 13)) * 0.5f;
        const float tw = label(s, dl, tx, ty, items[i].title, fg, false, 11, 13, false);
        if (i == d.sel) {
            const float base = text_top(s, ty) + face(s, false)->GetFontBaked(U(s, 11))->Ascent;
            fill(dl, Box{snap(tx), snap(base + 1), snap(tx + tw), snap(base + 1) + one(s)}, fg);
        }
        if (p.clicked)
            run = items[i].act;
        ly = r.y1 + U(s, 2);
    }
    const Box right{left.x1, body.y0, body.x1, body.y1};
    fill(dl, right, rgb(C::kWhite));
    {
        const float px0 = right.x0 + U(s, 14), px1 = right.x1 - U(s, 14);
        float y = right.y0 + U(s, 14);
        label(s, dl, px0, y, it.title, rgb(C::kNavy), true, 16, 20, false);
        y += U(s, 20) + U(s, 10);
        lines(s, dl, px0, y, wrap(s, it.text, px1 - px0), rgb(C::kText));
        const float by = right.y1 - U(s, 14) - U(s, 23);
        ImGui::PushID("go");
        if (buttons(s, dl, px0, px1, by, {{it.go, true}}, true) == 0)
            run = it.act;
        ImGui::PopID();
    }
    etch(s, dl, b.x0, body.y1, b.w(), false);
    const float fy = body.y1 + 2 * one(s);
    const float row_h = U(s, 23);
    const bool show = shared_str(s, "win98.welcome") != "off";
    if (check(s, dl, "##wl-show", b.x0 + U(s, 8), fy + U(s, 8) + (row_h - U(s, 13)) * 0.5f,
              "&Show this screen each time Herdr 98 starts", show))
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

// dialog.js 81-150: the full-window boot, shut down and restart screens.
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
        d.mode == "shutdown" || d.stage == 0 ? "Herdr is shutting down..." : "Starting Herdr 98...";
    fill(dl, b, rgb(0x000000));
    picture(s, dl, "img/splash.png", b);
    const float tw = text_w(s, msg, true, 14);
    label(s, dl, b.x0 + (b.w() - tw) * 0.5f, b.y1 - U(s, 40) - U(s, 14), msg, rgb(C::kWhite), true, 14, 14,
          false);
}

// dialog.js 878-916: a dialog's frame (kit.js win: title bar, help unless Explorer-ish, close) and
// its body. Returns the window's size.
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
    // kit.js win(): title (18), gap 1, the body.
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
    if (d.kind == "prompt")
        h = render_prompt(s, w, d, dl, body);
    else if (d.kind == "confirm" || d.kind == "message")
        h = render_confirm(s, w, d, dl, body);
    else if (d.kind == "run")
        h = render_run(s, w, d, dl, body);
    else if (d.kind == "shutdown")
        h = render_shutdown(s, w, d, dl, body);
    else if (d.kind == "logoff")
        h = render_logoff(s, w, d, dl, body);
    else if (d.kind == "about")
        h = render_about(s, w, d, dl, body);
    else if (d.kind == "taskman")
        h = render_taskman(s, w, d, dl, body);
    else if (d.kind == "display")
        h = render_display(s, w, d, dl, body);
    else if (d.kind == "datetime")
        h = render_datetime(s, w, d, dl, body);
    else if (d.kind == "mycomputer")
        h = render_mycomputer(s, w, d, dl, body);
    else if (d.kind == "network")
        h = render_network(s, w, d, dl, body);
    else if (d.kind == "bin")
        h = render_bin(s, w, d, dl, body);
    else if (d.kind == "welcome")
        h = render_welcome(s, w, d, dl, body);
    else {
        label(s, dl, body.x0 + U(s, 10), body.y0 + U(s, 10), "Unknown dialog " + d.kind, rgb(C::kText), false,
              11, 13, false);
        h = U(s, 33);
    }
    const float height = snap(body.y0 + h - at.y + 3 * o);
    split.SetCurrentChannel(dl, 0);
    bevel(s, dl, box(at, ImVec2(width, height)), Bevel::kWindow, rgb(C::kFace));
    split.Merge(dl);
    return ImVec2(width, height);
}

}  // namespace w98
