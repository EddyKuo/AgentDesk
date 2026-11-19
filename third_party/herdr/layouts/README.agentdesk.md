# herdr-app 的內建版面

`main.json`、`terminals.json`、`win98.json` 逐字取自 herdr-app 的 `defaults/layouts/`（commit 321c9a84bc928f1c31be59e73dad966d86a3e252，Apache-2.0，授權見 `../LICENSE`）。

建置時 `libs/ui/CMakeLists.txt` 把這三個檔案的內容編進 `agentdesk_ui`（`layout::builtin_text`），不另外複製到執行檔旁。AgentDesk 沿用舊名：`main` 叫 `workspace`，其餘同名。

要更新時直接換掉這三個檔案，不要手改內容。
