# 平台抽象層介面草案（macOS 移植準備）

版本 0.3｜日期：2026-11-24｜狀態：OQ-10 已決定（第 8 節、設計文件 ADR-30）；macOS 部分（第 9～12 步）已實作，見第 10 節。Windows 版行為以設計文件為準，不因這份文件改變。

## 1. 目的與範圍

AgentDesk 目前只支援 Windows 11 x64。使用者想評估做 macOS 版，這份草案先把「哪些地方碰到作業系統」整理成一組介面，讓之後的工作分成兩步：

1. **重構（只動 Windows）**：把現有的 Win32 呼叫搬到這些介面後面，Windows 版行為、測試、截圖基準都不變。
2. **移植**：在同一組介面下加 macOS 實作。

不在這份草案的範圍內：Mac 版的介面外觀調整、打包與簽章流程、Linux（介面會盡量照 POSIX 設計，但不承諾）。

## 2. 現況盤點

以 2026-11-24 的 main（`0038204`）為準，Windows 相依集中在下列地方：

| 範圍 | 檔案 | 用到的 Windows 機制 |
|---|---|---|
| 終端機程序 | `libs/platform/windows/*`（約 1,400 行） | ConPTY（隨附 `conpty.dll`）、Job Object、PEB 讀 cwd／命令列、`CommandLineToArgvW` 規則 |
| 等待程序結束 | `libs/host/src/session.cpp` actor 迴圈 | `WaitForMultipleObjects(queue_event, process_handle)` |
| host 熱更新 | `libs/host/src/handover.cpp` | `DuplicateHandle`、`ConptyPackPseudoConsole` |
| 本機 IPC | `libs/ipc/src/pipe.cpp`、`host_launcher.cpp` | Named Pipe、DACL、`GetNamedPipeClientProcessId`、overlapped I/O |
| host 單一實例 | `apps/host/main.cpp` | Named mutex、event |
| 子程序（git、更新、遙測、ssh） | `libs/host/src/git_tools.cpp`、`libs/ui/src/system_process.cpp`、`detached.hpp` | `CreateProcessW`、匿名 pipe、handle list、`DETACHED_PROCESS` |
| 資料與系統資訊 | `libs/host/src/store.cpp`、`session.cpp` | `%LOCALAPPDATA%`、`ReplaceFileW`、`UuidCreate`、`GetComputerNameA`、`SearchPathW`、登錄檔（WSL 發行版） |
| 雜湊與亂數 | `libs/ui/src/updates.cpp`、`telemetry.cpp` | BCrypt |
| 視窗外殼 | `apps/desktop/main.cpp`（677 行） | Win32 視窗、DX11、自繪標題列（`WM_NCCALCSIZE`／`WM_NCHITTEST`）、低階鍵盤 hook、DPI |
| 介面周邊 | `libs/ui/src/fonts.cpp`、`clipboard.cpp`、`links.cpp`、`apps/desktop/git_cache.cpp`、`settings_store.cpp` | 字型登錄檔、`CF_HDROP`／`CF_UNICODETEXT`、`ShellExecuteW`、`FindFirstChangeNotificationW`、`GetDpiForMonitor` |
| mod | `libs/ui/src/mods.cpp` | `LoadLibraryExW`／`GetProcAddress` |

`libs/terminal`、`libs/protocol`、Session 的執行緒模型、`Workspace`、`TerminalView` 和大部分 `apps/desktop` 介面程式都不碰作業系統，可以直接沿用。

## 3. 設計原則

1. **編譯期選實作，不用 virtual。** 一個執行檔只會跑在一種作業系統上，所以同一個標頭檔對應 `windows/` 或 `macos/` 底下不同的 `.cpp`，由 CMake 決定要編哪一份。需要可替換的地方（例如測試用的假 PTY）才另外定介面。
2. **介面上不出現 `HANDLE`、`HWND`、`fd`、`wchar_t`。** 原生物件藏在 pimpl 裡；字串一律 UTF-8 `std::string`，路徑用 `std::filesystem::path`。Windows 實作在邊界上轉 UTF-16。
3. **設計文件的不變式不因平台改變。** 包括：host 擁有全部執行狀態、背壓不丟位元組、每個 session 只有一個輸入控制者、executable 與 argv 分開存、輸出不能觸發命令或寫剪貼簿、新 host 不憑 PID 接管程序。各平台用自己的機制達成同一個保證，做不到的要在介面上明說（例如 `supported()` 回傳 false），不能悄悄降級。
4. **平台差異寫在能力旗標裡，不散落在呼叫端。** 呼叫端看到的是「這個平台能不能熱更新」，不是 `#ifdef _WIN32`。
5. **重構階段 Windows 測試要全綠。** 每一步都跑完整的 `ctest`，截圖基準不變。

## 4. 目錄與建置結構

