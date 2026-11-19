// SidePanels: the Files and Changes panels' data (listing, preview, git status and diffs),
// loaded on one worker thread.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

SidePanels::~SidePanels() {
    worker_.request_stop();
    cv_.notify_all();
}

void SidePanels::track_files(const std::string& root, const std::string& machine) {
    std::lock_guard lock(mutex);
    const auto now = Clock::now();
    if (root != files_root || machine != files_machine) {
        files_contexts_[files_machine + "|" + files_root] = {expanded, preview_path};
        files_root = root;
        files_machine = machine;
        const auto it = files_contexts_.find(machine + "|" + root);
        expanded = it != files_contexts_.end() ? it->second.first : std::set<std::string>{""};
        expanded.insert("");
        preview_path = it != files_contexts_.end() ? it->second.second : std::string();
        listing.clear();
        marks.clear();
        files_error.clear();
        files_at_ = {};
        watch(machine.empty() ? root : std::string());  // remote folders refresh every 10 s
    }
    if (!files_loading && (files_at_ == Clock::time_point{} || now - files_at_ > std::chrono::seconds(10) ||
                           files_changed_.exchange(false))) {
        files_loading = true;
        files_at_ = now;
        post(Panel::kFiles, [this, mc = machine, root, folders = expanded, preview = preview_path] {
            load_files(mc, root, folders, preview);
        });
    }
}

void SidePanels::track_changes(const std::string& cwd, const std::string& machine) {
    std::lock_guard lock(mutex);
    const auto now = Clock::now();
    if (cwd != changes_cwd || machine != changes_machine) {
        changes_cwd = cwd;
        changes_machine = machine;
        repo_top.clear();
        not_repo = false;
        changes.clear();
        open_diff.clear();
        user_closed = false;
        diff.clear();
        diff_message.clear();
        commit_note.clear();
        commit_msg[0] = 0;
        changes_at_ = {};
        watch(machine.empty() ? cwd : std::string());
    }
    if (!changes_loading && !committing &&
        (changes_at_ == Clock::time_point{} || now - changes_at_ > std::chrono::seconds(10) ||
         changes_changed_.exchange(false))) {
        changes_loading = true;
        changes_at_ = now;
        post(Panel::kChanges, [this, mc = machine, cwd, open = open_diff, closed = user_closed] {
            load_changes(mc, cwd, open, closed);
        });
    }
}

void SidePanels::toggle_folder(const std::string& rel) {
    std::lock_guard lock(mutex);
    if (expanded.contains(rel)) {
        // Collapse it and every expanded folder below it; drop their listings.
        for (auto it = expanded.begin(); it != expanded.end();) {
            if (*it == rel || it->rfind(rel + "/", 0) == 0) {
                listing.erase(*it);
                it = expanded.erase(it);
            } else {
                ++it;
            }
        }
        return;
    }
    expanded.insert(rel);
    post(Panel::kFiles, [this, mc = files_machine, root = files_root, rel] {
        auto entries = list_folder(mc, root, rel);
        std::lock_guard l(mutex);
        if (root == files_root && mc == files_machine)
            listing[rel] = std::move(entries.first);
    });
}

void SidePanels::open_file(const std::string& rel) {
    std::lock_guard lock(mutex);
    preview_path = rel;
    preview_text.clear();
    preview_message = "Loading file\xE2\x80\xA6";
    sel_line_a = sel_line_b = -1;
    sel_dragging = false;
    post(Panel::kFiles, [this, mc = files_machine, root = files_root, rel] { load_preview(mc, root, rel); });
}

void SidePanels::close_preview() {
    std::lock_guard lock(mutex);
    preview_path.clear();
}

void SidePanels::select_change(const std::string& path) {
    std::lock_guard lock(mutex);
    if (open_diff == path) {
        open_diff.clear();
        user_closed = true;
        diff.clear();
        diff_message.clear();
        return;
    }
    open_diff = path;
    user_closed = false;
    diff.clear();
    diff_message = "Loading\xE2\x80\xA6";
    Change c;
    for (const auto& x : changes)
        if (x.path == path)
            c = x;
    post(Panel::kChanges, [this, mc = changes_machine, top = repo_top, c] { load_diff(mc, top, c); });
}

