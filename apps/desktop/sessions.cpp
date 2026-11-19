// Sessions to workspace: placing new sessions, syncing the host's session list into the
// workspace, persisting the layout document, mods, attention and alert sounds.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// ------------------------------------------------------------------ sessions -> workspace

Json make_doc(const App::State& s) {
    Json doc = s.ws.to_json();
    doc["hidden"] = Json(std::vector<std::string>(s.hidden.begin(), s.hidden.end()));
    return doc;
}

}  // namespace

void adopt_doc(App::State& s, const Json& doc) {
    s.ws = ui::Workspace::from_json(doc);
    s.hidden.clear();
    for (const auto& h : doc.value("hidden", Json::array()))
        if (h.is_string())
            s.hidden.insert(h.get<std::string>());
    s.known.clear();
    for (const auto& id : s.ws.all_sessions())
        s.known.insert(id);
    s.known.insert(s.hidden.begin(), s.hidden.end());
    s.last_doc = make_doc(s).dump();
    s.last_sent_doc = s.last_doc;
}

// `args` go after the profile's own (session.create "args"; Herdr 98's Run line).
void create_session(App::State& s, const std::string& profile, const std::string& cwd,
                    const std::string& name, Placement placement, const std::vector<std::string>& args) {
    Json params{{"profile_id", profile}, {"cwd", cwd}};
    if (!name.empty())
        params["name"] = name;
    if (!args.empty())
        params["args"] = args;
    s.launch_busy = true;
    s.host_for(placement.machine)
        ->call(
            "session.create", params,
            [st = &s, placement](Json result, std::string error) mutable {
                std::lock_guard lock(st->placements_mutex);
                if (!error.empty()) {
                    st->async_errors.push_back(error);
                    return;
                }
                placement.session = safe::string_or(result, "session_id");
                st->placements.push_back(placement);
            },
            std::chrono::seconds(15));
}

namespace {
void place(App::State& s, const Placement& p) {
    s.known.insert(p.session);
    s.launched_here[p.session] = Clock::now();
    s.awaiting_list[p.session] = Clock::now();
    if (!p.replace.empty() && s.ws.replace(p.replace, p.session)) {
        s.focus(p.session);
        return;
    }
    switch (p.target) {
        case LaunchTarget::kSplitRight:
        case LaunchTarget::kSplitDown:
            if (!p.split_of.empty() &&
                s.ws.split(p.split_of, p.session,
                           p.target == LaunchTarget::kSplitRight ? Dir::kRight : Dir::kDown)) {
                s.focus(p.session);
                return;
            }
            [[fallthrough]];
        case LaunchTarget::kTab: {
            int sp = p.space_id.empty() ? p.space : -1;
            for (int i = 0; i < static_cast<int>(s.ws.spaces.size()) && !p.space_id.empty(); ++i)
                if (s.ws.spaces[static_cast<std::size_t>(i)].id == p.space_id)
                    sp = i;
            sp = sp >= 0 && sp < static_cast<int>(s.ws.spaces.size()) &&
                             s.ws.spaces[static_cast<std::size_t>(sp)].machine == p.machine
                     ? sp
                     : -1;
            if (sp < 0) {
                for (int i = 0; i < static_cast<int>(s.ws.spaces.size()) && sp < 0; ++i)
                    if (s.ws.spaces[static_cast<std::size_t>(i)].machine == p.machine)
                        sp = i;
            }
            if (sp >= 0) {
                s.ws.add_tab(p.session, sp, p.tab_name);
                s.focus(p.session);
                return;
            }
            [[fallthrough]];
        }
        case LaunchTarget::kSpace:
            s.ws.add_space(p.session, p.space_name);
            s.ws.spaces.back().machine = p.machine;
            s.focus(p.session);
            return;
    }
}

}  // namespace

