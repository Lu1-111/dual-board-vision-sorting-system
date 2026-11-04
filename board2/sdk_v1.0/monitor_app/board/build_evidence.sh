#!/bin/bash
set -eu
WORK="$HOME/monitor-build"
export PATH="$WORK/toolchain/bin:$PATH"
mkdir -p "$WORK/evidence-board"
cd "$WORK/evidence-board"
"$WORK/qt-board/bin/qmake" /mnt/d/PRJ/sdk/monitor_app/evidence_bridge/bridge.pro
make -j6
aarch64-linux-gnu-strip evidence_bridge
cp evidence_bridge /mnt/d/PRJ/sdk/monitor_app/release/
