#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export TOOLCHAIN_BIN=${TOOLCHAIN_BIN:-/home/uisrc/uisrc-lab-anlogic/tools/aarch64-linux/bin}
export QTDIR=${QTDIR:-/home/uisrc/uisrc-lab-qt/images/qt5-arm64-5.13.2}
export PATH="$TOOLCHAIN_BIN:$PATH"
export QMAKESPEC="$QTDIR/mkspecs/linux-aarch64-gnu-g++"
test -x "$TOOLCHAIN_BIN/aarch64-linux-gnu-g++"
test -x "$QTDIR/bin/qmake"
mkdir -p "$here/.build-board" "$here/release"
cd "$here/.build-board"
"$QTDIR/bin/qmake" "$here/camera_dataset_app.pro" CONFIG+=release
make -j"${BUILD_JOBS:-2}"
install -m 0755 camera_dataset_app "$here/release/camera_dataset_app"
file "$here/release/camera_dataset_app"
