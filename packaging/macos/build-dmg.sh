#!/usr/bin/env bash
# Bundles Qt into VocalInk.app, signs it ad hoc and wraps it in a drag-to-install dmg.
#
#   packaging/macos/build-dmg.sh <build-dir> [version]
#
# Set CODESIGN_IDENTITY to a "Developer ID Application" identity to sign for
# distribution (notarisation is a separate step).
#
# The virtual mic driver (VocalInkVirtualMic.driver) goes into
# Contents/Resources when it was built: either already inside the app (configure
# with -DVOCALINK_WITH_MAC_DRIVER=ON) or from $VOCALINK_MAC_DRIVER / the build
# directory. Without it the app offers BlackHole instead.
set -euo pipefail

BUILD_DIR=$(cd "${1:?usage: build-dmg.sh <build-dir> [version]}" && pwd)
VERSION=${2:-dev}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DIST="$ROOT/dist"
STAGE="$DIST/macos"
APP_SRC=$(find "$BUILD_DIR" -maxdepth 3 -name "VocalInk.app" -type d | head -n 1)
[ -n "$APP_SRC" ] || { echo "VocalInk.app not found in $BUILD_DIR" >&2; exit 1; }

rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R "$APP_SRC" "$STAGE/Vocal Ink.app"
APP="$STAGE/Vocal Ink.app"

MACDEPLOYQT=$(command -v macdeployqt || echo "${QT_ROOT_DIR:-}/bin/macdeployqt")
"$MACDEPLOYQT" "$APP" -always-overwrite

IDENTITY=${CODESIGN_IDENTITY:--}

# Bundle the virtual mic driver if one was built (not in Contents/PlugIns: macdeployqt owns that).
DRIVER="$APP/Contents/Resources/VocalInkVirtualMic.driver"
if [ ! -d "$DRIVER" ]; then
    DRIVER_SRC=${VOCALINK_MAC_DRIVER:-}
    if [ -z "$DRIVER_SRC" ]; then
        DRIVER_SRC=$(find "$BUILD_DIR" -maxdepth 4 -name "VocalInkVirtualMic.driver" -type d \
                     -not -path "*/VocalInk.app/*" | head -n 1)
    fi
    if [ -n "$DRIVER_SRC" ] && [ -f "$DRIVER_SRC/Contents/Info.plist" ]; then
        cp -R "$DRIVER_SRC" "$DRIVER"
    fi
fi

# Sign inside-out: the driver first (it is installed and loaded on its own), then the app.
if [ -d "$DRIVER" ]; then
    echo "Including the virtual mic driver"
    if [ "$IDENTITY" = "-" ]; then
        codesign --force --sign - --timestamp=none "$DRIVER"
    else
        codesign --force --options runtime --timestamp --sign "$IDENTITY" "$DRIVER"
    fi
    codesign --verify --strict "$DRIVER"
else
    echo "No virtual mic driver was built; the app will suggest BlackHole."
fi

if [ "$IDENTITY" = "-" ]; then
    codesign --force --deep --sign - "$APP"
else
    codesign --force --deep --options runtime --timestamp --sign "$IDENTITY" "$APP"
fi
codesign --verify --deep --strict "$APP"

cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
ln -s /Applications "$STAGE/Applications"
ARCH=$(uname -m)
DMG="$DIST/VocalInk-$VERSION-macos-$ARCH.dmg"
rm -f "$DMG"
hdiutil create -volname "Vocal Ink" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
echo "Created $DMG"
