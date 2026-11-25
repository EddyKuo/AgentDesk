#!/bin/sh
# macOS release package (ADR-30): dist/AgentDesk-<version>-macos-arm64.zip holding
# AgentDesk-<version>-macos-arm64/AgentDesk.app, the layout the updater unpacks (updates.cpp).
#
#   scripts/package_macos.sh [--skip-build] [--sign IDENTITY]
#
# Without --sign the app is signed ad hoc ("-"): it runs on this Mac, and elsewhere only after the
# person allows it (Gatekeeper). A Developer ID signature and notarization need an Apple Developer
# account (design doc 12.3 OQ-11).
set -eu
cd "$(dirname "$0")/.."
root=$(pwd)
preset=clang-macos-arm64-release
identity=-
build=1
while [ $# -gt 0 ]; do
  case "$1" in
    --skip-build) build=0 ;;
    --sign) identity="$2"; shift ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

if [ "$build" = 1 ]; then
  [ -f "build/$preset/CMakeCache.txt" ] || cmake --preset "$preset"
  cmake --build --preset "$preset"
fi

bin="build/$preset/apps/desktop"
share="build/$preset/vcpkg_installed/arm64-osx-agentdesk/share"
version=$("$bin/agent-ui" --version | awk '{print $2}')
name="AgentDesk-$version-macos-arm64"
dist="$root/dist"
stage="$dist/$name"
app="$stage/AgentDesk.app"
rm -rf "$stage" "$dist/$name.zip"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"

# Programs.
for f in agent-ui agent-host agentctl; do
  cp "$bin/$f" "$app/Contents/MacOS/"
done

# The files the programs look for in Resources (fonts, sounds, program table, themes, the example
# mods) and the legal texts.
for d in fonts sounds programs themes mods; do
  cp -R "$bin/$d" "$app/Contents/Resources/"
done
cp LICENSE "$app/Contents/Resources/"
cp assets/icon/agentdesk.icns "$app/Contents/Resources/"
mkdir -p "$app/Contents/Resources/docs"
cp docs/user-guide.md docs/integrations/agent-hooks.md docs/compatibility.md docs/mods.md \
   "$app/Contents/Resources/docs/"

# The mod SDK: headers, the examples' sources and their CMakeLists.txt.
sdk="$app/Contents/Resources/sdk"
mkdir -p "$sdk/include/agentdesk" "$sdk/examples"
cp libs/modapi/include/agentdesk/mod_api.h libs/modapi/include/agentdesk/mod_sdk.hpp "$sdk/include/agentdesk/"
for m in mods/*/; do
  cp -R "$m" "$sdk/examples/"
done
rm -f "$sdk/examples/CMakeLists.txt"
cp libs/modapi/sdk/CMakeLists.txt "$sdk/"
cp -R "build/$preset/vcpkg_installed/arm64-osx-agentdesk/include/nlohmann" "$sdk/include/"

# THIRD_PARTY_NOTICES.md: every shipped component with its own license text. The list is the
# Windows package's (tools/package) without ConPTY, plus FreeType and the libraries it links.
notices="$app/Contents/Resources/THIRD_PARTY_NOTICES.md"
component() {  # name url license-file
  printf '\n## %s\n\nSource: %s\n\n```\n' "$1" "$2" >> "$notices"
  sed -e :a -e '/^\n*$/{$d;N;ba' -e '}' "$3" >> "$notices"
  printf '```\n' >> "$notices"
}
{
  printf '# Third-party notices\n\n'
  printf 'AgentDesk (MIT, see LICENSE) includes the following components. GoogleTest is used for testing only and is not shipped.\n'
} > "$notices"
component "libvterm 0.3.3" "https://www.leonerd.org.uk/code/libvterm/" third_party/libvterm/LICENSE
component "Dear ImGui 1.93.0 (docking), with its macOS backend (modified, third_party/imgui_osx)" \
  "https://github.com/ocornut/imgui" "$share/imgui/copyright"
component "nlohmann/json 3.12.0" "https://github.com/nlohmann/json" "$share/nlohmann-json/copyright"
component "herdr-app (behaviour and parts ported to C++, modified)" "https://github.com/herdrdev/herdr-app" \
  third_party/herdr/LICENSE
component "Geist font" "https://github.com/vercel/geist-font" third_party/fonts/OFL-Geist.txt
component "JetBrains Mono font" "https://github.com/JetBrains/JetBrainsMono" third_party/fonts/OFL-JetBrainsMono.txt
component "Symbols Nerd Font" "https://github.com/ryanoasis/nerd-fonts" third_party/fonts/LICENSE-SymbolsNerdFont.txt
component "lunasvg 3.5.0 (SVG rendering)" "https://github.com/sammycage/lunasvg" "$share/lunasvg/copyright"
component "plutovg 1.3.3 (used by lunasvg)" "https://github.com/sammycage/plutovg" "$share/plutovg/copyright"
component "FreeType (font rendering)" "https://freetype.org" "$share/freetype/copyright"
component "libpng (used by FreeType)" "http://www.libpng.org" "$share/libpng/copyright"
component "zlib (used by FreeType)" "https://zlib.net" "$share/zlib/copyright"
component "bzip2 (used by FreeType)" "https://sourceware.org/bzip2/" "$share/bzip2/copyright"
component "Brotli (used by FreeType)" "https://github.com/google/brotli" "$share/brotli/copyright"
component "LobeHub icons (agent icons, programs/icons)" "https://github.com/lobehub/lobe-icons" \
  third_party/herdr/programs/icons/LICENSE-LobeHub.txt
component "Lucide icons (glyphs, programs/icons)" "https://lucide.dev" third_party/herdr/programs/icons/LICENSE-Lucide.txt
component "oh-my-pi (the omp icon, programs/icons)" "https://github.com/can1357/oh-my-pi" \
  third_party/herdr/programs/icons/LICENSE-oh-my-pi.txt
component "Simple Icons (brand icons, programs/icons; CC0 1.0, brands are their owners' trademarks)" \
  "https://simpleicons.org" third_party/herdr/programs/icons/LICENSES.md

# Info.plist: the bundle's identity, version and minimum system.
cat > "$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>AgentDesk</string>
  <key>CFBundleDisplayName</key><string>AgentDesk</string>
  <key>CFBundleIdentifier</key><string>org.agentdesk.AgentDesk</string>
  <key>CFBundleExecutable</key><string>agent-ui</string>
  <key>CFBundleIconFile</key><string>agentdesk</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$version</string>
  <key>CFBundleVersion</key><string>$version</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.developer-tools</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
</dict>
</plist>
EOF

# Signature: the mods and the helper programs first, then the bundle.
find "$app/Contents/Resources/mods" -name '*.dylib' -exec codesign --force --timestamp=none -s "$identity" {} \;
for f in agent-host agentctl agent-ui; do
  codesign --force --timestamp=none --options runtime -s "$identity" "$app/Contents/MacOS/$f"
done
codesign --force --timestamp=none --options runtime -s "$identity" "$app"
codesign --verify --strict "$app"

# The zip keeps the AgentDesk-<version>-macos-arm64/ folder, as the updater expects.
(cd "$dist" && ditto -c -k --sequesterRsrc --keepParent "$name" "$name.zip")
sha=$(shasum -a 256 "$dist/$name.zip" | awk '{print $1}')
size=$(du -k "$dist/$name.zip" | awk '{printf "%.1f", $1/1024}')
echo "$dist/$name.zip  ${size} MiB  sha256 $sha"
