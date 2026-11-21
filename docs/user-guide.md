# AgentDesk 使用說明

AgentDesk 是 Windows 11 x64 的 AI coding agent 工作台：在可分割的終端機面板裡同時跑 Claude Code、Codex CLI 或任何命令列程式，關掉視窗後工作繼續跑，重開時接回原來的版面，並告訴你哪個 Agent 在等你。

## 安裝

1. 解壓 `AgentDesk-<版本>-win-x64.zip` 到任意資料夾，例如 `%LOCALAPPDATA%\Programs\AgentDesk`。不需要系統管理員權限，也不寫登錄檔。
2. 確認 `conpty\conpty.dll` 與 `conpty\x64\OpenConsole.exe` 跟執行檔放在一起。少了它們，Claude Code 的狀態回報會失效（見「限制」）。
3. 先自行安裝並登入要用的 Agent CLI。AgentDesk 不處理登入，也不複製任何 token。

## 畫面

執行 `agent-ui.exe`。它會在背景啟動 `agent-host.exe`，所有工作都跑在 host 裡。視窗沒有系統標題列。左上角三顆鈕依序是關閉、最小化、縮放（最大化），顏色跟著主題：關閉偏紅、最小化偏黃、縮放偏綠，色調取自主題的「需要你」「工作中」「完成」顏色，並保證在底色上看得清楚。滑鼠移上去會顯示符號，視窗不在前景時三顆都變灰；Terminals 版面打開直角時畫成小方塊，Classic 版面是文字按鈕。想用 Windows 式的按鈕，到「設定 > Look > Window buttons」選 Windows，按鈕就會回到分頁列右端。拖曳分頁列或左上的品牌列可以移動視窗，雙擊切換最大化，也能用 Windows 的貼齊功能。

視窗會記住上次的位置、大小，以及是否最大化，下次開啟時回到原處；那台螢幕不在了，就以同樣大小開在滑鼠所在的螢幕中間。第一次開啟時，大小依螢幕決定：約為 1280×820 乘上 Windows 的縮放比例與 AgentDesk 的縮放（設定裡的 zoom），並保持在螢幕可用範圍的一半到九成之間。所以 Full HD 100% 和 4K 150% 開出來的視窗，看起來的比例差不多。想重新套用預設大小，關掉 AgentDesk 後刪除 `window.json`。

- **側欄**：預設寬 268 px，拖曳右緣可在 44～480 px 之間調整。窄於 160 px 時改成只顯示編號與狀態的窄欄；品牌列的收合鈕在兩者之間切換，Ctrl+Shift+B 整個隱藏。隱藏後，分頁列最左邊會出現側欄按鈕，點一下就回來。
- **Spaces**：一個工作地點，通常是一個 repo。每列是彩色字母頭像、名稱，靠右是 `repo · 分支`，再加上領先／落後遠端的 ↑N／↓N（名稱已經說明的部分、main／master 會省略）。沒取名的 space 以第一個終端機所在的 repo 頂層資料夾命名，家目錄顯示 `~`；在 shell 裡 `cd` 之後會跟著變。linked worktree 的 space 以 `├─`／`└─` 掛在主 repo 的 space 下面。拖曳可調整順序；右鍵選單有改名、新增／開啟／移除 worktree、關閉。
- **Agents**：有回報狀態的 Agent。預設依「Needs you／Done／Working／Idle」分段，同段內最近變動的在前；排序鈕可改成依 space 分組。每列是程式圖示、space 名稱與工作名稱、距離上次變動多久、狀態標記；滑鼠移上去可以釘選到最上面。子 Agent 以樹狀線列在父 Agent 下面。
- **分頁列**：膠囊狀的分頁，關閉鈕只在目前分頁。雙擊改名，拖到別的分頁上放開可調整順序，在分頁列上轉動滑鼠滾輪可以切換分頁。沒取名的 Agent 分頁跟著 Agent 的即時標題。「+」開啟新增選單；右側是 Files、Changes 兩個面板按鈕。命令面板的「Settle tab」可以把分頁收起來：收起的分頁不顯示在分頁列，程式照常執行；用 Ctrl+Shift+K 選它，或它需要你時，會回到分頁列。
- **面板**：分頁裡的終端機，圓角外框，目前面板有強調色邊框。標題列顯示程式、名稱、資料夾與分支、狀態；分割、放大、關閉按鈕在滑鼠移上去時出現。拖曳標題列中間的把手，可以把面板放到另一個面板的邊緣（移到那一側），或放在中間（互換位置）。
- **Files／Changes**：右側面板，跟著目前面板 shell 所在的資料夾。Files 是可展開的檔案樹，檔名旁有 git 狀態字母，點檔案可唯讀預覽。Changes 列出所有未提交的檔案與 +／− 行數，點檔案看差異，下方可輸入訊息 commit（會先 `git add -A`）。開關狀態依 space 記住。
- **狀態列**：機器、目前分支、Agent 數量與需要你的數量，點右側的快捷鍵提示可以開命令面板。

**Done** 的意思是「在你沒看的時候完成了」，看過之後變回 Idle。「看過」指那個終端機真的畫在畫面上：Stage 底片條的縮圖、Inbox 卡片上的畫面預覽、Garden 的小動物、專注模式藏起來的面板都不算。沒有可信的狀態來源時，AgentDesk 不會自己猜狀態。

## 通知

Agent 需要你時一定會播放提示音；你沒在看的 Agent 完成時也會。你沒在看那個 Agent 時，右下角會出現通知（5 秒，錯誤 8 秒），點一下就跳到那個 Agent。通知從右邊滑進來，左邊的色條代表狀態（紅色需要你、綠色完成）。AgentDesk Mac 版面的通知在右上角，Stage 在頂端列下面。Windows 的「動畫效果」關掉時，AgentDesk 所有的淡入、滑入與顏色漸變都直接顯示結果。內建兩段提示音，可在「設定 > Alerts」關閉、試聽，或在 `ui.json` 的 `sound_done`／`sound_blocked` 改成自己的 MP3。

程式剛啟動就以錯誤結束時，通知會附上結束碼和最後幾行輸出。

專注模式（見「版面」）的提醒方式不同：預設只亮視窗邊緣並播放提示音，不跳通知。

## 新增工作

新 space、新分頁和分割都會先開一個 shell（預設依序找 PowerShell 7、Windows PowerShell、Command Prompt，可在「設定 > Look」更改），位置在目前面板 shell 所在的資料夾。用 `cd` 切到專案後再輸入 `claude` 或 `codex`；面板、space 名稱和分支都會跟著更新。

