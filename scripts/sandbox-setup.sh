#!/usr/bin/env bash
# Prepare a fresh Linux sandbox to build Easeletch: prebuilt Qt from the
# qt-toolchain release plus the system packages Qt Gui needs.
#
#   scripts/sandbox-setup.sh          install Qt to /opt/qt/gcc_64 and apt deps
#   scripts/build.sh --qt /opt/qt/gcc_64

set -euo pipefail

QT_URL="https://github.com/scottpeterman/easel/releases/download/qt-toolchain/qt-6.10.3-gcc_64.tar.xz"
QT_ROOT="/opt/qt"
QT_DIR="$QT_ROOT/gcc_64"

if [[ ! -f "$QT_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    mkdir -p "$QT_ROOT"
    curl -fsSL -o "$QT_ROOT/qt.tar.xz" "$QT_URL"
    tar -xf "$QT_ROOT/qt.tar.xz" -C "$QT_ROOT"
    rm -f "$QT_ROOT/qt.tar.xz"
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update || true
apt-get install -y ninja-build xvfb libxcb-cursor0 libgl1-mesa-dev libopengl-dev libxkbcommon-dev libvulkan-dev

"$QT_DIR/bin/qmake" -query QT_VERSION
echo "Qt ready at $QT_DIR. Build with: LC_ALL=C.UTF-8 scripts/build.sh --qt $QT_DIR"
# make executble
