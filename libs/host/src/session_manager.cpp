// SessionManager: request handling for IPC protocol v1, snapshot and delta fan-out, session
// metadata, and the host-side helpers for agents, worktrees and remote Files/Changes.
#include "agentdesk/host/session_manager.hpp"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <system_error>

#include "agentdesk/host/git_tools.hpp"
#include "agentdesk/ipc/host_launcher.hpp"
#include "agentdesk/platform/win/error.hpp"

#ifndef _WIN32
#include <unistd.h>

#include "agentdesk/platform/system.hpp"

// The one Win32 name the shared code below uses for the host's pid.
static inline std::uint32_t GetCurrentProcessId() { return static_cast<std::uint32_t>(::getpid()); }
#endif

namespace agentdesk::host {

namespace win = platform::win;
using protocol::ErrorCode;
using protocol::Json;
using protocol::Request;
using protocol::Response;

namespace {

constexpr std::size_t kMaxHistoryLines = 200;  // design doc 3.7 history.read
constexpr std::size_t kMaxSnapshotChunkBytes = 900 * 1024;
constexpr std::size_t kMaxRecords = 200;
// Exited sessions keep their Session (threads, engine, history) so the final screen can still be
// read; only the newest few do, otherwise create/stop churn grows threads and memory forever
// (design doc 1.5 stability target). Older ones keep metadata only.
constexpr std::size_t kMaxExitedLive = 8;
// git worktree add/remove may take minutes on a large repository (checkout, submodules).
constexpr auto kWorktreeTimeout = std::chrono::seconds(600);
constexpr auto kGitTimeout = std::chrono::seconds(60);
// A reply must fit one IPC frame (kMaxFrameBytes, 4 MiB) after JSON escaping.
constexpr std::size_t kMaxGitRunBytes = 3u * 1024u * 1024u;
constexpr std::size_t kMaxGitErrorBytes = 64u * 1024u;
// A session.create "task" waits this long for an agent without any status source, and twice
// as long again at most for one that has a source but never says Idle.
constexpr auto kTaskFallback = std::chrono::seconds(8);
constexpr std::size_t kMaxTaskBytes = 64u * 1024u;
constexpr std::size_t kMaxLabelKeyBytes = 64;

// Sorts the session table's entries oldest first.
constexpr auto kOldestFirst = [](const auto& a, const auto& b) {
    return a->record.created_at < b->record.created_at;
};

// JSON strings must be UTF-8: invalid bytes become U+FFFD.
std::string valid_utf8(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    std::size_t i = 0;
    while (i < in.size()) {
        const auto c = static_cast<unsigned char>(in[i]);
        std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool ok = n > 0 && i + n <= in.size();
        for (std::size_t k = 1; ok && k < n; ++k)
            ok = (static_cast<unsigned char>(in[i + k]) & 0xC0) == 0x80;
        if (ok) {
            out.append(in, i, n);
            i += n;
        } else {
            out += "\xEF\xBF\xBD";
            ++i;
        }
    }
    return out;
}

// Text for an exception that goes into a reply. std::system_error's message is in the ANSI code
// page (CP950 on a Traditional Chinese system), which is not UTF-8 and would make the reply
// unserializable: describe Windows errors with FormatMessageW instead.
std::string error_text(const std::exception& e) {
    std::string what = e.what();
#ifdef _WIN32
    if (const auto* se = dynamic_cast<const std::system_error*>(&e);
        se != nullptr && se->code().category() == std::system_category()) {
        const std::string ansi = se->code().message();
        std::string prefix = what;
        if (what.size() >= ansi.size() + 2 && what.ends_with(ansi))
            prefix = what.substr(0, what.size() - ansi.size() - 2);
        wchar_t* text = nullptr;
        const DWORD n = ::FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, static_cast<DWORD>(se->code().value()), 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
        const std::unique_ptr<wchar_t, decltype(&::LocalFree)> owned(text, &::LocalFree);
        std::string message;
        if (n > 0 && text != nullptr) {
            std::wstring_view w(text, n);
            while (!w.empty() &&
                   (w.back() == L'\r' || w.back() == L'\n' || w.back() == L' ' || w.back() == L'.'))
                w.remove_suffix(1);
            try {
                message = win::to_utf8(w);
            } catch (const std::exception&) {
            }
        }
        if (message.empty())
            message = "Windows error " + std::to_string(se->code().value());
        what = prefix.empty() ? message : prefix + ": " + message;
    }
#endif
    return valid_utf8(what);
}

Response bad(const Request& r, std::string msg) {
    return protocol::fail(r.id, ErrorCode::kBadRequest, std::move(msg));
}

#ifdef _WIN32
// WSL: a Linux cd never reaches wsl.exe's Windows current folder. bash runs PROMPT_COMMAND
// before each prompt; set it (passed into Linux through WSLENV) to send OSC 7 with the distro
// as the host, which the session maps to \\wsl.localhost\<distro>\... or C:\... for /mnt/c.
// Only for an interactive shell: a profile that runs a command (-e, --exec, --) is left alone.
// A ~/.bashrc that assigns PROMPT_COMMAND, or zsh/fish, replaces it; then the folder is unknown.
void add_wsl_folder_hook(platform::win::PtySpawnSpec& spec) {
    for (const auto& a : spec.args)
        if (a == L"-e" || a == L"--exec" || a == L"--")
            return;
    // OSC 133 A too: the prompt is back, so the program that ran before it (an agent) has ended.
    spec.env_overrides[L"PROMPT_COMMAND"] =
        L"printf '\\033]133;A\\007\\033]7;file://%s%s\\007' \"$WSL_DISTRO_NAME\" \"$PWD\"";
    std::wstring wslenv;
    if (const auto it = spec.env_overrides.find(L"WSLENV"); it != spec.env_overrides.end()) {
        wslenv = it->second;
    } else {
        wchar_t buf[4096]{};
        const DWORD n = ::GetEnvironmentVariableW(L"WSLENV", buf, 4096);
        if (n > 0 && n < 4096)
            wslenv.assign(buf, n);
    }
    if (wslenv.find(L"PROMPT_COMMAND") == std::wstring::npos)
        wslenv += (wslenv.empty() ? L"" : L":") + std::wstring(L"PROMPT_COMMAND/u");
    spec.env_overrides[L"WSLENV"] = wslenv;
}

std::wstring to_lower(std::wstring s) {
    for (auto& c : s)
        c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

// PowerShell's cd (Set-Location) does not change the process's Windows current folder, which is
// what the host reads to follow the shell. So the prompt is wrapped: after the person's own
// prompt runs, the location is copied into [Environment]::CurrentDirectory. Only for an interactive
// PowerShell the profile starts without its own -Command/-File; cmd needs nothing.
void add_folder_hook(platform::win::PtySpawnSpec& spec) {
    const std::wstring exe = to_lower(spec.executable.filename().native());
    if (exe == L"wsl.exe") {
        add_wsl_folder_hook(spec);
        return;
    }
    if (exe == L"cmd.exe") {
        // cmd's PROMPT can send escapes ($E): OSC 133 A before the person's own prompt (or the
        // default $P$G), so the host knows a program that ran before it (an agent) has ended.
        wchar_t buf[1024]{};
        const DWORD n = ::GetEnvironmentVariableW(L"PROMPT", buf, 1024);
        const std::wstring own = n > 0 && n < 1024 ? std::wstring(buf, n) : std::wstring(L"$P$G");
        if (own.find(L"]133;A") == std::wstring::npos)
            spec.env_overrides[L"PROMPT"] = L"$E]133;A$E\\" + own;
        return;
    }
    if (exe != L"pwsh.exe" && exe != L"powershell.exe")
        return;
    for (const auto& a : spec.args) {
        const std::wstring l = to_lower(a);
        if (l == L"-command" || l == L"-c" || l == L"-file" || l == L"-f" || l == L"-encodedcommand" ||
            l == L"-ec" || l == L"-noninteractive")
            return;
    }
    spec.args.push_back(L"-NoExit");
    spec.args.push_back(L"-Command");
    spec.args.push_back(
        L"$global:__agentdesk_prompt = $function:prompt; "
        L"function global:prompt { [Console]::Write([char]27 + ']133;A' + [char]7); "
        L"$r = & $global:__agentdesk_prompt; "
        L"try { $l = Get-Location; if ($l.Provider.Name -eq 'FileSystem') "
        L"{ [Environment]::CurrentDirectory = $l.ProviderPath } } catch {}; $r }");
}
#else
// macOS (ADR-30): every shell gets a terminal type and a UTF-8 locale (apps started from the
// Finder have neither), and the common shells report each prompt: OSC 133 A (the program before
// it, an agent, has ended) and OSC 7 (the folder). zsh reads startup files from $ZDOTDIR: ours
// restores the person's ZDOTDIR, sources their .zshenv and adds a precmd hook; their other files
// load as usual. bash gets PROMPT_COMMAND, which a profile that assigns it replaces.
std::filesystem::path zsh_hook_dir() {
    const auto dir = platform::runtime_dir() / "zsh";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto file = dir / ".zshenv";
    static constexpr const char* kZshenv =
        "# Written by AgentDesk: reports each prompt (OSC 133 A, OSC 7), then gets out of the way.\n"
        "if [[ -n \"${AGENTDESK_USER_ZDOTDIR+x}\" ]]; then ZDOTDIR=\"$AGENTDESK_USER_ZDOTDIR\"; "
        "else unset ZDOTDIR; fi\n"
        "unset AGENTDESK_USER_ZDOTDIR\n"
        "[[ -f \"${ZDOTDIR:-$HOME}/.zshenv\" ]] && source \"${ZDOTDIR:-$HOME}/.zshenv\"\n"
        "if [[ -o interactive ]]; then\n"
        "  __agentdesk_precmd() { printf '\\e]133;A\\a\\e]7;file://%s%s\\a' \"${HOST%%.*}\" \"${PWD// /%20}\"; }\n"
        "  autoload -Uz add-zsh-hook && add-zsh-hook precmd __agentdesk_precmd\n"
        "fi\n";
    std::ofstream(file, std::ios::trunc) << kZshenv;
    return dir;
}

void add_folder_hook(platform::win::PtySpawnSpec& spec) {
    if (!spec.env_overrides.contains(L"TERM"))
        spec.env_overrides[L"TERM"] = L"xterm-256color";
    spec.env_overrides[L"COLORTERM"] = L"truecolor";
    spec.env_overrides[L"TERM_PROGRAM"] = L"AgentDesk";
    // Programs the person installed (claude, node, git) are on the login shell's PATH, not on the
    // minimal one an app started from the Finder gets.
    if (const std::string path = platform::login_path(); !path.empty() && !spec.env_overrides.contains(L"PATH"))
        spec.env_overrides[L"PATH"] = win::to_wide(path);
    const char* lang = std::getenv("LANG");
    if (lang == nullptr || std::string_view(lang).find("UTF-8") == std::string_view::npos)
        spec.env_overrides[L"LANG"] = L"en_US.UTF-8";
    const std::string exe = spec.executable.filename().string();
    if (exe == "zsh") {
        if (const char* own = std::getenv("ZDOTDIR"); own != nullptr)
            spec.env_overrides[L"AGENTDESK_USER_ZDOTDIR"] = win::to_wide(own);
        try {
            spec.env_overrides[L"ZDOTDIR"] = zsh_hook_dir().wstring();
        } catch (const std::exception&) {
            // No hook: the folder and the end of a program are then unknown, nothing else breaks.
        }
    } else if (exe == "bash") {
        spec.env_overrides[L"PROMPT_COMMAND"] =
            L"printf '\\033]133;A\\007\\033]7;file://%s%s\\007' \"${HOSTNAME%%.*}\" \"${PWD// /%20}\"";
    }
}
#endif

std::optional<terminal::Key> key_from_name(std::string_view n) {
    static const std::pair<std::string_view, terminal::Key> kKeys[] = {
        {"enter", terminal::Key::kEnter},
        {"tab", terminal::Key::kTab},
        {"backspace", terminal::Key::kBackspace},
        {"escape", terminal::Key::kEscape},
        {"up", terminal::Key::kUp},
        {"down", terminal::Key::kDown},
        {"left", terminal::Key::kLeft},
        {"right", terminal::Key::kRight},
        {"insert", terminal::Key::kInsert},
        {"delete", terminal::Key::kDelete},
        {"home", terminal::Key::kHome},
        {"end", terminal::Key::kEnd},
        {"pageup", terminal::Key::kPageUp},
        {"pagedown", terminal::Key::kPageDown},
    };
    for (const auto& [name, key] : kKeys) {
        if (name == n)
            return key;
    }
    if (n.size() >= 2 && n[0] == 'f') {
        const int f = std::atoi(std::string(n.substr(1)).c_str());
        if (f >= 1 && f <= 12)
            return static_cast<terminal::Key>(static_cast<int>(terminal::Key::kF1) + f - 1);
    }
    return std::nullopt;
}

std::uint8_t mods_from_json(const Json& j) {
    std::uint8_t m = terminal::kModNone;
    if (!j.is_object())
        return m;
    if (j.value("ctrl", false))
        m |= terminal::kModCtrl;
    if (j.value("shift", false))
        m |= terminal::kModShift;
    if (j.value("alt", false))
        m |= terminal::kModAlt;
    return m;
}

std::u32string utf8_to_u32(const std::string& s) {
    std::u32string out;
    const std::wstring w = win::to_wide(s);
    for (std::size_t i = 0; i < w.size(); ++i) {
        char32_t c = w[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size()) {
            c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<char32_t>(w[++i]) - 0xDC00);
        }
        out.push_back(c);
    }
    return out;
}

bool has_control_chars(const std::string& s) {
    return std::any_of(s.begin(), s.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u < 0x20 || u == 0x7F;
    });
}

// A label as session.create and session.label accept it: a short printable key, a printable
// string value.
bool valid_label(const std::string& key, const Json& value) {
    return value.is_string() && !key.empty() && key.size() <= kMaxLabelKeyBytes && !has_control_chars(key) &&
           !has_control_chars(value.get<std::string>());
}

terminal::MouseAction mouse_action_from(const std::string& action) {
    if (action == "press")
        return terminal::MouseAction::kPress;
    if (action == "release")
        return terminal::MouseAction::kRelease;
    return terminal::MouseAction::kMove;
}

bool busy(AgentState s) {
    return s == AgentState::kWorking || s == AgentState::kBlocked;
}

// An absolute path from a request parameter; nullopt when missing, relative or not printable.
std::optional<std::filesystem::path> absolute_path_param(const Request& req, const char* key) {
    const std::string p = req.params.value(key, "");
    if (p.empty() || has_control_chars(p))
        return std::nullopt;
    std::filesystem::path path(win::to_wide(p));
    if (!path.is_absolute())
        return std::nullopt;
    return path;
}

// The git commands and options the remote Changes panel and worktree dialogs may run; nullptr
// when `args` (without the safety prefix) is allowed, else why not.
const char* refuse_git_args(const std::vector<std::wstring>& args) {
    // The Changes panel, branch tails and the worktree dialogs (list, remove, submodule status).
    static const std::set<std::wstring> kCommands = {L"status",   L"diff",         L"rev-parse", L"rev-list",
                                                     L"add",      L"for-each-ref", L"commit",    L"log",
                                                     L"worktree", L"submodule"};
    if (!kCommands.contains(args.front()))
        return "git command not allowed";
    if ((args.front() == L"worktree" && (args.size() < 2 || (args[1] != L"list" && args[1] != L"remove"))) ||
        (args.front() == L"submodule" && (args.size() < 2 || args[1] != L"status")))
        return "git command not allowed";
    for (const auto& x : args) {
        if (x == L"--")
            break;
        if (x.starts_with(L"--ext-diff") || x.starts_with(L"--textconv") || x.starts_with(L"--output") ||
            x.starts_with(L"--exec") || x == L"-c" || x.starts_with(L"--config"))
            return "git option not allowed";
    }
    return nullptr;
}

// A damaged entry in profiles.json or sessions.json is skipped, never fatal: the host must start
// even when a settings file is broken (store.cpp, read_json_with_backup). Diagnostics only: the
// file, the entry's position and the error kind, never the entry's text (design doc 3.9).
void log_skipped_entry(const std::filesystem::path& data_dir, std::string_view file, std::size_t index,
                       const std::exception& e) {
    try {
        std::ofstream(data_dir / L"host.log", std::ios::app)
#ifdef _WIN32
            << iso8601_now() << " pid=" << ::GetCurrentProcessId() << " skipped " << file << " entry "
#else
            << iso8601_now() << " pid=" << ::getpid() << " skipped " << file << " entry "
#endif
            << index << " ("
            << (dynamic_cast<const Json::exception*>(&e) != nullptr ? "malformed" : "unreadable") << ")\n";
    } catch (const std::exception&) {
    }
}

// The entries of a settings file's array, parsed one by one; damaged ones are logged and skipped.
template <class T, class Parse>
std::vector<T> read_entries(const Json& list, const std::filesystem::path& data_dir, std::string_view file,
                            Parse parse) {
    std::vector<T> out;
    std::size_t index = 0;
    for (const auto& j : list) {
        try {
            out.push_back(parse(j));
        } catch (const std::exception& e) {
            log_skipped_entry(data_dir, file, index, e);
        }
        ++index;
    }
    return out;
}

// Rows of `next` that differ from `prev` (same size and generation): cells, the wrap flag, or
// the OSC 8 links on the row.
std::vector<int> changed_rows(const terminal::ScreenSnapshot& prev, const terminal::ScreenSnapshot& next) {
    const auto row_links = [](const terminal::ScreenSnapshot& s, int r) {
        std::vector<terminal::Hyperlink> out;
        for (const auto& l : s.links)
            if (l.row == r)
                out.push_back(l);
        return out;
    };
    const bool any_links = !next.links.empty() || !prev.links.empty();
    std::vector<int> changed;
    for (int r = 0; r < next.rows; ++r) {
        const auto a = prev.cells.begin() + static_cast<std::ptrdiff_t>(r) * next.cols;
        const auto b = next.cells.begin() + static_cast<std::ptrdiff_t>(r) * next.cols;
        if (!std::equal(a, a + next.cols, b) || next.row_wrapped(r) != prev.row_wrapped(r) ||
            (any_links && row_links(next, r) != row_links(prev, r)))
            changed.push_back(r);
    }
    return changed;
}

std::string line_text(const terminal::HistoryLine& line) {
    std::string text;
    for (const auto& cell : line.cells) {
        if (cell.width == 0)
            continue;
        if (cell.chars[0] == 0) {
            text.push_back(' ');
            continue;
        }
        for (const char32_t cp : cell.chars) {
            if (cp == 0)
                break;
            terminal::append_utf8(text, cp);
        }
    }
    while (!text.empty() && text.back() == ' ')
        text.pop_back();
    return text;
}

}  // namespace