按側欄 Spaces 旁的「+」會列出這台電腦有的 shell（PowerShell 7、Windows PowerShell、Command Prompt、WSL），選哪個就用哪個開新 space；分頁列「+」的選單也列出這些 shell，可以直接開成新分頁。快捷鍵 Ctrl+Shift+N（新 space）和 Ctrl+Shift+T（新分頁）一樣開預設的 shell。

- 新分頁：Ctrl+Shift+T，或分頁列「+」選單的 Terminal
- 新 space：Ctrl+Shift+N，或 Spaces 標題旁的「+」
- 往右或往下分割：Ctrl+Shift+O、Ctrl+Shift+E，或面板標題列的按鈕
- 直接啟動 Agent：分頁列「+」選單的「Start an agent」，可按 1～9；或命令面板的「Start a program…」
- git worktree：「+」選單或 space 右鍵選單的 New worktree。只要輸入分支名稱，AgentDesk 會在 `%USERPROFILE%\.agentdesk\worktrees\<repo>\<分支>` 建立 checkout（可在設定中更改），開成新的 space 並開一個 shell。已存在的分支直接 checkout，新分支從目前位置建立。遠端機器的 space 也可以用，checkout 建在那台電腦上。Open worktree 列出 repo 的所有 checkout；Remove worktree 會關閉該 space 並刪除 checkout 資料夾，分支保留，有未提交的變更時會先警告。

第一次啟動會開歡迎頁，可以選大小與主題、加入遠端機器、選擇要顯示的面板。之後可從命令面板的「Welcome」再打開。

## 快捷鍵

Ctrl+Shift+/ 顯示完整清單（可篩選），Ctrl+Shift+P 開啟命令面板（模糊搜尋，最近用過的排在最前面）。常用的：

| 快捷鍵 | 動作 |
|---|---|
| Ctrl+Shift+K | 前往：搜尋 space、分頁或 Agent（Ctrl+D 關閉選到的分頁） |
| Ctrl+Shift+T／Ctrl+Shift+N | 新分頁／新 space（開 shell） |
| Ctrl+Shift+O／Ctrl+Shift+E | 往右／往下分割 |
| Ctrl+Shift+Z | 放大或還原面板 |
| Ctrl+Shift+X／Ctrl+Shift+W | 關閉面板／分頁（結束裡面的程式） |
| Ctrl+Tab、Ctrl+1～9、Alt+[ ／Alt+] | 切換分頁 |
| Alt+1～9、Ctrl+Shift+PgUp/PgDn | 切換 space |
| Alt+H/J/K/L 或 Alt+方向鍵 | 移到左下上右的面板 |
| Alt+Shift+H/J/K/L | 與該方向的面板交換位置 |
| Alt+↑／Alt+↓ | 上一個／下一個 Agent |
| F2／Shift+F2 | 分頁／space 改名 |
| Ctrl+Shift+F | 在終端機裡搜尋 |
| Ctrl+Shift+B | 隱藏或顯示側欄（在沒有側欄的 terminals 版面，切回 AgentDesk 版面） |
| Ctrl+Shift+L | 切換版面 |
| Ctrl+Shift+M | 進出 Stage 主舞台 |
| Ctrl+Shift+I | 進出 Inbox 收件匣 |
| Ctrl+Shift+2 | 進出 Pair 並排比較 |
| Ctrl+Shift+. | 進出專注模式 |
| Ctrl+Shift+R | 進出 Review 審查台 |
| Ctrl+Shift+H | 開關時間軸（Timeline） |
| Ctrl+= ／Ctrl+- ／Ctrl+0 | 放大／縮小／重設整個視窗 |
| Ctrl+, | 設定 |

有些按鍵只在特定版面有作用，在其他版面照常送給程式：

| 版面 | 快捷鍵 | 動作 |
|---|---|---|
| Stage | Alt+Left／Alt+Right | 上一個／下一個 Agent 換上舞台（其他版面是移到左右的面板） |
| Review | F8／Shift+F8 | 下一個／上一個改動的檔案 |
| Pair | Alt+P | 游標移到共用輸入列 |
| Pair | Alt+Shift+S | 左右交換 |
| Terminals | Alt+R | 調整大小模式 |
| Terminals | Alt+Shift+Enter | 往面板較長的一邊分割 |
| Terminals | Alt+Shift+Q | 關閉面板 |
| Terminals | Alt+Shift+G | 開關面板間距 |
| AgentDesk Classic | F10 | 打開選單列 |

Garden 沒有預設按鍵；想要的話，在 keys.json 綁 `garden.toggle`。Inbox 佇列被點選時的方向鍵、Enter、R、1～9、S 見「Inbox」一節。

這些按鍵由 AgentDesk 使用，不會送進終端機裡的程式。要改鍵位，在「設定 > Look」按「Edit keys.json」，例如：

```json
{ "global": { "ctrl-shift-t": "", "ctrl-alt-n": "tab.new" } }
```

值為空字串表示取消綁定。格式如下：

- 區段：`global`（整個視窗）、`terminal`（終端機有焦點時）、`layout:<版面名稱>`（顯示該版面時）。同一個鍵在多個區段都有設定時，依 terminal、layout、global 的順序取用。
- 動作可以是命令名稱、`命令:值`、`{"command": "layout.switch", "args": {"name": "terminals"}}`、`send:<文字>`（送進終端機），或 `terminal`（這個鍵交給程式）。
- 檔案有錯（無法讀的按鍵、未知的區段或命令）時，整個檔案都不套用，按鍵維持預設，錯誤會顯示在設定頁。
- 沒按修飾鍵的 F1～F12，只有在 keys.json 裡有綁定時才由 AgentDesk 處理，沒綁定的照常送給程式。預設只綁了 F2、Review 的 F8 和 Classic 的 F10。

存檔後立刻生效，不必重新啟動。檔案不存在時，AgentDesk 會寫入預設的按鍵；檔案有錯時，原因會寫在同一個資料夾的 `keys.error`。

## 終端機操作

