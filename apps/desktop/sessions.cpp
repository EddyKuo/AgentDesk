// Sessions to workspace: placing new sessions, syncing the host's session list into the
// workspace, persisting the layout document, mods, attention and alert sounds.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
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

void create_session(App::State& s, const std::string& profile, const std::string& cwd,
                    const std::string& name, Placement placement) {
    Json params{{"profile_id", profile}, {"cwd", cwd}};
    if (!name.empty())
        params["name"] = name;
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
// The space a new tab goes to: the placement's space (by id when it has one) if it is on the
// placement's machine, else the first space on that machine; -1 when there is none.
int tab_space_for(const App::State& s, const Placement& p) {
    const int count = static_cast<int>(s.ws.spaces.size());
    int sp = p.space_id.empty() ? p.space : -1;
    for (int i = 0; i < count && !p.space_id.empty(); ++i)
        if (s.ws.spaces[static_cast<std::size_t>(i)].id == p.space_id)
            sp = i;
    if (sp >= 0 && sp < count && s.ws.spaces[static_cast<std::size_t>(sp)].machine == p.machine)
        return sp;
    for (int i = 0; i < count; ++i)
        if (s.ws.spaces[static_cast<std::size_t>(i)].machine == p.machine)
            return i;
    return -1;
}

// Puts a session this window created where it was asked for. A split falls back to a tab, a
// tab to a new space.
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
        case LaunchTarget::kTab:
            if (const int sp = tab_space_for(s, p); sp >= 0) {
                s.ws.add_tab(p.session, sp);
                s.focus(p.session);
                return;
            }
            [[fallthrough]];
        case LaunchTarget::kSpace:
            s.ws.add_space(p.session, p.space_name);
            s.ws.spaces.back().machine = p.machine;
            s.focus(p.session);
            return;
    }
}

// The answers to session.create that arrived since the last frame: places the new sessions and
// shows the errors.
void take_placements(App::State& s) {
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
        s.toasts.push_back(
            {"Could not start", e, "", g_pal->needs_you, Clock::now() + std::chrono::seconds(10)});
    }
    s.async_errors.clear();
}

bool ended(const ui::SessionView& v) {
    return v.process_state == "Exited" || v.lost;
}

// The sessions whose panes stay. A pane whose terminal ended (exited, killed, or lost with the
// agent-host that ran it) goes; its tab, then its space, close when they empty. A machine that
// is not connected right now keeps its panes (its sessions still run there), and so does a
// session this window created that the host's list does not show yet (for up to 15 s).
std::set<std::string> live_sessions(App::State& s) {
    std::set<std::string> present;
    for (const auto& sess : s.sessions) {
        s.awaiting_list.erase(sess->id());
        if (!ended(s.views_by_id[sess->id()]))
            present.insert(sess->id());
    }
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
    return present;
}

// Sessions seen for the first time that this window did not place: into the space of the same
// folder, else a new space. Only live terminals are placed; a sub-agent is listed under its
// parent instead. Once the workspace has spaces, the shown space and tab stay.
void place_new_sessions(App::State& s) {
    const bool initial = s.ws.spaces.empty();
    for (const auto& sess : s.sessions) {
        const auto& id = sess->id();
        if (s.known.contains(id))
            continue;
        s.known.insert(id);
        const auto& v = s.views_by_id[id];
        if (ended(v))
            continue;
        if (!v.parent.empty() && s.views_by_id.count(v.parent))
            continue;  // a sub-agent: peeked or opened on demand
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
}

}  // namespace

// Places new sessions (this window's creates first, then ones that appeared from elsewhere:
// agentctl, another window, first start) and removes panes whose terminals ended.
void sync_sessions(App::State& s) {
    take_placements(s);
    const std::set<std::string> present = live_sessions(s);
    place_new_sessions(s);
    s.ws.prune(present);
    for (auto it = s.hidden.begin(); it != s.hidden.end();)
        it = present.contains(*it) ? std::next(it) : s.hidden.erase(it);
    for (auto it = s.views.begin(); it != s.views.end();)
        it = present.contains(it->first) ? std::next(it) : s.views.erase(it);
}