```
libs/platform/
  include/agentdesk/platform/   ← 平台中立的介面（本草案）
    pty.hpp  process.hpp  ipc_transport.hpp  instance_lock.hpp
    system.hpp  dynamic_library.hpp  handover.hpp
  windows/                      ← 現有程式，改為實作上面的介面
  macos/                        ← 新增（POSIX 部分能共用的放 posix/）
  posix/
apps/desktop/
  shell/window_shell.hpp        ← 視窗外殼介面
  shell/win32_dx11.cpp          ← 由現在的 main.cpp 搬過來
  shell/macos_metal.mm          ← 新增（Objective-C++）
```

CMake 用 `if(WIN32)`／`elseif(APPLE)` 選子目錄。`CMakePresets.json` 新增 `clang-macos-arm64-debug`／`-release`。vcpkg 的 ImGui feature 依平台分：Windows 用 `win32-binding`、`dx11-binding`，macOS 用 `osx-binding`、`metal-binding`（manifest 支援用 `"platform"` 條件）。

## 5. 介面草案

標頭檔草稿只列對外介面，命名照 `.clang-format`、`.clang-tidy` 的規則。

### 5.1 PTY 與終端機程序（`pty.hpp`）

取代 `platform::win::PtyProcess`。熱更新用的方法搬到 5.5，這裡只留一般生命週期。

```cpp
namespace agentdesk::platform {

struct PtySize {
    std::uint16_t cols = 120;
    std::uint16_t rows = 40;
};

struct PtySpawnSpec {
    std::filesystem::path executable;  // launched directly; never through a shell string
    std::vector<std::string> args;     // UTF-8, argv[1..]
    std::filesystem::path cwd;         // must exist; never silently replaced
    std::map<std::string, std::string> env_overrides;  // empty value removes the variable
    PtySize size;
};

// Reader thread, in order, every byte. Must not block for long (backpressure is the caller's).
using PtyOutputSink = std::function<void(std::span<const char>)>;
// Called once, from a platform thread, when the root process has exited. The callback only
// posts to the session's queue; the session then calls close() from its actor thread.
using PtyExitSink = std::function<void()>;

struct ForegroundInfo {
    std::string program;            // lower-case image name, no extension; empty if none
    std::string cwd;                // UTF-8; empty if unreadable
    std::vector<std::string> argv;  // empty if unreadable
};

// One child program in a pseudo terminal. Everything it starts ends with it (Windows: a
// kill-on-close job; macOS: its own session and process group, plus SIGHUP when the master
// closes). Thread rules as today: write/resize/interrupt from one thread at a time; close()
// not on the reader thread or a UI thread.
class Pty {
public:
    // Throws std::system_error on failure, or std::invalid_argument for a rejected executable
    // (Windows: .cmd/.bat).
    static std::unique_ptr<Pty> spawn(const PtySpawnSpec& spec, PtyOutputSink on_output, PtyExitSink on_exit);
    ~Pty();

    void write(std::string_view bytes);
    void resize(PtySize size);
    void interrupt();                      // writes 0x03; the terminal turns it into SIGINT / Ctrl+C
    void terminate();                      // ends the whole process tree now
    std::optional<int> exit_code() const;  // nullopt while running
    [[nodiscard]] std::uint32_t pid() const noexcept;
    [[nodiscard]] std::size_t live_processes() const;  // root and descendants still running
    [[nodiscard]] ForegroundInfo foreground() const;

    // Lets the reader drain the last output, joins it, then ends what is left. Idempotent.
    void close();

    // Pause/resume between two reads (backpressure and handover).
    bool pause_reader(std::chrono::milliseconds timeout);
    void resume_reader();

    struct Impl;
private:
    explicit Pty(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// What the platform's terminals can do; the host reports these in hello / status.
struct PtyCapabilities {
    bool answers_osc7501_query = false;  // Windows: bundled ConPTY only (ADR-08); macOS: always
    bool handover = false;               // see 5.5
};
PtyCapabilities pty_capabilities();

}  // namespace agentdesk::platform
```

和現在不一樣的地方：

- **等待程序結束改成回呼。** `Session::actor_loop` 現在用 `WaitForMultipleObjects` 同時等佇列事件和程序 handle。改成 `PtyExitSink` 往 actor 佇列丟一則「root 已結束」訊息，actor 只要等自己的佇列（`std::condition_variable` 加逾時），就不用再碰任何原生 handle。Windows 實作用 `RegisterWaitForSingleObject`；macOS 用 kqueue 的 `EVFILT_PROC`／`NOTE_EXIT`，或一條 `waitpid` 執行緒。
- **`ConPtyApi` 不再外露。** 載入隨附 ConPTY 的工作留在 Windows 實作內部（第一次 `spawn` 時載入，或由 host 啟動時呼叫 Windows 專用的初始化函式）。`HostConfig::conpty` 改成 `PtyCapabilities`。
- **`job_active_processes()` 改名為 `live_processes()`。** Windows 讀 Job 的計數；macOS 列出該程序群組（`proc_listpids` 加 `PROC_PGRP_ONLY`）。

