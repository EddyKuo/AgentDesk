// agentctl: command-line client of agent-host (design doc 3.8). The entry point and what differs
// per system are in cli_win.cpp / cli_posix.cpp.
// Exit codes: 0 ok, 2 bad arguments, 3 host unreachable, 4 session not found or ambiguous,
// 5 operation refused, 6 start failed, 7 timeout. With --json, stdout carries only the result.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "agentdesk/ipc/host_launcher.hpp"
#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/crt.hpp"
#include "agentdesk/platform/system.hpp"
#include "agentdesk/platform/text.hpp"
#include "agentdesk/protocol/messages.hpp"
#include "agentdesk/ui/updates.hpp"
#include "cli_os.hpp"

namespace {

namespace ipc = agentdesk::ipc;
namespace protocol = agentdesk::protocol;
namespace platform = agentdesk::platform;
using protocol::ErrorCode;
using protocol::Json;
using namespace std::chrono_literals;

enum Exit : int {
    kOk = 0,
    kBadArgs = 2,
    kNoHost = 3,
    kNotFound = 4,
    kRefused = 5,
    kStartFailed = 6,
    kTimeout = 7
};

struct Args {
    std::string command;
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;  // --name value
    std::vector<std::string> flags;              // --json, --enter ...
    std::vector<std::string> passthrough;        // after "--"
    std::vector<std::string> labels;             // --label k=v (repeatable)
    [[nodiscard]] bool has(const std::string& f) const {
        return std::find(flags.begin(), flags.end(), f) != flags.end();
    }
    [[nodiscard]] std::optional<std::string> opt(const std::string& k) const {
        const auto it = options.find(k);
        return it == options.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
};

const std::vector<std::string> kValueOptions = {
    "--profile", "--name",   "--cwd",     "--lines", "--file", "--text",       "--source",
    "--state",   "--kind",   "--message", "--seq",   "--pipe", "--timeout-ms", "--key",
    "--task",    "--parent", "--until",   "--to",    "--from", "--version",    "--exe"};

Args parse_args(int argc, wchar_t** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string s = platform::to_utf8(argv[i]);
        if (s == "--") {
            for (++i; i < argc; ++i)
                a.passthrough.push_back(platform::to_utf8(argv[i]));
            break;
        }
        if (s == "--label") {
            if (i + 1 >= argc)
                throw std::invalid_argument("missing value for --label");
            a.labels.push_back(platform::to_utf8(argv[++i]));
            continue;
        }
        if (s.rfind("--", 0) == 0) {
            if (std::find(kValueOptions.begin(), kValueOptions.end(), s) != kValueOptions.end()) {
                if (i + 1 >= argc)
                    throw std::invalid_argument("missing value for " + s);
                a.options[s] = platform::to_utf8(argv[++i]);
            } else {
                a.flags.push_back(s);
            }
        } else if (a.command.empty()) {
            a.command = s;
        } else {
            a.positional.push_back(s);
        }
    }
    return a;
}

void out(const std::string& s) {
    // stdout gets UTF-8 bytes both on a console and when redirected.
    std::cout << s;
    std::cout.flush();
}

void diag(const std::string& s) {
    std::cerr << "agentctl: " << s << '\n';
}

int exit_for(ErrorCode c) {
    switch (c) {
        case ErrorCode::kBadRequest:
            return kBadArgs;
        case ErrorCode::kNotFound:
        case ErrorCode::kAmbiguous:
            return kNotFound;
        case ErrorCode::kProfileInvalid:
        case ErrorCode::kSpawnFailed:
            return kStartFailed;
        default:
            return kRefused;
    }
}

struct Ctx {
    Args args;
    bool json = false;
    std::chrono::milliseconds timeout{5000};  // design doc 3.3: CLI RPC timeout 5 s
    std::unique_ptr<ipc::PipeClient> client;
};

// Runs one RPC; prints diagnostics and returns an exit code on failure.
std::optional<int> call(Ctx& ctx, const std::string& method, Json params, Json& result) {
    const auto resp = ctx.client->call(method, std::move(params), ctx.timeout);
    if (!resp) {
        diag(method + ": no response within " + std::to_string(ctx.timeout.count()) +
             " ms (the operation may still have run; check with `agentctl list`)");
        return kTimeout;
    }
    if (resp->error) {
        diag(std::string(protocol::to_string(*resp->error)) + ": " + resp->error_message);
        if (ctx.json) {
            out(Json{{"error",
                      {{"code", protocol::to_string(*resp->error)}, {"message", resp->error_message}}}}
                    .dump() +
                "\n");
        }
        return exit_for(*resp->error);
    }
    result = resp->result.value_or(Json::object());
    return std::nullopt;
}

// An environment variable; "" when unset.
std::string env(const char* name) {
    return platform::env_var(name).value_or(std::string());
}

Json labels_json(const std::vector<std::string>& list) {
    Json j = Json::object();
    for (const auto& kv : list) {
        const auto eq = kv.find('=');
        if (eq == std::string::npos || eq == 0)
            throw std::invalid_argument("labels are key=value: " + kv);
        j[kv.substr(0, eq)] = kv.substr(eq + 1);
    }
    return j;
}

std::string session_arg(const Ctx& ctx) {
    if (ctx.args.positional.empty())
        throw std::invalid_argument("session name or id is required");
    return ctx.args.positional[0];
}

// A whole file's bytes; nullopt when it cannot be opened.
std::optional<std::string> read_file(const std::string& path) {
    std::ifstream in(std::filesystem::path(platform::to_wide(path)), std::ios::binary);
    if (!in)
        return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// With --json the host's result, else `text`.
void print_result(const Ctx& ctx, const Json& result, const std::string& text) {
    out(ctx.json ? result.dump() + "\n" : text);
}

int cmd_run(Ctx& ctx) {
    const auto profile = ctx.args.opt("--profile");
    if (!profile)
        throw std::invalid_argument("--profile is required");
    Json params{
        {"profile_id", *profile},
        {"cwd", ctx.args.opt("--cwd").value_or(platform::to_utf8(std::filesystem::current_path().native()))},
        {"args", ctx.args.passthrough}};
    if (const auto n = ctx.args.opt("--name"))
        params["name"] = *n;
    if (const auto t = ctx.args.opt("--task"))
        params["task"] = *t;  // first message, sent once the agent is ready
    // Run from inside a session: that session is the parent (a sub-agent), unless --no-parent.
    if (const auto p = ctx.args.opt("--parent"))
        params["parent"] = *p;
    else if (const auto self = env("AGENTDESK_SESSION_ID"); !self.empty() && !ctx.args.has("--no-parent"))
        params["parent"] = self;
    if (!ctx.args.labels.empty())
        params["labels"] = labels_json(ctx.args.labels);
    Json r;
    if (const auto e = call(ctx, "session.create", params, r))
        return *e;
    print_result(ctx, r, r.value("session_id", "") + "\n");
    return kOk;
}

int cmd_list(Ctx& ctx) {
    Json r;
    if (const auto e = call(ctx, "session.list", Json::object(), r))
        return *e;
    if (ctx.json) {
        out(r.dump() + "\n");
        return kOk;
    }
    std::ostringstream s;
    s << "ID                                    NAME                 PROCESS   AGENT     CWD\n";
    for (const auto& j : r.value("sessions", Json::array())) {
        auto pad = [](std::string v, std::size_t w) {
            if (v.size() < w)
                v.append(w - v.size(), ' ');
            return v + ' ';
        };
        s << pad(j.value("id", ""), 37) << pad(j.value("name", ""), 20)
          << pad(j.value("process_state", ""), 9) << pad(j.value("agent_state", ""), 9) << j.value("cwd", "")
          << '\n';
    }
    out(s.str());
    return kOk;
}

// timeline [<session>] [--since 15m|1h|4h|24h|<minutes>]: when each session's agent and process
// state changed (local time), oldest session first; --json gives the host's answer as it is.
int cmd_timeline(Ctx& ctx) {
    Json params = Json::object();
    if (!ctx.args.positional.empty())
        params["session"] = ctx.args.positional[0];
    const std::int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    if (const auto since = ctx.args.opt("--since")) {
        long long minutes = std::atoll(since->c_str());
        if (!since->empty() && since->back() == 'h')
            minutes *= 60;
        if (minutes <= 0)
            throw std::invalid_argument("--since takes minutes or hours: 15m, 1h, 24h");
        params["since_ms"] = now - minutes * 60000;
    }
    Json r;
    if (const auto e = call(ctx, "session.timeline", params, r))
        return *e;
    if (ctx.json) {
        out(r.dump() + "\n");
        return kOk;
    }
    std::ostringstream s;
    for (const auto& j : r.value("sessions", Json::array())) {
        s << j.value("id", "").substr(0, 8) << "  " << j.value("name", "") << (j.value("ended", false) ? "  (ended)" : "")
          << (j.value("truncated", false) ? "  (earlier history dropped)" : "") << '\n';
        for (const auto& m : j.value("marks", Json::array())) {
            if (!m.is_array() || m.size() < 3 || !m[0].is_number_integer())
                continue;
            const auto t = static_cast<std::time_t>(m[0].get<std::int64_t>() / 1000);
            std::tm tm{};
            ::localtime_s(&tm, &t);
            char when[32];
            std::strftime(when, sizeof when, "%m-%d %H:%M:%S", &tm);
            s << "  " << when << "  " << m[1].get<std::string>() << " / " << m[2].get<std::string>() << '\n';
        }
    }
    out(s.str());
    return kOk;
}

// Collects the chunked "screen.snapshot" events for one snapshot id.
struct SnapshotCollector {
    std::mutex mu;
    std::condition_variable cv;
    std::map<std::string, std::vector<Json>> chunks;
    std::map<std::string, int> expected;
};

int cmd_read(Ctx& ctx) {
    const std::string session = session_arg(ctx);
    const int lines = std::clamp(std::stoi(ctx.args.opt("--lines").value_or("100")), 1, 200);
    auto collector = std::make_shared<SnapshotCollector>();
    ctx.client->set_event_handler([collector](protocol::Event e) {
        if (e.event != "screen.snapshot")
            return;
        std::lock_guard lock(collector->mu);
        const std::string id = e.body.value("snapshot_id", "");
        collector->expected[id] = e.body.value("chunk_count", 1);
        collector->chunks[id].push_back(std::move(e.body));
        collector->cv.notify_all();
    });
    Json hist;
    if (const auto e =
            call(ctx, "history.read", {{"session", session}, {"tail", true}, {"max_lines", lines}}, hist))
        return *e;
    Json meta;
    if (const auto e = call(ctx, "screen.snapshot", {{"session", session}}, meta))
        return *e;
    const std::string snapshot_id = meta.value("snapshot_id", "");
    agentdesk::terminal::ScreenSnapshot snap;
    snap.rows = meta.value("rows", 0);
    snap.cols = meta.value("cols", 0);
    snap.cells.resize(static_cast<std::size_t>(snap.rows) * static_cast<std::size_t>(snap.cols));
    {
        std::unique_lock lock(collector->mu);
        if (!collector->cv.wait_for(lock, ctx.timeout, [&] {
                return collector->expected.contains(snapshot_id) &&
                       static_cast<int>(collector->chunks[snapshot_id].size()) >=
                           collector->expected[snapshot_id];
            })) {
            diag("screen snapshot did not arrive in time");
            return kTimeout;
        }
        for (const auto& chunk : collector->chunks[snapshot_id])
            protocol::decode_rows_into(chunk.at("rows"), snap);
    }
    std::vector<std::string> text;
    for (const auto& l : hist.value("lines", Json::array()))
        text.push_back(l.value("text", ""));
    std::vector<std::string> screen;
    for (int r = 0; r < snap.rows; ++r)
        screen.push_back(snap.row_text(r));
    while (!screen.empty() && screen.back().empty())
        screen.pop_back();
    for (auto& s : screen)
        text.push_back(std::move(s));
    if (text.size() > static_cast<std::size_t>(lines))
        text.erase(text.begin(), text.end() - lines);
    if (ctx.json) {
        out(Json{{"lines", text}, {"history_truncated", hist.value("truncated", false)}}.dump() + "\n");
    } else {
        std::string all;
        for (const auto& t : text)
            all += t + "\n";
        out(all);
    }
    return kOk;
}

int cmd_send(Ctx& ctx) {
    const std::string session = session_arg(ctx);
    // One session.input; `input` holds the kind and its fields.
    const auto send = [&](Json input) {
        input["session"] = session;
        Json r;
        return call(ctx, "session.input", std::move(input), r);
    };
    if (const auto file = ctx.args.opt("--file")) {
        const auto text = read_file(*file);
        if (!text)
            throw std::invalid_argument("cannot read " + *file);
        if (const auto e = send({{"kind", "paste"}, {"text", *text}}))
            return *e;
    } else if (const auto text = ctx.args.opt("--text")) {
        if (const auto e = send({{"kind", "text"}, {"text", *text}}))
            return *e;
    } else if (const auto key = ctx.args.opt("--key")) {
        if (const auto e = send({{"kind", "key"}, {"key", *key}}))
            return *e;
    } else if (!ctx.args.has("--enter")) {
        throw std::invalid_argument("send needs --text, --file, --key or --enter");
    }
    // Enter only when asked (design doc 3.8): the agent may be waiting for a confirmation.
    if (ctx.args.has("--enter")) {
        if (const auto e = send({{"kind", "key"}, {"key", "enter"}}))
            return *e;
    }
    if (ctx.json)
        out(Json{{"sent", true}}.dump() + "\n");
    return kOk;
}

int cmd_stop(Ctx& ctx) {
    const std::string session = session_arg(ctx);
    const bool force = ctx.args.has("--force");
    if (!force && !ctx.args.has("--interrupt"))
        throw std::invalid_argument("stop needs --interrupt or --force");
    Json r;
    if (const auto e =
            call(ctx, "session.stop", {{"session", session}, {"mode", force ? "force" : "interrupt"}}, r))
        return *e;
    print_result(ctx, r, force ? "force stop sent\n" : "interrupt sent\n");
    return kOk;
}

int cmd_report_state(Ctx& ctx) {
    const std::string session = session_arg(ctx);
    const auto source = ctx.args.opt("--source");
    const auto state = ctx.args.opt("--state");
    const auto seq = ctx.args.opt("--seq");
    if (!source || !state || !seq)
        throw std::invalid_argument("report-state needs --source, --state and --seq");
    Json params{{"session", session}, {"source", *source}, {"state", *state}, {"seq", std::stoull(*seq)}};
    if (const auto k = ctx.args.opt("--kind"))
        params["kind"] = *k;
    if (const auto m = ctx.args.opt("--message"))
        params["message"] = *m;
    Json r;
    if (const auto e = call(ctx, "state.report", params, r))
        return *e;
    if (ctx.json)
        out(r.dump() + "\n");
    return kOk;
}

// Blocks until the agent finished (idle), asks for the user (blocked), exits, or changes.
int cmd_wait(Ctx& ctx) {
    const std::string session = session_arg(ctx);
    const auto wait_ms = std::stoll(ctx.args.opt("--timeout-ms").value_or("600000"));
    ctx.timeout = std::chrono::milliseconds(wait_ms + 10000);
    Json r;
    if (const auto e = call(ctx, "session.wait",
                            {{"session", session},
                             {"until", ctx.args.opt("--until").value_or("idle")},
                             {"timeout_ms", wait_ms}},
                            r))
        return *e;
    if (ctx.json)
        out(r.dump() + "\n");
    else
        out(r.value("agent_state", "") + " (" + r.value("process_state", "") + ")\n");
    return r.value("reached", false) ? kOk : kTimeout;
}

// From inside a sub-agent: report text to the session that started it (or --to).
int cmd_report(Ctx& ctx) {
    std::string from = ctx.args.opt("--from").value_or(env("AGENTDESK_SESSION_ID"));
    if (from.empty())
        throw std::invalid_argument("report runs inside an AgentDesk session (or pass --from)");
    std::string text;
    if (const auto f = ctx.args.opt("--file")) {
        text = read_file(*f).value_or(std::string());  // unreadable: "report needs text" below
    } else if (const auto t = ctx.args.opt("--text")) {
        text = *t;
    } else {
        for (const auto& p : ctx.args.positional)
            text += (text.empty() ? "" : " ") + p;
    }
    if (text.empty())
        throw std::invalid_argument("report needs text (argument, --text or --file)");
    Json params{{"session", from}, {"text", text}};
    if (const auto to = ctx.args.opt("--to"))
        params["to"] = *to;
    Json r;
    if (const auto e = call(ctx, "session.report", params, r))
        return *e;
    print_result(ctx, r, "report queued for " + r.value("to", "") + "\n");
    return kOk;
}

int cmd_label(Ctx& ctx) {
    const std::string session = session_arg(ctx);
    std::vector<std::string> kv(ctx.args.positional.begin() + 1, ctx.args.positional.end());
    kv.insert(kv.end(), ctx.args.labels.begin(), ctx.args.labels.end());
    Json r;
    if (const auto e = call(ctx, "session.label", {{"session", session}, {"labels", labels_json(kv)}}, r))
        return *e;
    out(r.dump() + "\n");
    return kOk;
}

const char* kSkill = R"(AgentDesk: running helper agents (for coding agents running inside AgentDesk)

You run in an AgentDesk terminal (AGENTDESK_SESSION_ID is set). agentctl is in AGENTDESK_CTL.
Sessions you start are your sub-agents: they show under you in the sidebar and keep running
when the window closes.

  Start a helper with a first task (it is sent once the helper is ready):
    agentctl run --profile claude-native --name reviewer --cwd <dir> --task "review the diff in <dir> and report problems"
  See what runs and what state it is in:
    agentctl list --json
  Wait until it finished, needs a person, or exited (exit code 7 on timeout):
    agentctl wait reviewer --until idle --timeout-ms 600000
  Read its screen and recent output:
    agentctl read reviewer --lines 80
  Send it a message (add --enter to submit):
    agentctl send reviewer --text "also check the tests" --enter
  From inside a helper, report back to the agent that started you. The text arrives in that
  agent's input, tagged "[from <name> <id>]", once it is not busy:
    agentctl report "found 3 problems: ..."
  Tag sessions:
    agentctl label reviewer role=review issue=123
  Stop it:
    agentctl stop reviewer --interrupt

Profiles: agentctl run needs a profile id; ask the person or look at profiles.json in AgentDesk's data folder
(%LOCALAPPDATA%\AgentDesk on Windows, ~/Library/Application Support/AgentDesk on macOS).
Never send prompts to agents the person did not ask you to start.
)";

int cmd_shutdown(Ctx& ctx) {
    Json r;
    if (const auto e = call(ctx, "host.shutdown", {{"stop_sessions", ctx.args.has("--stop-sessions")}}, r))
        return *e;
    print_result(ctx, r, "host shutting down\n");
    return kOk;
}

// "agentctl hook": the status hook for agent CLIs (Codex CLI, Claude Code), in place of a script.
// Reads the hook event JSON from stdin and reports it like `report-state` (design doc 3.6).
// Contract with the CLI: print NOTHING and always exit 0. Printing would be parsed by the CLI
// (Codex Stop/Interrupt accept only JSON) and a decision on PermissionRequest would bypass the
// person's approval prompt, which AgentDesk must never do. Never starts a host; gives up fast.
int run_hook() {
    const std::string input = agentdesk::cli::os::read_stdin(1024 * 1024);
    const std::string session = env("AGENTDESK_SESSION_ID");
    const std::string pipe = env("AGENTDESK_PIPE");
    if (session.empty() || pipe.empty())
        return kOk;  // not inside an AgentDesk session
    const auto evt = Json::parse(input, nullptr, false);
    if (!evt.is_object())
        return kOk;
    const std::string name = evt.value("hook_event_name", "");
    std::string state;
    std::string kind;
    if (name == "UserPromptSubmit" || name == "PostToolUse") {
        state = "working";
    } else if (name == "PermissionRequest") {
        state = "blocked";
        kind = "permission";
    } else if (name == "Stop" || name == "Interrupt") {  // Interrupt: Codex, the person stopped the turn
        state = "idle";
    }
    if (state.empty())
        return kOk;
    // seq must grow across restarts: milliseconds since the epoch (design doc 3.6).
    const auto seq = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                    std::chrono::system_clock::now().time_since_epoch())
                                                    .count());
    Json params{
        {"session", session}, {"source", "agentdesk-hook"}, {"state", state}, {"seq", seq}};
    if (!kind.empty())
        params["kind"] = kind;
    try {
        auto client = ipc::PipeClient::connect(pipe, 1500ms);
        if (client)
            client->call("state.report", params, 1500ms);
    } catch (...) {
        // the host may be restarting; the hook must not fail the agent
    }
    return kOk;
}

