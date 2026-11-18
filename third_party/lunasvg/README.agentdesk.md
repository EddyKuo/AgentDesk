# lunasvg (vcpkg)

| Item | Value |
|---|---|
| Version | lunasvg 3.5.0, with plutovg 1.3.3 (its renderer) |
| Source | vcpkg port `lunasvg` (manifest `vcpkg.json`, baseline `afa249f`, pinned by `overrides`); https://github.com/sammycage/lunasvg, https://github.com/sammycage/plutovg |
| License | MIT (both), `share/lunasvg/copyright`, `share/plutovg/copyright` in the vcpkg tree; listed in the release's THIRD_PARTY_NOTICES |
| Changes | none |

Why: Herdr 98 (herdr-app `defaults/mods/win98`) draws with SVG icons and pictures (the 32/16/9 px
pixel-art icons, the card faces with their `<text>` and patterns, Clippy, the layout preview).
herdr renders them with its own SVG engine; AgentDesk rasterises them once per size with lunasvg
into ImGui textures (`libs/ui/src/image_cache.cpp`). nanosvg (also in vcpkg) was not enough: it
has no `<text>` and no `<pattern>`, which the Solitaire cards use. PNGs are decoded with the
Windows Imaging Component (no extra dependency).