- 輸入：一般按鍵、方向鍵、F1～F12、注音等輸入法組字後的文字。Shift+Enter、Ctrl+Enter 和 Esc 會原樣送到程式（例如 Claude Code 用 Shift+Enter 換行）。
- Ctrl+C：有選取時複製，沒有選取時送中斷。Ctrl+Shift+C 固定複製，Ctrl+V 或 Ctrl+Shift+V 貼上。
- 滑鼠：拖曳選取、雙擊選字、三擊選整行，放開就複製（可在設定關閉）。自動換行的長行複製出來是一整行；全選包含歷史。選取時拖到面板外，畫面會自動捲動。
- 連結：按住 Ctrl 時，網址、檔案路徑和程式標記的連結會加上底線，Ctrl＋點一下就會開啟。程式和指令檔只會在檔案總管中標出，不會執行。
- 貼上：中鍵也能貼上。在檔案總管複製檔案後按 Ctrl+V，或把檔案拖進終端機，都會貼上加了引號的路徑。剪貼簿只有圖片時，Ctrl+V 會交給程式，讓 Agent 自己讀圖。
- 會讀滑鼠的程式（例如 vim、less、htop）：點擊、拖曳和滾輪直接交給程式；按住 Shift 可以照常選取文字。
- 右鍵預設開選單。在右鍵選單選「Send right-clicks to pane」後，這個面板的右鍵改交給程式；Shift＋右鍵一定開選單。
- 全螢幕程式沒有開啟滑鼠功能時，滾輪會改送上下方向鍵。
- 捲動：滑鼠滾輪捲進歷史（最多 5000 行），右側有捲軸可以拖曳。一開始打字就回到即時畫面。
- 搜尋：Ctrl+Shift+F，搜尋畫面與歷史，Enter 往前找，Shift+Enter 往後找，Esc 關閉。
- 貼上多行文字前會先顯示預覽，確認後才送出；含 NUL 或超過 1 MiB 的內容會被拒絕。
- 同一個工作同時只有一個視窗能輸入。其他視窗顯示 Read-only，按「Take control」可明確接手。

## 關閉與停止

| 動作 | 結果 |
|---|---|
| 視窗右上角 X | 只關閉介面，工作繼續在背景執行。第一次會顯示一次說明 |
| 面板的 ×、Ctrl+Shift+X | 結束這個面板的程式。最後一個面板會一併關閉分頁 |
| 分頁的 ×、Ctrl+Shift+W | 結束分頁裡的程式，不詢問。關閉 space 的最後一個分頁時，會先問「Close space?」 |
| space 右鍵 > Close | 先確認，再結束 space 裡所有程式 |
| 子 Agent 面板右鍵 > Send back to … | 收起面板，子 Agent 繼續跑，仍列在父 Agent 底下 |
| 命令面板「Stop the focused session」 | 送出 Ctrl+C。5 秒後仍未結束會出現「Force stop」 |
| 命令面板「Exit and stop all sessions」 | 列出仍在執行的工作，確認後全部停止並結束 host |

登出、重新開機或 host 異常結束後，原本的工作無法續接，會顯示為 Lost；AgentDesk 不會自動重跑任何 prompt。終端機結束或變成 Lost 時，它的面板會自動關掉；分頁空了就關分頁，space 空了就關 space。

## 子 Agent

在 AgentDesk 的終端機裡執行 `agentctl run` 時，新的 session 會成為目前 session 的子 Agent：它列在父 Agent 底下，不會自動開成面板。點它可以浮動預覽（peek），按「↗ keep」改開成面板。給 Agent 看的使用說明：`agentctl --skill`。

```bat
agentctl run --profile claude-native --name reviewer --cwd C:\work\demo --task "review the diff and report problems"
agentctl wait reviewer --until idle --timeout-ms 600000
agentctl read reviewer --lines 80
:: 在子 Agent 裡回報給父 Agent：文字會送進父 Agent 的輸入，開頭加上 [from reviewer ...]
agentctl report "found 3 problems: ..."
```

`--task` 在子 Agent 就緒（回報 Idle）後才送出；沒有狀態回報的程式則等 8 秒。回報在父 Agent 工作中或等待使用者時先排隊，等它閒下來才送。

## 遠端機器

按側欄左下的「Add machine」，從 `~/.ssh/config` 列出的主機中選一台，或直接輸入 `user@host`。AgentDesk 會執行 `ssh <目標> agentctl bridge`，把那台電腦的 space 和 Agent 顯示在同一個側欄，它們在那台電腦上執行，這台電腦休眠時也繼續跑。

需求：遠端是 Windows 並安裝了 AgentDesk，`agentctl` 在 PATH 上（或填完整路徑），並設定好金鑰登入。AgentDesk 不會在背景跳出密碼輸入；需要密碼或要確認主機金鑰時，設定頁會顯示 ssh 的訊息，請先在自己的終端機執行一次 `ssh <目標>`。

## WSL

裝了 WSL 時，每個 Linux 發行版（例如 Ubuntu）會自動加進 profile，在分頁列「+」選單的 Terminal 底下出現「Ubuntu (WSL)」。它在目前面板所在的資料夾開一個 Linux shell（`D:\src` 會是 `/mnt/d/src`），你再在裡面執行 Linux 版的 `claude` 或 `codex`。Docker Desktop 內部用的發行版不列出。想讓新分頁、新 space 預設就開 WSL，在「設定 > Look」的 Shell 選它。

在 Linux 裡 `cd` 之後，space 名稱、分支和 Files／Changes 面板都會跟著變。`/mnt/c/...` 對應到 `C:\...`，其他位置對應到 `\\wsl.localhost\<發行版>\...`。AgentDesk 透過 `WSLENV` 設定 bash 的 `PROMPT_COMMAND`，讓它每次顯示提示字元前回報所在資料夾（OSC 7）。你 WSL 裡的檔案都不會被修改。

- 資料夾追蹤只支援 bash。用 zsh、fish，或你的 `~/.bashrc` 自己設定了 `PROMPT_COMMAND` 時，資料夾會停在啟動時的位置。
- Files／Changes 對 WSL 裡的 repo 執行 Windows 的 git，並對這次呼叫加上 `safe.directory=*`（否則 git 會因檔案擁有者是 Linux 使用者而拒絕）。WSL 的檔案走網路檔案系統，大 repo 會比較慢，面板至少每 10 秒更新一次。
- WSL 不能當「遠端機器」：Add machine 需要對方執行 Windows 版的 AgentDesk。

如果你原本就有 `profiles.json`，AgentDesk 會補上 WSL profile，只補這一次；之後你刪掉的不會再加回來。

## 版面

Ctrl+Shift+L、命令面板或「設定 > Look」切換。設定 › Look 列出七個版面，Ctrl+Shift+L 依序輪替：AgentDesk、Terminals、Stage、Inbox、Review、Pair、AgentDesk Mac。另外有兩個好玩的版面（AgentDesk Classic、Garden），見下方「好玩的版面」。

