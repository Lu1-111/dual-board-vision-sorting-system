#!/bin/bash
set -eu
SRC=/mnt/d/PRJ/sdk/monitor_app
WORK="$HOME/monitor-build"
QT="$HOME/qt-host/5.15.2/gcc_64"
export LD_LIBRARY_PATH="$QT/lib"
export QT_QPA_PLATFORM=offscreen
export MONITOR_LIVE_DIR="$WORK/nonexistent-live-test-source"
mkdir -p "$WORK/live-cli-test"
cd "$WORK/live-cli-test"
"$QT/bin/qmake" "$SRC/monitor_app.pro" QMAKE_LIBDIR+="$WORK/host-link"
make -j6 >/dev/null
if [ ! -e fonts ]; then ln -s /mnt/d/PRJ/sdk/ui_app/fonts fonts; fi
set +e
timeout 4s ./monitor_ui --can-udp 39119 --windowed --start-page live \
    --screenshot "$SRC/release/live-ui-smoke/cli-live-no-data.png"
rc=$?
set -e
[ "$rc" -eq 124 ]
test -s "$SRC/release/live-ui-smoke/cli-live-no-data.png"
set +e
./monitor_ui --start-page wrong >/dev/null 2>&1
rc=$?
set -e
[ "$rc" -eq 2 ]
printf 'live CLI smoke: PASS (starts live, screenshot at 2s, continues monitoring, rejects invalid page)\n'
