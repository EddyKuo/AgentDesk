# AgentDesk

Windows 原生的 AI coding agent 終端機工作台：在可分割的面板裡同時跑多個 Agent CLI（首發支援 Claude Code 與 Codex CLI），關掉視窗工作照常執行，重開時接回原畫面，並顯示每個 Agent 是否在等你。

設計參考 [herdr](https://github.com/herdrdev/herdr)，但是獨立實作，不追求相容。

## 目前狀態

設計與規劃已完成，**尚未開始寫程式**。下一步是 Sprint 1：M0 技術 POC（`sprint/current/plan.md`）。

## 文件

| 路徑 | 內容 |
|---|---|
| `docs/design/AgentDesk_Design.md` | 產品需求與系統設計（單一真相來源） |
| `contracts/approved/PRD.md` | 鎖定版本的 PRD 契約 |
| `docs/research/herdr-reference.md` | herdr 研究筆記 |
| `docs/poc/M0-report-template.md` | M0 POC 報告範本 |
| `sprint/` | Sprint 計畫與狀態 |
| `.decisions/` | 治理與流程決策（產品與技術決策在設計文件 7.1） |
| `CLAUDE.md` | 給 Claude Code 的專案指引 |

## 技術基線

C++20、Dear ImGui 1.93.0（docking）＋Win32＋DirectX 11、ConPTY、libvterm 0.3.3、本機 Named Pipe。開發只用 Visual Studio 2022 Build Tools（MSVC 14.44）、CMake、Ninja 與 vcpkg，不需另外安裝工具。

## 授權

MIT，見 `LICENSE`。
