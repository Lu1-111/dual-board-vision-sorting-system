#!/bin/bash
set -eu
WORK="$HOME/monitor-build"
QT="$HOME/qt-host/5.15.2/gcc_64"
export LD_LIBRARY_PATH="$QT/lib"
export QT_QPA_PLATFORM=offscreen
export MONITOR_EVIDENCE_DIR="$WORK/test-evidence-data"
mkdir -p "$WORK/evidence-tests" "$WORK/host-tests" "$WORK/can-host-tests"
cd "$WORK/evidence-tests"
"$QT/bin/qmake" /mnt/d/PRJ/sdk/monitor_app/tests/evidence_smoke.pro QMAKE_LIBDIR+="$WORK/host-link"
make -j6 >/dev/null
./evidence_smoke
cd "$WORK/host-tests"
"$QT/bin/qmake" /mnt/d/PRJ/sdk/monitor_app/tests/monitor_smoke.pro QMAKE_LIBDIR+="$WORK/host-link"
make -j6 >/dev/null
./monitor_smoke /mnt/d/PRJ/sdk/monitor_app/release/evidence-ui.png /mnt/d/PRJ/sdk/ui_app/fonts
cd "$WORK/can-host-tests"
"$QT/bin/qmake" /mnt/d/PRJ/sdk/monitor_app/tests/can_smoke.pro QMAKE_LIBDIR+="$WORK/host-link"
make -j6 >/dev/null
g++ -std=c++11 -O2 /mnt/d/PRJ/sdk/can_monitor/sender.cpp -o "$WORK/can_sender_host"
./can_smoke "$WORK/can_sender_host" 39009
