#!/bin/bash
set -eu
SRC=/mnt/d/PRJ/sdk/monitor_app
WORK="$HOME/monitor-build"
QT="$HOME/qt-host/5.15.2/gcc_64"
OUTPUT=${1:-"$SRC/release/roi-ui-smoke"}
export LD_LIBRARY_PATH="$QT/lib"
export QT_QPA_PLATFORM=offscreen
export MONITOR_EVIDENCE_DIR="$WORK/test-evidence-data"
export MONITOR_LIVE_DIR="$WORK/nonexistent-live-test-source"
mkdir -p "$WORK/live-roi-tests" "$WORK/host-roi-tests" "$OUTPUT"
cd "$WORK/live-roi-tests"
"$QT/bin/qmake" "$SRC/tests/livevision_smoke.pro" QMAKE_LIBDIR+="$WORK/host-link"
make -j6 >/dev/null
./livevision_smoke "$OUTPUT" /mnt/d/PRJ/sdk/ui_app/fonts \
    /mnt/d/PRJ/sdk/blue_conveyor_state/robustness_v3/roi_experiment/roi_reference.jpg
cd "$WORK/host-roi-tests"
"$QT/bin/qmake" "$SRC/tests/monitor_smoke.pro" QMAKE_LIBDIR+="$WORK/host-link"
make -j6 >/dev/null
./monitor_smoke "$OUTPUT/navigation.png" /mnt/d/PRJ/sdk/ui_app/fonts
