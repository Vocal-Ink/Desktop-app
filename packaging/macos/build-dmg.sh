#!/usr/bin/env bash
# Bundles Qt into VocalInk.app, signs it ad hoc and wraps it in a drag-to-install dmg.
#
#   packaging/macos/build-dmg.sh <build-dir> [version]
#
# Set CODESIGN_IDENTITY to a "Developer ID Application" identity to sign for
# distribution (notarisation is a separate step).
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
