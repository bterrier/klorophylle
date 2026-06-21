#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Regenerate the whole platform icon set from the brand SVGs in branding/.
#   source (full icon):   branding/icon.svg   — light cyan-white rounded square + mark
#   source (foreground):  branding/mark.svg   — transparent mark for the Android adaptive fg
#
# Outputs (mirroring the WatchFlower assets/ layout):
#   linux/   klorophylle.svg + klorophylle.desktop + hicolor/<size>/apps/klorophylle.png
#   windows/ klorophylle.ico (16..256) + klorophylle.rc
#   macos/   klorophylle.icns + Images.xcassets/AppIcon.appiconset/ + Info.plist
#   android/ res/mipmap-*/ic_launcher[_round].png + adaptive (mipmap-anydpi-v26 + drawable fg
#            + values/ic_launcher_background.xml) + AndroidManifest.xml      [staged for mobile]
#   ios/     Images.xcassets/AppIcon.appiconset/ + Info.plist               [staged for mobile]
#
# Desktop targets are wired into src/app/CMakeLists.txt now; mobile is generated/staged
# (the mobile *build* lands later). Re-run after editing branding/icon.svg.
#
# Requires: inkscape (rasterise), ImageMagick `convert` (.ico/.icns/resize/mask).
# Note: .icns here is built by ImageMagick on Linux; on macOS `iconutil -c icns
# AppIcon.appiconset` produces an Apple-optimal file if you prefer.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/branding/icon.svg"
FG="$HERE/branding/mark.svg"
APP="klorophylle"
BG="#f2fbfb"   # surface / Android adaptive background (the design system)

command -v inkscape >/dev/null || { echo "error: inkscape not found" >&2; exit 1; }
command -v convert  >/dev/null || { echo "error: ImageMagick 'convert' not found" >&2; exit 1; }
[ -f "$SRC" ] || { echo "error: $SRC missing" >&2; exit 1; }

tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
say() { printf '  %s\n' "$*"; }

render() { # svg size outfile  — square PNG at <size>px
    inkscape "$1" -o "$3" -w "$2" -h "$2" >/dev/null 2>&1
}
round() { # squarePNG size dstPNG  — clip to a circle (Android *_round)
    local r=$(( $2 / 2 ))
    convert -size "$2x$2" xc:none -fill white -draw "circle $r,$r $r,0" "$tmp/_mask.png"
    convert "$1" "$tmp/_mask.png" -alpha off -compose CopyOpacity -composite "$3"
}
adaptive_fg() { # px dstPNG  — mark centred at ~66% on a transparent square (adaptive safe zone)
    local inner=$(( $1 * 66 / 100 ))
    render "$FG" "$inner" "$tmp/_fg.png"
    convert -size "$1x$1" xc:none "$tmp/_fg.png" -gravity center -composite "$2"
}

echo "Klorophylle icon set  <-  branding/icon.svg"

# ---- Linux -----------------------------------------------------------------
echo "[linux]"
mkdir -p "$HERE/linux"
cp "$SRC" "$HERE/linux/$APP.svg"
cat > "$HERE/linux/$APP.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Klorophylle
Comment=Plant-first monitoring for Bluetooth Low Energy soil & climate sensors
Exec=$APP
Icon=$APP
Categories=Utility;DataVisualization;Qt;
EOF
for s in 16 24 32 48 64 128 256 512; do
    mkdir -p "$HERE/linux/hicolor/${s}x${s}/apps"
    render "$SRC" "$s" "$HERE/linux/hicolor/${s}x${s}/apps/$APP.png"
done
say "klorophylle.svg + klorophylle.desktop + hicolor 16..512"

# ---- Windows ---------------------------------------------------------------
echo "[windows]"
mkdir -p "$HERE/windows"
win=()
for s in 16 24 32 48 64 128 256; do render "$SRC" "$s" "$tmp/win_$s.png"; win+=("$tmp/win_$s.png"); done
convert "${win[@]}" "$HERE/windows/$APP.ico"
printf 'IDI_ICON1   ICON    DISCARDABLE     "%s.ico"\n' "$APP" > "$HERE/windows/$APP.rc"
say "klorophylle.ico (16..256) + klorophylle.rc"