macOS 實作重點：

| 項目 | 做法 |
|---|---|
| 建立 | `posix_openpt` 開 master／slave，`posix_spawn` 加 `POSIX_SPAWN_SETSID`，slave 接 stdin／stdout／stderr；在子程序設控制終端機需要 `TIOCSCTTY`，`posix_spawn` 做不到時改用 `fork` 加 `execve`（`fork` 和 `execve` 之間只能做 async-signal-safe 的事） |
| 不經 shell | 直接 `execve(executable, argv)`，沒有 `.cmd` 那類問題，但仍拒絕非一般檔案或沒有執行權限的檔案 |
| 尺寸 | `ioctl(master, TIOCSWINSZ)` |
| 中斷 | 寫入 0x03，由 line discipline 送 SIGINT 給前景程序群組 |
| 結束整棵程序樹 | `killpg(pgid, SIGHUP)`，等 `config_.stop_wait`（5 秒）後再 `SIGKILL`。在背景改了自己程序群組的子孫抓不到，這是和 Job Object 的已知差異，寫進風險 |
| host 當掉時 | master fd 隨 host 一起關閉，session leader 收到 SIGHUP，shell 跟著結束。行為和 Job 的 kill-on-close 接近，但不保證不理 SIGHUP 的程式會結束 |
| 前景程式 | `tcgetpgrp(master)` 取前景程序群組，比 Windows 那套「shell 最新的直接子程序」規則可靠；cwd 用 `proc_pidinfo(PROC_PIDVNODEPATHINFO)`，argv 用 `sysctl(KERN_PROCARGS2)` |
| OSC 7501 | Unix PTY 不會攔截查詢回覆，`answers_osc7501_query` 一律為 true，不需要隨附元件 |

### 5.2 一般子程序（`process.hpp`）

`git_tools.cpp`、`system_process.cpp`、ssh bridge（`connect_over_command`）和啟動 host 都需要「不經 shell 起一個程式、接它的 stdin／stdout」。目前這些各寫各的 `CreateProcessW`，收成一個介面。

```cpp
namespace agentdesk::platform {

struct ProcessSpec {
    std::filesystem::path executable;
    std::vector<std::string> args;
    std::filesystem::path cwd;  // empty: inherit
    std::map<std::string, std::string> env_overrides;
    bool pipe_stdin = false;
    bool pipe_stdout = true;
    bool merge_stderr = false;
    bool end_with_parent = true;  // Windows: kill-on-close job; macOS: own process group, killed in ~Process
};

// A child that is not a terminal. Never inherits anything except the pipes it asked for.
class Process {
public:
    static std::unique_ptr<Process> spawn(const ProcessSpec& spec);
    ~Process();  // with end_with_parent: ends the child if still running

    // Blocking stream I/O; return false at end of stream / broken pipe.
    bool write(std::string_view bytes);
    void close_stdin();
    std::optional<std::string> read_some(std::size_t max);  // nullopt at EOF

    std::optional<int> wait(std::chrono::milliseconds timeout);
    void kill();
    [[nodiscard]] std::uint32_t pid() const noexcept;

    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};

// Runs to completion and returns stdout (git, version checks). Kills it after `timeout`.
struct RunResult {
    int exit_code = -1;
    std::string out;
    bool timed_out = false;
};
RunResult run_capture(const ProcessSpec& spec, std::chrono::milliseconds timeout);

// A background process that outlives its launcher (agent-host; the app after an update).
// Windows: DETACHED_PROCESS + breakaway as today. macOS: posix_spawn + POSIX_SPAWN_SETSID,
// stdio to /dev/null.
struct DetachedResult {
    std::uint32_t pid = 0;
    bool dies_with_launcher = false;  // Windows only: breakaway denied inside a kill-on-close job
};
DetachedResult spawn_detached(const std::filesystem::path& exe, const std::vector<std::string>& args);

// PATH lookup without running anything (Windows: SearchPathW + app execution aliases).
std::optional<std::filesystem::path> find_executable(std::string_view name);

}  // namespace agentdesk::platform
```

`command_line.hpp`（`CommandLineToArgvW` 的引號規則）留在 Windows 實作內部，不出現在平台中立的介面上。

### 5.3 本機 IPC（`ipc_transport.hpp`、`instance_lock.hpp`）

`libs/ipc` 的分幀、佇列預算（4 MiB 可丟、16 MiB 斷線）、request／response 配對、`hello` 協商都和平台無關，問題出在 `Connection`、`PipeServer`、`PipeClient` 直接拿著 Named Pipe handle 做 overlapped I/O。做法是在它們下面墊一層「位元組串流」，上面的邏輯不動。

