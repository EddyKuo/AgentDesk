# Code review：macOS 關閉終端機時結束 session 內的 job

- 日期：2026-11-26
- 範圍：分支 `fix/pty-close-session-jobs` 尚未 commit 的修改（7 個檔案，+58／-9）
  - `libs/platform/posix/src/proc_info.hpp`、`proc_info_macos.cpp`：新增 `posix::session_members(sid)`
  - `libs/platform/posix/src/pty_posix.cpp`：`signal_all()` 先列出成員再送訊號；`remaining()` 加上以 session id 列舉
  - `tests/integration/posix/pty_test.cpp`：新增 `PosixPty.CloseEndsJobsLeftByAShellThatExited`
  - 文件：設計文件 v3.6、`CLAUDE.md`、`sprint/current/status.md`
- 方式：`/code-review medium`，審查結果再對照原始碼確認
- 結論：可以合併，但建議先處理第 1 項（改動很小）。三項都已修正，見文末

## 發現

| # | 嚴重度 | 位置 | 問題 |
|---|---|---|---|
| 1 | important | `pty_posix.cpp:238` | shell 已被回收後，pid 被別的 session leader 重用，`close()` 會對那個 session 送 SIGHUP、SIGKILL |
| 2 | 既有問題 | `pty_posix.cpp:224` | `killpg(pid)` 有同樣的 pid 重用風險，只是範圍是一個 process group |
| 3 | nit | `proc_info_macos.cpp:107` | `session_members()` 每次掃過系統所有程序，`close()` 的等待迴圈每 5 ms 掃一次 |

### 1. pid 重用時可能結束別的終端機（important）

`waiter_loop` 用 `waitpid` 回收 shell 之後，`on_root_exit` 才呼叫 `close()`。`close()` 用同一個 pid 呼叫 `session_members(pid)` 與 `killpg(pid)`。

發生條件：

1. shell 和它留下的 job 都已結束（session 裡沒有任何程序）。
2. waiter 回收 shell，pid 釋放。
3. 在 `close()` 執行前，pid 繞回來被另一個呼叫 `setsid()` 的程序拿走，例如另一個 AgentDesk 終端機的 shell。
4. `session_members(pid)` 列出那個 session 的所有程序，兩輪訊號都送給它們。

只要舊 session 還有程序活著，XNU 就不會把這個 pid 配給新程序（它會檢查 pid、process group 與 session 是否還在用，這點依 XNU 原始碼的印象，未實測）。因此只有在舊 session 已經空了的時候才會出事，空窗也只有回收到 `close()` 之間這一段。機率很低，但後果是誤殺使用者別的工作，所以列為 important。

建議修法：`Impl` 已經知道自己回收過 root（`exit_code` 有值）。回收後如果 `kill(pid, 0)` 成功，代表 pid 已被別的程序拿走，這時跳過 `killpg(pid)` 與 `session_members(pid)`，只用先前列出的清單。另一種做法是記下 root 的啟動時間，只對啟動時間相符的 session 動作。

### 2. `killpg(pid)` 原本就有的 pid 重用風險（既有問題）

成因同第 1 項：root 被回收後，`pid` 可能變成別人的 process group id，`signal_all()` 的兩輪（含 SIGKILL）都會打到它。這次的改動沒有造成這個問題，只是把範圍從一個 group 擴大到整個 session。第 1 項的修法可以一併解決。

### 3. `session_members()` 的成本（nit）

`session_members()` 用 `PROC_ALL_PIDS` 列出所有程序，每個再呼叫 `getsid()` 與 `proc_pidinfo()`。`remaining()` 在 `close()` 的等待迴圈裡每 5 ms 呼叫一次，最長 2 秒；程序很多、job 又遲遲不結束時，`close()` 期間會有大量系統呼叫，而且 host 這時持有 `close_mutex`。

建議修法：完整掃描只在送訊號前做一次；等待迴圈只檢查已經列出的 pid 是否還活著（`kill(p, 0)` 或 `is_zombie`）。

## 沒有問題的部分

- 新測試解析 `job=<pid>` 的方式、`live_processes() == 1` 的預期，以及測試失敗時自己清掉留下的 job，都合理。
- 先列成員再送訊號的順序，正確處理了 session leader 結束後 tty 與 session 分開的情況。
- `CLAUDE.md` 的規範都有遵守：註解用英文，設計文件升版並加版本紀錄，`status.md` 只追加一列。

## 處理結果（2026-11-26，同一個 commit）

- [x] 第 1、2 項：spawn 與 adopt 時記下 root 的啟動時間（`posix::start_time`）。`root_pid_reused()` 發現這個 pid 上的程序啟動時間不同，就不再 `killpg(pid)`，也不再以 session id 列舉；終端機本身的程序仍由 tty 列出。root 還是 zombie 或已經沒有程序時，啟動時間相同或讀不到，照原本方式處理，所以 shell 先結束留下的 job 仍會被結束。pid 重用無法在測試中穩定重現，沒有另外加測試。
- [x] 第 3 項：`signal_all()` 回傳它送過訊號的程序，`close()` 的等待迴圈只檢查這些 pid（`posix::is_running`），不再每 5 ms 掃一次全部程序。
- 驗證：Debug、Release 各 452／452；`PosixPty.Close*` 四個測試重複 50 次都通過。
