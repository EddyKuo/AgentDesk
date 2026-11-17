# libvterm（vendored）

| 項目 | 值 |
|---|---|
| 版本 | 0.3.3 |
| 來源 | https://www.leonerd.org.uk/code/libvterm/libvterm-0.3.3.tar.gz |
| SHA-256（tarball） | `09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0` |
| 取得日期 | 2026-11-17 |
| 授權 | MIT（`LICENSE`） |
| 收錄內容 | `include/`、`src/`（含 tarball 已附的 `src/encoding/*.inc`）、`LICENSE` |
| 修改 | 無。建置設定在 `CMakeLists.txt` |

vcpkg 沒有 libvterm port，所以以原始碼收錄（設計文件 3.1、OQ-07）。官方 tarball 已附產生好的 `.inc`，建置不需要 perl。
