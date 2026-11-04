#!/bin/bash
set -eu
SRC=/mnt/d/PRJ/sdk/monitor_app
WORK="$HOME/monitor-build"
QT="$HOME/qt-host/5.15.2/gcc_64"
export LD_LIBRARY_PATH="$QT/lib"
export QT_QPA_PLATFORM=offscreen
export MONITOR_EVIDENCE_DIR="$WORK/test-evidence-data"
export MONITOR_LIVE_DIR="$WORK/nonexistent-live-test-source"
mkdir -p "$WORK/live-ui-tests" "$WORK/host-tests" "$WORK/can-host-tests" "$SRC/release/live-ui-smoke"
cd "$WORK/live-ui-tests"
"$QT/bin/qmake" "$SRC/tests/livevision_smoke.pro" QMAKE_LIBDIR+="$WORK/host-link"
make -j6 >/dev/null
./livevision_smoke "$SRC/release/live-ui-smoke" /mnt/d/PRJ/sdk/ui_app/fonts
cd "$WORK/host-tests"
"$QT/bin/qmake" "$SRC/tests/monitor_smoke.pro" QMAKE_LIBDIR+="$WORK/host-link"
make -j6 >/dev/null
./monitor_smoke "$SRC/release/live-ui-smoke/navigation.png" /mnt/d/PRJ/sdk/ui_app/fonts
cd "$WORK/can-host-tests"
"$QT/bin/qmake" "$SRC/tests/can_smoke.pro" QMAKE_LIBDIR+="$WORK/host-link"
make -j6 >/dev/null
g++ -std=c++11 -O2 /mnt/d/PRJ/sdk/can_monitor/sender.cpp -o "$WORK/can_sender_host"
./can_smoke "$WORK/can_sender_host" 39009