// Places sessions that appeared without a placement (agentctl, another window, first start):
// into the space of the same folder, else a new space; never steals focus once loaded.
void sync_sessions(App::State& s) {
    {
        std::lock_guard lock(s.placements_mutex);
        for (const auto& p : s.placements)
            if (!p.session.empty())
                place(s, p);
        if (!s.placements.empty())
            s.launch_busy = false;
        s.placements.clear();
        for (auto& e : s.async_errors) {
            s.launch_error = e;
            s.launch_busy = false;
            // The launcher may be closed (new terminal, split, space): say it where it is seen.
            s.toasts.push_back({"Could not start", e, "", g_pal->needs_you, Clock::now() + std::chrono::seconds(10)});
        }
        s.async_errors.clear();
    }
    std::set<std::string> present;
    for (const auto& sess : s.sessions) {
        s.awaiting_list.erase(sess->id());
        // herdr window.rs 2262-2283, 2695-2719 (reconcile / checked_closed): a pane whose terminal
        // ended (exited, killed) or is gone is removed; its tab, then its space, close when
        // they empty (model.rs 403-422). Lost: the agent-host that ran it is gone.
        const auto& v = s.views_by_id[sess->id()];
        if (v.process_state == "Exited" || v.lost)
            continue;
        present.insert(sess->id());
    }
    // A machine that is not connected right now keeps its panes (its sessions still run there).
    for (const auto& sp : s.ws.spaces)
        if (!s.machine_connected(sp.machine))
            for (const auto& t : sp.tabs)
                for (const auto& id : s.ws.sessions_in(t))
                    present.insert(id);
    for (auto it = s.awaiting_list.begin(); it != s.awaiting_list.end();) {
        if (Clock::now() - it->second > std::chrono::seconds(15)) {
            it = s.awaiting_list.erase(it);
        } else {
            present.insert(it->first);
            ++it;
        }
    }
    const bool initial = s.ws.spaces.empty();
    for (const auto& sess : s.sessions) {
        const auto& id = sess->id();
        if (s.known.contains(id))
            continue;
        s.known.insert(id);
        const auto& v = s.views_by_id[id];
        if (v.process_state == "Exited" || v.lost)
            continue;  // ended: never opened (herdr places live terminals only, window.rs 2277-2283)
        if (!v.parent.empty() && s.views_by_id.count(v.parent))
            continue;  // a sub-agent: listed under its parent, peeked or opened on demand
        const int before_space = s.ws.active_space;
        const int before_tab = s.ws.spaces.empty() ? 0 : s.ws.active()->active_tab;
        const std::string m = s.machine_of.count(id) ? s.machine_of[id] : std::string();
        const int sp = s.space_for_cwd(v.cwd, m);
        if (sp >= 0) {
            s.ws.add_tab(id, sp);
        } else {
            s.ws.add_space(id);
            s.ws.spaces.back().machine = m;
        }
        if (!initial && before_space < static_cast<int>(s.ws.spaces.size())) {
            s.ws.active_space = before_space;  // keep what the user looks at
            if (auto* a = s.ws.active())
                a->active_tab = std::min(before_tab, static_cast<int>(a->tabs.size()) - 1);
        }
    }
    s.ws.prune(present);
    for (auto it = s.hidden.begin(); it != s.hidden.end();)
        it = present.contains(*it) ? std::next(it) : s.hidden.erase(it);
    for (auto it = s.views.begin(); it != s.views.end();)
        it = present.contains(it->first) ? std::next(it) : s.views.erase(it);
}

void persist(App::State& s) {
    const std::string doc = make_doc(s).dump();
    if (doc != s.last_doc) {
        s.last_doc = doc;
        s.doc_dirty = true;
        s.doc_dirty_at = Clock::now();
    }
    if (s.doc_dirty && Clock::now() - s.doc_dirty_at > std::chrono::milliseconds(400) &&
        s.host->connected()) {
        s.doc_dirty = false;
        if (doc != s.last_sent_doc) {
            s.last_sent_doc = doc;
            s.host->set_ui_state(Json::parse(doc));
        }
    }
}

