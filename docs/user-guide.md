# AgentDesk 使用說明

AgentDesk 是 Windows 11 x64 的 AI coding agent 工作台：在可分割的終端機面板裡同時跑 Claude Code、Codex CLI 或任何命令列程式，關掉視窗後工作繼續跑，重開時接回原來的版面，並告訴你哪個 Agent 在等你。

## 安裝

1. 解壓 `AgentDesk-<版本>-win-x64.zip` 到任意資料夾，例如 `%LOCALAPPDATA%\Programs\AgentDesk`。不需要系統管理員權限，也不寫登錄檔。
2. 確認 `conpty\conpty.dll` 與 `conpty\x64\OpenConsole.exe` 跟執行檔放在一起。少了它們，Claude Code 的狀態回報會失效（見「限制」）。
3. 先自行安裝並登入要用的 Agent CLI。AgentDesk 不處理登入，也不複製任何 token。

## 畫面

執行 `agent-ui.exe`。它會在背景啟動 `agent-host.exe`，所有工作都跑在 host 裡。視窗沒有系統標題列，最小化、最大化、關閉按鈕在分頁列右端；拖曳分頁列或左上的品牌列可以移動視窗，雙擊切換最大化，也能用 Windows 的貼齊功能。

視窗會記住上次的位置、大小，以及是否最大化，下次開啟時回到原處；那台螢幕不在了，就以同樣大小開在滑鼠所在的螢幕中間。第一次開啟時，大小依螢幕決定：約為 1280×820 乘上 Windows 的縮放比例與 AgentDesk 的縮放（設定裡的 zoom），並保持在螢幕可用範圍的一半到九成之間。所以 Full HD 100% 和 4K 150% 開出來的視窗，看起來的比例差不多。想重新套用預設大小，關掉 AgentDesk 後刪除 `window.json`。

- **側欄**：預設寬 268 px，拖曳右緣可在 44～480 px 之間調整。窄於 160 px 時改成只顯示編號與狀態的窄欄；品牌列的收合鈕在兩者之間切換，Ctrl+Shift+B 整個隱藏。隱藏後，分頁列最左邊會出現側欄按鈕，點一下就回來。
- **Spaces**：一個工作地點，通常是一個 repo。每列是彩色字母頭像、名稱，靠右是 `repo · 分支`，再加上領先／落後遠端的 ↑N／↓N（名稱已經說明的部分、main／master 會省略）。沒取名的 space 以第一個終端機所在的 repo 頂層資料夾命名，家目錄顯示 `~`；在 shell 裡 `cd` 之後會跟著變。linked worktree 的 space 以 `├─`／`└─` 掛在主 repo 的 space 下面。拖曳可調整順序；右鍵選單有改名、新增／開啟／移除 worktree、關閉。
- **Agents**：有回報狀態的 Agent。預設依「Needs you／Done／Working／Idle」分段，同段內最近變動的在前；排序鈕可改成依 space 分組。每列是程式圖示、space 名稱與工作名稱、距離上次變動多久、狀態標記；滑鼠移上去可以釘選到最上面。子 Agent 以樹狀線列在父 Agent 下面。
- **分頁列**：膠囊狀的分頁，關閉鈕只在目前分頁。雙擊改名，拖到別的分頁上放開可調整順序，在分頁列上轉動滑鼠滾輪可以切換分頁。沒取名的 Agent 分頁跟著 Agent 的即時標題。「+」開啟新增選單；右側是 Files、Changes 兩個面板按鈕。命令面板的「Settle tab」可以把分頁收起來：收起的分頁不顯示在分頁列，程式照常執行；用 Ctrl+Shift+K 選它，或它需要你時，會回到分頁列。
- **面板**：分頁裡的終端機，圓角外框，目前面板有強調色邊框。標題列顯示程式、名稱、資料夾與分支、狀態；分割、放大、關閉按鈕在滑鼠移上去時出現。拖曳標題列中間的把手，可以把面板放到另一個面板的邊緣（移到那一側），或放在中間（互換位置）。
- **Files／Changes**：右側面板，跟著目前面板 shell 所在的資料夾。Files 是可展開的檔案樹，檔名旁有 git 狀態字母，點檔案可唯讀預覽。Changes 列出所有未提交的檔案與 +／− 行數，點檔案看差異，下方可輸入訊息 commit（會先 `git add -A`）。開關狀態依 space 記住。
- **狀態列**：機器、目前分支、Agent 數量與需要你的數量，點右側的快捷鍵提示可以開命令面板。

**Done** 的意思是「在你沒看的時候完成了」，看過之後變回 Idle。沒有可信的狀態來源時，AgentDesk 不會自己猜狀態。

## 通知

Agent 需要你時一定會播放提示音；你沒在看的 Agent 完成時也會。你沒在看那個 Agent 時，右下角會出現通知（5 秒，錯誤 8 秒），點一下就跳到那個 Agent。內建兩段提示音，可在「設定 > Alerts」關閉、試聽，或在 `ui.json` 的 `sound_done`／`sound_blocked` 改成自己的 MP3。