SessionManager::SessionManager(HostConfig config) : config_(std::move(config)) {
    load_profiles();
    // Previous host's sessions: history only, never adopted by PID (design doc 2.2 step 6).
    if (const auto doc = read_json_with_backup(config_.data_dir / L"sessions.json")) {
        auto records = read_entries<SessionRecord>(doc->value("sessions", Json::array()), config_.data_dir,
                                                   "sessions.json", session_record_from_json);
        for (auto& r : mark_orphans_lost(std::move(records))) {
            auto m = std::make_shared<Managed>();
            m->last_process_state = r.process_state;
            m->record = std::move(r);
            sessions_.emplace(m->record.id, m);
        }
        std::lock_guard lock(mutex_);
        persist_locked();
    }
    start_maintenance();
}

void SessionManager::start_maintenance() {
    maintenance_ = std::thread([this] {
        while (!stopping_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            try {
                deliver_pending();
            } catch (const std::exception&) {
            }
        }
    });
}

SessionManager::~SessionManager() {
    stopping_ = true;
    if (maintenance_.joinable())
        maintenance_.join();
    stop_all();
    std::map<std::string, ManagedPtr> doomed;
    {
        std::lock_guard lock(mutex_);
        doomed.swap(sessions_);
    }
    doomed.clear();  // Session destructors join their actors outside mutex_
}