```cpp
namespace agentdesk::platform {

// A connected, full-duplex byte stream. Blocking reads on one thread, writes from another;
// cancel() wakes a blocked read and makes later calls fail. Thread-safe as described.
class ByteStream {
public:
    virtual ~ByteStream() = default;
    virtual std::size_t read_some(std::span<char> buf) = 0;  // 0 = closed
    virtual bool write_all(std::string_view bytes) = 0;
    virtual void cancel() = 0;
};

// Who is on the other end, checked by the server before any message is read.
struct PeerIdentity {
    std::uint32_t pid = 0;
    bool same_user = false;  // Windows: client SID == ours; macOS: getpeereid() uid == geteuid()
};

// A per-user local endpoint name; the platform turns it into a path or pipe name.
//   Windows: \\.\pipe\AgentDesk-<SID>-s<session>
//   macOS:   $TMPDIR/AgentDesk/host.sock  ($TMPDIR is per user; folder 0700, socket 0600)
std::string default_endpoint();

class StreamListener {
public:
    // Throws std::system_error when another live process owns the endpoint.
    static std::unique_ptr<StreamListener> listen(const std::string& endpoint);
    ~StreamListener();
    // Blocks until a client connects or stop() is called (then returns nullptr).
    // Clients that are not the same user are rejected and counted, never returned.
    std::unique_ptr<ByteStream> accept(PeerIdentity* peer);
    void stop();
    [[nodiscard]] std::uint64_t rejected_clients() const noexcept;
    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};

// Connects with the same owner check on the server side; nullptr on timeout.
std::unique_ptr<ByteStream> connect_stream(const std::string& endpoint, std::chrono::milliseconds timeout);
// A child's stdin/stdout as one stream (ssh -T ... agentctl bridge).
std::unique_ptr<ByteStream> process_stream(std::unique_ptr<Process> child);

// Only one agent-host per user (+ logon session on Windows).
// Windows: named mutex Local\AgentDesk-host-...; macOS: flock() on $TMPDIR/AgentDesk/host.lock.
class InstanceLock {
public:
    static std::unique_ptr<InstanceLock> try_acquire(const std::string& name);  // nullptr if held
    ~InstanceLock();
};

}  // namespace agentdesk::platform
```

上層調整：`ipc::Connection` 和 `ipc::PipeClient` 改為持有 `std::unique_ptr<platform::ByteStream>`；`PipeServer` 改名為 `ipc::Server`，接受迴圈用 `StreamListener`；`open_raw_pipe` 改成 `connect_stream`；`PipeClient::adopt(read, write)` 改成接受任一 `ByteStream`。端點名稱在介面上從 `std::wstring` 改為 UTF-8 `std::string`，`--pipe NAME` 參數保留原名稱以免動到現有腳本。

macOS 注意事項：

- Unix socket 路徑上限 104 bytes，`$TMPDIR`（`/var/folders/xx/.../T/`）大約 50 bytes，夠用；但 `AGENTDESK_DATA_DIR` 之類可以自訂的位置不能拿來放 socket。
- 啟動時遇到殘留的 socket 檔：先試連，連不上才 `unlink`，而且要在拿到 `InstanceLock` 之後才做。
- 安全邊界和 Windows 相同：只防其他使用者，不防同帳號的惡意程式（設計文件 3.7）。

### 5.4 系統資訊、檔案與雜項（`system.hpp`）

```cpp
namespace agentdesk::platform {

// Folders. AGENTDESK_DATA_DIR overrides data_dir() on every platform.
std::filesystem::path data_dir();        // Windows: %LOCALAPPDATA%\AgentDesk; macOS: ~/Library/Application Support/AgentDesk
std::filesystem::path executable_path(); // GetModuleFileNameW / _NSGetExecutablePath
std::filesystem::path home_dir();

// Replaces `target` with `temp` in one step (ReplaceFileW / rename()); the old file stays if this fails.
void replace_file_atomically(const std::filesystem::path& temp, const std::filesystem::path& target);

std::string computer_name();                // GetComputerNameA / gethostname, UTF-8
std::string new_uuid();                     // lower-case, with dashes
void random_bytes(std::span<std::byte> out);  // BCryptGenRandom / arc4random_buf
std::array<std::byte, 32> sha256_file(const std::filesystem::path& file);  // BCrypt / CommonCrypto

// Shells offered as profiles: Windows pwsh, powershell, cmd, WSL distros (ADR-16);
// macOS: $SHELL first, then /etc/shells (zsh, bash, fish ...).
struct ShellCandidate {
    std::string id;
    std::string name;
    std::filesystem::path executable;
    std::vector<std::string> args;  // macOS login shells get "-l"
};
std::vector<ShellCandidate> detect_shells();

// Changes inside a folder (git status refresh). The callback runs on a platform thread and may be
// called for unrelated changes; callers debounce. Windows: FindFirstChangeNotificationW;
// macOS: FSEvents.
class FolderWatch {
public:
    static std::unique_ptr<FolderWatch> start(const std::filesystem::path& folder, std::function<void()> on_change);
    ~FolderWatch();
};

}  // namespace agentdesk::platform
```

