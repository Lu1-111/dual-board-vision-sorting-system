#!/bin/bash
set -eu
SRC=/mnt/d/PRJ/sdk/monitor_app
WORK="$HOME/monitor-build"
mode=${1:-board}
case "$mode" in
    host)
        qmake="$HOME/qt-host/5.15.2/gcc_64/bin/qmake"
        extra="QMAKE_LIBDIR+=$WORK/host-link"
        stripper=strip
        output=monitor_ui_uncertain_host
        ;;
    board)
        export PATH="$WORK/toolchain/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
        qmake="$WORK/qt-board/bin/qmake"
        extra=
        stripper=aarch64-linux-gnu-strip
        output=monitor_ui_uncertain
        ;;
    *) echo 'Usage: build_uncertain_app.sh host|board' >&2; exit 2;;
esac
mkdir -p "$WORK/app-$mode-uncertain" "$SRC/release"
cd "$WORK/app-$mode-uncertain"
"$qmake" "$SRC/monitor_app.pro" $extra
make -j6
"$stripper" --strip-unneeded monitor_ui
cp monitor_ui "$SRC/release/$output"