- **workspace**（設定裡顯示為 AgentDesk）：預設，側欄＋分頁＋分割面板＋右側面板。
- **terminals**：沒有側欄與狀態列的版面，可以改成直角、調整間距，並用鍵盤調整面板大小，見下方「Terminals」。上方列最左邊是側欄按鈕（點了切回有側欄的 AgentDesk 版面，Ctrl+Shift+B 也可以），接著是目前的 space，點一下開啟 Ctrl+Shift+K 的選單；每個分頁顯示程式圖示、狀態與資料夾；右邊是設定（齒輪，可在這裡切回 AgentDesk 版面）、命令面板與新分頁按鈕，滑鼠停在按鈕上會顯示名稱與快捷鍵。面板標題列的按鈕一直顯示。
- **mac**（設定裡顯示為 AgentDesk Mac）：Mac 風格的版面，畫法與配色都是 AgentDesk 自己的。最上面是選單列：圓形視窗按鈕、AgentDesk 標誌（點了開 AgentDesk 選單：What's new、設定、檢查更新、隱藏、關閉視窗），以及 File、Edit、View、Window、Help 五個選單，選單打開時把滑鼠移到另一個標題就換成那個選單；右邊是需要你與執行中的 Agent 數量（點一下跳到第一個需要你的 Agent）和時鐘。中間是桌布，目前的分頁放在一個浮動視窗裡，視窗標題列左邊三顆小圓鈕：紅色與黃色把這個分頁收進 Dock（程式繼續跑），綠色讓視窗填滿桌面，再按一次恢復。最下面是 Dock：每個 space 一個頭像（目前的 space 下面有一個點），接著是目前 space 的分頁（顯示程式圖示，有程式在跑的下面有點，右上角標出需要你、完成或執行中，收起來的分頁顏色變淡），最後是新增與設定。滑鼠移過 Dock 圖塊會放大並顯示名稱；右鍵分頁圖塊有改名、新分頁、關閉。這個版面的通知出現在右上角、選單列下方。Ctrl+Shift+B 會切到有側欄的 AgentDesk 版面。
- **stage**（Stage）：一個 Agent 在大的主舞台上，其他 Agent 在下方排成即時縮圖，見下方「主舞台」。
- **inbox**（Inbox）：需要你的 Agent 排成一列卡片，不用切過去就能回答，見下方「收件匣」。
- **review**（Review）：左邊是終端機，右邊是改動與 diff，見下方「審查台」。
- **pair**（Pair）：兩個 Agent 左右並排，同一個任務交給兩邊比一比，見下方「並排比較」。
- **classic**（AgentDesk Classic）、**garden**（Garden）：好玩的版面，見下方「好玩的版面」。

另外有一個任何版面都能用的專注模式（Ctrl+Shift+.），和隨時能打開的時間軸（Ctrl+Shift+H）。

以前用 Mission Control 的人，開啟後會直接換成 Stage，原本的篩選設定也會帶過來；用 AgentDesk Tiling 的人會換成 Terminals 並打開直角；AgentDesk Cockpit 已經拿掉，原本用它的人會回到 AgentDesk 版面。

**第一次使用提示**：每個版面（和專注模式）第一次出現時，畫面中央會跳出一張提示卡，說明這個版面適合做什麼，並列出幾個常用操作與快捷鍵（依你的 keys.json 顯示）。按「Got it」、Esc 或點卡片外面就關閉，之後不會再自己出現；按「Don't show tips」則所有版面都不再跳卡。想再看一次，從命令面板執行「Tips for this layout」，或用 Mac、Classic 版面的 Help 選單，或設定 › Look 的「Layout tips」列；那一列的開關也能重新打開提示。

### Terminals

只有終端機，分頁在頂端一條列上。設定 › Look 選到 Terminals 時可以調整：

- **Square corners**：面板改成直角，像平鋪式視窗管理員。左上角的視窗按鈕也會變成小方塊。
- **Gaps**：面板之間與視窗邊緣的間距（0～24 px），開關也可以按 Alt+Shift+G。關掉時面板彼此相貼，共用一條邊線。

Terminals 另有幾個鍵盤操作：Alt+R 進入調整大小模式（H、J、K、L 或方向鍵每次 40 px，加 Shift 每次 10 px，Esc 或 Enter 結束，模式中按鍵不會送進終端機，面板區下方會出現提示卡）；Alt+Shift+Enter 往面板較長的那一邊分割；Alt+Shift+Q 關閉面板。shell 的 Alt+R（readline 還原整行）在這個版面會被攔下，不想要的話可以在 keys.json 的 `layout:terminals` 區段改掉。

### 主舞台（Stage）

Stage 把一個 Agent 放在大大的主舞台上，可以直接在裡面打字；其他 Agent 在下方的底片條排成一排即時縮圖，每張都看得到程式、名稱、狀態和畫面最後幾行。按 **Ctrl+Shift+M** 切換到 Stage，再按一次回到原本的版面。

- 點縮圖就把它換上舞台，原本台上的 Agent 回到底片條。也可以按 Alt+Left／Alt+Right 換到上一個／下一個 Agent；頂端列右邊的「←」按鈕會換回剛才那一個。
- 需要你的 Agent 縮圖會發光，並排到最前面（等最久的在前），同時照常播放提示音。其他縮圖維持固定的順序，不會跟著狀態跳來跳去。
- 底片條上方的 All／Needs you／Working 可以篩選縮圖，S／M／L 調整縮圖大小。視窗太矮時會自動用小一號的縮圖，讓舞台保持夠大。縮圖太多時用滑鼠滾輪左右捲動，或點兩端的箭頭。
- 在縮圖上按右鍵可以放上舞台、偷看（peek）、重新命名或關閉。
- 子 Agent 沒有自己的縮圖，父 Agent 的縮圖會顯示數量。
- Esc 照常送給終端機（Claude Code 用 Esc 中斷 Agent），不會離開 Stage。
- 設定 › Look 選到 Stage 時，可以讓一般終端機也有縮圖（Plain terminals），也可以打開「Switch to an agent that needs you」：有 Agent 需要你時自己換上台。這個選項預設關閉；打開後，你正在打字、有選單開著，或台上的 Agent 也在等你時，都不會自動換台。

只看縮圖不算「看過」：完成的 Agent 會一直標示完成，直到你把它放上舞台。

### 收件匣（Inbox）

同時跑好幾個 Agent 時，最花時間的是搞清楚「誰在等我、要我做什麼」。Inbox 把需要你的 Agent 排成一列卡片，按 **Ctrl+Shift+I** 打開，再按一次回到原本的版面。也可以在設定 › Look 選「Inbox」。

每張卡片寫著 Agent 的名稱、在哪個 space 和分支、資料夾改了幾個檔案、等了多久，以及 Agent 自己在回報裡說它要什麼；下面是它畫面最後幾行，方便你判斷。最新開始等的排最前面，上方的「Newest／Oldest」可以改成等最久的排最前面。

