#!/bin/sh
set -eu
role=${1:?board1 or board2}
BASE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
backup=/root/before-state-update-$(date +%s)
mkdir -p "$backup"
for f in /etc/localtime /etc/TZ; do [ ! -e "$f" ] || cp "$f" "$backup/"; done
case "$role" in
board2)
    cp /opt/monitor-ui/monitor_ui /opt/monitor-ui/can_sender /opt/evidence-bridge/evidence_bridge "$backup/"
    /etc/init.d/S99monitor-ui stop
    /etc/init.d/S41monitor-link stop
    cp "$BASE/monitor_ui" "$BASE/can_sender" /opt/monitor-ui/
    cp "$BASE/evidence_bridge" /opt/evidence-bridge/
    chmod 755 /opt/monitor-ui/monitor_ui /opt/monitor-ui/can_sender /opt/evidence-bridge/evidence_bridge
    # Leave UI stopped briefly so caller can calibrate time before fresh events start.
    ;;
board1)
    cp /opt/can-monitor/can_sender /opt/evidence-bridge/evidence_bridge "$backup/"
    cp "$BASE/can_sender" /opt/can-monitor/
    cp "$BASE/evidence_bridge" /opt/evidence-bridge/
    chmod 755 /opt/can-monitor/can_sender /opt/evidence-bridge/evidence_bridge
    ;;
*) exit 2;;
esac
cp "$BASE/Shanghai.tz" /etc/localtime
printf '%s\n' CST-8 > /etc/TZ
sync
echo "UPDATED $role backup=$backup; calibrate time, then start board2 services"