// Agents that need the user / finished unseen: sound, toast, taskbar flash.
// Events for mods, then whatever they asked for.
void run_mods(App::State& s) {
    std::vector<ui::ModSession> list;
    const auto focused = s.ws.focused_session();
    for (const auto& [id, v] : s.views_by_id) {
        ui::ModSession m;
        m.id = id;
        m.title = s.pane_title(id);
        m.program = v.program;
        m.cwd = v.cwd;
        m.agent_state = v.agent_state;
        m.process_state = v.lost ? std::string("Lost") : v.process_state;
        m.machine = s.machine_of.count(id) ? s.machine_of[id] : std::string();
        m.parent = v.parent;
        m.created_at = v.created_at;
        m.labels = v.labels;
        m.focused = id == focused;
        list.push_back(std::move(m));
    }
    s.mods->set_sessions(list);
    for (const auto& m : list) {
        auto it = s.last_agent_state.find(m.id);
        if (it == s.last_agent_state.end()) {
            s.mods->emit("session_added", {{"session", m.id}, {"title", m.title}});
            s.last_agent_state[m.id] = m.agent_state;
            if (m.agent_state != "Unknown")
                s.mods->emit("agent_state", {{"session", m.id},
                                             {"state", m.agent_state},
                                             {"previous", "Unknown"},
                                             {"title", m.title}});
        } else if (it->second != m.agent_state) {
            s.mods->emit(
                "agent_state",
                {{"session", m.id}, {"state", m.agent_state}, {"previous", it->second}, {"title", m.title}});
            it->second = m.agent_state;
        }
        if (m.process_state == "Exited" && s.prev_process_state[m.id] != "Exited")
            s.mods->emit("session_exited", {{"session", m.id}, {"title", m.title}});
    }
    if (Clock::now() >= s.next_tick) {
        s.next_tick = Clock::now() + std::chrono::seconds(1);
        s.mods->emit("tick", Json::object());
    }
    auto out = s.mods->take_output();
    for (const auto& n : out.notes)
        s.toast(n.title, n.body, "", g_pal->info);
    for (const auto& snd : out.sends) {
        if (const auto it = s.by_id.find(snd.session); it != s.by_id.end()) {
            it->second->send(host::PasteInput{snd.text});
            if (snd.enter)
                it->second->send(host::RawInput{"\r"});
        }
    }
    for (const auto& argv : out.execs) {
        // Started directly, never through a shell; no window; not waited for.
        wchar_t exe[MAX_PATH]{};
        if (::SearchPathW(nullptr, argv[0].c_str(), L".exe", MAX_PATH, exe, nullptr) == 0)
            continue;
        std::wstring cmd =
            win::build_command_line(exe, std::vector<std::wstring>(argv.begin() + 1, argv.end()));
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (::CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                             &si, &pi)) {
            ::CloseHandle(pi.hThread);
            ::CloseHandle(pi.hProcess);
        }
    }
    for (const auto& c : out.commands)
        s.pending_mod_commands.push_back(c);
}