處理方式：

- **快速回答**：卡片左下的按鈕，例如 Claude Code 權限提示的 Yes／No。按下去就把對應的按鍵送進那個終端機。
- **回覆**：在卡片下方的欄位打字，Enter 送出（會自動補一個 Enter）。送出後卡片會顯示「✓ Sent」。
- **Open**：在右邊的面板打開那個 Agent，直接操作它的終端機。
- **Snooze**：先收起來。Agent 換了問題，或離開等待後又回來，卡片會再出現。上方的「N snoozed」可以讓它們全部回來。重開 AgentDesk 後，收起來的卡片都會回來。

卡片下面的「DONE」區是你還沒看過的已完成 Agent，可以 Open，也可以按 Dismiss 標成看過。全部處理完會出現「Inbox zero」。

鍵盤：先點一下左邊的佇列，↑／↓（或 K／J）選卡片，Enter 打開，R 回覆，1～9 按快速回答，S 延後（在完成列上是 Dismiss），D 或 Delete 標成看過。這些鍵只在佇列被點選時有效，點一下右邊的終端機就回到一般打字，不會被攔下。

卡片上顯示的「Agent 要什麼」只來自 Agent 自己的狀態回報（Claude Code 的 OSC 7501 或 `agentctl report-state --message`）。沒有回報訊息時，卡片會說明請看下面的畫面；AgentDesk 不會從畫面文字猜狀態。

**設定快速回答**：在 `ui.json` 加上 `inbox_answers`，依程式（programs.json 的名稱，例如 `claude`、`codex`）列出按鈕的文字和要送出的按鍵。例如：

```json
"inbox_answers": {
  "claude": [
    { "label": "Yes", "send": "1", "kinds": ["permission"] },
    { "label": "No", "send": "\u001b", "kinds": ["permission"] }
  ],
  "my-agent": [
    { "label": "Continue", "send": "", "enter": true }
  ],
  "*": []
}
```

- `send`：原封不動送進終端機的按鍵。控制鍵用 JSON 的跳脫寫法，例如 `\u001b` 是 Esc。
- `enter`：送完再補一個 Enter。`send` 和 `enter` 至少要有一個。
- `kinds`：只在這幾種回報出現（`permission` 權限、`question` 問題、`auth` 登入）；沒寫就都會出現。
- `label`：按鈕上的字，最多 40 bytes；沒寫就用送出的內容命名。
- 每個程式最多 9 個按鈕（對應 1～9 鍵）。`"*"` 代表其他所有程式；寫成空陣列代表不要按鈕。
- 存檔後，Inbox 開著時幾秒內就會生效，不用重開。AgentDesk 存設定時不會改動你寫的 `inbox_answers`。格式不對的項目會被略過。

AgentDesk 只為兩個程式附了預設，而且只用在權限提示：Claude Code 是 Yes＝`1`、No＝Esc，Codex 是 Yes＝`y`、No＝Esc。這兩組是依官方文件與提示畫面推得的，**還沒用真的 CLI 驗證過**，第一次用時請看一下終端機確認按鍵有效；CLI 改了提示格式時，請在 `inbox_answers` 覆寫。

### 並排比較（Pair）

**Pair** 把兩個 Agent 左右並排，下方一個共用的輸入列，可以把同一個任務同時交給兩邊，例如 Claude Code 和 Codex，或同一個 Agent 開兩次，看誰先做完、做得比較好。按 **Ctrl+Shift+2** 切到 Pair，再按一次回到原本的版面；Ctrl+Shift+L 也會輪到它。

第一次打開 Pair 時會看到設定卡：

- 左右各挑一個程式。
- **Separate worktrees**（預設打開）：目前的資料夾是 git 儲存庫時，兩邊各自在一個新的 worktree 裡工作（`pair/` 開頭的新分支），不會改到同一份檔案。未提交的變更不會帶過去。資料夾不是 git 儲存庫時，兩邊只能在同一個資料夾裡工作，卡片上會提醒你。
- 按 **Start pair**。已經有兩個 Agent 在跑時，也可以按 **Pair two open agents** 直接把它們配成一對。

配好之後：

- 兩邊都是一般的終端機，可以直接點進去打字。
- 在下方的輸入列打字（點它或按 Alt+P），按 Enter 送給兩邊；**Left only**、**Right only** 只送一邊。點終端機或按 Esc 就回到終端機。
- 每一邊的標題列會顯示這一輪花了多久，以及那個資料夾改了幾個檔案、增減幾行。
- 一邊先做完時，它的面板頂端會出現一條終點線。兩邊都做完後，上方的結果條會告訴你誰先完成、快了多少，並可以 **Review** 任一邊的 diff（看完按 Ctrl+Shift+R 回到 Pair），或按 **Keep** 標記你要留下的那一邊（只是標記，不會動到 git）。
- 上方列的交換按鈕（Alt+Shift+S）把左右對調；面板標題列的 **Change** 可以換成另一個正在跑的 Agent；**Close pair** 解除配對，兩個終端機照常在「Pair」分頁裡繼續跑。

誰先完成只依 Agent 自己回報的狀態判斷，不會從畫面文字猜。Pair 建立的 worktree 不會自動刪除，用完請用「Remove worktree」清掉。輸入列送出的方式是先貼上文字再按 Enter，真的 Claude Code 與 Codex 上的效果還沒實測，若 Agent 沒有開始做，請直接在它的終端機按一次 Enter。Ctrl+Shift+2 被 AgentDesk 拿來切換 Pair，終端機裡的程式收不到這組鍵；需要的話在 keys.json 改掉。

### 專注模式（Focus）

想專心寫程式、只偶爾看一下 Agent 時，按 **Ctrl+Shift+.**（或在命令面板執行「Focus mode」）。畫面只剩目前的終端機，底下一條細列：

- 左邊是這個終端機的程式與 space。
- 中間的小點代表其他 Agent：紅色實心是需要你，綠色實心是完成，黃色空心圈是工作中，灰色小點是閒置。滑鼠移上去會顯示是哪個 Agent，點一下就換成它，仍留在專注模式。放不下的會顯示「+N」，點了開啟搜尋。
- 右邊的「Exit focus」或再按一次 Ctrl+Shift+. 就回到原本的版面，分割與放大狀態都不變。

滑鼠在終端機上緣停一下，面板標題列就會浮現，可以分割、放大或關閉。Esc、Ctrl+C 照常送給終端機。