void SidePanels::commit() {
    std::lock_guard lock(mutex);
    // herdr changes/main.js commit(): this.message.trim()
    std::string msg = commit_msg;
    const auto first = msg.find_first_not_of(" \t\r\n");
    msg = first == std::string::npos ? std::string()
                                     : msg.substr(first, msg.find_last_not_of(" \t\r\n") - first + 1);
    if (msg.empty() || repo_top.empty() || committing || changes_loading)
        return;
    committing = true;
    commit_note.clear();
    post(Panel::kChanges, [this, mc = changes_machine, top = repo_top, msg] {
        auto r = git_run(mc, top, {L"add", L"-A"});
        if (r.started && r.exit_code == 0)
            r = git_run(mc, top, {L"commit", L"-m", win::to_wide(msg)});
        std::lock_guard l(mutex);
        committing = false;
        if (r.started && r.exit_code == 0) {
            commit_msg[0] = 0;
        } else {
            // (stderr || stdout).trim(), as herdr shows it
            commit_note = "Commit failed: " + (r.started ? r.message() : std::string("git not found"));
        }
        changes_at_ = {};
    });
}

std::string SidePanels::join(const std::string& root, const std::string& rel) {
    if (rel.empty())
        return root;
    std::string p = root;
    if (!p.empty() && p.back() != '\\')
        p += '\\';
    for (const char c : rel)
        p += c == '/' ? '\\' : c;
    return p;
}

std::string SidePanels::lower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::pair<std::vector<SidePanels::FileEntry>, bool> SidePanels::list_folder(const std::string& mc,
                                                                            const std::string& root,
                                                                            const std::string& rel,
                                                                            bool* failed) {
    std::vector<FileEntry> out;
    bool more = false;
    if (!mc.empty()) {
        const auto r = remote ? remote(mc, "fs.list", Json{{"path", join(root, rel)}, {"max", kPerFolder}})
                              : std::nullopt;
        if (!r) {
            if (failed != nullptr)
                *failed = true;
            return {out, false};
        }
        if (const Json* entries = safe::field(*r, "entries"); entries != nullptr && entries->is_array())
            for (const auto& e : *entries)
                if (const std::string name = safe::string_or(e, "name"); !name.empty())
                    out.push_back({name, safe::bool_or(e, "dir", false)});
        more = safe::bool_or(*r, "more", false);
        std::sort(out.begin(), out.end(), [](const FileEntry& a, const FileEntry& b) {
            if (a.dir != b.dir)
                return a.dir;
            return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
        });
        return {out, more};
    }
    std::error_code ec;
    std::filesystem::directory_iterator it(win::to_wide(join(root, rel)),
                                           std::filesystem::directory_options::skip_permission_denied, ec);
    for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        const std::string name = safe::utf8_lossy(it->path().filename().native());
        if (name == ".git")
            continue;
        if (out.size() >= kPerFolder) {
            more = true;
            break;
        }
        std::error_code e2;
        out.push_back({name, it->is_directory(e2)});
    }
    std::sort(out.begin(), out.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.dir != b.dir)
            return a.dir;
        return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return {out, more};
}

host::ToolResult SidePanels::git_run(const std::string& mc, const std::string& cwd,
                                     std::vector<std::wstring> args) {
    if (!mc.empty()) {
        host::ToolResult r;
        Json a = Json::array();
        for (const auto& x : args)
            a.push_back(win::to_utf8(x));
        const auto res = remote ? remote(mc, "git.run", Json{{"cwd", cwd}, {"args", a}}) : std::nullopt;
        if (res) {
            r.started = true;
            r.exit_code = static_cast<unsigned long>(safe::int_or(*res, "exit_code", 1));
            r.output = safe::string_or(*res, "output");
            r.error = safe::string_or(*res, "error");
        }
        return r;
    }
    const std::filesystem::path p(win::to_wide(cwd));
    args.insert(args.begin(), {L"git", L"--no-optional-locks", L"-c", L"core.fsmonitor=false", L"-c",
                               L"diff.autoRefreshIndex=false", L"-C", p.native()});
    return host::run_tool(args, p, std::chrono::seconds(60));
}