# ---- macOS -----------------------------------------------------------------
echo "[macos]"
SET="$HERE/macos/Images.xcassets/AppIcon.appiconset"
mkdir -p "$SET"
icns=()
emit_mac() { # base px1 px2  -> icon_<base>.png (px1) + icon_<base>@2x.png (px2)
    render "$SRC" "$2" "$SET/icon_${1}.png";    icns+=("$SET/icon_${1}.png")
    render "$SRC" "$3" "$SET/icon_${1}@2x.png"; icns+=("$SET/icon_${1}@2x.png")
}
emit_mac 16x16   16  32
emit_mac 32x32   32  64
emit_mac 128x128 128 256
emit_mac 256x256 256 512
emit_mac 512x512 512 1024
# Best .icns: Apple's iconutil (macOS) consumes our Apple-named PNGs directly; it wants a
# folder literally named *.iconset. On Linux fall back to ImageMagick (single-image icns —
# fine as a stand-in; the real .icns is produced when the macOS build runs).
if command -v iconutil >/dev/null 2>&1; then
    cp -r "$SET" "$tmp/$APP.iconset"
    iconutil -c icns "$tmp/$APP.iconset" -o "$HERE/macos/$APP.icns"
else
    convert "$SET/icon_512x512@2x.png" "$HERE/macos/$APP.icns" 2>/dev/null \
        || { render "$SRC" 512 "$tmp/big.png"; convert "$tmp/big.png" "$HERE/macos/$APP.icns"; }
fi
cat > "$SET/Contents.json" <<'EOF'
{
  "images" : [
    { "size":"16x16","idiom":"mac","filename":"icon_16x16.png","scale":"1x" },
    { "size":"16x16","idiom":"mac","filename":"icon_16x16@2x.png","scale":"2x" },
    { "size":"32x32","idiom":"mac","filename":"icon_32x32.png","scale":"1x" },
    { "size":"32x32","idiom":"mac","filename":"icon_32x32@2x.png","scale":"2x" },
    { "size":"128x128","idiom":"mac","filename":"icon_128x128.png","scale":"1x" },
    { "size":"128x128","idiom":"mac","filename":"icon_128x128@2x.png","scale":"2x" },
    { "size":"256x256","idiom":"mac","filename":"icon_256x256.png","scale":"1x" },
    { "size":"256x256","idiom":"mac","filename":"icon_256x256@2x.png","scale":"2x" },
    { "size":"512x512","idiom":"mac","filename":"icon_512x512.png","scale":"1x" },
    { "size":"512x512","idiom":"mac","filename":"icon_512x512@2x.png","scale":"2x" }
  ],
  "info" : { "version":1, "author":"klorophylle" }
}
EOF
cat > "$HERE/macos/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
    <key>CFBundleName</key><string>Klorophylle</string>
    <key>CFBundleDisplayName</key><string>Klorophylle</string>
    <key>CFBundleExecutable</key><string>klorophylle</string>
    <key>CFBundleIdentifier</key><string>com.klorophylle.app</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleIconFile</key><string>klorophylle.icns</string>
    <key>NSHighResolutionCapable</key><true/>
</dict></plist>
EOF
say "klorophylle.icns + AppIcon.appiconset (16..512@2x) + Info.plist"

# ---- Android (staged for mobile) ------------------------------------------
echo "[android]"
declare -A dens=( [mdpi]=48 [hdpi]=72 [xhdpi]=96 [xxhdpi]=144 [xxxhdpi]=192 )
declare -A fgpx=( [mdpi]=108 [hdpi]=162 [xhdpi]=216 [xxhdpi]=324 [xxxhdpi]=432 )
for d in "${!dens[@]}"; do
    mkdir -p "$HERE/android/res/mipmap-$d" "$HERE/android/res/drawable-$d"
    render "$SRC" "${dens[$d]}" "$HERE/android/res/mipmap-$d/ic_launcher.png"
    round "$HERE/android/res/mipmap-$d/ic_launcher.png" "${dens[$d]}" "$HERE/android/res/mipmap-$d/ic_launcher_round.png"
    adaptive_fg "${fgpx[$d]}" "$HERE/android/res/drawable-$d/ic_launcher_foreground.png"
done
mkdir -p "$HERE/android/res/mipmap-anydpi-v26" "$HERE/android/res/values"
cat > "$HERE/android/res/mipmap-anydpi-v26/ic_launcher.xml" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/ic_launcher_background"/>
    <foreground android:drawable="@drawable/ic_launcher_foreground"/>
</adaptive-icon>
EOF
cat > "$HERE/android/res/mipmap-anydpi-v26/ic_launcher_round.xml" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/ic_launcher_background"/>
    <foreground android:drawable="@drawable/ic_launcher_foreground"/>
