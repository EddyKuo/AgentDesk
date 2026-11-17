# Microsoft.Windows.Console.ConPTY（隨附）

| 項目 | 值 |
|---|---|
| 版本 | 1.24.260710001（microsoft/terminal v1.24.11911.0） |
| 取得方式 | `scripts/fetch-conpty.ps1`，釘選版本並驗證 nupkg、`conpty.dll`、`OpenConsole.exe` 三個 SHA-256 |
| 放置位置 | `bin/conpty/conpty.dll`、`bin/conpty/x64/OpenConsole.exe`（不進版控，`.gitignore`） |
| 授權 | MIT（`LICENSE`），第三方聲明見 `NOTICE.md`，兩者皆取自 microsoft/terminal 主分支，2026-11-17 |
| 決策 | 設計文件 ADR-08（M0 S1-5 實測後定案） |

為什麼需要：2026-11-17 在 Windows 10.0.26300 實測，系統內建 ConPTY 會自行回應 DA1，並丟掉 host 寫回的 `OSC 7501 ; ?` 回覆，程式因此判定終端機不支援狀態回報。隨附版本會依序轉送兩種回覆。證據在 `tests/integration/windows/conpty_compare_test.cpp`。

散布 AgentDesk 時，`conpty\` 目錄必須和本目錄的 `LICENSE`、`NOTICE.md` 一起放在執行檔旁。
