// Add a machine: the ssh host list and the dialog that adds one.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// ------------------------------------------------------------------ add a machine (herdr machine-add)
// Port of herdr defaults/mods/machine-add/main.js parseConfig/includeFiles: named Host entries (no
// patterns), "Key value" or "Key=value", a Match block belongs to no host, Include followed 16
// deep (~/ is home, relative is under ~/.ssh, * ? [ match files).
std::vector<std::pair<std::string, std::string>> ssh_config_hosts() {
    struct Host {
        std::string alias;
        std::vector<std::string> aliases;
        std::string hostname, user;
    };
    std::vector<Host> hosts;
    wchar_t home_buf[MAX_PATH]{};
    if (::GetEnvironmentVariableW(L"USERPROFILE", home_buf, MAX_PATH) == 0)
        return {};
    const std::filesystem::path home(home_buf);
    auto include_files = [&](const std::string& value) {
        std::vector<std::filesystem::path> files;
        std::vector<std::string> words;
        for (std::size_t i = 0; i < value.size();) {
            while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i])))
                ++i;
            if (i >= value.size())
                break;
            std::size_t j = i;
            if (value[i] == '"') {
                j = value.find('"', i + 1);
                words.push_back(value.substr(i + 1, (j == std::string::npos ? value.size() : j) - i - 1));
                i = j == std::string::npos ? value.size() : j + 1;
                continue;
            }
            while (j < value.size() && !std::isspace(static_cast<unsigned char>(value[j])))
                ++j;
            words.push_back(value.substr(i, j - i));
            i = j;
        }
        for (const auto& w : words) {
            std::filesystem::path p;
            if (w.rfind("~/", 0) == 0 || w.rfind("~\\", 0) == 0)
                p = home / win::to_wide(w.substr(2));
            else if (std::filesystem::path(win::to_wide(w)).is_absolute() || w.rfind("/", 0) == 0)
                p = win::to_wide(w);
            else
                p = home / L".ssh" / win::to_wide(w);
            p.make_preferred();
            if (w.find_first_of("*?[") == std::string::npos) {
                files.push_back(p);
                continue;
            }
            // A pattern in the file name: the files it matches, in name order.
            WIN32_FIND_DATAW fd{};
            const HANDLE h = ::FindFirstFileW(p.c_str(), &fd);
            std::vector<std::filesystem::path> found;
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                        found.push_back(p.parent_path() / fd.cFileName);
                } while (::FindNextFileW(h, &fd));
                ::FindClose(h);
            }
            std::sort(found.begin(), found.end());
            files.insert(files.end(), found.begin(), found.end());
        }
        return files;
    };
    auto known = [&](const std::string& name) {
        return std::any_of(hosts.begin(), hosts.end(), [&](const Host& x) {
            return x.alias == name || std::find(x.aliases.begin(), x.aliases.end(), name) != x.aliases.end();
        });
    };
    std::function<void(const std::filesystem::path&, int)> parse = [&](const std::filesystem::path& file, int depth) {
        std::ifstream in(file, std::ios::binary);
        if (!in)
            return;
        int cur = -1;  // index into hosts; -1: lines belong to no pickable host
        for (std::string raw; std::getline(in, raw);) {
            const std::string line = trimmed(raw);
            // ^([^\s#=]+)\s*=?\s*(.*)$
            std::size_t k = 0;
            while (k < line.size() && !std::isspace(static_cast<unsigned char>(line[k])) && line[k] != '#' && line[k] != '=')
                ++k;
            if (k == 0)
                continue;
            std::string key = line.substr(0, k);
            for (auto& c : key)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::size_t v = k;
            while (v < line.size() && std::isspace(static_cast<unsigned char>(line[v])))
                ++v;
            if (v < line.size() && line[v] == '=')
                ++v;
            const std::string value = trimmed(line.substr(v));
            if (key == "host" || key == "match") {
                std::vector<std::string> names;
                if (key == "host") {
                    std::stringstream ss(value);
                    for (std::string h; ss >> h;)
                        if (h.find_first_of("*?!") == std::string::npos && !known(h) &&
                            std::find(names.begin(), names.end(), h) == names.end())
                            names.push_back(h);
                }
                cur = -1;
                if (!names.empty()) {
                    hosts.push_back({names[0], std::vector<std::string>(names.begin() + 1, names.end()), {}, {}});
                    cur = static_cast<int>(hosts.size()) - 1;
                }
            } else if (key == "include" && depth < 16) {
                for (const auto& f : include_files(value))
                    parse(f, depth + 1);
            } else if (cur >= 0 && key == "hostname") {
                hosts[static_cast<std::size_t>(cur)].hostname = value;
            } else if (cur >= 0 && key == "user") {
                hosts[static_cast<std::size_t>(cur)].user = value;
            }
        }
    };
    try {
        parse(home / L".ssh" / L"config", 0);
    } catch (const std::exception&) {
        // a name that is not UTF-8: keep what was read
    }
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& h : hosts) {
        std::string sub = h.user.empty() ? h.hostname : h.hostname.empty() ? h.user : h.user + "@" + h.hostname;
        if (sub.empty() && !h.aliases.empty()) {
            sub = "also ";
            for (std::size_t i = 0; i < h.aliases.size(); ++i)
                sub += (i ? ", " : "") + h.aliases[i];
        }
        out.emplace_back(h.alias, sub);
    }
    return out;
}

