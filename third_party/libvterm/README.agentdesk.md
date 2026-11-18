# libvterm（vendored）

| 項目 | 值 |
|---|---|
| 版本 | 0.3.3 |
| 來源 | https://www.leonerd.org.uk/code/libvterm/libvterm-0.3.3.tar.gz |
| SHA-256（tarball） | `09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0` |
| 取得日期 | 2026-11-17 |
| 授權 | MIT（`LICENSE`） |
| 收錄內容 | `include/`、`src/`（含 tarball 已附的 `src/encoding/*.inc`）、`LICENSE` |
| 修改 | 只有一處新增，上游檔案原有內容沒動：agent-host 自我更新時把整個終端機狀態交給新版 host（設計文件 7.4 OQ-09）。新增 `include/vterm_agentdesk.h`、`src/agentdesk_handover.h`、`src/agentdesk_handover.c`，並在 `src/screen.c` 檔尾加上讀寫畫面緩衝區的函式（screen.c 的 cell 型別是檔案私有）。建置設定在 `CMakeLists.txt` |

vcpkg 沒有 libvterm port，所以以原始碼收錄（設計文件 3.1、OQ-07）。官方 tarball 已附產生好的 `.inc`，建置不需要 perl。
