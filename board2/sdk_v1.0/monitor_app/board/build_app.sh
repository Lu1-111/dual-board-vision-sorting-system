#!/bin/bash
set -eu
SRC=/mnt/d/PRJ/sdk/monitor_app
WORK="$HOME/monitor-build"
export PATH="$WORK/toolchain/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
mkdir -p "$WORK/app-board"
cd "$WORK/app-board"
"$WORK/qt-board/bin/qmake" "$SRC/monitor_app.pro"
make -j6
aarch64-linux-gnu-strip monitor_ui
cp monitor_ui "$SRC/release/monitor_ui"
