#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SVG="$SCRIPT_DIR/wxterminal.svg"
ICONSET="$SCRIPT_DIR/icon.iconset"
ICNS="$SCRIPT_DIR/icon.icns"

# Convert SVG to a high-res PNG via rsvg-convert (if available) or qlmanage
tmp_png="$SCRIPT_DIR/icon_1024.png"

if command -v rsvg-convert &>/dev/null; then
    rsvg-convert -w 1024 -h 1024 "$SVG" -o "$tmp_png"
elif command -v inkscape &>/dev/null; then
    inkscape --export-png="$tmp_png" -w 1024 -h 1024 "$SVG"
else
    # Fall back to qlmanage (slower but always present on macOS)
    qlmanage -t -s 1024 -o "$SCRIPT_DIR" "$SVG" >/dev/null 2>&1
    mv "$SCRIPT_DIR/wxterminal.svg.png" "$tmp_png"
fi

mkdir -p "$ICONSET"

# Generate all required sizes from the 1024px master
for size in 16 32 64 128 256 512; do
    sips -z $size $size "$tmp_png" --out "$ICONSET/icon_${size}x${size}.png"     >/dev/null
    sips -z $((size*2)) $((size*2)) "$tmp_png" --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
done

iconutil -c icns "$ICONSET" -o "$ICNS"

rm -rf "$ICONSET" "$tmp_png"
echo "Created: $ICNS"