有 Agent 需要你時，視窗邊緣會亮一下，不跳通知。預設也會播放提示音；要完全安靜，到 **設定 › Alerts › In focus mode** 選「Edge」；選「Usual」則和平常一樣跳通知。視窗不在前景時，一律照平常的方式提醒。關掉 AgentDesk 再開，仍會停在專注模式。

### 審查台（Review）

按 Ctrl+Shift+R 切到審查台，再按一次回到原本的版面。左邊是目前的終端機，右邊列出這個 Agent 資料夾裡還沒 commit 的改動和 diff，最下面可以直接 commit。

- 焦點換到另一個 Agent，右邊就跟著換。焦點移到一般 shell 時，右邊預設保留上一個 Agent 的改動；想讓它一律跟著焦點，把 `ui.json` 的 `review_follow` 改成 `"focused"`。
- 按 F8、Shift+F8 換到下一個、上一個檔案，到最後一個會繞回第一個。這兩個鍵只在審查台有作用，在其他版面照常送給終端機。
- Agent 做完一輪時清單會自動重讀，也可以按右上角的重新整理。
- 同一個資料夾有兩個以上的 Agent 在跑時，上方會出現「N agents in this repo」：清單裡是所有人的改動，分不出是誰改的。要分開，請讓每個 Agent 用自己的 worktree。
- Agent 還在工作時也可以 commit，但會看到提醒，之後它再改的檔案會出現在下一次的清單裡。

Ctrl+Shift+M、Ctrl+Shift+I、Ctrl+Shift+R、Ctrl+Shift+2 只記得一層「原本的版面」：從 terminals 進 Stage、再進 Review，按 Ctrl+Shift+R 會回到 Stage，再按 Ctrl+Shift+M 則回到 AgentDesk 版面，不是 terminals。

### 時間軸（Timeline）

按 Ctrl+Shift+H 打開時間軸，看每個 Agent 什麼時候在工作、什麼時候在等你、什麼時候做完。按 Esc 或再按一次 Ctrl+Shift+H 關閉。

- 上方可以切換範圍：15 分鐘、1 小時、4 小時、24 小時（也可以按 1～4）。←、→ 往前往後移，End 回到現在，Ctrl+滾輪在幾種範圍之間放大縮小。
- 黃色條是工作中，紅色條是需要你（夠寬時中間有「!」），「✓」是做完的時間點，細線是閒置，「×」是程式結束。滑鼠停在色條上可以看起訖時間。
- 點一列或一段色條，就切到那個 Agent。
- 勾選「Since you left」時，視窗離開焦點之後的那段時間會加上底色，回來時可以直接看出那段時間誰卡住、誰做完。
- 紀錄存在 agent-host 裡，關掉 AgentDesk 也會繼續記錄，最多保留 24 小時、每個終端機 1,000 筆，只記時間和狀態，不記畫面內容。更新 agent-host 之後，紀錄會從更新的時間重新開始；遠端機器的 agent-host 太舊時，那台會顯示需要更新。
- 只有回報過狀態的終端機會出現，一般 shell 不會。
- 終端機用 `agentctl timeline` 也能看同樣的紀錄（見「agentctl」）。

### 好玩的版面

設定 › Look 的「Fun layouts」打開後，AgentDesk Classic 和 Garden 會出現在版面卡片裡，Ctrl+Shift+L 也會輪到它們。沒打開時，一樣可以從命令面板輸入「Layout: AgentDesk Classic」或「Layout: Garden」切過去。

**Garden**：每個 Agent 畫成一隻小動物，住在一片小花園裡，適合開在第二台螢幕，用餘光就看得出誰在忙、誰在等你。小動物的動作只看 Agent 回報的狀態：

- 沿著小路跳步：工作中。
- 面向你揮手、頭上有紅色「!」：需要你。
- 開心跳一下、冒出閃光，然後坐下：完成。
- 閉眼睡覺、飄著「z z」：閒置。
- 半透明、安靜坐著：沒有狀態。

每隻小動物的顏色是它所在 space 的顏色，耳朵、尾巴這些小地方依 Agent 而不同，同一個 Agent 每次都長一樣。子 Agent 是旁邊比較小的那隻，超過兩隻時會顯示「+n」。上方列有需要你、工作中、完成的數量。

滑鼠停在小動物上會看到程式、狀態、持續多久和最後一則訊息。點一下會回到原本的版面並切到那個 Agent；右鍵可以 Peek、重新命名或關閉面板。要回花園，按上方列最右邊的按鈕，或在命令面板輸入「Layout: Garden」；想要快速鍵的話，在 keys.json 綁 `garden.toggle`。

深色主題是夜晚的花園，淺色主題是白天。視窗不在前景時動畫會放慢、降低更新頻率；Windows 的「動畫效果」關掉時，小動物都停在定格姿勢。

**AgentDesk Classic**：整個視窗用字元畫成單色終端機的樣子，選單、分頁、框線和狀態全是文字。功能和 AgentDesk 版面一樣（space、分頁、分割、命令面板、通知）。

- F10 打開選單列，左右鍵換選單、上下鍵選列、Enter 執行、Esc 關閉。這個鍵只在 Classic 版面有作用；全螢幕終端程式若用 F10，可以在 keys.json 的 `layout:classic` 區段改成別的鍵。
- Ctrl+Shift+B 開關文字側欄。
- 狀態一律有字元，不只靠顏色：`[!]` 需要你（反白）、`[~]` 工作中、`[+]` 完成、`[.]` 閒置、`[?]` 未知。
- 設定 › Look 的「Screen effects」：Off、Light（掃描線與暗角，預設）、Full（再加文字微光）。淺色主題與 Windows 高對比時一律不畫特效。
- 「Monochrome terminal」會把終端機的顏色轉成主題的單色色階。要看彩色 diff 時請關掉。

Classic 附有為它設計的主題（Phosphor Green／Phosphor Paper，另外還有 Phosphor Amber），設定 › Look 會出現建議列，按 Use 才會套用（見「主題」）。

### 自訂版面

自訂版面放在 `%LOCALAPPDATA%\AgentDesk\layouts\<名稱>.json`：

```json
{ "name": "wall", "sidebar": false, "tab_bar": false, "status_bar": true, "theme": "ember" }
```

命令面板的「Duplicate layout」把目前的版面複製成 `<名稱>-copy.json` 並切換過去，之後直接改這個檔案。「Reset layout」把版面還原成隨附的樣子，並重設側欄與右側面板的寬度；你改過的檔案會改名成 `<名稱>.json.old-<時間>` 保留。

## 設定

