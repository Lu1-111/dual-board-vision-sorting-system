#!/bin/sh
set -eu
role=${1:?board1 or board2 required}
case "$role" in board1) dev=eth0;; board2) dev=eth1;; *) exit 2;; esac
BASE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
backup=/root/before-evidence-$(date +%s)
mkdir -p "$backup"
cp /etc/dhcpcd.conf "$backup/dhcpcd.conf"
for f in /etc/monitor-link-role /etc/monitor-link.token /etc/init.d/S41monitor-link; do
  [ ! -e "$f" ] || cp "$f" "$backup/"
done
if [ "$role" = board2 ]; then
  cp /opt/monitor-ui/monitor_ui "$backup/monitor_ui"
  /etc/init.d/S99monitor-ui stop
fi
if [ -x /etc/init.d/S41monitor-link ]; then /etc/init.d/S41monitor-link stop; fi
mkdir -p /opt/evidence-bridge/plugins/imageformats
cp "$BASE/evidence_bridge" "$BASE/serial_bridge.sh" /opt/evidence-bridge/
cp "$BASE/libqjpeg.so" /opt/evidence-bridge/plugins/imageformats/
cp "$BASE/S41monitor-link" /etc/init.d/S41monitor-link
cp "$BASE/monitor-link.token" /etc/monitor-link.token
chmod 600 /etc/monitor-link.token
echo "$role" > /etc/monitor-link-role
chmod 755 /etc/init.d/S41monitor-link /opt/evidence-bridge/evidence_bridge /opt/evidence-bridge/serial_bridge.sh
if [ -f "$BASE/Shanghai.tz" ]; then
  [ ! -e /etc/localtime ] || cp /etc/localtime "$backup/localtime"
  [ ! -e /etc/TZ ] || cp /etc/TZ "$backup/TZ"
  cp "$BASE/Shanghai.tz" /etc/localtime
  printf '%s\n' CST-8 > /etc/TZ
fi
# Only exclude this isolated point-to-point Ethernet interface from DHCP.
grep -q "^denyinterfaces $dev$" /etc/dhcpcd.conf || echo "denyinterfaces $dev" >> /etc/dhcpcd.conf
dhcpcd -k "$dev" >/dev/null 2>&1 || true
ip addr del 169.254.125.44/16 dev "$dev" 2>/dev/null || true
/etc/init.d/S41monitor-link start
if [ "$role" = board2 ]; then
  mkdir -p /opt/monitor-ui/plugins/imageformats
  cp "$BASE/libqjpeg.so" /opt/monitor-ui/plugins/imageformats/
  cp "$BASE/monitor_ui" /opt/monitor-ui/monitor_ui
  chmod 755 /opt/monitor-ui/monitor_ui
  /etc/init.d/S99monitor-ui start || {
    cp "$backup/monitor_ui" /opt/monitor-ui/monitor_ui
    /etc/init.d/S99monitor-ui start
    echo 'UI update failed, prior UI restored'; exit 1
  }
fi
sync
echo "INSTALLED $role backup=$backup"
