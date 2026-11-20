# lunasvg (vcpkg)

| Item | Value |
|---|---|
| Version | lunasvg 3.5.0, with plutovg 1.3.3 (its renderer) |
| Source | vcpkg port `lunasvg` (manifest `vcpkg.json`, baseline `afa249f`, pinned by `overrides`); https://github.com/sammycage/lunasvg, https://github.com/sammycage/plutovg |
| License | MIT (both), `share/lunasvg/copyright`, `share/plutovg/copyright` in the vcpkg tree; listed in the release's THIRD_PARTY_NOTICES |
| Changes | none |

Why: the program icons (`third_party/herdr/programs/icons`) are SVG files. AgentDesk rasterises
them once per size with lunasvg into ImGui textures (`libs/ui/src/image_cache.cpp`).
