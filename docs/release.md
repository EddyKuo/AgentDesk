# Making a release

## 1. Build and test

```bat
scripts\build.cmd msvc-x64-release all
```

A fresh clone needs the bundled ConPTY first; see "Building from source" in the README. Every test must pass. Note that `build.cmd` ends any `agent-ui` and `agent-host` running from `build\msvc-x64-release\`, together with their terminals.

## 2. Package

```bat
build\msvc-x64-release\tools\agentdesk_package.exe --version 0.2.0
```

This writes two files to `dist\`:

- `AgentDesk-<version>-win-x64.zip`: the three executables, `conpty\` (the bundled ConPTY), data folders, example mods, the mod SDK, `LICENSE`, `THIRD_PARTY_NOTICES.md` and `docs\`.
- `AgentDesk-<version>-win-x64-symbols.zip`: the PDBs. Keep it to read crash reports (`%LOCALAPPDATA%\AgentDesk\ui-crash.txt`); it does not need to be published.

Then smoke-test the zip in a clean folder, with an isolated pipe and data folder (`vt_probe` plays a fake agent that reports its state):

```bat
build\msvc-x64-release\tools\qa\smoke_zip.exe --zip dist\AgentDesk-0.2.0-win-x64.zip --probe build\msvc-x64-release\tools\vt_probe.exe
```

`--version` defaults to 0.1.0, so always pass it.

## 2b. macOS package

```sh
scripts/package_macos.sh            # Release build, then dist/AgentDesk-<version>-macos-arm64.zip
scripts/package_macos.sh --sign "Developer ID Application: <name>"   # with a Developer ID
```

The zip holds `AgentDesk-<version>-macos-arm64/AgentDesk.app` (the layout the updater unpacks): the three programs in `Contents/MacOS`, and fonts, sounds, programs, themes, example mods, the mod SDK, `LICENSE`, `THIRD_PARTY_NOTICES.md` and `docs/` in `Contents/Resources`. Without `--sign` it is signed ad hoc. Notarization (`xcrun notarytool`) needs an Apple Developer account and is not scripted yet (design doc 12.3, OQ-11). The update manifest's macOS target is `darwin-aarch64`.

Smoke-test it like the Windows zip: unzip into a clean folder and run `AgentDesk.app/Contents/MacOS/agent-ui --pipe smoke --start zsh --exit-after-ms 5000 --screenshot shot.png` with `AGENTDESK_DATA_DIR` pointing at an empty folder.

## 3. Publish on GitHub

Steps 1 and 2 are for checking a release locally; publishing is done by `.github/workflows/release.yml`:

1. Make sure `project(... VERSION x.y.z)` in `CMakeLists.txt` is the version to release; the workflow stops if the tag says otherwise.
2. Tag the commit on `main` and push the tag: `git tag v0.2.0` and `git push origin v0.2.0`.
3. The workflow builds and tests the Release preset on Windows and macOS, packages both zips, smoke-tests the Windows zip, and creates the GitHub Release with `AgentDesk-<version>-win-x64.zip`, `AgentDesk-<version>-macos-arm64.zip`, their SHA-256 and the generated change list. Versions below 1.0 are marked pre-release. The Windows symbols zip stays in the run's artifacts (`package-windows`).

If a run fails before publishing, fix it and run the workflow again by hand (Actions, Release, "Run workflow", with the existing tag). If the release already exists, delete it first (`gh release delete v0.2.0`), or the last step fails.

## Code signing and SmartScreen

The executables are not signed. Windows SmartScreen therefore shows "Windows protected your PC" on first run until the download builds reputation; the README tells people how to continue. Signing removes the warning and needs a code-signing certificate (for example Azure Trusted Signing, or an OV/EV certificate from a certificate authority). When there is one, sign `agent-ui.exe`, `agent-host.exe`, `agentctl.exe` and the example `mod.dll` files with `signtool sign /fd SHA256 /tr <timestamp-url> /td SHA256` before packaging.

## Update checks (optional)

AgentDesk has no default update server; it checks only when `AGENTDESK_RELEASES_URL` is set. The server is a static folder:

```
<AGENTDESK_RELEASES_URL>/app/latest.json     the newest AgentDesk
<AGENTDESK_RELEASES_URL>/core/latest.json    the newest agent-host
```

Both files have the same shape:

```json
{
  "version": "0.2.1",
  "targets": {
    "windows-x86_64": {
      "url": "https://github.com/<owner>/AgentDesk/releases/download/v0.2.1/AgentDesk-0.2.1-win-x64.zip",
      "sha256": "<sha256 of that zip>"
    }
  }
}
```

GitHub release asset names cannot contain `/`, so the two `latest.json` files cannot live in a release itself. Host them on any static site (GitHub Pages works) and let `url` point at the release asset.

## Telemetry

Usage statistics are off unless `AGENTDESK_TELEMETRY_URL` is set; the public build sends nothing.