`dynamic_library.hpp`（mod 用）：

```cpp
namespace agentdesk::platform {
// mods\<name>\mod.dll (Windows) or mods/<name>/mod.dylib (macOS); absolute path, no search order.
std::string_view mod_library_file_name();
class DynamicLibrary {
public:
    static std::unique_ptr<DynamicLibrary> load(const std::filesystem::path& file, std::string* error);
    void* symbol(const char* name) const;  // nullptr if missing
    ~DynamicLibrary();
};
}
```

mod 的 C ABI（`mod_api.h`）不變，但 mod 要依平台各自編譯，相容性規則（ADR-29）要多一條「平台不同就不載入」。

### 5.5 host 熱更新（`handover.hpp`）

這是平台差異最大的一塊。Windows 版把每個終端機的 handle `DuplicateHandle` 給新 host，再用 `ConptyPackPseudoConsole` 重組 HPCON（ADR-19、OQ-09）。macOS 可以用 Unix socket 的 `SCM_RIGHTS` 把 PTY master fd 傳給新 host，但「新 host 逐一驗證收到的東西」要重新設計：Windows 有 Job 成員資格和 kill-on-close 可以查，macOS 只能核對 fd 是不是 PTY master、`tcgetpgrp` 和 `getsid` 對不對得上。

草案只先定介面的形狀：

```cpp
namespace agentdesk::platform {

// Everything one terminal needs to keep running in another host process. Opaque to the host;
// only the platform knows what is inside (Windows: duplicated handles; macOS: fds).
class PtyHandoverState;

struct HandoverSupport {
    bool supported = false;
    std::string why_not;  // for the log and the update dialog
};
HandoverSupport handover_support();

// Added to class Pty (5.1):
//
//   // Old host: with the reader paused, packs this terminal for `child_pid` (the new host
//   // process) and sends it over `channel`. *this stays the owner until release_after_handover().
//   bool send_for_handover(ByteStream& channel, std::uint32_t child_pid, std::string* why);
//   void release_after_handover();
//   // New host: receives, checks and takes ownership. Throws if anything does not check out.
//   static std::unique_ptr<Pty> adopt(ByteStream& channel, PtyOutputSink on_output, PtyExitSink on_exit);

}  // namespace agentdesk::platform
```

使用者決定 macOS 第一版就要支援熱更新（第 8 節第 4 項），`handover_support().supported` 在兩個平台都是 true。macOS 的做法：

1. 舊 host 用 `socketpair` 開私有通道，啟動新 host（`--takeover`），把通道的一端交給它。
2. 凍結每個 session（reader 停在 parser 的 ground state、輸入清空、不再監看結束），用 `SCM_RIGHTS` 把 PTY master fd 傳過去，連同 root pid、程序群組、session id 與引擎狀態。
3. 新 host 逐一核對：fd 是 PTY master（`ptsname` 成功）、`tcgetpgrp(fd)` 的群組屬於該 session（`getsid` 等於 root pid）、root pid 還活著且 uid 相同。任何一項不符就整個放棄，舊 host 照常繼續。
4. 之後的 ready／go 交接順序與 Windows 相同。新 host 不能 `waitpid` 不是自己子程序的 root，改用 kqueue `EVFILT_PROC`／`NOTE_EXIT` 監看結束；exit code 拿不到時記為 unknown，這是和 Windows 的差異（設計文件 5.2、第 9 章）。

Windows 照現行做法，不受影響。

### 5.6 視窗外殼（`apps/desktop/shell/window_shell.hpp`）

`App` 本身只靠 ImGui，真正綁 Win32 的是 `AppConfig::hwnd` 和 `main.cpp`。把 `main.cpp` 的工作切成「外殼」和「App」兩邊，外殼負責視窗、繪圖裝置、事件迴圈、標題列和輸入法；App 照舊只管 ImGui 畫面。