void SessionManager::load_profiles() {
    const auto file = config_.data_dir / L"profiles.json";
    if (const auto doc = read_json_with_backup(file)) {
        profiles_ = read_entries<Profile>(doc->value("profiles", Json::array()), config_.data_dir,
                                          "profiles.json", profile_from_json);
        // Files written before WSL profiles existed get them once; a profile the person removes
        // afterwards stays removed ("wsl_detected").
        if (const auto wd = doc->find("wsl_detected");
            wd != doc->end() && wd->is_boolean() && wd->get<bool>())
            return;
        for (auto& p : detect_wsl_profiles())
            if (std::none_of(profiles_.begin(), profiles_.end(),
                             [&](const Profile& q) { return q.id == p.id; }))
                profiles_.push_back(std::move(p));
        save_profiles(file);
        return;
    }
    profiles_ = detect_default_profiles();
    save_profiles(file);
}

void SessionManager::save_profiles(const std::filesystem::path& file) {
    Json list = Json::array();
    for (const auto& p : profiles_)
        list.push_back(to_json(p));
    write_json_atomic(file, {{"schema_version", kSchemaVersion}, {"wsl_detected", true}, {"profiles", list}});
}

void SessionManager::persist_locked() {
    if (handed_over_)
        return;  // sessions.json is the new host's now
    std::vector<SessionRecord> records;
    for (const auto& [id, m] : sessions_)
        records.push_back(m->record);
    std::sort(records.begin(), records.end(),
              [](const auto& a, const auto& b) { return a.created_at < b.created_at; });
    if (records.size() > kMaxRecords)
        records.erase(records.begin(), records.end() - kMaxRecords);
    Json list = Json::array();
    for (const auto& r : records)
        list.push_back(to_json(r));
    try {
        write_json_atomic(config_.data_dir / L"sessions.json",
                          {{"schema_version", kSchemaVersion}, {"sessions", list}});
    } catch (const std::exception&) {
        // Disk full or locked: metadata is history only; running sessions are unaffected.
    }
}

void SessionManager::on_open(const ipc::ConnectionPtr&) {}

void SessionManager::on_close(const ipc::ConnectionPtr& conn) {
    std::lock_guard lock(mutex_);
    ui_watchers_.erase(conn->id());
    for (auto& [id, m] : sessions_) {
        m->subscribers.erase(conn->id());
        if (m->controller == conn->id())
            m->controller.reset();  // holder disconnected: released
    }
}

void SessionManager::on_message(const ipc::ConnectionPtr& conn, std::string payload) {
    auto parsed = protocol::parse_message(payload);
    if (!parsed.message) {
        // Unparseable or wrong version: reply when possible, then drop the client.
        protocol::Response r;
        r.id = "unknown";
        r.error = parsed.version_mismatch ? ErrorCode::kVersionMismatch : ErrorCode::kBadRequest;
        r.error_message = parsed.error;
        conn->send(protocol::serialize(r));
        if (parsed.version_mismatch)
            conn->close();
        return;
    }
    const auto* req = std::get_if<Request>(&*parsed.message);
    if (req == nullptr)
        return;  // clients do not send responses or events
    Response resp;
    try {
        resp = dispatch(conn, *req);
    } catch (const std::exception& e) {
        resp = protocol::fail(req->id, ErrorCode::kInternal, error_text(e));
    }
    // Nothing may escape this connection thread: an exception here would end the host and every
    // terminal in it.
    try {
        if (!conn->send(protocol::serialize(resp)) && !conn->closed()) {
            // Larger than one frame (4 MiB): say so instead of leaving the client to time out.
            conn->send(protocol::serialize(
                protocol::fail(req->id, ErrorCode::kInternal, "reply too large for one message")));
        }
    } catch (const std::exception& e) {
        try {
            conn->send(protocol::serialize(protocol::fail(req->id, ErrorCode::kInternal, error_text(e))));
        } catch (const std::exception&) {
            conn->close();  // cannot even say why: drop the client, keep the host
        }
    }
}