// stdin/stdout <-> the local host pipe, byte for byte. Runs on a remote machine under ssh:
// "ssh <machine> agentctl bridge" lets another PC's AgentDesk use this machine's agent-host.
// stdout carries only protocol bytes; diagnostics go to stderr.
int run_bridge(const std::string& pipe, bool may_start) {
    auto host = ipc::connect_or_start_host(ipc::sibling_executable("agent-host"), pipe, 10s, may_start);
    if (!host.client) {
        diag("cannot reach agent-host on this machine");
        return kNoHost;
    }
    host.client->close();
    if (!agentdesk::cli::os::bridge_stdio(pipe)) {  // exits the process once the bridge ends
        diag("cannot open the agent-host pipe");
        return kNoHost;
    }
    return kOk;
}

// agentctl update: agent-host to its latest release; terminals keep running (agent-host hands
// them to the new one, design doc 7.4 OQ-09). --json: {"now": "agent-host X"} on stdout, or
// {"error": {"message", "hint"}} on stderr.
int update_failed(const Ctx& ctx, const std::string& message, const std::string& hint = {}) {
    if (ctx.json) {
        Json e = {{"message", message}};
        if (!hint.empty())
            e["hint"] = hint;
        std::cerr << Json{{"error", e}}.dump() << '\n';
    } else {
        diag(message + (hint.empty() ? std::string() : " (" + hint + ")"));
    }
    return kRefused;
}

