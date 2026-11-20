# Writing mods

A mod is a Windows DLL that AgentDesk loads into `agent-ui.exe`. It can react to agents changing state, add command-palette entries, draw panels in the sidebar or the Details card, add text after tab titles and agent rows, type into terminals and start programs.

The interface is plain C (`mod_api.h`), so any compiler that can build a Windows DLL works. C++ mods use the header-only wrapper `mod_sdk.hpp`. Both are MIT-licensed like the rest of AgentDesk.

## Before you start: what a mod can do

A mod runs **inside AgentDesk with the rights of the Windows account that runs it**. It can read and change that person's files, start programs and type into their terminals, including terminals where an agent is working. AgentDesk asks before it turns a mod on and says so, but it does not sandbox mods. Treat a mod like any program you install, and publish yours with its source so people can check it.

## Files

```
%LOCALAPPDATA%\AgentDesk\mods\<name>\
    mod.dll          the mod (required)
    mod.json         {"description": "One line shown in Settings > Mods"}   (optional)
    settings.json    the mod's own settings, read through setting(key)         (optional)
```

`<name>` is the folder name. It is how the mod is enabled (`mods_enabled` in `ui.json`) and the prefix of its keys.json actions (`<name>.<command>`). A mod is off until the person turns it on in **Settings › Mods**. That page also shows load errors and has **Reload**.

The DLL's dependencies are looked up in the mod's own folder first, then System32, never in the current folder or on PATH. Ship any DLL you depend on next to `mod.dll`, or link the C++ runtime statically (`/MT`).

## Building

The release zip has an `sdk\` folder: `include\agentdesk\` (the two headers and nlohmann/json, which `mod_sdk.hpp` uses), `examples\` (the sources of the three example mods) and a `CMakeLists.txt` that builds them. From a Visual Studio 2022 x64 developer prompt:

```bat
cd sdk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Each mod comes out as `build\mods\<name>\mod.dll` with its `mod.json`. Copy that folder into `%LOCALAPPDATA%\AgentDesk\mods` and turn it on. Add your own mod to the CMakeLists with `agentdesk_mod(<name> <sources>)`.

## A first mod (C++)

```cpp
#include <agentdesk/mod_sdk.hpp>

class Hello : public agentdesk::mod::Mod {
public:
    Hello() { host().add_command("count", "Count agents"); }

    void on_event(const std::string& event, const agentdesk::mod::Json& ev) override {
        if (event == "agent_state" && ev.value("state", "") == "Blocked") {
            host().notify(ev.value("title", "") + " needs you");
            host().run("agent.focus:" + ev.value("session", ""));
        }
    }

    void command(const std::string&) override {
        host().notify(std::to_string(host().sessions().size()) + " sessions");
    }
};

AGENTDESK_MOD(Hello, "Says when an agent needs you")
```

## What AgentDesk offers (`host()`)

| Call | Does |
|---|---|
| `run(command)` | Runs a window command: `tab.new`, `agent.focus:<session>`, `layout:terminals`, ... (the same names keys.json binds) |
| `send(session, text, enter)` | Types text into a session, optionally pressing Enter |
| `notify(title, body)` | Shows a notification in the corner |
| `exec({program, args...})` | Starts a program directly, without a shell; the program is looked up on PATH; at most 64 arguments |
| `add_command(id, label)` | Adds a command-palette entry; picking it calls `command(id)` |
| `sessions()` | Every session: `id`, `title`, `program`, `cwd`, `state`, `process`, `machine`, `parent`, `created_at`, `labels`, `focused` |
| `setting(key, default)` | A value from the mod's `settings.json` |

Requests made during a call take effect after the call returns.

## What a mod offers (override in `Mod`)

| Method | Called when |
|---|---|
| `on_event(event, payload)` | `agent_state` {session, state, previous, title}; `session_added` {session}; `session_exited` {session, exit_code}; `tick` {} about once a second |
| `panels(where)` | The sidebar (`"sidebar"`) or the Details card (`"details"`) is drawn. Return `Panel`s built from `text`, `row`, `badge`, `progress`, `button` and `separator`; colors are `""`, `dim`, `accent`, `good`, `warn`, `bad` |
| `click(panel, widget)` | A button in one of the mod's panels was clicked |
| `decorate(kind, subject)` | Text after a tab title (`"tab"`, subject {title, sessions}) or an agent row (`"agent"`, subject a session); return `""` for nothing |
| `command(id)` | A palette entry from `add_command` was picked |

Agent states are `Working`, `Blocked`, `Done`, `Idle` and `Unknown`. They come only from structured reports (OSC 7501 or `agentctl report-state`), never from screen text.

## Rules that keep AgentDesk responsive

- **Every call into the mod runs on the UI thread. Return quickly.** `panels` and `decorate` run on every frame that draws them; `tick` comes once a second.
- **Never block**: no network calls, no waiting on a process, no sleeping. Do slow work on your own thread and keep the result for the next call; start programs with `exec`, which does not wait.
- **A mod that never returns freezes the AgentDesk window.** Nothing can interrupt a call that loops forever. The terminals keep running in `agent-host.exe`, so the person ends `agent-ui.exe` in Task Manager, turns the mod off in `ui.json` (`mods_enabled`), and starts AgentDesk again.
- **A mod that crashes** (an access violation, an exception escaping a call) is turned off, and the error appears in Settings › Mods. Its DLL stays loaded until AgentDesk restarts. The C++ wrapper catches exceptions from your methods and reports them as a notification.
- Strings you return must stay valid until the next call into the mod; the C++ wrapper handles this.

## Compatibility

`AGENTDESK_MOD_API_VERSION` is the version of the interface a mod was built against, and the version the running AgentDesk speaks. The current version is 1.

- A new version only **adds** members at the end of `agentdesk_host` and `agentdesk_mod`. Existing members never change meaning or move.
- AgentDesk loads a mod whose version is between 1 and its own. Members an older mod does not know about stay NULL.
- A mod built against a newer header than the running AgentDesk is refused, with a message in Settings › Mods, because it may call something that AgentDesk lacks. The C++ wrapper likewise refuses to start under an AgentDesk older than its header.
- Breaking the interface would need a new export name instead of `agentdesk_mod_init`, so an old DLL can never be mistaken for a new one.

## Without C++

Export one function from the DLL:

```c
__declspec(dllexport) int agentdesk_mod_init(const agentdesk_host* host, agentdesk_mod* mod);
```

Fill `*mod` (set `api_version` to `AGENTDESK_MOD_API_VERSION`; any callback may be NULL) and return 0, or return non-zero to refuse. Structured data crosses the boundary as UTF-8 JSON text. The comments in `mod_api.h` are the reference.

## Publishing a mod

- Publish the source with the DLL, and say what it does with files, programs and the network.
- Ship a `mod.json` description.
- Keep secrets (tokens, topics) out of the DLL; read them from `settings.json`, which the person fills in.
- If you sign your DLL, people can check where it came from.