Response SessionManager::dispatch(const ipc::ConnectionPtr& conn, const Request& req) {
    const auto& m = req.method;
    // An update is moving every session to a new host (design doc 7.4 OQ-09): nothing may change
    // under it. Reads still answer; the client retries the rest against the new host.
    if (updating_ || handed_over_) {
        static const std::set<std::string> kReadOnly = {"hello",       "profile.list", "session.list",
                                                        "session.get", "history.read", "ui.state.get",
                                                        "fs.list",     "fs.read"};
        if (!kReadOnly.contains(m))
            return protocol::fail(req.id, ErrorCode::kOverloaded, "agent-host is updating", true);
    }
    if (m == "host.update")
        return do_update(req);
    if (m == "hello")
        return do_hello(req);
    if (m == "profile.list")
        return do_profile_list(req);
    if (m == "session.create")
        return do_create(req);
    if (m == "session.list")
        return do_list(req);
    if (m == "session.get")
        return do_get(req);
    if (m == "session.attach")
        return do_attach(conn, req);
    if (m == "session.detach")
        return do_detach(conn, req);
    if (m == "session.input")
        return do_input(conn, req);
    if (m == "session.resize")
        return do_resize(conn, req);
    if (m == "session.stop")
        return do_stop(req);
    if (m == "screen.snapshot")
        return do_snapshot(conn, req);
    if (m == "history.read")
        return do_history(req);
    if (m == "state.report")
        return do_state_report(req);
    if (m == "host.shutdown")
        return do_shutdown(req);
    if (m == "ui.state.get")
        return do_ui_state_get(conn, req);
    if (m == "ui.state.set")
        return do_ui_state_set(conn, req);
    if (m == "worktree.add")
        return do_worktree_add(req);
    if (m == "fs.list")
        return do_fs_list(req);
    if (m == "fs.read")
        return do_fs_read(req);
    if (m == "git.run")
        return do_git_run(req);
    if (m == "session.wait")
        return do_wait(req);
    if (m == "session.label")
        return do_label(req);
    if (m == "session.report")
        return do_report(req);
    return bad(req, "unknown method: " + m);
}

Response SessionManager::do_ui_state_get(const ipc::ConnectionPtr& conn, const Request& req) {
    std::lock_guard lock(mutex_);
    if (!ui_state_loaded_) {
        ui_state_loaded_ = true;
        if (const auto j = read_json_with_backup(config_.data_dir / L"ui-state.json")) {
            ui_state_ = j->value("doc", Json::object());
            ui_state_rev_ = j->value("rev", static_cast<std::uint64_t>(0));
        }
    }
    if (req.params.value("watch", false))
        ui_watchers_[conn->id()] = conn;
    return protocol::ok(req.id, {{"rev", ui_state_rev_}, {"doc", ui_state_}});
}

Response SessionManager::do_ui_state_set(const ipc::ConnectionPtr& conn, const Request& req) {
    const auto it = req.params.find("doc");
    if (it == req.params.end() || !it->is_object())
        return bad(req, "doc must be an object");
    if (it->dump().size() > 1024 * 1024)
        return bad(req, "doc larger than 1 MiB");
    std::lock_guard lock(mutex_);
    ui_state_loaded_ = true;
    ui_state_ = *it;
    ++ui_state_rev_;
    try {
        write_json_atomic(config_.data_dir / L"ui-state.json",
                          {{"schema_version", 1}, {"rev", ui_state_rev_}, {"doc", ui_state_}});
    } catch (const std::exception&) {
        // Like sessions.json: a write failure must not stop the host (kept in memory).
    }
    protocol::Event e;
    e.event = "ui.state";
    e.body = {{"rev", ui_state_rev_}, {"doc", ui_state_}, {"from", conn->id()}};
    const auto payload = protocol::serialize(e);
    for (auto w = ui_watchers_.begin(); w != ui_watchers_.end();) {
        auto c = w->second.lock();
        if (!c) {
            w = ui_watchers_.erase(w);
            continue;
        }
        if (c->id() != conn->id())
            c->send(payload);
        ++w;
    }
    return protocol::ok(req.id, {{"rev", ui_state_rev_}});
}

// ---- Files / Changes panels for a client on another machine. The pipe only admits this Windows
// user, so these read what that user can read; the UI on this machine reads the same folders
// directly.

Response SessionManager::do_fs_list(const Request& req) {
    const auto dir = absolute_path_param(req, "path");
    if (!dir)
        return bad(req, "path must be an absolute folder");
    const std::size_t max = static_cast<std::size_t>(std::clamp(req.params.value("max", 500), 1, 5000));
    std::error_code ec;
    if (!std::filesystem::is_directory(*dir, ec))
        return protocol::fail(req.id, ErrorCode::kNotFound, "not a folder");
    Json entries = Json::array();
    bool more = false;
    std::filesystem::directory_iterator it(*dir, std::filesystem::directory_options::skip_permission_denied,
                                           ec);
    for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        const std::string name = win::to_utf8(it->path().filename().native());
        if (name == ".git")
            continue;
        if (entries.size() >= max) {
            more = true;
            break;
        }
        std::error_code e2;
        entries.push_back({{"name", name}, {"dir", it->is_directory(e2)}});
    }
    return protocol::ok(req.id, {{"entries", std::move(entries)}, {"more", more}});
}

Response SessionManager::do_fs_read(const Request& req) {
    const auto file = absolute_path_param(req, "path");
    if (!file)
        return bad(req, "path must be absolute");
    std::error_code ec;
    const auto size = std::filesystem::file_size(*file, ec);
    if (ec)
        return protocol::ok(req.id, {{"missing", true}});
    constexpr std::uintmax_t kMax = 1024 * 1024;
    if (size > kMax)
        return protocol::ok(req.id, {{"too_large", true}, {"size", size}});
    std::ifstream in(*file, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.find('\0') != std::string::npos)
        return protocol::ok(req.id, {{"binary", true}});
    return protocol::ok(req.id, {{"text", valid_utf8(text)}});
}

// git for the Changes panel: only the read and commit commands it uses, with the same safety
// switches as the local panel (no index refresh, no fsmonitor, no external diff or textconv).
Response SessionManager::do_git_run(const Request& req) {
    const auto cwd = absolute_path_param(req, "cwd");
    if (!cwd)
        return bad(req, "cwd must be an absolute folder");
    const auto& a = req.params.value("args", Json::array());
    if (!a.is_array() || a.empty() || a.size() > 32)
        return bad(req, "args must be a non-empty array");
    std::vector<std::wstring> args;
    for (const auto& x : a) {
        if (!x.is_string() || has_control_chars(x.get<std::string>()))
            return bad(req, "args must be plain strings");
        args.push_back(win::to_wide(x.get<std::string>()));
    }
    if (const char* why = refuse_git_args(args))
        return bad(req, why);
    // worktree remove may take minutes.
    const bool long_running = args.front() == L"worktree";
    args.insert(args.begin(), {L"git", L"--no-optional-locks", L"-c", L"core.fsmonitor=false", L"-c",
                               L"diff.autoRefreshIndex=false", L"-C", cwd->wstring()});
    std::error_code ec;
    if (!std::filesystem::is_directory(*cwd, ec))
        return protocol::fail(req.id, ErrorCode::kNotFound, "not a folder");
    const auto r = run_tool(args, *cwd, long_running ? kWorktreeTimeout : kGitTimeout);
    if (!r.started)
        return protocol::fail(req.id, ErrorCode::kSpawnFailed, "git was not found on PATH");
    if (r.timed_out)
        return protocol::fail(req.id, ErrorCode::kInternal, "git timed out");
    // stdout and stderr apart: callers parse only stdout and show stderr when it failed.
    std::string out = r.output;
    std::string err = r.error;
    if (out.size() > kMaxGitRunBytes)
        out.resize(kMaxGitRunBytes);
    if (err.size() > kMaxGitErrorBytes)
        err.resize(kMaxGitErrorBytes);
    return protocol::ok(
        req.id, {{"exit_code", r.exit_code}, {"output", valid_utf8(out)}, {"error", valid_utf8(err)}});
}