// Sends the UI document to the host 400 ms after its last change (while connected).
void persist(App::State& s) {
    const std::string doc = make_doc(s).dump();
    if (doc != s.last_doc) {
        s.last_doc = doc;
        s.doc_changed();
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

namespace {
std::vector<ui::ModSession> mod_sessions(App::State& s) {
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
    return list;
}

// session_added, agent_state and session_exited for what changed since the last call.
void emit_session_events(App::State& s, const std::vector<ui::ModSession>& list) {
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
}

// A program a mod asked to start: directly (never through a shell), without a window, not
// waited for. argv[0] is looked up on PATH.
void start_detached(const std::vector<std::wstring>& argv) {
    os::start_detached(argv);
}

}  // namespace

// Events for mods (sessions, a tick every second), then whatever they asked for: notes, text
// sent to a terminal, programs to start and commands (run on the next frame).
void run_mods(App::State& s) {
    const auto list = mod_sessions(s);
    s.mods->set_sessions(list);
    emit_session_events(s, list);
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
    for (const auto& argv : out.execs)
        start_detached(argv);
    for (const auto& c : out.commands)
        s.pending_mod_commands.push_back(c);
}

namespace {
// An agent needs the person or finished. The sound plays either way (when on); a tab put away
// comes back; the toast ("<space> / <tab>") only when the person was not looking at it.
void alert(App::State& s, const std::string& id, const ui::SessionView& v, bool looking) {
    const bool blocked = v.agent_state == "Blocked";
    const std::string title = s.pane_title(id);
    if (s.settings.sounds)
        play_alert(s, blocked);
    if (s.ws.unsettle_session(id))
        s.doc_changed();
    if (looking)
        return;
    std::string where;
    const auto loc = s.ws.find(id);
    if (loc.space >= 0) {
        const auto& t =
            s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
        where = s.ws.space_title(loc.space, s.session_cwds()) + " / " +
                (t.name.empty() ? std::to_string(loc.tab + 1) : t.name);
    }
    s.toast(blocked ? title + " needs you" : title + " finished", where, id,
            blocked ? g_pal->needs_you : g_pal->focus);
}

// A terminal started here that exited by itself with an error within 2 s (any time for exit code
// 127, command not found): "Terminal failed to start" with the exit code and its last 5
// non-blank lines. One the person closed says nothing.
void report_failed_start(App::State& s, const std::string& id, const ui::SessionView& v) {
    const auto l = s.launched_here.find(id);
    if (v.process_state != "Exited" || !v.exit_code || *v.exit_code == 0 || s.hidden.contains(id) ||
        l == s.launched_here.end() ||
        (Clock::now() - l->second > std::chrono::seconds(2) && *v.exit_code != 127))
        return;
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

}  // namespace

// Feeds every agent's state to the attention tracker (alerts for new needs-you / done), reports
// terminals that failed to start, and marks what the person is looking at as seen.
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
        if (s.attention.update(id, v.agent_state, looking) && !s.config.automated)
            alert(s, id, v, looking);
        if (s.prev_process_state[id] != v.process_state) {
            s.prev_process_state[id] = v.process_state;
            report_failed_start(s, id, v);
        }
    }
    // Looking at a pane clears its "done".
    if (s.window_focused)
        for (const auto& id : visible)
            s.attention.seen(id);
}

// ------------------------------------------------------------------ sounds
// The bundled done.mp3 / request.mp3 (or the person's own MP3), played through MCI on a thread
// with a unique alias so sounds can overlap. Without a file, the Windows sound.
void play_alert(App::State& s, bool blocked) {
    std::filesystem::path file;
    const std::string custom = blocked ? s.settings.sound_blocked : s.settings.sound_done;
    std::error_code ec;
    if (!custom.empty() && std::filesystem::is_regular_file(win::to_wide(custom), ec))
        file = win::to_wide(custom);
    if (file.empty())
        file = exe_dir() / L"sounds" / (blocked ? L"request.mp3" : L"done.mp3");
    os::play_alert(file, blocked);
}
}  // namespace detail

}  // namespace agentdesk::desktop
