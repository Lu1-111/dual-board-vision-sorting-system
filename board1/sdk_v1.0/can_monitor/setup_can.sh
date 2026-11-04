#!/bin/sh
# Configure explicitly; never silently turn on a physical bus in offline demo mode.
set -eu
MODE=${1:-}
IFACE=${2:-can0}
case "$MODE" in loopback|physical) ;; *) echo 'Usage: setup_can.sh loopback|physical [can0|can1]'; exit 2;; esac
case "$IFACE" in can0|can1) ;; *) echo 'Only can0/can1 supported'; exit 2;; esac
ip link set "$IFACE" down
if [ "$MODE" = loopback ]; then LOOP=on; else LOOP=off; fi
# Board BSP needs valid data timing. Application still sends CLASSIC 8-byte CAN frames.
ip link set "$IFACE" type can bitrate 500000 dbitrate 2000000 fd on loopback "$LOOP" restart-ms 0
ip link set "$IFACE" up
ip -details link show "$IFACE"