```cpp
namespace agentdesk::desktop {

// What App may ask of the window it lives in. Implemented by the platform shell.
class WindowHost {
public:
    virtual ~WindowHost() = default;
    virtual void request_frame() = 0;                // replaces AppConfig::wake
    virtual void set_title(const std::string& utf8) = 0;
    virtual void minimize() = 0;
    virtual void toggle_maximize() = 0;
    virtual bool is_maximized() const = 0;
    virtual void begin_drag() = 0;                    // custom title bar: start a window move
    // Areas of the custom title bar, in window pixels: drag area vs. buttons. The shell uses them
    // for WM_NCHITTEST (Windows) or the NSWindow drag region (macOS).
    virtual void set_title_bar_regions(std::span<const Rect> drag, std::span<const Rect> no_drag) = 0;
    // Space the system reserves in the title bar (macOS traffic lights; zero on Windows, which
    // draws its own buttons).
    virtual Insets title_bar_insets() const = 0;
    virtual float content_scale() const = 0;         // DPI / Retina factor
    virtual void set_ime_rect(Rect caret) = 0;       // where the IME candidate window goes
    virtual void flash_attention() = 0;              // taskbar flash / dock bounce
    virtual void notify(const std::string& title, const std::string& body) = 0;
    virtual void play_sound(const std::filesystem::path& file) = 0;
};

// Platform services the UI library uses (libs/ui), not tied to a window.
struct ClipboardContent;                // as today: files, text, image flag
ClipboardContent read_clipboard();      // Windows: CF_HDROP / CF_UNICODETEXT; macOS: NSPasteboard
void write_clipboard_text(const std::string& utf8);
void open_with_system(const LinkTarget& target);  // ShellExecuteW / NSWorkspace openURL; programs are revealed, never run
void reveal_in_file_manager(const std::filesystem::path& path);  // Explorer /select / NSWorkspace activateFileViewerSelectingURLs

// Fonts the platform provides for fallback and the font pickers.
struct SystemFonts {
    std::vector<std::filesystem::path> cjk;     // Windows: msjh.ttc; macOS: PingFang.ttc
    std::vector<std::filesystem::path> symbol;  // Segoe UI Symbol / Apple Symbols
    std::vector<std::filesystem::path> emoji;   // Segoe UI Emoji / Apple Color Emoji
    std::filesystem::path ui_fallback;          // Segoe UI / SF Pro (bundled Geist missing)
    std::filesystem::path mono_fallback;        // Cascadia Mono / Menlo
};
SystemFonts system_fonts();
std::vector<ui::FontFamily> installed_font_families();  // registry / CTFontManager

int run_shell(const ShellOptions& options);  // main loop; replaces wWinMain's body

}  // namespace agentdesk::desktop
```

兩個平台的實作：

| 項目 | Windows（現有，搬家） | macOS（新增） |
|---|---|---|
| 視窗與繪圖 | Win32＋DX11、`imgui_impl_win32`／`imgui_impl_dx11` | NSWindow＋MTKView、`imgui_impl_osx`／`imgui_impl_metal`，用 Objective-C++ 寫，不另外加 GLFW 或 SDL |
| 自繪標題列 | `WM_NCCALCSIZE`／`WM_NCHITTEST` | `NSWindowStyleMaskFullSizeContentView`＋`titlebarAppearsTransparent`；保留系統紅綠燈按鈕，介面左邊讓出 `title_bar_insets()`；拖曳區域靠覆寫 `mouseDownCanMoveWindow` 或 `performWindowDragWithEvent:` |
| 輸入法 | IMM（ImGui backend 處理） | `NSTextInputClient`；`imgui_impl_osx` 只有基本支援，注音選字窗位置和組字中的文字（marked text）可能要自己補，第 10 步先做 PoC 確認 |
| 保留快捷鍵 | 低階鍵盤 hook（Win 鍵組合） | 不需要；Cmd+Q、Cmd+W 等交給選單列，`keys.json` 的預設值改用 Cmd |
| 檔案拖放 | `WM_DROPFILES` | `NSDraggingDestination` |
| DPI | `GetDpiForMonitor`、`WM_DPICHANGED` | `backingScaleFactor`、`viewDidChangeBackingProperties` |

快捷鍵照 herdr-app 在 macOS 的做法（第 8 節第 5 項）：herdr-app 有一份獨立的預設鍵表 `defaults/keys.macos.json`，以 Cmd 為主，`ctrl-1..9` 用來選 space；設定另有 macOS 專用的 `option_as_alt`（`"auto"|"off"|"on"|"left"|"right"`，設定頁的 Keyboard 區）。AgentDesk 照做：

- 新增 macOS 專用的預設鍵表（內容照 herdr-app 的 `keys.macos.json` 轉寫，開工前先讀原始碼逐條對照，寫進 `docs/research/herdr-app/`）。
- `Keymap` 的和弦語法加 `cmd` 修飾鍵，只在 macOS 有效。
- 設定加 `option_as_alt`，決定 Option 鍵在終端機裡要不要當成 Alt（ESC 前綴）送出。
- 使用者的 `keys.json` 照字面解讀，不做 Ctrl／Cmd 自動轉換。終端機裡的 Ctrl 一律照原樣送給程式。

## 6. 對照表：現有程式搬到哪裡