// herdr machine-add shellArg(): a target as a shell reads it.
std::string shell_arg(const std::string& target) {
    const bool plain = !target.empty() && std::all_of(target.begin(), target.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || std::string_view("_@%+=:,./-").find(c) != std::string_view::npos;
    });
    if (plain)
        return target;
    std::string out = "'";
    for (const char c : target)
        out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

}  // namespace

void draw_machine_add(App::State& s) {
    if (s.show_machine_add) {
        ImGui::OpenPopup("##machineadd");
        s.show_machine_add = false;
        s.ssh_hosts = ssh_config_hosts();
        s.machine_buf[0] = 0;
        std::erase_if(s.machine_add_state, [](const auto& kv) { return kv.second != "adding"; });
        s.machine_add_pending.clear();
    }
    center_next(px(560), 0.12f);
    push_overlay_style();
    const bool open = ImGui::BeginPopupModal("##machineadd", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    pop_overlay_style();
    if (!open)
        return;
    bool close = overlay_title(s, "Add a machine");
    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
    ImGui::TextWrapped("A server, a desktop at home: AgentDesk reaches it over ssh, and agents there keep running while "
                       "this PC sleeps. It needs AgentDesk there and key-based ssh login.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, px(6)));
    // A machine counts as added once it answers (herdr: `hc machine add` succeeded).
    auto machine_of_target = [&](const std::string& target) -> const MachineConfig* {
        for (const auto& m : s.settings.machines)
            if (m.target == target || m.name == target)
                return &m;
        return nullptr;
    };
    auto added = [&](const std::string& target) {
        const auto* m = machine_of_target(target);
        return m != nullptr && !s.machine_add_state.contains(target);
    };
    // herdr add(): connect at once; "Connecting" until it answers or says why not.
    auto add = [&](std::string target) {
        target = trimmed(target);
        if (target.empty() || s.machine_add_state[target] == "adding")
            return;
        s.machine_add_pending.erase(target);
        s.machine_add_state[target] = "adding";
        if (machine_of_target(target) == nullptr) {
            const auto at = target.find('@');
            std::string name = at == std::string::npos ? target : target.substr(at + 1);
            while (std::any_of(s.settings.machines.begin(), s.settings.machines.end(),
                               [&](const MachineConfig& m) { return m.name == name; }))
                name += "-2";
            s.settings.machines.push_back({name, target, ""});
            save_settings(s.settings);
            start_remotes(s);
        }
    };
    // Follow the connections being made: answered -> added; refused -> not added, and why.
    for (auto it = s.machine_add_state.begin(); it != s.machine_add_state.end();) {
        const std::string target = it->first;
        const auto* m = machine_of_target(target);
        if (it->second != "adding" || m == nullptr) {
            ++it;
            continue;
        }
        if (s.machine_connected(m->name)) {
            if (trimmed(s.machine_buf) == target)
                s.machine_buf[0] = 0;
            it = s.machine_add_state.erase(it);
            continue;
        }
        std::string why;
        {
            std::lock_guard lock(s.remotes_mutex);
            for (const auto& r : s.remotes)
                if (r.cfg.name == m->name)
                    why = r.host->last_error();
        }
        if (!why.empty()) {
            std::string low = why;
            for (auto& c : low)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            const bool answer = low.find("password") != std::string::npos || low.find("passphrase") != std::string::npos ||
                                low.find("host key") != std::string::npos ||
                                low.find("permission denied") != std::string::npos || low.find("login") != std::string::npos;
            if (answer) {
                s.machine_add_pending.insert(target);
                it->second = "Couldn't add " + target +
                             " here: it needs you to answer something (a password, a new host key, or installing "
                             "AgentDesk there). Run `ssh " + shell_arg(target) +
                             "` in a terminal, then add it again.";
            } else {
                it->second = why;
            }
            // Not added: it stays out of the machine list until it answers.
            const std::string name = m->name;
            std::erase_if(s.settings.machines, [&](const MachineConfig& x) { return x.name == name; });
            save_settings(s.settings);
            start_remotes(s);
        }
        ++it;
    }
    // One row (herdr row()): server icon, name, where it goes, then Added / Connecting / Add.
    auto row = [&](const std::string& target, const std::string& label, const std::string& sub) {
        const auto st = s.machine_add_state.find(target);
        const bool done = added(target);
        const bool adding = st != s.machine_add_state.end() && st->second == "adding";
        const bool idle = !done && st == s.machine_add_state.end();
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::PushID(target.c_str());
        const float rw = ImGui::GetContentRegionAvail().x;
        bool clicked = ImGui::InvisibleButton("##h", ImVec2(rw, px(40)));
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* d = ImGui::GetWindowDrawList();
        if (hov)
            d->AddRectFilled(a, ImVec2(a.x + rw, a.y + px(40)), g_pal->hover, px(8));
        const float cy = a.y + px(20);
        draw_icon(s, d, ui::icon::kServer, fpx(15), ImVec2(a.x + px(17), cy), g_pal->dim);
        d->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + px(35), cy - fpx(13) * 0.6f), g_pal->text, label.c_str());
        const float lw = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, label.c_str()).x;
        // machine-add/main.js 247-266: "Added" (ansi2), a 12 px working spinner and
        // "Connecting" (12 px muted), else the bordered "Add" button (px 10, h 26, rounded 7,
        // raised, 12 px medium) -- also after a failed add, when it is the way to try again.
        const char* right = done ? "\xE2\x9C\x93 Added" : adding ? "Connecting" : "Add";
        const bool button = !done && !adding;
        const float rtw = (button ? s.fonts.ui_medium : s.fonts.ui)->CalcTextSizeA(fpx(12), FLT_MAX, 0, right).x;
        const float rw_right = button ? rtw + px(20) : adding ? px(12) + px(6) + rtw : rtw;
        text_clipped(d, s.fonts.ui, fpx(12), ImVec2(a.x + px(45) + lw, cy - fpx(12) * 0.6f), g_pal->faint, sub,
                     std::max(0.0f, rw - px(45) - lw - rw_right - px(20)));
        const ImVec2 ra(a.x + rw - px(10) - rw_right, cy - px(13));
        if (button) {
            const bool bhov = hov && ImGui::IsMouseHoveringRect(ra, ImVec2(ra.x + rw_right, ra.y + px(26)));
            d->AddRectFilled(ra, ImVec2(ra.x + rw_right, ra.y + px(26)), bhov ? g_pal->hover : raised_color(), px(7));
            d->AddRect(ra, ImVec2(ra.x + rw_right, ra.y + px(26)), g_pal->border, px(7));
            d->AddText(s.fonts.ui_medium, fpx(12), ImVec2(ra.x + px(10), cy - fpx(12) * 0.6f), g_pal->text, right);
        } else if (adding) {
            status_mark(s, d, ra.x - px(1), cy, Attention::kWorking, true);
            d->AddText(s.fonts.ui, fpx(12), ImVec2(ra.x + px(12) + px(6), cy - fpx(12) * 0.6f), g_pal->dim, right);
        } else {
            d->AddText(s.fonts.ui, fpx(12), ImVec2(ra.x, cy - fpx(12) * 0.6f), g_pal->done, right);
        }
        // The row adds while idle; after a failed add only its Add button does (main.js 268, 252).
        if (clicked && (idle || (button && ImGui::IsMouseHoveringRect(ra, ImVec2(ra.x + rw_right, ra.y + px(26))))))
            add(target);
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + px(40)));
        if (st != s.machine_add_state.end() && st->second != "adding") {
            // main.js 233-243, 287: px 35, pb 6; the message (12 px blocked) and, when it needs
            // an answer, Copy.
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(35));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + rw - px(70));
            ImGui::PushFont(s.fonts.ui, 12);
            ImGui::TextColored(v4(g_pal->needs_you), "%s", st->second.c_str());
            ImGui::PopFont();
            ImGui::PopTextWrapPos();
            if (s.machine_add_pending.contains(target)) {
                const std::string command = "ssh " + shell_arg(target);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(35));
                if (ImGui::SmallButton(s.machine_add_copied == command ? "Copied" : "Copy")) {
                    ImGui::SetClipboardText(command.c_str());
                    s.machine_add_copied = command;
                }
            }
            ImGui::Dummy(ImVec2(0, px(6)));
        }
        ImGui::PopID();
    };
    // machine-add/main.js 320, 335: the section titles, 11.5 px medium dim, as written (pt 16 /
    // pt 14, pb 6).
    auto section = [&](const char* text, float pt) {
        ImGui::Dummy(ImVec2(0, std::max(0.0f, pt - ImGui::GetStyle().ItemSpacing.y)));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, text);
        ImGui::GetWindowDrawList()->AddText(s.fonts.ui_medium, fpx(11.5f), p, g_pal->faint, text);
        ImGui::Dummy(ImVec2(t.x, t.y + px(6) - ImGui::GetStyle().ItemSpacing.y));
    };
    section(s.ssh_hosts.empty() ? "Your ssh config lists no hosts" : "From your ssh config", px(16) - px(6));
    // herdr ma-hosts: at most 280 px, then it scrolls.
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, px(280)));
    ImGui::BeginChild("##ma-hosts", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoBackground);
    for (const auto& [alias, where] : s.ssh_hosts)
        if (!added(alias))
            row(alias, alias, where);
    ImGui::EndChild();
    for (const auto& m : s.settings.machines)
        if (added(m.target))
            row(m.target, m.name, m.target);
    section("Or type how ssh reaches it", px(14));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - px(90));
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();  // herdr machine-add/main.js 73: the field has focus on open
    const bool enter = ImGui::InputTextWithHint("##target", "user@host", s.machine_buf, sizeof s.machine_buf,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine(0, px(8));
    const std::string typed = trimmed(s.machine_buf);
    ImGui::BeginDisabled(typed.empty());
    primary_button_style(true);
    const bool go = ImGui::Button("Add", ImVec2(px(80), 0)) || enter;
    primary_button_style(false);
    ImGui::EndDisabled();
    if (go && !typed.empty())
        add(typed);
    // The typed target's state under the box (herdr: Connecting, why not, or what to answer).
    if (const auto st = s.machine_add_state.find(typed); !typed.empty() && st != s.machine_add_state.end()) {
        ImGui::PushTextWrapPos(0.0f);
        if (st->second == "adding") {
            // As the rows show it (machine-add/main.js 251): a spinner and "Connecting".
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* d = ImGui::GetWindowDrawList();
            const float h = std::round(fpx(12) * 1.3f);
            status_mark(s, d, p.x - px(1), p.y + h * 0.5f, Attention::kWorking, true);
            d->AddText(s.fonts.ui, fpx(12), ImVec2(p.x + px(12) + px(6), p.y + (h - fpx(12)) * 0.5f), g_pal->dim,
                       "Connecting");
            ImGui::Dummy(ImVec2(px(100), h));
        } else

            ImGui::TextColored(v4(g_pal->needs_you), "%s", st->second.c_str());
        ImGui::PopTextWrapPos();
        if (s.machine_add_pending.contains(typed)) {
            const std::string command = "ssh " + shell_arg(typed);
            if (ImGui::SmallButton(s.machine_add_copied == command ? "Copied##t" : "Copy##t")) {
                ImGui::SetClipboardText(command.c_str());
                s.machine_add_copied = command;
            }
        }
    }
    if (close || ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click())
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
}  // namespace detail

}  // namespace agentdesk::desktop