std::string SidePanels::git_out(const std::string& mc, const std::string& cwd, std::vector<std::wstring> args,
                                bool* ok) {
    const auto r = git_run(mc, cwd, std::move(args));
    if (ok != nullptr)
        *ok = r.started && r.exit_code == 0;
    return r.output;
}

std::string SidePanels::top_of(const std::string& mc, const std::string& cwd) {
    bool ok = false;
    std::string out = git_out(mc, cwd, {L"rev-parse", L"--show-toplevel"}, &ok);
    if (!ok)
        return {};
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    for (auto& c : out)
        if (c == '/')
            c = '\\';
    return out;
}

std::vector<std::pair<std::string, std::string>> SidePanels::status(const std::string& mc,
                                                                    const std::string& top, bool* ok) {
    const std::string out = git_out(
        mc, top, {L"status", L"--porcelain=v1", L"-z", L"--no-renames", L"--untracked-files=all"}, ok);
    std::vector<std::pair<std::string, std::string>> r;
    std::size_t i = 0;
    while (i + 3 < out.size()) {
        const std::size_t z = out.find('\0', i);
        if (z == std::string::npos)
            break;
        r.emplace_back(out.substr(i, 2), out.substr(i + 3, z - i - 3));
        i = z + 1;
    }
    return r;
}

char SidePanels::letter_of(const std::string& xy) {
    if (xy.find('U') != std::string::npos || xy == "AA" || xy == "DD")
        return 'U';
    if (xy.find('D') != std::string::npos)
        return 'D';
    if (xy == "??" || xy.find('A') != std::string::npos)
        return 'A';
    return 'M';
}

char SidePanels::file_mark_of(const std::string& xy) {
    if (xy == "??" || xy.find('A') != std::string::npos)
        return 'A';
    if (xy.find('U') != std::string::npos)
        return 'U';
    if (xy.find('D') != std::string::npos)
        return 'D';
    return 'M';
}

void SidePanels::load_files(const std::string& mc, const std::string& root, std::set<std::string> folders,
                            const std::string& preview) {
    std::map<std::string, std::vector<FileEntry>> got;
    std::string error;
    bool limited = false;
    std::size_t total = 0;
    for (const auto& rel : folders) {
        bool failed = false;
        auto [entries, more] = list_folder(mc, root, rel, &failed);
        if (rel.empty() && entries.empty()) {
            std::error_code ec;
            if (failed || (mc.empty() && !std::filesystem::is_directory(win::to_wide(root), ec)))
                error = mc.empty() ? "Cannot list this folder" : "Cannot list this folder on " + mc;
        }
        limited = limited || more || total + entries.size() > kTotal;
        if (total + entries.size() > kTotal)
            entries.resize(kTotal - std::min(total, kTotal));
        total += entries.size();
        got[rel] = std::move(entries);
    }
    // Git marks: keyed by path relative to `root` (normalised; herdr's string join misses on Windows).
    std::map<std::string, char> m;
    const std::string top = top_of(mc, root);
    if (!top.empty()) {
        bool ok = false;
        const auto st = status(mc, top, &ok);
        std::string prefix = lower(root.size() > top.size() ? root.substr(top.size() + 1) : std::string());
        for (auto& c : prefix)
            if (c == '\\')
                c = '/';
        std::size_t n = 0;
        for (const auto& [xy, path] : st) {
            if (++n > 5000)
                break;
            std::string rel = lower(path);
            if (!prefix.empty()) {
                if (rel.rfind(prefix + "/", 0) != 0)
                    continue;
                rel = rel.substr(prefix.size() + 1);
            }
            m[rel] = file_mark_of(xy);
        }
    }
    std::lock_guard lock(mutex);
    if (root != files_root || mc != files_machine) {
        files_loading = false;
        return;
    }
    for (auto& [rel, entries] : got)
        if (expanded.contains(rel))
            listing[rel] = std::move(entries);
    marks = std::move(m);
    files_error = error;
    files_limited = limited;
    files_loading = false;
    if (!preview.empty() && preview == preview_path && preview_text.empty())
        post(Panel::kFiles, [this, mc, root, preview] { load_preview(mc, root, preview); });
}