| 現有 | 新介面 | Windows 實作 | macOS 實作 |
|---|---|---|---|
| `platform::win::PtyProcess`、`KillOnCloseJob`、`conpty_api` | `platform::Pty`（5.1） | `windows/pty_conpty.cpp` | `posix/pty_unix.cpp`＋`macos/process_info.cpp` |
| `process_tree.hpp` | `Pty::foreground()` | `windows/process_tree.cpp` | `tcgetpgrp`＋`libproc` |
| `detached.hpp`、`git_tools.cpp`、`ui/system_process.cpp` | `platform::Process`、`run_capture`、`spawn_detached`（5.2） | `windows/process.cpp` | `posix/process.cpp` |
| `ipc/pipe.cpp` 的 handle 部分 | `ByteStream`、`StreamListener`（5.3） | `windows/named_pipe.cpp` | `posix/unix_socket.cpp` |
| `apps/host/main.cpp` 的 mutex | `InstanceLock` | named mutex | `flock` |
| `store.cpp`（路徑、`ReplaceFileW`、UUID、WSL） | `system.hpp`（5.4） | `windows/system.cpp` | `macos/system.cpp` |
| `updates.cpp`、`telemetry.cpp` 的 BCrypt | `sha256_file`、`random_bytes` | BCrypt | CommonCrypto、`arc4random_buf` |
| `git_cache.cpp` 的變更通知 | `FolderWatch` | `FindFirstChangeNotificationW` | FSEvents |
| `ui/mods.cpp` 的 `LoadLibraryExW` | `DynamicLibrary` | `LoadLibraryExW` | `dlopen(RTLD_NOW | RTLD_LOCAL)` |
| `handover.cpp` | `handover.hpp`（5.5） | 現有做法 | 第一版不支援 |
| `apps/desktop/main.cpp`、`AppConfig::hwnd` | `WindowHost`、`run_shell`（5.6） | `shell/win32_dx11.cpp` | `shell/macos_metal.mm` |
| `ui/clipboard.cpp`、`links.cpp` 的開啟、`fonts.cpp` 的系統字型 | 5.6 的服務函式 | 現有程式 | AppKit／CoreText |
| `apps/cli/main.cpp` 的 Win32 呼叫 | 上面各介面 | — | — |
| 隨附 ConPTY、`fetch_conpty.exe` | 只在 Windows 建置 | 不變 | 不需要 |

## 7. 重構順序（Windows 先行）

每一步都是一個可以單獨合併的 PR，`ctest` 全綠、截圖基準不變才進下一步。

1. **字串與路徑**：`PtySpawnSpec::args`、`Profile::args`、端點名稱等介面改為 UTF-8；Windows 實作在邊界轉換。純機械改動，量最大但風險最低。
2. **Session 不再等原生 handle**：加 `PtyExitSink`，actor 迴圈改為只等自己的佇列。這步改到 Session 的 actor 執行緒，要補一個「程序結束與大量輸出同時發生」的測試，確認最後的輸出不會在結束事件之後才到。
3. **`platform::Pty` 介面**：`PtyProcess` 改為 `Pty` 的 Windows 實作，`ConPtyApi` 收進實作內部。
4. **`Process`、`spawn_detached`、`find_executable`**：合併三份 `CreateProcessW`。
5. **`ByteStream`／`StreamListener`**：`ipc` 改為走串流介面。整合測試 `win_integration_tests` 的 pipe 部分要全部通過。
6. **`system.hpp`、`DynamicLibrary`、`FolderWatch`**。
7. **視窗外殼**：`main.cpp` 拆成 `WindowHost` 實作，`App` 不再引用 `<windows.h>`。
8. **CMake 分平台**：`libs/platform/windows` 只在 `WIN32` 編譯；在 Windows 上確認 `libs/terminal`、`libs/protocol`、`libs/host`、`libs/ui` 的標頭檔都不再引用 `<windows.h>`（加一個 CI 檢查）。

做完這 8 步，Windows 版的對外行為不變，接著分三步補 macOS 實作：

9. macOS：`Pty`、`Process`、`ByteStream`、`system.hpp`，先讓 `agent-host`、`agentctl`、`pty_run`、`vt_probe` 跑起來，用 `unit_tests` 加新的 POSIX 整合測試驗證背壓、detach／reattach、OSC 7501。
10. macOS：host 熱更新（5.5），沿用 Windows 的 handover 測試情境。
11. macOS：視窗外殼、輸入法、字型、剪貼簿、快捷鍵（`keys.macos.json`、`option_as_alt`）。
12. macOS：mod、打包（`.app`、codesign、notarization）、更新。

第一版做到第 12 步（第 8 節第 3 項）。

## 8. 使用者的決定（OQ-10，2026-11-24）

| # | 問題 | 決定 | 影響 |
|---|---|---|---|
| 1 | 要不要做 macOS 版 | 做 | 設計文件新增 ADR-30；PRD 契約升為 3.2.0（FR-39、FR-40） |
| 2 | 開發工具 | 放寬 ADR-11：macOS 上可使用 Xcode Command Line Tools（clang、Metal、AppKit 標頭）與 vcpkg | Windows 開發環境的限制不變 |
| 3 | 第一版範圍 | 一次做到完整版（第 7 節第 12 步） | 包含介面、輸入法、mod、打包 |
| 4 | host 熱更新 | 第一版就要支援 | 5.5 改為 `SCM_RIGHTS` 方案，排在第 10 步 |
| 5 | 快捷鍵 | 照 herdr-app 在 macOS 的行為 | 5.6：`keys.macos.json`、`cmd` 修飾鍵、`option_as_alt` |
| 6 | 硬體與版本 | 只支援 Apple Silicon（arm64），最低 macOS 13 Ventura | preset 只建 `arm64`；CI 用 macOS arm64 runner |

