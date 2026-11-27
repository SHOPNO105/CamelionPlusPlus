#!/usr/bin/env bash
# Package the bundled plugins as Plugins Admin release zips:
#   dist/plugins/<Name>-<version>-linux-<arch>.zip   (containing <Name>.so at the root)
# and print the SHA-256 "id" to put into nppPluginList's src/pl.linux-<arch>.json.
#
#   tools/package-plugins.sh [build-dir]
#
# x64 plugins come from the CMake build tree. arm64 plugins that only need the SDK
# (no GTK) are cross-compiled with aarch64-linux-gnu-g++ when it is installed.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$here/build}"
out="$here/dist/plugins"
mkdir -p "$out"
stamp="202611260000"          # fixed timestamp => reproducible zips

zipone() {   # name version arch so-path
  local name="$1" ver="$2" arch="$3" so="$4"
  local tmp; tmp="$(mktemp -d)"
  cp "$so" "$tmp/$name.so"
  cp "$here/LICENSE" "$tmp/LICENSE"
  touch -t "$stamp" "$tmp/$name.so" "$tmp/LICENSE"
  local zip="$out/$name-$ver-linux-$arch.zip"
  rm -f "$zip"
  (cd "$tmp" && zip -q -X -D "$zip" "$name.so" LICENSE)
  rm -rf "$tmp"
  printf '%-14s %-6s %-6s %s  %s\n' "$name" "$ver" "$arch" "$(sha256sum "$zip" | cut -d' ' -f1)" "$(basename "$zip")"
}

version_of() { grep -o 'CAMELION_PLUGIN_VERSION("[^"]*")' "$1" | sed 's/.*("\(.*\)")/\1/'; }

for name in CamConverter CamMimeTools CamDocList; do
  src="$here/plugins/$name/$name.cpp"
  ver="$(version_of "$src")"
  so="$build/plugins/$name/$name.so"
  [ -f "$so" ] && zipone "$name" "$ver" x64 "$so"
done

if command -v aarch64-linux-gnu-g++ >/dev/null; then
  for name in CamConverter CamMimeTools; do   # SDK-only plugins: no GTK needed
    src="$here/plugins/$name/$name.cpp"
    ver="$(version_of "$src")"
    so="$(mktemp -d)/$name.so"
    aarch64-linux-gnu-g++ -std=c++17 -O2 -fPIC -shared -fvisibility=hidden -I"$here/sdk/include" "$src" -o "$so" -s
    zipone "$name" "$ver" arm64 "$so"
  done
fi