程式剛啟動就以錯誤結束時，通知會附上結束碼和最後幾行輸出。

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
| Ctrl+= ／Ctrl+- ／Ctrl+0 | 放大／縮小／重設整個視窗 |
| Ctrl+, | 設定 |

這些按鍵由 AgentDesk 使用，不會送進終端機裡的程式。要改鍵位，在「設定 > Look」按「Edit keys.json」，例如：

```json
{ "global": { "ctrl-shift-t": "", "ctrl-alt-n": "tab.new" } }
```

值為空字串表示取消綁定。格式如下：

- 區段：`global`（整個視窗）、`terminal`（終端機有焦點時）、`layout:<版面名稱>`（顯示該版面時）。同一個鍵在多個區段都有設定時，依 terminal、layout、global 的順序取用。
- 動作可以是命令名稱、`命令:值`、`{"command": "layout.switch", "args": {"name": "terminals"}}`、`send:<文字>`（送進終端機），或 `terminal`（這個鍵交給程式）。
- 檔案有錯（無法讀的按鍵、未知的區段或命令）時，整個檔案都不套用，按鍵維持預設，錯誤會顯示在設定頁。

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

Ctrl+Shift+L、命令面板或「設定 > Look」切換：

- **workspace**（設定裡顯示為 AgentDesk）：預設，側欄＋分頁＋分割面板＋右側面板。
- **terminals**：沒有側欄與狀態列的版面。上方列最左邊是側欄按鈕（點了切回有側欄的 AgentDesk 版面，Ctrl+Shift+B 也可以），接著是目前的 space，點一下開啟 Ctrl+Shift+K 的選單；每個分頁顯示程式圖示、狀態與資料夾；右邊是設定（齒輪，可在這裡切回 AgentDesk 版面）、命令面板與新分頁按鈕，滑鼠停在按鈕上會顯示名稱與快捷鍵。面板標題列的按鈕一直顯示。

自訂版面放在 `%LOCALAPPDATA%\AgentDesk\layouts\<名稱>.json`：

```json
{ "name": "wall", "sidebar": false, "tab_bar": false, "status_bar": true, "theme": "ember" }
```

命令面板的「Duplicate layout」把目前的版面複製成 `<名稱>-copy.json` 並切換過去，之後直接改這個檔案。「Reset layout」把版面還原成隨附的樣子，並重設側欄與右側面板的寬度；你改過的檔案會改名成 `<名稱>.json.old-<時間>` 保留。

## 設定

Ctrl+, 開啟，左側分成 Look、Machines、Alerts、Mods、Updates、Privacy。每次修改都立即生效並儲存，沒有儲存鈕。

- **Look**：版面與主題卡片（我們的 Midnight、Ember、Moss、Paper，加上 graphite、ink、moss、paper、atom one dark；把同樣格式的主題檔放進 `%LOCALAPPDATA%\AgentDesk\themes\` 也會列出來，每個版面各自記住深色與淺色主題）；整體縮放、介面文字大小（預設 15 px，覺得太小可以調大）、終端機字型與大小、行高、字寬、捲動速度；新終端機用的 shell、放開滑鼠就複製、keys.json、worktree 資料夾。
- **Machines**：遠端機器清單與「Add a machine…」。
- **Alerts**：提示音開關與試聽、右下角通知開關。
- **Mods**：啟用或停用 mod、看錯誤、重新載入。
- **Updates**：AgentDesk 與 agent-host 的版本、「What's new」（更新後第一次啟動會自動顯示一次）、「Check now」。AgentDesk 沒有內建的更新伺服器，要設定環境變數 `AGENTDESK_RELEASES_URL` 才會每小時檢查一次；有新版時會在右下角提示，按「Restart to update」換版，或下次結束時換。agent-host 可以單獨更新，終端機裡的程式不會中斷。
- **Privacy**：使用統計的開關，以及「What we send」逐字顯示會送出的內容。只有設定 `AGENTDESK_TELEMETRY_URL` 時才會送出；`DO_NOT_TRACK` 或 `AGENTDESK_TELEMETRY=0` 會關閉這一次執行。

左下的「Send feedback」開一個草稿視窗，草稿最後會附上 AgentDesk 與 Windows 的版本。Send feedback 的草稿不會自己送出：按「Copy」複製，或按「Save to file」存到 `%LOCALAPPDATA%\AgentDesk\feedback\`，再用你習慣的方式寄出。

介面字型是隨附的 Geist，終端機預設是隨附的 JetBrains Mono Medium；中文字以微軟正黑體補上。

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

Mod 在 AgentDesk 裡以你的權限執行，跟你安裝的其他程式一樣，所以只啟用你信任的 mod。`exec` 直接啟動程式，不經過 shell。mod 當掉（例如記憶體存取錯誤）時會被停用，錯誤顯示在設定頁，AgentDesk 照常執行；但 mod 卡在無窮迴圈時，整個介面會停住。舊版的 Lua mod（`main.lua`）已不支援，設定頁會顯示錯誤；三個範例會自動換成 C++ 版本，原本的 `settings.json` 保留。

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
agentctl shutdown --stop-sessions
```

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
