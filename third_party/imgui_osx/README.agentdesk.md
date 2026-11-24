# Dear ImGui macOS platform backend (imgui_impl_osx)

- Source: Dear ImGui 1.93.0 (docking), `backends/imgui_impl_osx.mm` and `.h`, as vcpkg builds it
  (baseline `afa249f`).
- License: MIT (`LICENSE.txt`), the same as the Dear ImGui library AgentDesk already uses.
- Why it is here instead of vcpkg's `osx-binding` feature: the upstream backend ignores an input
  method's marked text (`hasMarkedText` is always NO), so Zhuyin, Pinyin and Kana compositions are
  invisible and their Space/Enter/arrow keys reach the program (design doc ADR-30, FR-39).
- AgentDesk changes (each marked `AgentDesk:` in the source):
  - `KeyEventResponder` keeps the marked text and reports it through
    `ImGui_ImplOSX_SetMarkedTextCallback`; committing (`insertText`) or `unmarkText` ends it.
  - While marked text exists, key events go to the input method only (no `AddKeyEvent`).
