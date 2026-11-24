# AgentDesk

English | [繁體中文](README.zh-TW.md)

AgentDesk is a native workbench for AI coding agents on Windows 11 and macOS. Run several agent CLIs side by side in split terminal panes (Claude Code and Codex CLI first; any command-line program works). Close the window and the work keeps running; open it again and you are back where you were, with the same layout. The sidebar shows which agent is waiting for you and which one is done.

The interface and behaviour are ported from the source of [herdr-app](https://github.com/herdrdev/herdr-app) and rewritten in C++. AgentDesk does not aim for protocol compatibility with herdr.

Current version: 0.2.0. Platforms: Windows 11 x64, and macOS 13 Ventura or later on Apple Silicon.

## How it works

AgentDesk is three programs:

| Program | Role |
|---|---|
| `agent-host` | One background process per user. It owns every terminal: the pseudo-terminal, the child processes, the terminal engine (libvterm) and the scrollback. |
| `agent-ui` | The window (`AgentDesk.app` on macOS). It is only a client of agent-host: it draws snapshots and live updates and sends your input. |
| `agentctl` | The command-line client. It also reports agent status from hooks, relays the protocol over ssh, and updates agent-host. |

They talk over a local channel limited to your own user account: a named pipe on Windows, a Unix socket on macOS. Because the host owns everything, closing the window only disconnects.

## Features

- **Keeps running in the background.** Closing the window only disconnects; reopening reattaches with a snapshot followed by live updates.
- **Spaces, tabs and splits.** A space is a place you work in, usually a repo. It holds tabs, and each tab splits right or down, zooms, and swaps panes by drag. The host keeps the layout, so it comes back when you reopen, in every window.
- **Trustworthy agent status.** Process state and agent state are shown separately. Agent state comes only from structured reports: OSC 7501 from Claude Code 2.1.295 and later (no setup needed), and `agentctl hook` for Codex CLI. Without a trusted source the state is Unknown; AgentDesk never guesses from screen text. When the shell shows its prompt again, the agent's status is cleared.
- **Attention and notifications.** An agent that needs you always plays a sound. An agent you are not watching that finishes is marked Done and shows a toast; click it to jump there.
- **Shell picker.** The sidebar's "+" lists the shells found on the machine: PowerShell 7, Windows PowerShell, Command Prompt and each WSL distribution on Windows; your login shell and `/etc/shells` on macOS.
- **Folder tracking.** AgentDesk adds a prompt hook to PowerShell, Command Prompt, WSL bash, zsh and bash, so `cd` updates the space name, the branch and the Files/Changes panels.
- **Files and Changes panels.** A file tree with git status and a read-only preview, and the uncommitted changes with diffs and commit.
- **Command palette and keys.** A fuzzy command palette, a list of every shortcut, a jump list for spaces, tabs and agents, and `keys.json` to rebind keys. macOS uses its own Cmd-based key table and an Option-as-Alt setting.
- **git worktrees.** Type a branch name to create a checkout and open it as a new space.
- **Sub-agents.** `agentctl run --task ...` inside an AgentDesk terminal starts a sub-agent; `agentctl wait` waits for it and `agentctl report` sends its result back to the parent.
- **Remote machines.** Through `ssh <target> agentctl bridge`, another machine's spaces and agents appear in the same sidebar.
- **Layouts, themes and mods.** Two built-in layouts (workspace and terminals) plus custom JSON layouts; eight built-in themes plus theme files; mods written in C++ (`mod.dll` / `mod.dylib`) that add commands, panels and notifications.
- **Updates without losing terminals.** AgentDesk can update itself, and agent-host can update on its own: the new host takes over every terminal and the programs in them keep running. There is no built-in update server: set `AGENTDESK_RELEASES_URL` to enable checks.

The full manual (in Traditional Chinese) is [docs/user-guide.md](docs/user-guide.md).

## Requirements

- Windows 11 x64, or macOS 13 Ventura or later on Apple Silicon. A normal user account is enough.
- Install and sign in to the agent CLIs you want to use. AgentDesk does not handle sign-in and never copies tokens. Claude Code reports status from version 2.1.295; Codex CLI needs a hook (see [docs/integrations/agent-hooks.md](docs/integrations/agent-hooks.md)).

## Install

### Windows

1. Unzip `AgentDesk-<version>-win-x64.zip` anywhere, for example `%LOCALAPPDATA%\Programs\AgentDesk`. No administrator rights and no registry writes.
2. Keep `conpty\conpty.dll` and `conpty\x64\OpenConsole.exe` next to the executables. This is the bundled ConPTY: the one built into Windows drops the reply to the status query, so without it Claude Code reports no status and agent-host cannot update on its own.
3. Run `agent-ui.exe`. It starts `agent-host.exe` in the background; the first start opens a welcome page.

The executables are not code-signed yet, so the first run may show "Windows protected your PC". Make sure the zip came from this project's GitHub Releases, then choose "More info" → "Run anyway".

### macOS

1. Unzip `AgentDesk-<version>-macos-arm64.zip` and move `AgentDesk.app` to Applications.
2. The app is signed ad hoc, not with a Developer ID, so the first start needs Control-click → Open (or System Settings → Privacy & Security → Open Anyway).
3. Open AgentDesk. Terminals get your login shell's `PATH`, so programs installed with Homebrew or npm are found even when the app is started from the Finder.

### Where things are

| | Windows | macOS |
|---|---|---|
| Settings and data | `%LOCALAPPDATA%\AgentDesk` | `~/Library/Application Support/AgentDesk` |
| Fonts, sounds, themes, example mods | next to the executables | `AgentDesk.app/Contents/Resources` |
| git worktrees | `%USERPROFILE%\.agentdesk\worktrees` | `~/.agentdesk/worktrees` |

`AGENTDESK_DATA_DIR` moves the data folder (for portable use and tests).

## Quick start

1. A shell opens first. `cd` into a project; the space name and branch follow.
2. Type `claude` or `codex` to start an agent. When it needs you, the sidebar's Agents section marks it and a sound plays.
3. Split, open tabs and spaces:

   | | Windows | macOS |
   |---|---|---|
   | Split right / down | Ctrl+Shift+O / Ctrl+Shift+E | ⌘D / ⇧⌘D |
   | New tab / space | Ctrl+Shift+T / Ctrl+Shift+N | ⌘T / ⌘N |
   | Command palette | Ctrl+Shift+P | ⇧⌘P |
   | Go to a space, tab or agent | Ctrl+Shift+K | ⌘P |
   | All shortcuts | Ctrl+Shift+/ | ⌘/ |
   | Settings | Ctrl+, | ⌘, |

4. Close the window: the work keeps running. Open AgentDesk again to reattach.

## agentctl

```sh
agentctl run --profile claude-native --name review --cwd ~/src/app
agentctl list --json
agentctl read review --lines 100
agentctl send review --text "run the tests" --enter
agentctl wait review --until idle
agentctl stop review --interrupt        # then --force if it does not stop
agentctl update                         # replace agent-host; terminals keep running
agentctl --skill                        # instructions for an agent running inside AgentDesk
```

With `--json`, stdout carries only the result. Exit codes: 0 ok, 2 bad arguments, 3 host unreachable, 4 not found or ambiguous, 5 refused, 6 start failed, 7 timeout. `send` never presses Enter unless you pass `--enter`.

## Building from source

### Windows

Tools: Visual Studio 2022 Build Tools 17.14 (MSVC 14.44, Windows SDK 10.0.26100.0), CMake 3.25 or later, Ninja, vcpkg in manifest mode (`C:\vcpkg`, triplet `x64-windows-static-md`) and Git for Windows.

`scripts\build.cmd` enters the VS 2022 x64 environment, sets `VCPKG_ROOT` and English compiler messages, then runs the CMake presets:

```bat
scripts\build.cmd msvc-x64-debug all          :: configure + build + ctest
scripts\build.cmd msvc-x64-debug build        :: build only
scripts\build.cmd msvc-x64-release configure  :: configure a new preset first
```

A fresh clone needs the bundled ConPTY: build once, run `build\msvc-x64-debug\tools\fetch_conpty.exe` (it downloads a pinned version and checks its SHA-256 hashes), then configure again. Before building, `build.cmd` ends any `agent-ui` and `agent-host` started from that build folder, because they lock the executables; set `AGENTDESK_KEEP_HOST=1` to skip this. The release zip is made by `agentdesk_package.exe` after a Release build ([docs/release.md](docs/release.md)).

### macOS

Tools: Xcode Command Line Tools, CMake 3.25 or later, Ninja, and vcpkg cloned to `~/vcpkg` and bootstrapped. The presets use the overlay triplet `cmake/triplets/arm64-osx-agentdesk.cmake` (minimum macOS 13). No bundled ConPTY is needed: a Unix pty passes the status reply through.

```sh
scripts/build.sh clang-macos-arm64-debug all     # configure + build + ctest
scripts/build.sh clang-macos-arm64-debug build
scripts/package_macos.sh                         # Release build, AgentDesk.app and dist/*.zip
```

`agent-ui --screenshot out.png --exit-after-ms N` writes the last frame for automated checks without screen-recording permission.

### Tests

GoogleTest through ctest. Labels: `unit` on both platforms, `windows_integration` on Windows, `posix_integration` on macOS.

```sh
ctest --test-dir build/<preset> -L unit
build/<preset>/tests/unit_tests --gtest_filter='VtEngine.*'
```

Everything compiles with warnings as errors (`/W4 /permissive- /WX`, or `-Wall -Wextra -Wpedantic -Werror`). Formatting and static analysis use `.clang-format` and `.clang-tidy`.

## Repository layout

One source tree serves both platforms. Shared code has no platform `#ifdef`s: what differs sits behind small internal interfaces (`*_os.hpp`), implemented in `*_win.cpp` or `win/` for Windows and `*_posix.cpp` or `macos/` for macOS, and each folder's CMakeLists picks the right files. Headers whose declarations differ live in `include_win/` and `include_posix/`.

| Path | Contents |
|---|---|
| `apps/desktop` | agent-ui: the shared window contents; the Win32/DX11 shell (`main.cpp`, `win/`) and the AppKit/Metal shell (`macos/`) |
| `apps/host` | agent-host entry points (`main.cpp`, `main_posix.cpp`) |
| `apps/cli` | agentctl: shared commands plus `cli_win.cpp` / `cli_posix.cpp` |
| `libs/platform` | Platform layer: neutral interfaces, the Windows implementation (ConPTY, Job, quoting) and the POSIX one (pty, posix_spawn, Unix sockets) |
| `libs/terminal` | `VtEngine` (libvterm wrapper, snapshots, history, key encoding), OSC 7501 parsing |
| `libs/protocol`, `libs/ipc` | IPC framing, messages, error codes, screen encoding; the local channel and the ssh relay |
| `libs/host` | `Session` actor, `SessionManager` (the protocol), persistence, git tools, agent-host handover |
| `libs/ui` | `TerminalView`, `Workspace`, the layout engine, `Keymap`, `ModHost`, fonts, links, updates, telemetry |
| `libs/modapi`, `mods/` | The mod interface (plain C `mod_api.h`, header-only `mod_sdk.hpp`) and three example mods |
| `tools/` | Development, measurement and packaging tools (not shipped) |
| `tests/` | GoogleTest: shared unit tests, per-platform unit tests, Windows and POSIX integration tests, fixtures |
| `third_party/` | libvterm, the bundled ConPTY's license, fonts, the modified ImGui macOS backend, herdr-app's license and resources |
| `docs/`, `contracts/`, `sprint/`, `.decisions/` | Documentation, contracts, sprint records and process decisions (mostly in Traditional Chinese) |

## Documentation

| Document | Contents |
|---|---|
| [docs/user-guide.md](docs/user-guide.md) | User guide |
| [contracts/approved/PRD.md](contracts/approved/PRD.md) | Product requirements and acceptance criteria |
| [docs/design/AgentDesk_Design.md](docs/design/AgentDesk_Design.md) | Detailed design: architecture, protocol, host, UI, platform differences; decisions in chapter 11 |
| [docs/design/platform-abstraction.md](docs/design/platform-abstraction.md) | The platform layer and the macOS port |
| [docs/mods.md](docs/mods.md) | Writing mods: the API, compatibility, safety |
| [docs/integrations/agent-hooks.md](docs/integrations/agent-hooks.md) | Installing the agent status hooks |
| [docs/release.md](docs/release.md) | Making a release |
| [docs/compatibility.md](docs/compatibility.md) | Compatibility matrix |
| [CLAUDE.md](CLAUDE.md) | Project guide for Claude Code: commands, architecture, invariants |

## Privacy and network

AgentDesk has no default server. It checks for updates only when `AGENTDESK_RELEASES_URL` is set, and sends anonymous daily usage counts only when `AGENTDESK_TELEMETRY_URL` is set (Settings → Privacy shows exactly what would be sent and turns it off; `DO_NOT_TRACK` also turns it off). Feedback is copied or saved to a file, never sent. Diagnostic logs never contain what you type.

## License

AgentDesk is released under the MIT License; see [LICENSE](LICENSE).

Third-party components (libvterm, ConPTY, fonts, lunasvg, the ImGui macOS backend) list their source, version and license in `third_party/*/README.agentdesk.md`. The release's `THIRD_PARTY_NOTICES.md` carries every bundled component's notices.

## Acknowledgements

AgentDesk's interface and behaviour are ported to C++ from the source of [herdr-app](https://github.com/herdrdev/herdr-app) (Apache-2.0). Its license is in [third_party/herdr/LICENSE](third_party/herdr/LICENSE) and in the release's THIRD_PARTY_NOTICES. AgentDesk is an independent project and is not affiliated with herdr.