Ctrl+, 開啟，左側分成 Look、Machines、Alerts、Mods、Updates、Privacy。每次修改都立即生效並儲存，沒有儲存鈕。

- **Look**：版面與主題卡片（我們的 Midnight、Ember、Moss、Paper，加上 graphite、ink、moss、paper、atom one dark，以及隨附的 Dusk、Dawn、Phosphor Green、Phosphor Amber、Phosphor Paper；把同樣格式的主題檔放進 `%LOCALAPPDATA%\AgentDesk\themes\` 也會列出來，每個版面各自記住深色與淺色主題。排程、變色、建議主題與主題編輯器見下方「主題」）；「Fun layouts」開關（見「好玩的版面」）；選到 Terminals、Stage、Classic 時，各自多出幾列選項；整體縮放、介面文字大小（預設 15 px，覺得太小可以調大）、終端機字型與大小、行高、字寬、捲動速度；新終端機用的 shell、放開滑鼠就複製、keys.json、worktree 資料夾。
- **Machines**：遠端機器清單與「Add a machine…」。
- **Alerts**：提示音開關與試聽、右下角通知開關，以及專注模式的提醒方式（In focus mode：Edge、Edge + sound、Usual）。
- **Mods**：啟用或停用 mod、看錯誤、重新載入。
- **Updates**：AgentDesk 與 agent-host 的版本、「What's new」（更新後第一次啟動會自動顯示一次）、「Check now」。AgentDesk 沒有內建的更新伺服器，要設定環境變數 `AGENTDESK_RELEASES_URL` 才會每小時檢查一次；有新版時會在右下角提示，按「Restart to update」換版，或下次結束時換。agent-host 可以單獨更新，終端機裡的程式不會中斷。
- **Privacy**：使用統計的開關，以及「What we send」逐字顯示會送出的內容。只有設定 `AGENTDESK_TELEMETRY_URL` 時才會送出；`DO_NOT_TRACK` 或 `AGENTDESK_TELEMETRY=0` 會關閉這一次執行。

左下的「Send feedback」開一個草稿視窗，草稿最後會附上 AgentDesk 與 Windows 的版本。Send feedback 的草稿不會自己送出：按「Copy」複製，或按「Save to file」存到 `%LOCALAPPDATA%\AgentDesk\feedback\`，再用你習慣的方式寄出。

介面字型是隨附的 Geist，終端機預設是隨附的 JetBrains Mono Medium；中文字以微軟正黑體補上。

## 主題

### 依時間切換淺色與深色

設定 › Look › Theme 的 Appearance 選 **Schedule**，AgentDesk 就會自己換淺色和深色。

- **Fixed times**：填淺色開始和深色開始的時間（例如 07:00 與 19:00）。欄位裡按上下鍵，一次調 15 分鐘。夜班可以把淺色時間設得比深色晚，例如 22:00 到 06:00。
- **Sunrise, sunset**：先在 Place 填緯度（必填）和經度（可不填，不填就用時區的標準經線推算）。AgentDesk 不連網，也不讀 Windows 定位，位置只存到小數一位。Shift 可以把日出、日落整體提早或延後，最多兩小時。
- 下方那一行會寫出今天幾點換色、現在是哪一種。
- 想暫時換成另一種，執行命令「Toggle light / dark」（`appearance.toggle`），到下一個切換時間就回到排程。

### 版面的建議主題

有些版面附了配好的深淺主題：AgentDesk Mac 配 Dusk 與 Dawn，Classic 配 Phosphor Green 與 Phosphor Paper。原本選了 Cockpit Night 或 Cockpit Day 的人，會回到預設主題。如果你現在用的不是那一組，設定 › Look › Theme 會出現一列「Made for …」，按 **Use** 才會換。AgentDesk 不會自己改你的主題。建議的主題卡右上角有一顆小星星。

### 跟著 Agent 狀態變色

把 **Tint by agent state** 設成 Subtle 之後，只要有 Agent 需要你，整個介面的底色會稍微偏暖，強調色也跟著偏一點。終端機裡的顏色、文字和狀態色都不變。打開 **Also when done**，有完成但還沒看的 Agent 時會帶一點冷色，強度減半。變色前會先檢查對比，所以淺色主題幾乎看不出變化。Windows 關掉動畫效果時直接換色，不漸變。

### 主題編輯器

在主題卡上按右鍵選 **Edit a copy…**，或按每排最後的 **New theme**，就會打開編輯器。改任何顏色，整個視窗馬上跟著變。

- 每個顏色旁邊是它在主要背景上的對比：打勾表示夠清楚，三角形表示偏低。標著 `auto` 的顏色是從背景推算的，改了才會寫進檔案；旁邊的復原鈕可以改回推算值。
- 右邊的範例可以切換成紅色盲、綠色盲、藍色盲看到的樣子（只影響範例）。Checks 列出對比不足和難以分辨的狀態色。對比低一樣可以存，只有終端機文字低於 3:1 時會多問一次。
- 按 **Save**（或 Ctrl+S）存成 `%LOCALAPPDATA%\AgentDesk\themes\<名稱>.json`，存完立刻套用，不用重開。檔案格式和手寫的主題檔一樣。想另存一份，改 Name 欄位再存。
- 自己的主題卡右鍵還有 **Edit…** 和 **Delete…**。刪除不會真的刪檔，而是改名成 `.json.old-<時間>`；用到它的版面會回到預設主題。
- 同時開著另一個 AgentDesk 視窗時，那個視窗要重開才看得到新存的主題。

## 程式圖示

面板、分頁和 Agent 清單的圖示與名稱來自內建的程式表（執行檔旁的 `programs\programs.json`，125 個程式）。要改某個程式的名稱或圖示，在 `%LOCALAPPDATA%\AgentDesk\programs.json` 寫同樣格式的項目（只寫要改的欄位），存檔後立刻套用；檔案有錯時原因寫在 `programs.error`。

## Mod

Mod 是用 C++ 寫的 DLL，放在 `%LOCALAPPDATA%\AgentDesk\mods\<名稱>\mod.dll`，旁邊可以有 `mod.json`（說明文字）和 `settings.json`（mod 自己的設定）。它可以對 Agent 狀態做出反應、加命令面板項目、在側欄或 Details 加面板、在分頁和 Agent 列加文字、啟動程式。在「設定 > Mods」啟用或停用、看錯誤、重新載入。命令面板的「Duplicate mod」把一個 mod 的資料夾複製成 `<名稱>-copy`，預設不啟用。第一次啟動會放入三個範例，預設都不啟用：

- `tab-timer`：在分頁顯示 Agent 已工作多久。
- `ntfy-notify`：Agent 需要你時推播到手機。在它的 `settings.json` 填入 ntfy.sh 的 topic 後啟用。
- `agent-summary`：側欄面板，顯示需要你、工作中、閒置的數量。

自己寫 mod：zip 裡的 `sdk\include` 有介面標頭檔，`sdk\examples` 是三個範例的原始碼。用 VS 2022 編譯成 DLL，檔名 `mod.dll`；`sdk` 資料夾的 `CMakeLists.txt` 可以直接編出所有範例。最小的例子：

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

`host()` 提供 `run(命令)`、`send(session, 文字, 是否按 Enter)`、`notify(標題, 內文)`、`exec({程式, 參數...})`、`add_command(id, 名稱)`、`sessions()`、`setting(鍵, 預設值)`。可以覆寫的有 `on_event`（agent_state、session_added、session_exited、tick）、`panels(位置)`（用 `Panel` 的 `text`、`row`、`badge`、`progress`、`button`、`separator` 組出面板）、`click`、`decorate("tab"|"agent", ...)`、`command`。不用 C++ 的話，也可以直接實作 `mod_api.h` 的 C 介面。

Mod 在 AgentDesk 裡以你的權限執行，跟你安裝的其他程式一樣，所以只啟用你信任的 mod。在設定頁打開 mod 的開關時，AgentDesk 會先說明這一點，按「Turn on」才會啟用。`exec` 直接啟動程式，不經過 shell。mod 當掉（例如記憶體存取錯誤）時會被停用，錯誤顯示在設定頁，AgentDesk 照常執行；但 mod 卡在無窮迴圈時，整個介面會停住：終端機仍在 agent-host 裡繼續跑，用工作管理員結束 `agent-ui.exe`，在 `ui.json` 的 `mods_enabled` 拿掉那個 mod，再重新開啟 AgentDesk。寫 mod 的完整說明（介面、相容性規則、注意事項）在 [mods.md](mods.md)（英文）。舊版的 Lua mod（`main.lua`）已不支援，設定頁會顯示錯誤；三個範例會自動換成 C++ 版本，原本的 `settings.json` 保留。

## agentctl（命令列）

```bat
agentctl run --profile codex-native --name review --cwd C:\work\demo
agentctl list --json
agentctl read review --lines 100
agentctl send review --text "summarize the diff"          :: 不會自動按 Enter
agentctl send review --enter
agentctl send review --file C:\work\prompt.txt --enter
agentctl stop review --interrupt
agentctl stop review --force
agentctl wait review --until idle|blocked|exited|change --timeout-ms 600000
agentctl label review role=review issue=123
agentctl report "text"                                     :: 在子 Agent 裡回報給父 Agent
agentctl timeline review --since 1h                        :: 狀態何時變化（--since 15m|1h|4h|24h）
agentctl timeline --json
agentctl shutdown --stop-sessions
```

`agentctl timeline` 列出每個終端機的狀態變化（本地時間、Agent 狀態／程序狀態），不給名稱時列出全部；資料和時間軸一樣來自 agent-host，最多 24 小時。agent-host 太舊時會回報 unknown method，更新 agent-host 即可。

- 名稱有重複時會拒絕執行，請改用 `list` 顯示的 session id。
- `--json` 時 stdout 只有結果 JSON，診斷訊息在 stderr。
- 退出碼：0 成功、2 參數錯誤、3 連不到 host、4 找不到或名稱重複、5 操作被拒（例如別的視窗持有輸入、仍有工作在跑時要求 shutdown）、6 啟動失敗、7 逾時（逾時不代表沒執行，請用 `list` 確認）。
- `send` 只是模擬鍵盤輸入。Agent 正在等你確認時送字，可能會被當成回答；自動化請優先使用 Agent 自己的 headless 模式。

## Agent 狀態

| Agent | 狀態來源 | 設定 |
|---|---|---|
| Claude Code 2.1.295 以上 | 終端機狀態協定 OSC 7501 | 不需設定 |
| Codex CLI | hook 呼叫 `agentctl report-state` | 依 `docs\agent-hooks.md` 手動安裝 hook |
| 其他程式 | 無 | 顯示 Unknown |

## 資料位置

`%LOCALAPPDATA%\AgentDesk`：

- `profiles.json`：可自行編輯，新增 profile。
- `sessions.json`：工作紀錄。
- `ui-state.json`：space、分頁與分割，由 host 保存。
- `ui.json`：設定、機器、啟用的 mod、版面、側欄寬度、最近用過的命令。
- `window.json`：視窗的位置、大小與是否最大化。
- `keys.json`：自訂快捷鍵。
- `mods\`、`layouts\`：mod 與自訂版面。
- `themes\`：自己的主題檔，主題編輯器也存在這裡。
- `host.log`：只記錄 ID、事件與錯誤碼，不記錄輸入內容。
- `ui-crash.txt`：介面崩潰時的呼叫堆疊；回報問題時請附上。

設定檔損毀時會改用備份；備份也不能用時，損毀檔會改名為 `*.corrupt-<時間>` 保留，並以預設值啟動。

## 限制

- 只支援 Windows 11 x64；Windows 10 未驗證。不支援多人協作；沒有手機 app。WSL 的支援範圍見上面「WSL」一節。
- 重新開機、登出或 host 異常結束後無法續接原工作（顯示 Lost）。
- 歷史捲動為純文字，最多 5000 行；每個工作的歷史上限 10,000 行或 16 MiB，超過時捨棄最舊的部分。
- 字寬依 libvterm 0.3.3：東亞「模糊寬度」字元一律 1 欄；由零寬連接字組成的 emoji（例如家庭 emoji）不合併，會占多欄；emoji 以單色顯示；不支援 kitty keyboard protocol 與終端機圖片。
- 狀態協定 OSC 7501 的「RIS 清除全部紀錄」規則尚未實作（libvterm 不通知 RIS）。
- 沒有隨附 ConPTY 時，Windows 內建 ConPTY 會吞掉狀態查詢的回覆，Claude Code 因此不會回報狀態。
- 已結束的工作超過 8 個時，較舊的只保留清單紀錄，最後畫面與歷史不再保留。
- 遠端機器需要遠端也是 Windows 並安裝 AgentDesk；Linux／macOS 的 host 尚未提供。
- 介面沒有螢幕閱讀器支援（Dear ImGui 的限制）。
- 遠端機器的 Files／Changes 面板不會即時更新，每 10 秒更新一次。
- shell 所在資料夾的追蹤支援 PowerShell、cmd、WSL 的 bash，以及會送 OSC 7 的 shell；其他 shell 只顯示啟動時的資料夾。
