# Bundled fonts

The same families herdr-app ships (`assets/fonts`), as static weights because ImGui's font
loader renders only the default instance of a variable font. Copied next to `agent-ui.exe` as
`fonts\` at build time and shipped in the zip.

| File | Source | Version | License |
|---|---|---|---|
| Geist-Regular.ttf, Geist-Medium.ttf, Geist-SemiBold.ttf | https://github.com/vercel/geist-font `fonts/Geist/ttf/` | commit ef2597cfc42f4797676df105517ccc0a1cfa6569 (release v1.9.2) | SIL OFL 1.1, `OFL-Geist.txt` |
| JetBrainsMono-Medium.ttf, JetBrainsMono-Bold.ttf | https://github.com/JetBrains/JetBrainsMono `fonts/ttf/` | commit 19371302b95d218af43299bce79ddbddd0bc364d | SIL OFL 1.1, `OFL-JetBrainsMono.txt` |
| SymbolsNerdFont-Regular.ttf | herdr-app `assets/fonts/` (from https://github.com/ryanoasis/nerd-fonts) | herdr-app commit 321c9a8 | MIT, `LICENSE-SymbolsNerdFont.txt` |

Use in AgentDesk (herdr defaults): interface Geist, terminal JetBrains Mono Medium (weight 500)
with Bold for bold text, Symbols Nerd Font for icon glyphs. Microsoft JhengHei (system font,
not redistributed) is merged in for CJK.
