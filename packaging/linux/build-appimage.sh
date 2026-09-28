#!/usr/bin/env bash
# Builds dist/VocalInk-<version>-x86_64.AppImage from an existing CMake build.
#
#   packaging/linux/build-appimage.sh <build-dir> [version]
#
# Needs: Qt 6 with qmake on PATH (or $QT_ROOT_DIR set), curl, and FUSE or
# APPIMAGE_EXTRACT_AND_RUN=1 (set below) to run the linuxdeploy AppImages.
set -euo pipefail

BUILD_DIR=$(realpath "${1:?usage: build-appimage.sh <build-dir> [version]}")
VERSION=${2:-dev}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK="$ROOT/dist/appimage"
APPDIR="$WORK/AppDir"
ARCH=${ARCH:-x86_64}

mkdir -p "$WORK"
rm -rf "$APPDIR"

cmake --install "$BUILD_DIR" --prefix "$APPDIR/usr"

cd "$WORK"
for tool in linuxdeploy-$ARCH.AppImage linuxdeploy-plugin-qt-$ARCH.AppImage; do
    if [ ! -x "$tool" ]; then
        case $tool in
            linuxdeploy-plugin-qt*) repo=linuxdeploy/linuxdeploy-plugin-qt ;;
            *) repo=linuxdeploy/linuxdeploy ;;
        esac
        curl -fsSL -o "$tool" "https://github.com/$repo/releases/download/continuous/$tool"
        chmod +x "$tool"
    fi
done

export APPIMAGE_EXTRACT_AND_RUN=1
if [ -z "${QMAKE:-}" ]; then
    if [ -n "${QT_ROOT_DIR:-}" ] && [ -x "$QT_ROOT_DIR/bin/qmake" ]; then
        export QMAKE="$QT_ROOT_DIR/bin/qmake"
    else
        export QMAKE=$(command -v qmake6 || command -v qmake)
    fi
fi
# Multimedia (audio in/out), TextToSpeech (system voices) and the TLS backend
# for HTTPS are loaded as plugins, so name them explicitly.
export EXTRA_QT_MODULES="multimedia;texttospeech;network"
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
export LDAI_OUTPUT="VocalInk-$VERSION-$ARCH.AppImage"
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"

./linuxdeploy-$ARCH.AppImage \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/VocalInk" \
    --desktop-file "$APPDIR/usr/share/applications/org.vocalink.desktop.desktop" \
    --icon-file "$ROOT/resources/icons/app-256.png" \
    --icon-filename org.vocalink.desktop \
    --plugin qt \
    --output appimage

mkdir -p "$ROOT/dist"
mv -f "$LDAI_OUTPUT" "$ROOT/dist/"
echo "Created $ROOT/dist/$LDAI_OUTPUT"
