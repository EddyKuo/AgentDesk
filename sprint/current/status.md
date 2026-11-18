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
| 2026-11-19 | Herdr 98 版面（win98） | 完成：第三個內建版面，自己畫桌面、工作列、Start 選單、窗格標題列、對話框、Mines、Solitaire、Clippy，程式碼在 `apps/desktop/win98_*.inl`、純邏輯在 `libs/ui/src/win98.cpp` | layouts/win98.json；mods/win98 全部 JS | 585481d |
| 2026-11-19 | Herdr 98 的尺寸 | 桌面欄 84、工作列 28、窗格標題列 18、邊框 3 用 Win98 像素；窗格外距 10、間距 8、終端機內距 [4, 2] 用版面 px（`px()`） | win98.json 18-96；kit.js 441-479 | 585481d |
| 2026-11-19 | Win98 像素怎麼換算 | 一個 Win98 像素＝scale（Small Fonts 1、Large Fonts 2）×螢幕 DPI，不乘 zoom；herdr 先除以 zoom 再由 zoom 乘回，結果相同 | kit.js 4-6、40-51 | 585481d |
| 2026-11-19 | Herdr 98 的字型 | 從 `win98\fonts` 用 ImGui 1.92 動態字型載入：介面 Pixelated MS Sans Serif（Regular／Bold），終端機 PxPlus IBM VGA 8x16；切進版面時換上、離開時還原；終端機 16 px，Large Fonts 為 32 px（32／zoom） | win98.json 92-94；README.md 8-10；kit.js 451-452 | 585481d |
| 2026-11-19 | 圖片成為 DX11 貼圖 | SVG 用 lunasvg 3.5.0（MIT，vcpkg，加 plutovg 1.3.3）依畫出的像素大小點陣化，PNG 用 WIC 解碼；以 ImGui 1.92 user texture 交給 DX11 後端建立，裝置遺失時由後端重建（`RegisterUserTexture` 只在 imgui_adapter 呼叫）。nanosvg 不支援 `<text>`、`<pattern>`，撲克牌用得到 | kit.js 202-225；sol.js 341-344；img/sol-*.svg | 2ad55b9 |
| 2026-11-19 | 單色 SVG（`svg().text_color()`） | 以 SVG 的形狀當遮罩填白，再用指定顏色著色，與 GPUI 的 svg 元素相同 | kit.js 222、233、302、351、384 | 2ad55b9 |
| 2026-11-19 | 撲克牌上的 DejaVu Serif | Windows 沒有這個字型，改用 Windows 內建的 Times New Roman（times.ttf、timesbd.ttf）給 lunasvg | img/sol-0-1.svg | 585481d |
| 2026-11-19 | herdr 的 overlay | popup（選單）、window（對話框、遊戲）、dialog（開機畫面、Solitaire 選項）都改成 ImGui 視窗：選單在旁邊按下就關，開啟它的那次按下不算；視窗依最後按下的順序疊放，按在控制項以外就拖曳；有 key 的遊戲視窗記住放下的位置；modal 畫 0.35 黑色遮罩並擋下旁邊的點擊 | herdr.d.ts 395-425；kit.js 356-386；window.rs 4767 | 585481d |
| 2026-11-19 | herdr 的 `shared()` | 持久化到 `<data>\win98.json`（scale、mute、desktop、clippy、welcome、boot、init、run、apps、mines、sol.draw／scoring／deck、float.clippy、start、menu.owner） | herdr.d.ts 451-454 | 585481d |
| 2026-11-19 | win98 主題 | `themes/win98.json` 做成 Palette「win98」，標為版面專用，不列在設定與命令面板的主題清單；終端機底色用窗格的 #000000（terminal_opacity 0）。選取色 #000080 改用 alpha 0x80，因為 AgentDesk 把選取畫在文字上面 | themes/win98.json；AGENTS.md 338-339；win98.json 46、97 | 585481d |
| 2026-11-19 | 終端機 ANSI 16 色 | 未做：AgentDesk 的 TerminalView 不依主題換 ANSI 色表（每個主題都一樣），win98 的 ansi 表沒有用上。待決定是否要讓 TerminalView 支援主題色表 | themes/win98.json `ansi` | — |
| 2026-11-19 | `layout:win98` 預設按鍵 | 預設 keys.json 加 `ctrl-escape`→`win98.start`、`ctrl-shift-escape`→`win98.taskman`。herdr 的 defaults/keys.json 沒有這段，依 README 加入。Windows 會先攔下 Ctrl+Esc（開始功能表）和 Ctrl+Shift+Esc（工作管理員），這兩個鍵在 Windows 上可能收不到 | mods/win98/README.md 12-17 | 585481d |
| 2026-11-19 | 版面段在沒有終端機聚焦時 | 視窗層級的快捷鍵也先查 `layout:<name>` 段，再查 global（原本只查 global） | src/keys.rs 115-127 | 585481d |
| 2026-11-19 | `win98.<command>` | 由 exec_command 轉給 Herdr 98 的工作列處理，只在 win98 版面顯示時生效；keys.json 驗證認得 README 列的命令與 close_tab、close_space | taskbar.js 126-176；README.md 12-17 | 585481d |
| 2026-11-19 | 縮到工作列（tab.settle） | herdr 讓最後一個顯示中的分頁也能 settle（仍顯示、標為 settled）；`Workspace::settle_tab` 加 `keep_shown`，只有 Herdr 98 使用，其他版面行為不變 | src/model.rs 470-484、857-862 | 585481d |
| 2026-11-19 | Run 對話框執行命令 | herdr 用 `sh -c "<cmd>\nexec $SHELL"`；Windows 改用 shell profile 執行後留著：PowerShell `-NoExit -Command <cmd>`、cmd `/K <cmd>`，其他 shell `-c`；session.create 加上 `args`。分頁名稱取第一個字並以 `/`、`\` 分段 | dialog.js 221-229 | 585481d |
| 2026-11-19 | `tab.new {name}` | Placement 加 `tab_name`，Herdr 98 開的分頁帶名字；沒有名字的分頁顯示序號（herdr 相同） | src/model.rs 283-288 | 585481d |
| 2026-11-19 | Start › Programs › Agents | herdr 用 `command -v` 找安裝的 agent；AgentDesk 改看 agent-host 有沒有同名 profile（claude、codex……）。圖示用 herdr defaults/icons 的 SVG（LobeHub，MIT） | taskbar.js 15-27、82-88 | 585481d |
| 2026-11-19 | 程式圖示 | 只移植 Start 選單那 11 個 agent 的圖示（含 tint、tile）；shell 和其他程式一律 MS-DOS 圖示。herdr programs.json 其他程式的圖示未移植，待補 | kit.js 210-225；defaults/programs.json | 585481d |
| 2026-11-19 | DOS 路徑 | herdr 的路徑以 `/` 開頭；Windows 路徑已有磁碟代號時保留，不再加 `C:`；家目錄比對不分大小寫 | kit.js 431-437 | 2ad55b9 |
| 2026-11-19 | 開機畫面的判斷 | herdr 用 `ps` 查 app 執行多久（45 秒內強制開機畫面）；AgentDesk 用 GetProcessTimes 查 agent-ui 自己 | taskbar.js 48-59、101-120 | 585481d |
| 2026-11-19 | About 的版本 | Version 用 AgentDesk 的版本；「herdr-core:」字樣保留、值是「?」（AgentDesk 沒有 herdr-core） | dialog.js 477、488 | 585481d |
| 2026-11-19 | Close Program 的位址 | herdr 顯示 `t3` 這類位址，AgentDesk 顯示 session id | dialog.js 247-251、521 | 585481d |
| 2026-11-19 | 設定 › Look 的 Herdr 98 卡片 | 顯示版面的 preview 圖 `win98.svg`，等比縮放置中，圓角 6、1 px 邊框，名稱「Herdr 98」；命令面板顯示「Layout: Herdr 98」 | settings/main.js 18、86-99、302-309；palette/main.js 87 | 585481d |
| 2026-11-19 | 歡迎頁提到 Herdr 98 | 最後一頁加上 herdr 的「More examples in Settings」段落 | welcome/main.js 451-459 | e309fb4 |
| 2026-11-19 | Clippy 浮動區塊 | 預設在畫面列左邊、靠下、離邊 12 px；拖曳時跟著游標（4 px 起算），放下後吸到最近的邊與三等分位置，存在 `win98.float.clippy`；對話框依邊靠齊。herdr 的彈簧動畫未做 | win98.json 70-80；layout.rs 732-751；floats.rs 14、78-96、253-300；clippy.js 469 | e309fb4 |
| 2026-11-19 | 鍵盤焦點 | 選單、對話框開啟時拿走鍵盤；關閉後交給最上面的對話框，沒有就回到終端機 | menu.js 229-230；dialog.js 50-51 | fd3740e |
| 2026-11-19 | ImGui 的 hover 判斷 | 終端機聚焦時 ImGui 一直有 active item，`IsAnyItemHovered()` 永遠為真；拖曳與桌面右鍵改用自己記錄的「游標下有控制項」 | kit.js 139-142 own() | fd3740e |
| 2026-11-19 | 疊放順序 | Herdr 98 的視窗、選單、Clippy 每個 frame 依順序提到最上層，但 AgentDesk 自己的設定、命令面板（ImGui popup）開著時不提 | herdr.d.ts 405 | fd3740e |
| 2026-11-19 | 細節對應 | 提示框用 AgentDesk 的 ImGui tooltip；選單超出視窗時夾回視窗內；`toLocaleTimeString` 的 AM／PM 前用一般空白（像素字型沒有 U+202F）；Mines 數字的 Arial Bold 用 Windows 內建的 Arial | taskbar.js 322；dialog.js 687；mines.js 157 | 585481d |
| 2026-11-19 | 開發用 `--input` 拖曳 | 加 `press:x,y`、`release:x,y`，用來驗證 Clippy 與視窗拖曳；不出貨 | 驗證工具，herdr 沒有對應 | e309fb4 |