Response SessionManager::do_worktree_add(const Request& req) {
    const std::string repo = req.params.value("repo", "");
    const std::string branch = req.params.value("branch", "");
    std::string path = req.params.value("path", "");
    if (repo.empty() || branch.empty() || has_control_chars(branch) || branch.starts_with("-"))
        return bad(req, "repo and branch are required; branch may not start with '-'");
    const std::filesystem::path repo_path(win::to_wide(repo));
    std::error_code ec;
    if (!std::filesystem::is_directory(repo_path, ec))
        return protocol::fail(req.id, ErrorCode::kBadRequest, "repo folder does not exist: " + repo);
    if (path.empty()) {
#ifdef _WIN32
        wchar_t home[MAX_PATH]{};
        const DWORD n = ::GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        const auto base = std::filesystem::path(std::wstring(home, n)) / L".agentdesk" / L"worktrees";
#else
        const auto base = platform::home_dir() / ".agentdesk" / "worktrees";
#endif
        const auto p = default_worktree_path(base, repo_path, branch).u8string();
        path.assign(p.begin(), p.end());
    }
    const std::wstring wbranch = win::to_wide(branch);
    const std::wstring wpath = win::to_wide(path);
    // A branch that exists is checked out; a new one starts from HEAD. Existence is asked of git, never read
    // from its (translated) messages.
    const auto known = run_tool(
        {L"git", L"-C", repo_path.wstring(), L"show-ref", L"--verify", L"--quiet", L"refs/heads/" + wbranch},
        repo_path, kGitTimeout);
    if (!known.started)
        return protocol::fail(req.id, ErrorCode::kSpawnFailed, "git was not found on PATH");
    const bool exists = !known.timed_out && known.exit_code == 0;
    std::vector<std::wstring> add = {L"git", L"-C", repo_path.wstring(), L"worktree", L"add"};
    if (exists)
        add.insert(add.end(), {wpath, wbranch});
    else
        add.insert(add.end(), {L"-b", wbranch, wpath, L"HEAD"});
    const auto r = run_tool(add, repo_path, kWorktreeTimeout);
    if (!r.started)
        return protocol::fail(req.id, ErrorCode::kSpawnFailed, "git was not found on PATH");
    if (r.timed_out)
        return protocol::fail(req.id, ErrorCode::kInternal, "git worktree add timed out");
    if (r.exit_code != 0) {
        const std::string why = r.message();
        return protocol::fail(req.id, ErrorCode::kBadRequest, why.empty() ? "git worktree add failed." : why);
    }
    return protocol::ok(req.id, {{"path", path}, {"branch", branch}, {"output", r.output}});
}

void SessionManager::deliver_pending() {
    struct Delivery {
        Session* session;
        std::string text;
    };
    std::vector<Delivery> out;
    if (updating_ || handed_over_)
        return;  // the sessions are frozen for a handover; the new host delivers
    {
        std::lock_guard lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        for (auto& [id, m] : sessions_) {
            if (!m->session)
                continue;
            const auto info = m->session->info();
            if (info.process_state != ProcessState::kRunning)
                continue;
            const bool has_source = !info.agent.source.empty();
            if (!m->pending_task.empty()) {
                // Wait for the agent to say it is ready (Idle); without any status source, give
                // it kTaskFallback to start.
                if ((has_source && info.agent.state == AgentState::kIdle) ||
                    (!has_source && now >= m->task_after) || now >= m->task_after + kTaskFallback * 2) {
                    out.push_back({m->session.get(), std::exchange(m->pending_task, {})});
                }
            }
            if (!m->pending_reports.empty() && !busy(info.agent.state) && m->pending_task.empty()) {
                for (auto& r : m->pending_reports)
                    out.push_back({m->session.get(), std::move(r)});
                m->pending_reports.clear();
            }
        }
        // Sessions are only destroyed outside mutex_ by prune_exited on request threads; the
        // send calls below are queued writes and do not block.
        for (const auto& d : out) {
            d.session->send(PasteInput{d.text});
            d.session->send(RawInput{"\r"});
        }
    }
}