bool SidePanels::read_text(const std::string& mc, const std::string& path, std::string& out,
                           std::string& message) {
    if (!mc.empty()) {
        const auto r = remote ? remote(mc, "fs.read", Json{{"path", path}}) : std::nullopt;
        if (!r || safe::bool_or(*r, "missing", false)) {
            message = r ? "This file is no longer available" : "No answer from " + mc;
            return false;
        }
        if (safe::bool_or(*r, "too_large", false)) {
            message = "Cannot preview this file (binary, too large, or unreadable)";
            return false;
        }
        if (safe::bool_or(*r, "binary", false)) {
            message = "Binary file \xC2\xB7 no text preview";
            return false;
        }
        out = safe::string_or(*r, "text");
        return true;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(win::to_wide(path), ec);
    if (ec) {
        message = "This file is no longer available";
        return false;
    }
    if (size > 1024 * 1024) {
        message = "Cannot preview this file (binary, too large, or unreadable)";
        return false;
    }
    std::ifstream in(std::filesystem::path(win::to_wide(path)), std::ios::binary);
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (out.find('\0') != std::string::npos) {
        message = "Binary file \xC2\xB7 no text preview";
        return false;
    }
    return true;
}

void SidePanels::load_preview(const std::string& mc, const std::string& root, const std::string& rel) {
    std::string text, message;
    if (read_text(mc, join(root, rel), text, message) && text.size() > 400000) {
        text.resize(400000);
        text += "\n\xE2\x80\xA6 (file truncated)";
    }
    std::lock_guard lock(mutex);
    if (root == files_root && mc == files_machine && rel == preview_path) {
        preview_text = std::move(text);
        preview_message = std::move(message);
    }
}

void SidePanels::load_changes(const std::string& mc, const std::string& cwd, std::string open, bool closed) {
    const std::string top = top_of(mc, cwd);
    if (top.empty()) {
        std::lock_guard lock(mutex);
        if (cwd == changes_cwd && mc == changes_machine) {
            not_repo = true;
            repo_top.clear();
            changes.clear();
        }
        changes_loading = false;  // also when the folder changed meanwhile: the next load may start
        return;
    }
    bool ok = false;
    const auto st = status(mc, top, &ok);
    std::string error = ok ? std::string() : std::string("git status failed");
    bool has_head = false;
    git_out(mc, top, {L"rev-parse", L"--verify", L"HEAD"}, &has_head);
    std::map<std::string, std::pair<int, int>> counts;
    std::set<std::string> binary;
    auto numstat = [&](std::vector<std::wstring> args) {
        const std::string out = git_out(mc, top, std::move(args));
        std::size_t i = 0;
        while (i < out.size()) {
            const std::size_t z = out.find('\0', i);
            if (z == std::string::npos)
                break;
            const std::string rec = out.substr(i, z - i);
            i = z + 1;
            std::stringstream ss(rec);
            std::string a, d, p;
            std::getline(ss, a, '\t');
            std::getline(ss, d, '\t');
            std::getline(ss, p);
            if (a == "-") {
                binary.insert(p);
                continue;
            }
            counts[p].first += std::atoi(a.c_str());
            counts[p].second += std::atoi(d.c_str());
        }
    };
    if (has_head) {
        numstat({L"diff", L"HEAD", L"--numstat", L"-z", L"--no-renames"});
    } else {
        numstat({L"diff", L"--cached", L"--numstat", L"-z", L"--no-renames"});
        numstat({L"diff", L"--numstat", L"-z", L"--no-renames"});
    }
    std::vector<Change> list;
    bool more = false;
    for (const auto& [xy, path] : st) {
        if (list.size() >= 1000) {
            more = true;
            break;
        }
        Change c;
        c.letter = letter_of(xy);
        c.path = path;
        c.binary = binary.contains(path);
        c.untracked = xy == "??" || (!has_head && xy.find('A') != std::string::npos);
        if (c.untracked) {
            std::string text, msg;
            if (read_text(mc, join(top, path), text, msg))
                c.add = static_cast<int>(std::count(text.begin(), text.end(), '\n') +
                                         (!text.empty() && text.back() != '\n' ? 1 : 0));
        } else if (const auto it = counts.find(path); it != counts.end()) {
            c.add = it->second.first;
            c.del = it->second.second;
        }
        list.push_back(std::move(c));
    }
    // Keep the open diff; open the first file when nothing is open and the user did not close it.
    Change to_open;
    bool load = false;
    if (!open.empty()) {
        for (const auto& c : list)
            if (c.path == open) {
                to_open = c;
                load = true;
            }
    } else if (!closed && !list.empty()) {
        to_open = list.front();
        load = true;
    }
    {
        std::lock_guard lock(mutex);
        if (cwd != changes_cwd || mc != changes_machine) {
            changes_loading = false;
            return;
        }
        repo_top = top;
        not_repo = false;
        changes = std::move(list);
        changes_more = more;
        changes_error = error;
        changes_loading = false;
        if (!load) {
            if (!open.empty()) {
                open_diff.clear();
                diff.clear();
                diff_message.clear();
            }
            return;
        }
        open_diff = to_open.path;
    }
    load_diff(mc, top, to_open);
}

std::string SidePanels::untab(const std::string& s) {
    std::string out;
    int col = 0;
    for (const char c : s) {
        if (c == '\t') {
            do {
                out += ' ';
                ++col;
            } while (col % 4 != 0);
        } else {
            out += c;
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
                ++col;
        }
    }
    return out.empty() ? std::string(" ") : out;
}

std::string SidePanels::cut(std::string s) {
    if (s.size() > 4000) {
        s.resize(4000);
        s += "\xE2\x80\xA6 (line truncated)";
    }
    return s;
}

void SidePanels::load_diff(const std::string& mc, const std::string& top, const Change& c) {
    std::vector<DiffRow> rows;
    std::string message;
    if (c.binary) {
        message = "Binary file changed";
    } else if (c.untracked) {
        std::string text, msg;
        if (!read_text(mc, join(top, c.path), text, msg)) {
            message = msg.find("Binary") != std::string::npos ? "Binary file \xC2\xB7 no text diff" : msg;
        } else {
            if (text.size() > 400000)
                text.resize(400000);
            std::stringstream ss(text);
            int n = 0;
            std::vector<std::string> lines;
            for (std::string l; std::getline(ss, l) && lines.size() < 1500;)
                lines.push_back(l);
            rows.push_back({DiffRow::kHunk, 0, "@@ -0,0 +1," + std::to_string(lines.size()) + " @@"});
            for (auto& l : lines) {
                if (!l.empty() && l.back() == '\r')
                    l.pop_back();
                rows.push_back({DiffRow::kAdd, ++n, cut(untab(l))});
            }
        }
    } else {
        bool ok = false;
        bool has_head = false;
        git_out(mc, top, {L"rev-parse", L"--verify", L"HEAD"}, &has_head);
        std::vector<std::wstring> args = {L"diff"};
        if (has_head)
            args.push_back(L"HEAD");
        for (const wchar_t* a : {L"--no-ext-diff", L"--no-textconv", L"--no-color", L"--"})
            args.push_back(a);
        args.push_back(win::to_wide(c.path));
        const std::string out = git_out(mc, top, args, &ok);
        if (!ok) {
            message = "Cannot preview this diff (too large or unreadable)";
        } else {
            std::stringstream ss(out);
            bool body = false;
            int old_n = 0, new_n = 0;
            for (std::string l; std::getline(ss, l) && rows.size() < 1500;) {
                if (!l.empty() && l.back() == '\r')
                    l.pop_back();
                if (l.rfind("@@", 0) == 0) {
                    body = true;
                    int a = 0, b2 = 0;
                    sscanf_s(l.c_str(), "@@ -%d%*[^+]+%d", &a, &b2);
                    old_n = a;
                    new_n = b2;
                    rows.push_back({DiffRow::kHunk, 0, l});
                    continue;
                }
                if (!body)
                    continue;
                if (!l.empty() && l[0] == '+')
                    rows.push_back({DiffRow::kAdd, new_n++, cut(untab(l.substr(1)))});
                else if (!l.empty() && l[0] == '-')
                    rows.push_back({DiffRow::kDel, old_n++, cut(untab(l.substr(1)))});
                else if (!l.empty() && l[0] == ' ') {
                    rows.push_back({DiffRow::kContext, new_n++, cut(untab(l.substr(1)))});
                    ++old_n;
                } else
                    rows.push_back({DiffRow::kOther, 0, l});
            }
            if (rows.empty())
                message = "No text diff available";
        }
    }
    std::lock_guard lock(mutex);
    if (top == repo_top && c.path == open_diff) {
        diff = std::move(rows);
        diff_message = std::move(message);
    }
}

void SidePanels::watch(const std::string& dir) {
    watch_dir_ = dir;
    cv_.notify_all();
}

void SidePanels::post(Panel panel, std::function<void()> fn) {
    tasks_.push_back({panel, std::move(fn)});
    cv_.notify_all();
}

void SidePanels::task_failed(Panel panel, const char* what) {
    std::lock_guard lock(mutex);
    const std::string note = std::string("Could not read this folder: ") + what;
    if (panel == Panel::kFiles) {
        files_loading = false;
        files_error = note;
        if (!preview_path.empty() && preview_text.empty())
            preview_message = note;
    } else {
        changes_loading = false;
        committing = false;
        changes_error = note;
        if (!open_diff.empty() && diff.empty())
            diff_message = note;
    }
}

void SidePanels::run(std::stop_token st) {
    std::string watching;
    HANDLE change = INVALID_HANDLE_VALUE;
    while (!st.stop_requested()) {
        Task task;
        std::string want;
        {
            std::unique_lock lock(mutex);
            cv_.wait_for(lock, st, std::chrono::milliseconds(150), [&] { return !tasks_.empty(); });
            if (st.stop_requested())
                break;
            want = watch_dir_;
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
        }
        if (want != watching) {
            if (change != INVALID_HANDLE_VALUE)
                ::FindCloseChangeNotification(change);
            change = INVALID_HANDLE_VALUE;
            try {
                if (!want.empty())
                    change = ::FindFirstChangeNotificationW(
                        win::to_wide(want).c_str(), TRUE,
                        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
            } catch (const std::exception&) {  // not watched; the 10 s refresh still runs
            }
            watching = want;
        }
        if (change != INVALID_HANDLE_VALUE && ::WaitForSingleObject(change, 0) == WAIT_OBJECT_0) {
            files_changed_ = true;
            changes_changed_ = true;
            ::FindNextChangeNotification(change);
        }
        if (task.fn) {
            try {
                task.fn();
            } catch (const std::exception& e) {
                task_failed(task.panel, e.what());
            } catch (...) {
                task_failed(task.panel, "unexpected error");
            }
        }
    }
    if (change != INVALID_HANDLE_VALUE)
        ::FindCloseChangeNotification(change);
}

}  // namespace detail

}  // namespace agentdesk::desktop
