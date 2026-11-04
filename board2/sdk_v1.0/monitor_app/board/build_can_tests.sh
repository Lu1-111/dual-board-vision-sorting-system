#!/bin/bash
set -eu
SRC=/mnt/d/PRJ/sdk/monitor_app
WORK="$HOME/monitor-build"
export PATH="$WORK/toolchain/bin:/usr/bin:/bin"
mkdir -p "$WORK/can-board-tests"
cd "$WORK/can-board-tests"
"$WORK/qt-board/bin/qmake" "$SRC/tests/can_smoke.pro"
make -j6
aarch64-linux-gnu-strip can_smoke
cp can_smoke "$SRC/release/"
