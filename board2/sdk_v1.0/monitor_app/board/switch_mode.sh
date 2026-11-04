#!/bin/sh
set -eu
case "${1:-}" in local|can-loopback|can-udp|can) ;; *) echo 'Usage: switch_mode.sh local|can-loopback|can-udp|can'; exit 2;; esac
OLD=$(cat /etc/monitor-ui-mode 2>/dev/null || echo local)
/etc/init.d/S99monitor-ui stop
printf '%s\n' "$1" > /etc/monitor-ui-mode
if ! /etc/init.d/S99monitor-ui start; then
    printf '%s\n' "$OLD" > /etc/monitor-ui-mode
    /etc/init.d/S99monitor-ui start
    echo 'New mode failed; restored previous mode'; exit 1
fi
sync