int update_done(const Ctx& ctx, const std::string& now) {
    out(ctx.json ? Json{{"now", now}}.dump() + "\n" : now + "\n");
    return kOk;
}

protocol::Json ctl_hello() {
    return {{"version", protocol::kProtocolVersion}, {"client", "agentctl"}};
}

// After host.update: the new agent-host serves the pipe once the old one let go. True once it
// answers as the instance the old host named.
bool successor_serves(const std::string& pipe, const std::string& instance) {
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto c = ipc::PipeClient::connect(pipe, std::chrono::milliseconds(500))) {
            const auto again = c->call("hello", ctl_hello());
            if (again && again->result && again->result->value("host_instance_id", "") == instance)
                return true;
        }
        std::this_thread::sleep_for(100ms);
    }
    return false;
}

int cmd_update(Ctx& ctx, const std::string& pipe) {
    namespace updates = agentdesk::ui::updates;
    updates::CoreStageRequest req;
    req.at_least = ctx.args.opt("--version");
    if (const auto exe = ctx.args.opt("--exe"))
        req.from = std::filesystem::path(platform::to_wide(*exe));
    // Never starts agent-host just to update it.
    auto client = ipc::PipeClient::connect(pipe, std::chrono::milliseconds(300));
    std::optional<protocol::Response> hello;
    if (client)
        hello = client->call("hello", ctl_hello(), ctx.timeout);
    if (!hello || hello->error || !hello->result) {
        // Nothing runs: the update is in place for agent-host's next start.
        req.host_exe = ipc::sibling_executable("agent-host");
        req.have = updates::host_version_of(req.host_exe).value_or("0.0.0");
        const auto staged = updates::stage_core(req);
        if (!staged.ok)
            return update_failed(ctx, staged.error, staged.hint);
        return update_done(ctx, "agent-host " + staged.version);
    }
    const Json& h = *hello->result;
    req.have = h.value("host_version", "");
    req.host_exe = std::filesystem::path(platform::to_wide(h.value("host_exe", "")));
    if (req.have.empty() || req.host_exe.empty())
        return update_failed(ctx, "this agent-host cannot update itself", "restart AgentDesk to update it");
    const auto staged = updates::stage_core(req);
    if (!staged.ok)
        return update_failed(ctx, staged.error, staged.hint);
    if (!staged.exe)
        return update_done(ctx, "agent-host " + staged.version);  // already the latest
    const auto r = client->call("host.update", {{"exe", platform::to_utf8(staged.exe->native())}}, 120s);
    if (!r || r->error || !r->result) {
        if (staged.swapped)
            updates::unstage_core(req.host_exe);
        return update_failed(ctx,
                             r && r->error ? r->error_message : std::string("agent-host did not answer"));
    }
    const std::string now = r->result->value("now", "agent-host " + staged.version);
    const std::string instance = r->result->value("host_instance_id", "");
    if (successor_serves(pipe, instance))
        return update_done(ctx, now);
    return update_failed(ctx, "the new agent-host did not start serving");
}

