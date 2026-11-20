# AgentDesk

English | [繁體中文](README.zh-TW.md)

AgentDesk is a native Windows 11 x64 workbench for AI coding agents. Run several agent CLIs side by side in split terminal panes (Claude Code and Codex CLI first; any command-line program works). Close the window and the work keeps running; open it again and you are back where you were, with the same layout. The sidebar shows which agent is waiting for you and which one is done.

The interface and behaviour are ported from the source of [herdr-app](https://github.com/herdrdev/herdr-app) and rewritten in C++. AgentDesk does not aim for protocol compatibility with herdr.

Current version: 0.2.0.

## Features

- **Keeps running in the background.** Terminals, child processes and screen state live in a background process, `agent-host.exe`. Closing the window only disconnects; reopening reattaches with a snapshot followed by live updates.
- **Spaces, tabs and splits.** A space is a place you work in (usually a repo). It holds tabs, and each tab splits right or down, zooms, and swaps panes by drag. The host keeps the layout, so it comes back when you reopen.
- **Trustworthy agent status.** Process state and agent state are shown separately. Agent state comes only from structured reports: OSC 7501 from Claude Code 2.1.295 and later (no setup needed), and a hook that calls `agentctl report-state` for Codex CLI. Without a trusted source the state is Unknown; AgentDesk never guesses from screen text.
- **Attention and notifications.** An agent that needs you always plays a sound. An agent you are not watching that finishes is marked Done and shows a toast; click it to jump there.
- **Shell picker.** The sidebar "+" lists the shells found on the machine (PowerShell 7, Windows PowerShell, Command Prompt and each WSL distribution).
- **WSL.** Installed distributions appear as shells; `cd` in bash updates the space name, branch and Files/Changes.
- **Files and Changes panels.** A file tree and the git changes of the current folder, with diffs and commit.
- **Command palette and keys.** Ctrl+Shift+P opens the palette, Ctrl+Shift+/ lists every shortcut, and `keys.json` rebinds them.
- **git worktrees.** Type a branch name to create a checkout and open it as a new space.
- **Sub-agents.** `agentctl run --task ...` inside an AgentDesk terminal starts a sub-agent; `agentctl wait` waits for it and `agentctl report` sends its result back to the parent.
- **Remote machines.** Through `ssh <target> agentctl bridge`, another Windows machine's spaces and agents appear in the same sidebar.
- **Layouts.** Eight built-in layouts plus custom JSON layouts: AgentDesk (the default workspace), Terminals, AgentDesk Mac (menu bar, floating window and Dock), Mission Control (every agent on one wall, needs-you first), Review (the terminal beside the agent's diff and a commit box), AgentDesk Tiling (keyboard-driven, square panes), AgentDesk Cockpit (gauges, an agent list and a state log) and AgentDesk Classic (the whole window drawn in characters, monochrome). All are drawn in AgentDesk's own style.
- **Focus mode.** Ctrl+Shift+. shows only the current terminal with a thin strip of status dots for the other agents; an agent that needs you lights the window edge and still plays a sound.
- **Timeline.** Ctrl+Shift+H shows when each agent worked, waited for you and finished. agent-host records only times and states (up to 24 hours), so it keeps recording while the window is closed; `agentctl timeline` prints the same history.
- **Themes and mods.** AgentDesk's own themes plus herdr's and themes made for the layouts. Appearance can switch between light and dark on a schedule (fixed times or sunrise and sunset from a latitude you type in; no network, no location service). A theme editor copies a theme, previews every change live and checks contrast. Mods are C++ DLLs that add commands, panels and notifications.
- **Updates.** AgentDesk and agent-host can update; when agent-host updates on its own, the new host takes over and the programs in your terminals keep running. There is no built-in update server: set `AGENTDESK_RELEASES_URL` to enable checks.
- **`agentctl` CLI.** `run`, `list`, `read`, `send`, `stop`, `wait` and more, with stable `--json` output and fixed exit codes.

The full manual (in Traditional Chinese) is [docs/user-guide.md](docs/user-guide.md).

## Requirements

- Windows 11 x64; a normal user account is enough. Windows 10 is untested.
- Install and sign in to the agent CLIs you want to use. AgentDesk does not handle sign-in and never copies tokens. Claude Code reports status from version 2.1.295.
- Building from source also needs Visual Studio 2022 Build Tools (see below).

## Install and run

1. Unzip `AgentDesk-<version>-win-x64.zip` anywhere, for example `%LOCALAPPDATA%\Programs\AgentDesk`. No administrator rights and no registry writes.
2. Keep `conpty\conpty.dll` and `conpty\x64\OpenConsole.exe` next to the executables. This is the bundled ConPTY: the one built into Windows drops the reply to the status query, so without it Claude Code reports no status and agent-host cannot update on its own.
3. Run `agent-ui.exe`. It starts `agent-host.exe` in the background; the first start opens a welcome page where you pick a size and theme.

**SmartScreen:** the first run may show "Windows protected your PC". AgentDesk's executables are not code-signed yet, and Windows shows this for any new download without reputation. Make sure the zip came from this project's GitHub Releases, then choose "More info" → "Run anyway". You can also build it yourself from source.

The zip also contains `agentctl.exe`, `LICENSE`, `THIRD_PARTY_NOTICES.md`, `docs\`, the example mods, and the mod `sdk\`. Data and settings live in `%LOCALAPPDATA%\AgentDesk`.

## Quick start

1. A shell opens first. `cd` into a project; the space name and branch follow.
2. Type `claude` or `codex` to start an agent. When it needs you, the sidebar's Agents section marks it and a sound plays.
3. Ctrl+Shift+O and Ctrl+Shift+E split right and down; Ctrl+Shift+T opens a tab and Ctrl+Shift+N a space.
4. Close the window: the work keeps running. Run `agent-ui.exe` again to reattach.
5. Codex CLI needs a hook for status reports: see [docs/integrations/agent-hooks.md](docs/integrations/agent-hooks.md).

## Building from source

Tools: Visual Studio 2022 Build Tools 17.14 (MSVC 14.44, Windows SDK 10.0.26100.0), CMake 3.25 or later, Ninja, vcpkg in manifest mode (default location `C:\vcpkg`, triplet `x64-windows-static-md`) and Git for Windows. The code is C++20 with Dear ImGui, Win32, DirectX 11, ConPTY and libvterm 0.3.3.

`scripts\build.cmd` enters the VS 2022 x64 environment, sets `VCPKG_ROOT` and English compiler messages, then runs the CMake presets:

```bat
scripts\build.cmd msvc-x64-debug all          :: configure + build + ctest
scripts\build.cmd msvc-x64-debug build        :: build only (preset already configured)
scripts\build.cmd msvc-x64-release configure  :: configure a new preset first
```

A fresh clone needs the bundled ConPTY: build once to get the tool, run it, then configure again.

```bat
scripts\build.cmd msvc-x64-debug all
build\msvc-x64-debug\tools\fetch_conpty.exe
scripts\build.cmd msvc-x64-debug configure
```

`fetch_conpty.exe` downloads a pinned version of Microsoft's ConPTY package and checks three SHA-256 hashes.

Before building, `build.cmd` ends any `agent-ui` and `agent-host` started from that build folder (`build\<preset>\`), because they lock the executables and `conpty.dll`. Ending agent-host also ends its terminals; set `AGENTDESK_KEEP_HOST=1` to skip this.

Tests use GoogleTest through ctest:

```bat
ctest --test-dir build\msvc-x64-debug -L unit          :: labels: unit, windows_integration
build\msvc-x64-debug\tests\unit_tests.exe --gtest_filter=VtEngine.*
```

The release zip is made by `agentdesk_package.exe` after a Release build; see [docs/release.md](docs/release.md). Everything compiles with `/W4 /permissive- /WX`; formatting and static analysis use the clang-format and clang-tidy that ship with Visual Studio (`.clang-format`, `.clang-tidy`).

## Repository layout

| Path | Contents |
|---|---|
| `apps/desktop` | `agent-ui.exe`: the Win32/DX11 shell and the whole interface |
| `apps/host` | `agent-host.exe`: the background process that owns ConPTY, child processes, the terminal engine and history |
| `apps/cli` | `agentctl.exe`: command-line client |
| `libs/platform/windows` | `PtyProcess` (ConPTY + Job + reader thread), argv quoting, ConPTY loading |
| `libs/terminal` | `VtEngine` (libvterm wrapper, snapshots, history, key encoding), OSC 7501 parsing |
| `libs/protocol`, `libs/ipc` | IPC framing, messages, error codes, screen encoding; named pipes |
| `libs/host` | `Session` actor (process + engine + state) |
| `libs/ui` | `TerminalView`, `Workspace`, `Keymap`, `ModHost`, the layout engine |
| `libs/modapi`, `mods/` | The mod interface (plain C `mod_api.h`, header-only `mod_sdk.hpp`) and three example mods |
| `tools/` | Development, measurement and packaging tools (not shipped) |
| `tests/` | GoogleTest unit and Windows integration tests, fixtures |
| `third_party/` | libvterm, the bundled ConPTY's license and notice, fonts, lunasvg notes, herdr-app's license and resources |
| `docs/`, `contracts/`, `sprint/`, `.decisions/` | Documentation, contracts, sprint records and process decisions (mostly in Traditional Chinese) |

## Documentation

| Document | Contents |
|---|---|
| [docs/user-guide.md](docs/user-guide.md) | User guide |
| [docs/mods.md](docs/mods.md) | Writing mods: the API, compatibility, safety |
| [docs/release.md](docs/release.md) | Making a release |
| [docs/design/AgentDesk_Design.md](docs/design/AgentDesk_Design.md) | Product requirements and system design; decisions in section 7.1 |
| [docs/integrations/agent-hooks.md](docs/integrations/agent-hooks.md) | Installing the agent status hooks |
| [docs/compatibility.md](docs/compatibility.md) | Compatibility matrix |
| [CLAUDE.md](CLAUDE.md) | Project guide for Claude Code: commands, architecture, invariants |

## License

AgentDesk is released under the MIT License; see [LICENSE](LICENSE).

Third-party components (libvterm, ConPTY, fonts, lunasvg) list their source, version, hashes and license in `third_party/*/README.agentdesk.md`. The release's `THIRD_PARTY_NOTICES.md` carries every bundled component's notices.

## Acknowledgements

AgentDesk's interface and behaviour are ported to C++ from the source of [herdr-app](https://github.com/herdrdev/herdr-app) (Apache-2.0). Its license is in [third_party/herdr/LICENSE](third_party/herdr/LICENSE) and in the release's THIRD_PARTY_NOTICES. AgentDesk is an independent project and is not affiliated with herdr.