</adaptive-icon>
EOF
cat > "$HERE/android/res/values/ic_launcher_background.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <color name="ic_launcher_background">$BG</color>
</resources>
EOF
if [ ! -f "$HERE/android/AndroidManifest.xml" ]; then
cat > "$HERE/android/AndroidManifest.xml" <<'EOF'
<?xml version="1.0"?>
<!-- Minimal manifest stub for the icon set; replace/extend when the mobile build lands. -->
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.klorophylle.app">
    <application android:icon="@mipmap/ic_launcher"
                 android:roundIcon="@mipmap/ic_launcher_round"
                 android:label="Klorophylle"/>
</manifest>
EOF
fi
say "mipmap/drawable 5 densities + adaptive icon + background color + manifest stub"

# ---- iOS (staged for mobile) -----------------------------------------------
echo "[ios]"
ISET="$HERE/ios/Images.xcassets/AppIcon.appiconset"
mkdir -p "$ISET"
ios_png() { render "$SRC" "$2" "$ISET/$1"; }   # name px
ios_png "Icon-20@2x.png"   40 ; ios_png "Icon-20@3x.png"   60
ios_png "Icon-29@2x.png"   58 ; ios_png "Icon-29@3x.png"   87
ios_png "Icon-40@2x.png"   80 ; ios_png "Icon-40@3x.png"  120
ios_png "Icon-60@2x.png"  120 ; ios_png "Icon-60@3x.png"  180
ios_png "Icon-20.png"      20 ; ios_png "Icon-20@2x-ipad.png" 40
ios_png "Icon-29.png"      29 ; ios_png "Icon-29@2x-ipad.png" 58
ios_png "Icon-40.png"      40 ; ios_png "Icon-40@2x-ipad.png" 80
ios_png "Icon-76.png"      76 ; ios_png "Icon-76@2x.png"     152
ios_png "Icon-83.5@2x.png" 167; ios_png "Icon-1024.png"    1024
cat > "$ISET/Contents.json" <<'EOF'
{
  "images" : [
    { "idiom":"iphone","size":"20x20","scale":"2x","filename":"Icon-20@2x.png" },
    { "idiom":"iphone","size":"20x20","scale":"3x","filename":"Icon-20@3x.png" },
    { "idiom":"iphone","size":"29x29","scale":"2x","filename":"Icon-29@2x.png" },
    { "idiom":"iphone","size":"29x29","scale":"3x","filename":"Icon-29@3x.png" },
    { "idiom":"iphone","size":"40x40","scale":"2x","filename":"Icon-40@2x.png" },
    { "idiom":"iphone","size":"40x40","scale":"3x","filename":"Icon-40@3x.png" },
    { "idiom":"iphone","size":"60x60","scale":"2x","filename":"Icon-60@2x.png" },
    { "idiom":"iphone","size":"60x60","scale":"3x","filename":"Icon-60@3x.png" },
    { "idiom":"ipad","size":"20x20","scale":"1x","filename":"Icon-20.png" },
    { "idiom":"ipad","size":"20x20","scale":"2x","filename":"Icon-20@2x-ipad.png" },
    { "idiom":"ipad","size":"29x29","scale":"1x","filename":"Icon-29.png" },
    { "idiom":"ipad","size":"29x29","scale":"2x","filename":"Icon-29@2x-ipad.png" },
    { "idiom":"ipad","size":"40x40","scale":"1x","filename":"Icon-40.png" },
    { "idiom":"ipad","size":"40x40","scale":"2x","filename":"Icon-40@2x-ipad.png" },
    { "idiom":"ipad","size":"76x76","scale":"1x","filename":"Icon-76.png" },
    { "idiom":"ipad","size":"76x76","scale":"2x","filename":"Icon-76@2x.png" },
    { "idiom":"ipad","size":"83.5x83.5","scale":"2x","filename":"Icon-83.5@2x.png" },
    { "idiom":"ios-marketing","size":"1024x1024","scale":"1x","filename":"Icon-1024.png" }
  ],
  "info" : { "version":1, "author":"klorophylle" }
}
EOF
cat > "$HERE/ios/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
    <key>CFBundleName</key><string>Klorophylle</string>
    <key>CFBundleDisplayName</key><string>Klorophylle</string>
    <key>CFBundleIdentifier</key><string>com.klorophylle.app</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleIconName</key><string>AppIcon</string>
</dict></plist>
EOF
say "AppIcon.appiconset (iPhone+iPad+marketing) + Info.plist"

echo "done."