## 9. 風險

| 風險 | 影響 | 對策 |
|---|---|---|
| macOS 沒有 Job Object，改了程序群組的子孫在 session 結束後可能還活著 | 違反「session 結束時整棵程序樹一起結束」的期望 | 結束時對以這個終端機為控制終端機的所有程序送訊號，不只根程序群組（已實作，設計文件 3.4） |
| `imgui_impl_osx` 的輸入法支援不完整 | 注音、日文輸入選字窗位置錯、組字中文字不顯示 | 第 11 步先做輸入法 PoC，不行就自己實作 `NSTextInputClient` |
| macOS 新 host 無法 `waitpid` 接手的程序 | 熱更新後的 session 結束時拿不到 exit code | 用 `EVFILT_PROC` 監看結束、exit code 記為 unknown；設計文件寫明差異 |
| 第 2 步改動 Session 的等待方式 | 程序結束時遺漏最後的輸出，或關閉時死結（背壓解除順序） | 保留現有順序：先解除背壓、再 close、再 drain；補壓力測試 |
| 重構期間 Windows 版退化 | 已驗收的功能壞掉 | 每步獨立 PR、全測試加截圖比對；第 1～8 步不改任何對外行為 |
| 自繪標題列在 macOS 和系統紅綠燈按鈕衝突 | 版面和 herdr-app 不一致 | 標題列左側讓出 `title_bar_insets()`；細節要查 herdr-app 在 macOS 的做法 |
| macOS 沒有程式碼簽章與公證 | Gatekeeper 擋下執行 | 需要 Apple Developer 帳號，屬於發布問題，第 12 步處理；帳號還沒決定（OQ-11） |

## 10. 實作狀態（2026-11-24）

macOS 部分（第 9～12 步）已在分支 `feature/sprint-11/macos-port` 完成，平台差異對照寫在設計文件第 9 章，待人工驗收的項目在設計文件 12.2。和這份草案不同的地方：

- Windows 先行的第 1～8 步還沒做：為了讓 Windows 程式少動、之後和 Windows 端的修改合併時少衝突，共用程式暫時沿用 `platform::win` 的名稱：macOS 上由 `libs/platform/posix/compat/` 提供同名的標頭檔，接到第 5 節的中立介面。第 1～8 步做完時，這一層就拿掉。
- 平台差異在建置時分開（2026-11-24 重構）：共用的 `.cpp` 不再有 `#ifdef _WIN32`。各模組不同的部分放在小的內部介面後面（`host_os.hpp`、`fonts_os.hpp`、`keymap_os.hpp`、`keyboard_os.hpp`、`links_os.hpp`、`mods_os.hpp`、`updates_os.hpp`、`apps/desktop/desktop_os.hpp`、`apps/cli/cli_os.hpp`），實作分成 `*_win.cpp`（或 `win/`）與 `*_posix.cpp`（或 `macos/`），由 CMake 挑選。宣告本身不同的標頭檔放在 `include_win/`、`include_posix/`，靠 include 路徑挑選。剩下的 `#ifdef` 只有第 5 節中立介面裡 POSIX 專用的成員，以及給第三方用的 `mod_sdk.hpp` 匯出巨集。原本整份是 Win32 的檔案（`pipe.cpp`、`handover.cpp`、`apps/host/main.cpp`、`apps/desktop/main.cpp`）留在原位，方便合併。
- **5.1**：`Pty` 多了熱更新用的 `read_available`、`handover_fd`、`release_after_handover`、`adopt`（只在 POSIX 上有）。root 程序結束一律用 kqueue 監看，熱更新時才能停止監看還在執行的程式。
- **5.2**：`ProcessSpec` 多了 `stderr_fd`（POSIX），讓 ssh 和 git 的 stderr 可以分開收。
- **5.3**：`--pipe` 名稱不含 `/` 時，對應到 runtime 資料夾裡的 `<名稱>.sock`。
- **5.4**：多了 `runtime_dir()`、`login_path()`、`find_on_login_path()`；`FolderWatch` 沒有做成共用介面，git 與 Files／Changes 的監看在 macOS 上改用輪詢修改時間（`macos/git_cache_macos.cpp`、`macos/side_panels_macos.cpp`）。
- **5.6**：`WindowHost` 沒有做成虛擬介面，改成 `apps/desktop/window_ops.hpp` 的幾個函式（Windows 在 `win/window_ops_win.cpp`，macOS 由外殼 `macos/main_macos.mm` 實作）。