const char* kUsage =
    "usage: agentctl <command> [options]\n"
    "  run --profile ID [--name N] [--cwd DIR] [-- extra args]\n"
    "  list\n"
    "  read <session> [--lines N]\n"
    "  send <session> (--text T | --file F | --key K) [--enter]\n"
    "  stop <session> (--interrupt | --force)\n"
    "  report-state <session> --source S --state STATE --seq N [--kind K] [--message M]\n"
    "  hook      status hook for Codex CLI / Claude Code: reads the event on stdin, prints nothing\n"
    "  shutdown [--stop-sessions]\n"
    "  run ... [--task TEXT] [--parent ID | --no-parent] [--label k=v]...\n"
    "  wait <session> [--until idle|blocked|exited|change] [--timeout-ms N]\n"
    "  report <text...> [--to SESSION] [--from SESSION]\n"
    "  label <session> k=v...\n"
    "  timeline [<session>] [--since 15m|1h|4h|24h|<minutes>]   when agent and process states changed\n"
    "  --skill   how agents use agentctl to run helper agents\n"
    "  bridge    relay stdin/stdout to this machine's agent-host (used through ssh)\n"
    "  update [--version V] [--exe PATH]\n"
    "            update agent-host to its latest release (AGENTDESK_RELEASES_URL), or to the\n"
    "            agent-host.exe at PATH; terminals keep running\n"
    "global: --json  --pipe NAME  --no-start  --timeout-ms N\n";

