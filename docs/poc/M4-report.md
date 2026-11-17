# M4 報告：驗收發布

日期：2026-11-17｜對應：設計文件 5.1 M4、Sprint 5｜詳細測試結果：`contracts/inbox/TestReport_sprint5.md`

## 結論

MVP 可發布。`scripts/package.ps1` 產生的 `AgentDesk-0.1.0-win-x64.zip`（2.3 MB）在乾淨目錄冒煙通過。驗收矩陣 22 項中 20 項通過，QA-17、QA-21 部分通過，QA-15 等人工驗收。

## 交付物

| 項目 | 位置 |
|---|---|
| portable zip、符號檔 zip | `dist\`（由 `scripts\package.ps1` 產生，不進版控） |
| 使用說明、相容性矩陣 | `docs/user-guide.md`、`docs/compatibility.md`（也放在 zip 的 `docs\`） |
| 第三方授權 | zip 內 `THIRD_PARTY_NOTICES.md`（由各依賴自己的授權檔組成）、`conpty\LICENSE`、`conpty\NOTICE.md` |
| 驗證工具 | `tools/soak`、`tools/qa/*.ps1`（QA-05、QA-07、QA-10、截圖、zip 冒煙） |

## M4 期間的主要變更

1. **穩定性**：soak 工具在 1 分鐘內就抓到已結束的 session 不釋放資源。修正後只保留最新 8 個的完整畫面，30 分鐘 soak 的 handle、執行緒、記憶體都沒有持續成長。
2. **介面**：依使用者要求改為接近 herdr 的版面（設計文件 ADR-12）：側欄 Spaces 與 Agents、分頁加分割面板、卡片面板、Details 卡片、細狀態列。
3. **中文顯示**：格寬原本取自會無條件進位的 `CalcTextSize`，造成游標偏移；中文字形沒有填滿兩格。改為真實寬度、逐格定位、寬字元放大置中，並補上符號與 emoji 的備援字型。
4. **安全檢查**：OSC 52 與惡意輸出不產生輸入、pipe DACL 讀回只有 SYSTEM 與目前使用者、log 不含輸入文字。

## 效能

| 指標 | 結果 | 目標 |
|---|---|---|
| 4 個 session 重新 attach 到畫面完整（Release） | p95 44 ms | < 2 秒 |
| 輸入回顯延遲（M0 量測） | p95 16.1 ms | 設計文件 1.5 |
| 30 分鐘 soak 私有記憶體 | 中段 170 MB、末段 150 MB | 無持續成長 |

## 待人工驗收

繁中注音與候選視窗、100%／150%／200% DPI、Claude Code 實際回報 working／blocked／done（需要送 prompt）、Codex hook 實機、8 小時 soak、另一個 Windows 帳號連線被拒。

## 注意事項

自動截圖測試曾多次在幾秒內強制結束 Claude Code，Claude Code 因此在它自己的設定裡關掉了全螢幕渲染器。之後的自動測試改用 PowerShell 與 vt_probe，不再強制結束實際的 Agent。要恢復時，在 Claude Code 裡執行 `/tui fullscreen`。
