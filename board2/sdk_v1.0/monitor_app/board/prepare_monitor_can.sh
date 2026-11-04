#!/bin/sh
set -eu
BASE=/opt/monitor-ui
PIDFILE=/var/run/monitor-can-sender.pid
MODE=$(cat /etc/monitor-ui-mode 2>/dev/null || echo local)
running() {
    test -r "$PIDFILE" || return 1
    PID=$(cat "$PIDFILE")
    case "$PID" in ''|*[!0-9]*) return 1;; esac
    test "$(cat /proc/$PID/comm 2>/dev/null)" = can_sender || return 1
    ! grep -q '^State:.*Z' /proc/$PID/status 2>/dev/null
}
case "${1:-}" in
start)
    running && exit 0
    case "$MODE" in
    local) exit 0;;
    can) "$BASE/setup_can.sh" physical can0; exit 0;;
    can-loopback) "$BASE/setup_can.sh" loopback can0; set -- --can can0;;
    can-udp) set -- --udp 39001;;
    *) echo "Unknown monitor mode: $MODE"; exit 2;;
    esac
    "$BASE/can_sender" "$@" --demo > /var/log/monitor-can-sender.log 2>&1 &
    echo $! > "$PIDFILE"
    ;;
stop)
    if running; then
        kill "$PID"
        n=0
        while running && [ "$n" -lt 30 ]; do sleep 0.1; n=$((n+1)); done
        if running; then echo 'CAN demo sender did not stop'; exit 1; fi
    fi
    rm -f "$PIDFILE"
    if [ "$MODE" = can-loopback ]; then
        ip link set can0 down
        ip link set can0 type can loopback off
    fi
    ;;
*) echo 'Usage: prepare_monitor_can.sh start|stop'; exit 2;;
esac
