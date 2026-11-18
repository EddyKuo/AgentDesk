---
sprint: 10
updated_at: 2026-11-18T23:59:00+08:00
plan: docs/design/AgentDesk_Design.md 5.6
---

# Sprint 10（herdr-app 移植）：狀態與決策紀錄

只追加不改寫。每一列寫明依據的 herdr-app 檔案與行號（commit 321c9a8）；沒有 herdr 依據的決策要寫原因。

| 時間 | 項目 | 決策 | 依據 | commit |
|------|------|------|------|--------|
| 2026-11-18 | 終端機有焦點時，第一下點擊被吃掉，分頁與 space 的右鍵選單打不開 | 修正：終端機只為了接收鍵盤而保持作用中，其他元件照常可以 hover 與點擊；在終端機裡開始的拖曳選取除外 | herdr 點一下就有反應；這是 ImGui 作用中元件規則造成的錯誤，herdr 沒有對應程式 | 178c716 |
| 2026-11-18 | 分頁太寬時，「+」新增選單點不開 | 修正：分頁列的裁切範圍同時限制點擊範圍，被裁掉的分頁 ✕ 不再蓋住「+」 | mods/tabs：分頁列 flex_1，超出的部分看不到也點不到 | 178c716 |
| 2026-11-18 | Add machine 打開時輸入框沒有焦點 | 修正：打開時焦點在輸入框 | mods/machine-add/main.js 73-74 | 178c716 |
| 2026-11-18 | Files 預覽無法顯示時，訊息被截斷 | 修正：介面字型、dim 色、內距 12、自動換行 | mods/files/main.js 177 | 178c716 |
| 2026-11-18 | 面板標題列的 ✕ 關不掉面板 | 修正：按住按鈕時標題列仍算 hover，按鈕不會在放開前消失 | mods/pane/main.js 86-90 | 578af17 |
| 2026-11-18 | 自己關掉面板也跳出「exited with code 1」 | 修正：只有自己結束、2 秒內、錯誤碼不是 0（127 不限時間）才提示；標題「Terminal failed to start」，內容是錯誤碼加最後 5 行 | src/window.rs 142-157、2463-2481 | 578af17 |
| 2026-11-18 | space 的名稱 | 不改：沒命名時用第一個終端機所在 checkout 的頂層資料夾名稱，不在 git 裡就用它的資料夾名稱，現有實作已經相同 | src/model.rs 271-280、635-647 | — |
| 2026-11-18 | 側欄 Agents 區塊 | 不改：只列出回報過 OSC 7501 狀態的終端機，顯示目前狀態，現有實作已經相同 | src/window.rs 1876、src/model.rs 597-605 | — |
| 2026-11-18 | 開發用 UI 腳本 `agent-ui --input`、`screenshot_ui --input` | 新增：在 agent-ui 視窗內把滑鼠、按鍵、文字餵給 ImGui，用來驗證點擊流程；不出貨、不碰其他視窗 | 驗證工具，herdr 沒有對應 | 178c716 |
| 2026-11-18 | Board 版面在視窗窄時欄位太窄 | 未改，待決定：herdr 沒有 Board 版面，沒有依據可以照做 | — | — |
| 2026-11-18 | Release 版 `agent-host` | 未重建：使用者的 host 正在執行，重建要先停掉它（會中斷 session）。`agent-ui.exe` 已重建 | — | — |
| 2026-11-19 | Board、Grid、Focus 版面 | 使用者決定移除：herdr-app 只有 main、terminals、win98 三個版面，自訂 JSON 版面保留 | src/window.rs LAYOUTS；使用者 2026-11-19 | — |
| 2026-11-19 | Herdr 98 版面、線上更新檢查、使用統計 | 使用者決定三項都要做（原本列為刻意保留的差異） | defaults/mods/win98、frame/settings Updates／Privacy、src/telemetry.rs；使用者 2026-11-19 | — |
| 2026-11-19 | 移除 Board、Grid、Focus | 完成：內建版面只剩 workspace（herdr 的 main）與 terminals；版面檔不再有 `main` 鍵 | src/window.rs LAYOUTS | 69519d6 |
| 2026-11-19 | 設定 › Look 的版面卡片 | 照 herdr 改：卡片寬 124、62 px 預覽圖、名稱在下方、選中的加打勾與強調色外框 | frame/settings/main.js 242-320 | 69519d6 |
| 2026-11-19 | 沒有預設伺服器 | 更新只在設了 `AGENTDESK_RELEASES_URL` 時檢查，使用統計只在設了 `AGENTDESK_TELEMETRY_URL` 時送出；兩者都沒有內建網址，等同 herdr 從原始碼建置的模式 | src/update.rs 8-11、72-74、122-129；src/telemetry.rs 12-16、100-106 | ec34a06 |
| 2026-11-19 | 版本比較、檢查時間、啟動時套用 | 照 herdr：semver 比較（rc 排在正式版前）、檢查失敗就清掉「上次成功時間」、啟動時只套用比目前新的版本且只重開一次（`AGENTDESK_UPDATE_EXPECTED`） | src/update.rs 201-203、402-417、436-441、459-479 | ec34a06 |
| 2026-11-19 | 檢查與下載流程 | 照 herdr：啟動 10 秒後檢查、之後每小時、失敗 30 分鐘後重試；讀 `app/latest.json`，取 `targets["windows-x86_64"]` 的 url 與 sha256，用 System32 的 curl 下載、比對 sha256、解壓到安裝資料夾旁的 `.download`，確認 `agent-ui --version` 相符後改名為 `.new` 並寫入 `.new.json` 收據 | src/update.rs 157-199、264-325、353-373、419-434 | ec34a06 |
| 2026-11-19 | 安裝資料夾的判斷 | 對應：herdr 看執行檔旁有沒有 herdr-app.svg；我們看有沒有 THIRD_PARTY_NOTICES.md（只有 zip 裡有，建置資料夾沒有） | src/update.rs 34-45；tools/package/package.cpp 171 | ec34a06 |
| 2026-11-19 | 壓縮檔格式 | 對應：herdr 是 tar.gz、裡面一個 herdr-app 資料夾；我們是 zip、裡面一個 `AgentDesk-<版本>-win-x64` 資料夾。System32 的 tar 用 `-xf` 解（加 `-z` 會拒絕 zip） | src/update.rs 47-48、309-325；tools/package/package.cpp 83 | ec34a06 |
| 2026-11-19 | `agent-ui --version` | 新增：回答 `agent-ui <版本>`（herdr 是 `herdr-app <版本>`），下載後用來確認版本 | src/update.rs 392-400 | ec34a06 |
| 2026-11-19 | 換版時機 | 照 herdr：按 Restart to update、下次結束程式、或上次沒正常結束時下次啟動前，把安裝資料夾改名為 `.old`、`.new` 改名回來，失敗就還原；Restart 後用 PowerShell 等本程序結束再開新版 | src/update.rs 481-542；src/main.rs 127-130、177-192；src/window.rs 3645-3657 | ec34a06 |
| 2026-11-19 | agent-host 的工作目錄 | 改為使用者家目錄（原本是自己的資料夾）：Windows 不能改名某個程序的工作目錄，否則更新永遠裝不上。herdr 為此把 app 移出安裝資料夾 | src/update.rs 443-457 | ec34a06 |
| 2026-11-19 | herdr-core 對應 agent-host | 對應：Updates 頁的第二列是 agent-host，版本由 hello 回報（新增 `host_version`）；設了伺服器才讀 `core/latest.json`，generation 對應管線協定版本 1 | src/update.rs 83-129、205-224；src/window.rs 879-887；src/hc.rs 207 | ec34a06 |
| 2026-11-19 | `app.update_core` | 未移植：herdr 執行 `hc update` 換掉 herdr-core 而終端機繼續跑；agent-host 沒有自我更新，終端機會跟著結束，且它本來就在 AgentDesk 的 zip 裡。指令保留 herdr 的「已是最新」檢查，有新版時回報要重開 AgentDesk 更新。待使用者決定（設計文件 7.4 OQ-09） | src/update.rs 236-262；src/window.rs 3604-3609 | ec34a06 |
| 2026-11-19 | 更新後清掉 `.old` | 對應：herdr 在 herdr-core 更新成功後刪 `.old`；我們在連上的 agent-host 已是新版本時才刪（舊 host 可能還在 `.old` 裡執行） | src/update.rs 581-621 | ec34a06 |
| 2026-11-19 | 更新的 release notes | 未移植：herdr 下載 `changelog.md` 給 What's new；AgentDesk 的 What's new 內容隨程式附帶 | src/update.rs 277-282；src/update_notes.rs | ec34a06 |
| 2026-11-19 | 設定 › Updates | 照 herdr：AgentDesk 列（What's new、Check now、Restart to update）、agent-host 列、說明文字與所有狀態文字；按 Check now 後顯示「Checking…」再顯示「✓ Up to date」3 秒；導覽列有更新時顯示 7 px 強調色圓點；圖示改為 refresh 與 shield | frame/settings/main.js 9-16、65-71、760-816、886-921 | ec34a06 |
| 2026-11-19 | 右下角更新提示 | 照 herdr：「AgentDesk X is ready」加 Restart、What changed／Open Updates、×；通知疊在它上方；agent-host 更新失敗的提示可關閉。「Updated to X」那一種不會出現，因為更新後 What's new 一定會自己打開 | src/window.rs 4727-4732、4800-4898、5455-5459 | ec34a06 |
| 2026-11-19 | 使用統計的收集 | 照 herdr：每個 UTC 日一份報告、today.json、跨日凍結到 queue、7 天後丟棄、隨機 id、送出失敗指數退避、400/404/410/413/422 直接丟棄、關掉時清空並換 id；`DO_NOT_TRACK`、`AGENTDESK_TELEMETRY=0`、CI 只關這一次 | src/telemetry.rs 27-183、352-697 | ec34a06 |
| 2026-11-19 | 報告內容 | 照 herdr：欄位與順序相同（serde_json 保留順序），分桶、版本只收正式格式、Agent 名稱來自 herdr 的 programs 表；os 為 windows、arch 為 x86_64 | src/telemetry.rs 187-315；src/programs.rs 212-237；defaults/programs.json | ec34a06 |
| 2026-11-19 | 觀察項目的對應 | 對應：Agent 名稱用 host 回報的程式名稱（agent-host 不轉傳 7501 的 `app`）；內建版面為 workspace、terminals；主題為我們的配色；內建 mod 指隨附的範例 mod，與隨附檔案相同才算未修改；只記錄 AgentDesk 自己的指令名稱，mod 與 `--run` 的指令不記 | src/window.rs 834-856、2995-2997；src/mods.rs 281-299 | ec34a06 |
| 2026-11-19 | ui.json 的 `telemetry` | 照 herdr：關閉寫 `false`，打開時刪掉這個鍵；檔案其他部分無法使用時仍讀它 | src/settings.rs 718-726；frame/settings/main.js 857-861 | ec34a06 |
| 2026-11-19 | 設定 › Privacy | 照 herdr：開關、說明、「What we send」的 Show／Hide，顯示待送報告、今天的草稿、上次送出的內容（JSON 兩格縮排、JetBrains Mono 12 px）；herdr 的 `toLocaleString` 改用 Windows 使用者地區格式 | frame/settings/main.js 819-882 | ec34a06 |
| 2026-11-19 | 驗證 | 單元與整合測試移植 herdr 的測試；本機假伺服器實測：下載、驗證、暫存、結束時換版、Restart 換版並重開、當掉後下次啟動換版、統計報告送到本機接收端 | src/update.rs 637-903；src/telemetry.rs 699-742 | ec34a06 |