void update_attention(App::State& s) {
    const ui::Tab* tab = s.ws.active_tab();
    std::set<std::string> visible;
    if (tab != nullptr) {
        if (!tab->zoomed.empty())
            visible.insert(tab->zoomed);
        else
            for (const auto& id : s.ws.sessions_in(*tab))
                visible.insert(id);
    }
    for (const auto& [id, v] : s.views_by_id) {
        const bool looking = s.window_focused && visible.contains(id);
        if (s.attention.update(id, v.agent_state, looking) && !s.config.automated) {
            // herdr: the sound plays either way; the note shows when you were not looking.
            const bool blocked = v.agent_state == "Blocked";
            const std::string title = s.pane_title(id);
            if (s.settings.sounds)
                play_alert(s, blocked);
            if (s.ws.unsettle_session(id)) {  // herdr: an alert brings a tab put away back
                s.doc_dirty = true;
                s.doc_dirty_at = Clock::now();
            }
            if (!looking) {
                std::string where;
                const auto loc = s.ws.find(id);
                if (loc.space >= 0) {
                    const auto& t = s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
                    where = s.ws.space_title(loc.space, s.session_cwds()) + " / " +
                            (t.name.empty() ? std::to_string(loc.tab + 1) : t.name);
                }
                s.toast(blocked ? title + " needs you" : title + " finished",
                        where, id, blocked ? g_pal->needs_you : g_pal->focus);  // herdr: "<space> / <tab>"
            }
        }
        // herdr window.rs 142-157, 2463-2481: a terminal that exited by itself with an error
        // within 2 s of starting here (any time for 127) -> "Terminal failed to start", the exit
        // code and its last 5 lines. One the person closed (killed) says nothing.
        const auto prev = s.prev_process_state[id];
        if (prev != v.process_state) {
            s.prev_process_state[id] = v.process_state;
            const auto l = s.launched_here.find(id);
            const bool closed = s.hidden.contains(id);
            if (v.process_state == "Exited" && v.exit_code && *v.exit_code != 0 && !closed &&
                l != s.launched_here.end() &&
                (Clock::now() - l->second <= std::chrono::seconds(2) || *v.exit_code == 127)) {
                std::string body = "Exit code " + std::to_string(*v.exit_code);
                if (const auto it = s.by_id.find(id); it != s.by_id.end()) {
                    const auto snap = it->second->snapshot();
                    std::vector<std::string> lines;
                    for (int r = 0; r < snap->rows; ++r) {
                        auto t = snap->row_text(r);
                        if (t.find_first_not_of(' ') != std::string::npos)
                            lines.push_back(t);
                    }
                    for (std::size_t i = lines.size() > 5 ? lines.size() - 5 : 0; i < lines.size(); ++i)
                        body += "\n" + lines[i];
                }
                s.toast("Terminal failed to start", body, id, g_pal->needs_you, 10);
            }
        }
    }
    // Looking at a pane clears its "done".
    if (s.window_focused)
        for (const auto& id : visible)
            s.attention.seen(id);
}

// ------------------------------------------------------------------ sounds (herdr sound.rs)
// Bundled done.mp3 / request.mp3 (or the person's own MP3), played through MCI on a thread with a
// unique alias so sounds can overlap.
void play_alert(App::State& s, bool blocked) {
    std::filesystem::path file;
    const std::string custom = blocked ? s.settings.sound_blocked : s.settings.sound_done;
    std::error_code ec;
    if (!custom.empty() && std::filesystem::is_regular_file(win::to_wide(custom), ec))
        file = win::to_wide(custom);
    if (file.empty()) {
        wchar_t buf[MAX_PATH]{};
        const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
        file = std::filesystem::path(std::wstring(buf, n)).parent_path() / L"sounds" /
               (blocked ? L"request.mp3" : L"done.mp3");
    }
    if (!std::filesystem::is_regular_file(file, ec)) {
        ::PlaySoundW(blocked ? L"SystemExclamation" : L"SystemAsterisk", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
        return;
    }
    static std::atomic<int> counter{0};
    std::thread([file, id = ++counter] {
        const std::wstring alias = L"agentdesk" + std::to_wstring(id);
        const std::wstring open = L"open \"" + file.native() + L"\" type mpegvideo alias " + alias;
        if (::mciSendStringW(open.c_str(), nullptr, 0, nullptr) == 0) {
            ::mciSendStringW((L"play " + alias + L" wait").c_str(), nullptr, 0, nullptr);
            ::mciSendStringW((L"close " + alias).c_str(), nullptr, 0, nullptr);
        }
    }).detach();
}
}  // namespace detail

}  // namespace agentdesk::desktop