Response SessionManager::do_wait(const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    const std::string until = req.params.value("until", "idle");
    if (until != "idle" && until != "blocked" && until != "exited" && until != "change")
        return bad(req, "until must be idle, blocked, exited or change");
    const auto timeout = std::chrono::milliseconds(
        std::clamp<long long>(req.params.value("timeout_ms", 600000LL), 0, 3600000LL));
    std::shared_ptr<Session> session;  // kept alive for this request (prune_exited)
    {
        std::lock_guard lock(mutex_);
        session = m->session;
    }
    if (!session)
        return protocol::ok(
            req.id, {{"reached", until == "exited"}, {"process_state", "Lost"}, {"agent_state", "Unknown"}});
    const auto first = session->info();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool seen_working = first.agent.state == AgentState::kWorking;
    for (;;) {
        const auto info = session->info();
        seen_working = seen_working || info.agent.state == AgentState::kWorking;
        bool reached = false;
        if (info.process_state == ProcessState::kExited || info.process_state == ProcessState::kFailed)
            reached = true;  // nothing more will happen
        else if (until == "idle")
            // Finished: idle/done after it has worked (or right away if it never reports working
            // but is already idle).
            reached = (info.agent.state == AgentState::kIdle || info.agent.state == AgentState::kDone) &&
                      (seen_working || first.agent.state != AgentState::kUnknown);
        else if (until == "blocked")
            reached = info.agent.state == AgentState::kBlocked;
        else if (until == "change")
            reached = info.agent.state != first.agent.state || info.process_state != first.process_state;
        if (reached || std::chrono::steady_clock::now() >= deadline || stopping_) {
            return protocol::ok(req.id, {{"reached", reached},
                                         {"process_state", to_string(info.process_state)},
                                         {"agent_state", to_string(info.agent.state)},
                                         {"agent_message", info.agent.message}});
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

Response SessionManager::do_label(const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    const auto it = req.params.find("labels");
    if (it == req.params.end() || !it->is_object())
        return bad(req, "labels must be an object");
    std::lock_guard lock(mutex_);
    for (const auto& [k, v] : it->items()) {
        if (!valid_label(k, v))
            return bad(req, "labels must be printable strings");
        if (v.get<std::string>().empty())
            m->record.labels.erase(k);
        else
            m->record.labels[k] = v.get<std::string>();
    }
    persist_locked();
    return protocol::ok(req.id, {{"labels", m->record.labels}});
}

// A sub-agent reports to the session that started it (or `to`): the text arrives in that
// agent's input, tagged with the sender, once it is not working or waiting for the user.
Response SessionManager::do_report(const Request& req) {
    std::optional<Response> err;
    const auto from = resolve(req, err);
    if (!from)
        return *err;
    const std::string text = req.params.value("text", "");
    if (text.empty() || text.size() > 64 * 1024 || text.find('\0') != std::string::npos)
        return bad(req, "text must be 1..64 KiB without NUL");
    std::lock_guard lock(mutex_);
    std::string to = req.params.value("to", "");
    if (to.empty())
        to = from->record.parent;
    if (to.empty())
        return protocol::fail(req.id, ErrorCode::kNotFound, "this session has no parent; pass \"to\"");
    const auto it = sessions_.find(to);
    if (it == sessions_.end() || !it->second->session)
        return protocol::fail(req.id, ErrorCode::kNotFound, "target session not found or not running: " + to);
    it->second->pending_reports.push_back("[from " + from->record.name + " " + from->record.id.substr(0, 8) +
                                          "] " + text);
    return protocol::ok(req.id, {{"queued", true}, {"to", to}});
}

void SessionManager::prune_exited() {
    if (updating_ || handed_over_)
        return;  // frozen sessions are being handed over
    std::vector<std::shared_ptr<Session>> doomed;
    {
        std::lock_guard lock(mutex_);
        std::vector<ManagedPtr> exited;
        for (const auto& [id, m] : sessions_) {
            if (m->session && m->session->info().process_state == ProcessState::kExited)
                exited.push_back(m);
        }
        if (exited.size() > kMaxExitedLive) {
            std::sort(exited.begin(), exited.end(), kOldestFirst);
            for (std::size_t i = 0; i + kMaxExitedLive < exited.size(); ++i) {
                exited[i]->subscribers.clear();
                exited[i]->controller.reset();
                doomed.push_back(std::move(exited[i]->session));  // metadata stays in the list
            }
        }
        // Metadata-only records are bounded too (same limit as sessions.json).
        std::vector<ManagedPtr> history_only;
        for (const auto& [id, m] : sessions_) {
            if (!m->session)
                history_only.push_back(m);
        }
        if (history_only.size() > kMaxRecords) {
            std::sort(history_only.begin(), history_only.end(), kOldestFirst);
            for (std::size_t i = 0; i + kMaxRecords < history_only.size(); ++i)
                sessions_.erase(history_only[i]->record.id);
        }
    }
    // Joins actor/writer threads outside mutex_ (see header locking rule), or leaves that to the
    // request still using the session, which drops its copy outside mutex_ too.
    doomed.clear();
}

Response SessionManager::do_hello(const Request& req) {
    const int version = req.params.value("version", 1);
    if (version != protocol::kProtocolVersion) {
        return protocol::fail(req.id, ErrorCode::kVersionMismatch, "host speaks protocol v1");
    }
    return protocol::ok(
        req.id, {{"version", protocol::kProtocolVersion},
                 {"host_instance_id", config_.host_instance_id},
                 {"host_version", config_.host_version},
                 {"host_pid", ::GetCurrentProcessId()},
                 // Self-update (design doc 7.4 OQ-09): the hosts whose sessions this one took
                 // over (a client keeps those sessions), and where it runs from (agentctl update).
                 {"succeeds", config_.succeeds},
                 {"host_exe", win::to_utf8(ipc::sibling_executable(L"agent-host.exe").native())},
                 {"capabilities",
                  {{"osc7501", config_.conpty.bundled}, {"bundled_conpty", config_.conpty.bundled}}}});
}

Response SessionManager::do_profile_list(const Request& req) {
    std::lock_guard lock(mutex_);
    Json list = Json::array();
    for (const auto& p : profiles_)
        list.push_back(to_json(p));
    return protocol::ok(req.id, {{"profiles", list}});
}

namespace {

// The labels of a session.create request; nullopt when one is not printable.
std::optional<std::map<std::string, std::string>> labels_param(const Json& params) {
    std::map<std::string, std::string> labels;
    if (const auto it = params.find("labels"); it != params.end() && it->is_object()) {
        for (const auto& [k, v] : it->items()) {
            if (!valid_label(k, v))
                return std::nullopt;
            labels[k] = v.get<std::string>();
        }
    }
    return labels;
}

// How a new session starts: the profile's program and arguments plus the request's extra
// arguments, the folder hook, the size, and the variables that let it reach agentctl.
SessionConfig new_session_config(const HostConfig& host, const Profile& profile, const Json& p,
                                 const std::string& session_id, const std::string& cwd) {
    SessionConfig sc;
    sc.api = host.conpty;
    sc.spec.executable = profile.executable;
    sc.spec.args = profile.args;
    for (const auto& a : p.value("args", Json::array()))
        sc.spec.args.push_back(win::to_wide(a.get<std::string>()));
    add_folder_hook(sc.spec);
    sc.spec.cwd = win::to_wide(cwd);  // never silently replaced (design doc 4.2)
    const int cols = std::clamp(p.value("cols", 120), 2, 1000);
    const int rows = std::clamp(p.value("rows", 40), 1, 500);
    sc.spec.size = {static_cast<short>(cols), static_cast<short>(rows)};
    sc.engine.cols = cols;
    sc.engine.rows = rows;
    sc.spec.env_overrides[L"AGENTDESK_SESSION_ID"] = win::to_wide(session_id);
    sc.spec.env_overrides[L"AGENTDESK_CTL"] = host.ctl_path.wstring();
    sc.spec.env_overrides[L"AGENTDESK_PIPE"] = host.pipe_name;
    return sc;
}

}  // namespace

SessionListener SessionManager::listener_for(const std::string& id) {
    SessionListener listener;
    listener.on_snapshot = [this, id](std::shared_ptr<const terminal::ScreenSnapshot> s) {
        fanout(id, std::move(s));
    };
    listener.on_info = [this, id](const SessionInfo& info) { on_session_info(id, info); };
    return listener;
}

Response SessionManager::do_create(const Request& req) {
    prune_exited();
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex_);
        std::erase_if(dedupe_,
                      [&](const auto& kv) { return now - kv.second.at > config_.request_dedupe_window; });
        if (const auto it = dedupe_.find(req.id); it != dedupe_.end())
            return it->second.response;  // retried request
    }
    const auto& p = req.params;
    const std::string profile_id = p.value("profile_id", "");
    const std::string cwd = p.value("cwd", "");
    if (profile_id.empty() || cwd.empty())
        return bad(req, "profile_id and cwd are required");
    std::optional<Profile> profile;
    {
        std::lock_guard lock(mutex_);
        for (const auto& pr : profiles_) {
            if (pr.id == profile_id)
                profile = pr;
        }
    }
    if (!profile)
        return protocol::fail(req.id, ErrorCode::kProfileInvalid, "unknown profile: " + profile_id);
    if (const auto problem = validate_profile(*profile))
        return protocol::fail(req.id, ErrorCode::kProfileInvalid, *problem);

    SessionRecord rec;
    rec.id = new_uuid();
    // A terminal has a name only when someone gives it one (agentctl run --name, a rename in the
    // UI); without one the UI shows it by its program. Never the profile's name.
    rec.name = p.value("name", std::string());
    rec.cwd = cwd;
    rec.profile_id = profile_id;
    rec.created_at = iso8601_now();
    rec.process_state = "Starting";
    rec.parent = p.value("parent", "");
    if (!rec.parent.empty()) {
        std::lock_guard lock(mutex_);
        if (!sessions_.contains(rec.parent))
            return protocol::fail(req.id, ErrorCode::kNotFound, "parent session not found: " + rec.parent);
    }
    auto labels = labels_param(p);
    if (!labels)
        return bad(req, "labels must be printable strings");
    rec.labels = std::move(*labels);
    const std::string task = p.value("task", "");
    if (task.size() > kMaxTaskBytes)
        return bad(req, "task larger than 64 KiB");

    const std::string id = rec.id;
    std::unique_ptr<Session> session;
    try {
        // Outside mutex_: the listeners lock it.
        session = Session::start(new_session_config(config_, *profile, p, id, cwd), listener_for(id));
    } catch (const std::exception& e) {
        auto r = protocol::fail(req.id, ErrorCode::kSpawnFailed, error_text(e));
        std::lock_guard lock(mutex_);
        dedupe_[req.id] = {now, r};
        return r;
    }
    auto m = std::make_shared<Managed>();
    m->record = rec;
    m->record.process_state = std::string(to_string(session->info().process_state));
    m->last_process_state = m->record.process_state;
    m->session = std::move(session);
    if (!task.empty()) {
        m->pending_task = task;
        m->task_after = std::chrono::steady_clock::now() + kTaskFallback;
    }
    Response r = protocol::ok(req.id, {{"session_id", id}, {"process_state", m->record.process_state}});
    std::lock_guard lock(mutex_);
    sessions_.emplace(id, m);
    dedupe_[req.id] = {now, r};
    persist_locked();
    return r;
}

SessionManager::ManagedPtr SessionManager::resolve(const Request& req, std::optional<Response>& error) {
    const std::string key = req.params.value("session", req.params.value("session_id", ""));
    if (key.empty()) {
        error = bad(req, "session is required");
        return nullptr;
    }
    std::lock_guard lock(mutex_);
    if (const auto it = sessions_.find(key); it != sessions_.end())
        return it->second;
    ManagedPtr found;
    int matches = 0;
    for (const auto& [id, m] : sessions_) {
        if (m->record.name == key) {
            found = m;
            ++matches;
        }
    }
    if (matches == 1)
        return found;
    error =
        matches == 0
            ? protocol::fail(req.id, ErrorCode::kNotFound, "no session named or with id " + key)
            : protocol::fail(req.id, ErrorCode::kAmbiguous,
                             std::to_string(matches) + " sessions are named " + key + "; use the session id");
    return nullptr;
}

Json SessionManager::describe(const Managed& m) const {
    Json j = to_json(m.record);
    j["controller"] = m.controller ? Json(*m.controller) : Json(nullptr);
    j["subscribers"] = m.subscribers.size();
    if (m.session) {
        const auto info = m.session->info();
        const auto snap = m.session->snapshot();
        j["process_state"] = to_string(info.process_state);
        j["pid"] = info.pid;
        j["exit_code"] = info.exit_code ? Json(*info.exit_code) : Json(nullptr);
        j["agent_state"] = to_string(info.agent.state);
        j["agent_source"] = info.agent.source;
        j["agent_message"] = info.agent.message;
        j["agent_kind"] = terminal::to_string(info.agent.kind);
        j["last_agent_state"] = to_string(info.agent.last_state);
        j["still_running_after_interrupt"] = info.still_running_after_interrupt;
        j["remaining_processes"] = info.remaining_processes;
        j["cols"] = snap->cols;
        j["rows"] = snap->rows;
        j["history_truncated"] = info.history_truncated;
        j["backpressure_waits"] = info.backpressure_waits;
        const auto fg = m.session->foreground();
        j["program"] = fg.program;
        j["program_argv"] = fg.argv;
        j["agent_app"] = info.agent.app;
        j["live_cwd"] = fg.cwd;  // the shell's folder after cd (empty: unknown, use cwd)
    } else {
        j["agent_state"] = "Unknown";
        j["history_only"] = true;  // Lost, or Exited and released to bound resources
    }
    return j;
}

Response SessionManager::do_list(const Request& req) {
    prune_exited();
    std::lock_guard lock(mutex_);
    Json list = Json::array();
    std::vector<ManagedPtr> ordered;
    for (const auto& [id, m] : sessions_)
        ordered.push_back(m);
    std::sort(ordered.begin(), ordered.end(), kOldestFirst);
    for (const auto& m : ordered)
        list.push_back(describe(*m));
    return protocol::ok(req.id, {{"sessions", list}, {"host_instance_id", config_.host_instance_id}});
}

Response SessionManager::do_get(const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    std::lock_guard lock(mutex_);
    return protocol::ok(req.id, describe(*m));
}

bool SessionManager::send_snapshot(ipc::Connection& conn, const std::string& session_id,
                                   const terminal::ScreenSnapshot& snap, bool droppable,
                                   std::string& snapshot_id) {
    snapshot_id = "snap-" + std::to_string(next_snapshot_id_++);
    const std::size_t row_bytes = static_cast<std::size_t>(std::max(snap.cols, 1)) * 32;
    const int rows_per_chunk = static_cast<int>(std::max<std::size_t>(1, kMaxSnapshotChunkBytes / row_bytes));
    const int chunk_count = std::max(1, (snap.rows + rows_per_chunk - 1) / rows_per_chunk);
    for (int i = 0; i < chunk_count; ++i) {
        protocol::Event e;
        e.event = "screen.snapshot";
        e.body = {{"session_id", session_id},
                  {"snapshot_id", snapshot_id},
                  {"chunk_index", i},
                  {"chunk_count", chunk_count},
                  {"meta", protocol::encode_snapshot_meta(snap)},
                  {"rows", protocol::encode_rows(snap, i * rows_per_chunk, (i + 1) * rows_per_chunk)}};
        if (!conn.send(protocol::serialize(e), droppable))
            return false;
    }
    return true;
}

Response SessionManager::do_attach(const ipc::ConnectionPtr& conn, const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    std::lock_guard lock(mutex_);
    if (!m->session)
        return protocol::fail(req.id, ErrorCode::kNotFound, "session is Lost (from a previous host)");
    // Snapshot and subscription under the same lock as fan-out: deltas start exactly after seq S.
    auto snap = m->session->snapshot();
    const bool want = req.params.value("want_control", false);
    const bool take = req.params.value("take_control", false);
    if (take || (want && !m->controller))
        m->controller = conn->id();
    m->subscribers[conn->id()] = Subscriber{conn, snap, false};
    std::string snapshot_id;
    if (!send_snapshot(*conn, m->record.id, *snap, false, snapshot_id)) {
        m->subscribers[conn->id()].needs_snapshot = true;
    }
    Json result = describe(*m);
    result["is_controller"] = m->controller == conn->id();
    result["snapshot_id"] = snapshot_id;
    result["snapshot_meta"] = protocol::encode_snapshot_meta(*snap);
    return protocol::ok(req.id, result);
}

Response SessionManager::do_detach(const ipc::ConnectionPtr& conn, const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    std::lock_guard lock(mutex_);
    m->subscribers.erase(conn->id());
    if (m->controller == conn->id())
        m->controller.reset();
    return protocol::ok(req.id, {{"detached", true}});
}

Response SessionManager::do_input(const ipc::ConnectionPtr& conn, const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    std::shared_ptr<Session> session;  // kept alive for this request (prune_exited)
    {
        std::lock_guard lock(mutex_);
        if (!m->session)
            return protocol::fail(req.id, ErrorCode::kNotFound, "session is Lost");
        // One input controller per session; others are read-only (design doc 3.7, QA-14).
        if (m->controller && *m->controller != conn->id()) {
            return protocol::fail(req.id, ErrorCode::kNotController, "another client controls this session");
        }
        session = m->session;
    }
    const auto& p = req.params;
    const std::string kind = p.value("kind", "text");
    if (kind == "text") {
        session->send(
            TextInput{utf8_to_u32(p.value("text", "")), mods_from_json(p.value("mods", Json::object()))});
    } else if (kind == "key") {
        const auto key = key_from_name(p.value("key", ""));
        if (!key)
            return bad(req, "unknown key");
        session->send(KeyInput{*key, mods_from_json(p.value("mods", Json::object()))});
    } else if (kind == "paste") {
        const std::string text = p.value("text", "");
        if (text.size() > 1024 * 1024 || text.find('\0') != std::string::npos)
            return bad(req, "paste rejected (NUL or > 1 MiB)");
        session->send(PasteInput{text});
    } else if (kind == "raw") {
        session->send(RawInput{p.value("bytes", "")});
    } else if (kind == "mouse") {
        const std::string action = p.value("action", "press");
        const int button = p.value("button", 0);
        if (button < 0 || button > 5 || (action != "press" && action != "release" && action != "move"))
            return bad(req, "bad mouse event");
        session->send(MouseInput{std::clamp(p.value("row", 0), 0, 10000),
                                 std::clamp(p.value("col", 0), 0, 10000), button, mouse_action_from(action),
                                 mods_from_json(p.value("mods", Json::object()))});
    } else {
        return bad(req, "unknown input kind");
    }
    return protocol::ok(req.id, {{"accepted", true}});
}

Response SessionManager::do_resize(const ipc::ConnectionPtr& conn, const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    std::lock_guard lock(mutex_);
    if (!m->session)
        return protocol::fail(req.id, ErrorCode::kNotFound, "session is Lost");
    if (m->controller && *m->controller != conn->id()) {
        return protocol::fail(req.id, ErrorCode::kNotController, "only the controller sets the size");
    }
    m->session->resize(req.params.value("rows", 40), req.params.value("cols", 120));
    return protocol::ok(req.id, {{"accepted", true}});
}

Response SessionManager::do_stop(const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    const std::string mode = req.params.value("mode", "interrupt");
    std::lock_guard lock(mutex_);
    if (!m->session)
        return protocol::fail(req.id, ErrorCode::kNotFound, "session is Lost");
    if (mode == "interrupt")
        m->session->interrupt();
    else if (mode == "force")
        m->session->force_stop();
    else
        return bad(req, "mode must be interrupt or force");
    return protocol::ok(req.id, {{"mode", mode}, {"session_id", m->record.id}});
}

Response SessionManager::do_snapshot(const ipc::ConnectionPtr& conn, const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    std::lock_guard lock(mutex_);
    if (!m->session)
        return protocol::fail(req.id, ErrorCode::kNotFound, "session is Lost");
    auto snap = m->session->snapshot();
    std::string snapshot_id;
    if (!send_snapshot(*conn, m->record.id, *snap, false, snapshot_id)) {
        return protocol::fail(req.id, ErrorCode::kOverloaded, "client send queue is full", true);
    }
    if (auto it = m->subscribers.find(conn->id()); it != m->subscribers.end()) {
        it->second.last = snap;  // resync point for later deltas
        it->second.needs_snapshot = false;
    }
    Json meta = protocol::encode_snapshot_meta(*snap);
    meta["snapshot_id"] = snapshot_id;
    return protocol::ok(req.id, meta);
}

Response SessionManager::do_history(const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    std::shared_ptr<Session> session;  // kept alive for this request (prune_exited)
    {
        std::lock_guard lock(mutex_);
        session = m->session;
    }
    if (!session)
        return protocol::fail(req.id, ErrorCode::kNotFound, "session is Lost");
    const auto from = req.params.value("from_line_id", static_cast<std::uint64_t>(0));
    const auto max = std::min<std::size_t>(req.params.value("max_lines", kMaxHistoryLines), kMaxHistoryLines);
    // Blocking call into the actor: must not hold mutex_ (see header).
    const bool tail = req.params.value("tail", false);
    const auto lines = tail ? session->history_tail(max) : session->history(from, max);
    const auto info = session->info();
    Json out = Json::array();
    for (const auto& line : lines)
        out.push_back({{"line_id", line.line_id}, {"text", line_text(line)}, {"wrapped", line.wrapped}});
    return protocol::ok(req.id, {{"lines", out},
                                 {"first_available_line_id", info.first_history_line_id},
                                 {"truncated", info.history_truncated}});
}

Response SessionManager::do_state_report(const Request& req) {
    std::optional<Response> err;
    const auto m = resolve(req, err);
    if (!m)
        return *err;
    const auto& p = req.params;
    const std::string source = p.value("source", "");
    if (source.empty() || source.size() > 64 || has_control_chars(source))
        return bad(req, "source must be 1..64 printable characters");
    if (source.rfind("agentdesk:", 0) == 0)
        return bad(req, "the agentdesk: prefix is reserved");
    const auto state = parse_agent_state(p.value("state", ""));
    if (!state)
        return bad(req, "state must be idle|working|done|blocked|error|unknown");
    if (!p.contains("seq") || !p["seq"].is_number_unsigned())
        return bad(req, "seq must be an unsigned integer");
    const std::string message = p.value("message", "");
    if (message.size() > 2048 || has_control_chars(message))
        return bad(req, "message must be one line of at most 2048 bytes");
    terminal::StatusKind kind = terminal::StatusKind::kNone;
    const std::string k = p.value("kind", "");
    if (k == "permission")
        kind = terminal::StatusKind::kPermission;
    else if (k == "question")
        kind = terminal::StatusKind::kQuestion;
    else if (k == "auth")
        kind = terminal::StatusKind::kAuth;
    else if (!k.empty())
        return bad(req, "kind must be permission|question|auth");
    std::lock_guard lock(mutex_);
    if (!m->session)
        return protocol::fail(req.id, ErrorCode::kNotFound, "session is Lost");
    m->session->report_state(source, p["seq"].get<std::uint64_t>(), *state, kind, message);
    return protocol::ok(req.id, {{"accepted", true}});
}

Response SessionManager::do_shutdown(const Request& req) {
    const bool stop_sessions = req.params.value("stop_sessions", false);
    const auto running = running_session_count();
    if (running > 0 && !stop_sessions) {
        return protocol::fail(req.id, ErrorCode::kActiveSessions,
                              std::to_string(running) + " sessions are still running");
    }
    stop_all();
    if (config_.on_shutdown)
        config_.on_shutdown();
    return protocol::ok(req.id, {{"stopped_sessions", running}});
}

std::size_t SessionManager::running_session_count() const {
    std::lock_guard lock(mutex_);
    std::size_t n = 0;
    for (const auto& [id, m] : sessions_) {
        if (!m->session)
            continue;
        const auto s = m->session->info().process_state;
        if (s == ProcessState::kRunning || s == ProcessState::kStopping || s == ProcessState::kStarting)
            ++n;
    }
    return n;
}

void SessionManager::stop_all() {
    std::lock_guard lock(mutex_);
    for (auto& [id, m] : sessions_) {
        if (m->session && m->session->info().process_state != ProcessState::kExited)
            m->session->force_stop();
    }
}

void SessionManager::fanout(const std::string& id, std::shared_ptr<const terminal::ScreenSnapshot> snap) {
    std::lock_guard lock(mutex_);
    const auto it = sessions_.find(id);
    if (it == sessions_.end())
        return;
    auto& m = *it->second;
    for (auto sit = m.subscribers.begin(); sit != m.subscribers.end();) {
        auto conn = sit->second.conn.lock();
        if (!conn || conn->closed()) {
            sit = m.subscribers.erase(sit);
            continue;
        }
        auto& sub = sit->second;
        const bool full = sub.needs_snapshot || !sub.last || sub.last->generation != snap->generation ||
                          sub.last->rows != snap->rows || sub.last->cols != snap->cols;
        if (full) {
            std::string snapshot_id;
            if (send_snapshot(*conn, id, *snap, true, snapshot_id)) {
                sub.last = snap;
                sub.needs_snapshot = false;
            } else {
                sub.needs_snapshot = true;  // resent from the newest snapshot once the queue drains
            }
        } else if (snap->seq != sub.last->seq) {
            Json rows = Json::array();
            for (const int r : changed_rows(*sub.last, *snap)) {
                for (auto& row : protocol::encode_rows(*snap, r, r + 1))
                    rows.push_back(std::move(row));
            }
            protocol::Event e;
            e.event = "screen.delta";
            e.body = {{"session_id", id},
                      {"generation", snap->generation},
                      {"base_seq", sub.last->seq},
                      {"seq", snap->seq},
                      {"meta", protocol::encode_snapshot_meta(*snap)},
                      {"rows", std::move(rows)}};
            if (conn->send(protocol::serialize(e), true)) {
                sub.last = snap;
            } else {
                sub.needs_snapshot = true;  // design doc 3.3: drop deltas, resync with a snapshot
            }
        }
        ++sit;
    }
}

void SessionManager::on_session_info(const std::string& id, const SessionInfo& info) {
    std::lock_guard lock(mutex_);
    const auto it = sessions_.find(id);
    if (it == sessions_.end())
        return;
    auto& m = *it->second;
    const std::string state(to_string(info.process_state));
    if (state != m.last_process_state) {
        m.last_process_state = state;
        m.record.process_state = state;
        m.record.exit_code = info.exit_code;
        persist_locked();
    }
    protocol::Event e;
    e.event = "session.state";
    e.body = {{"session_id", id},
              {"process_state", state},
              {"exit_code", info.exit_code ? Json(*info.exit_code) : Json(nullptr)},
              {"agent_state", to_string(info.agent.state)},
              {"agent_source", info.agent.source},
              {"agent_kind", terminal::to_string(info.agent.kind)},
              {"agent_message", info.agent.message},
              {"last_agent_state", to_string(info.agent.last_state)},
              {"still_running_after_interrupt", info.still_running_after_interrupt},
              {"remaining_processes", info.remaining_processes}};
    const std::string payload = protocol::serialize(e);
    for (auto& [cid, sub] : m.subscribers) {
        if (auto conn = sub.conn.lock())
            conn->send(payload);  // state changes are reliable
    }
}

}  // namespace agentdesk::host