// report-state runs inside agent hooks: never start a host from there, and inherit the pipe
// advertised to the session (design doc 3.6).
std::string session_pipe(const Ctx& ctx) {
    if (const auto p = ctx.args.opt("--pipe"))
        return *p;
    if (const std::string p = env("AGENTDESK_PIPE"); !p.empty())
        return p;
    return ipc::default_pipe_name();
}

int run_command(Ctx& ctx) {
    using Command = int (*)(Ctx&);
    static const std::map<std::string, Command> kCommands = {
        {"run", cmd_run},           {"list", cmd_list}, {"read", cmd_read},
        {"send", cmd_send},         {"stop", cmd_stop}, {"report-state", cmd_report_state},
        {"shutdown", cmd_shutdown}, {"wait", cmd_wait}, {"report", cmd_report},
        {"label", cmd_label},       {"timeline", cmd_timeline}};
    const auto& c = ctx.args.command;
    if (const auto it = kCommands.find(c); it != kCommands.end())
        return it->second(ctx);
    diag("unknown command: " + c);
    std::cerr << kUsage;
    return kBadArgs;
}

}  // namespace

int agentdesk::cli::agentctl_main(int argc, wchar_t** argv) {
    Ctx ctx;
    try {
        ctx.args = parse_args(argc, argv);
    } catch (const std::exception& e) {
        diag(e.what());
        return kBadArgs;
    }
    if (ctx.args.has("--skill") || ctx.args.command == "skill") {
        out(kSkill);
        return kOk;
    }
    if (ctx.args.command.empty() || ctx.args.command == "help" || ctx.args.has("--help")) {
        std::cerr << kUsage;
        return ctx.args.command.empty() ? kBadArgs : kOk;
    }
    ctx.json = ctx.args.has("--json");
    if (const auto t = ctx.args.opt("--timeout-ms"))
        ctx.timeout = std::chrono::milliseconds(std::stoll(*t));

    if (ctx.args.command == "hook")
        return run_hook();  // no output, always 0
    if (ctx.args.command == "bridge") {
        std::string pipe = ipc::default_pipe_name();
        if (const auto p = ctx.args.opt("--pipe"))
            pipe = *p;
        return run_bridge(pipe, !ctx.args.has("--no-start"));
    }

    const std::string pipe = session_pipe(ctx);
    if (ctx.args.command == "update")
        return cmd_update(ctx, pipe);
    const bool may_start = !ctx.args.has("--no-start") && ctx.args.command != "report-state" &&
                           ctx.args.command != "shutdown" && ctx.args.command != "report";
    auto host = ipc::connect_or_start_host(ipc::sibling_executable("agent-host"), pipe, 10s, may_start);
    if (!host.client) {
        diag(host.error.empty() ? "cannot reach agent-host (pipe " + pipe + ")" : host.error);
        return kNoHost;
    }
    if (host.host_dies_with_launcher) {
        diag("warning: this terminal runs in a job that will end agent-host when it closes");
    }
    ctx.client = std::move(host.client);

    try {
        return run_command(ctx);
    } catch (const std::invalid_argument& e) {
        diag(e.what());
        return kBadArgs;
    } catch (const std::exception& e) {
        diag(e.what());
        return kRefused;
    }
}
