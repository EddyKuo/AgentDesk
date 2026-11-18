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
