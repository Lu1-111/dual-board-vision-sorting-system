#!/bin/sh
# Isolated U9 / J12 pin 13 / logical axis 5 test.
# Supply two physically verified endpoint pulses. No default 90-degree range.
# Usage: sh base_10_steps.sh endpoint_a_us endpoint_b_us
set -u
test "$#" -eq 2 || { echo 'Two verified endpoint pulses are required' >&2; exit 2; }
a=$1; b=$2
config=/home/uisrc/qt_play/arm_config.ini
log=/tmp/base-10.log
progress=/tmp/base-10-progress
if pidof armctl >/dev/null 2>&1; then
    echo 'Refusing: armctl is already running' >&2
    exit 1
fi
# Validate numeric/config bounds without touching GPIO before starting hardware.
printf 'ack 1000 1500 1500 1500 1500 1500 confirm\ncalibrate 5 %s 20 confirm\ncalibrate 5 %s 20 confirm\noff\nquit\n' "$a" "$b" |
    /usr/bin/armctl --simulate --config "$config" >/tmp/base-input-check.log 2>&1 || exit 2
printf '0\n' > "$progress"
{
    printf 'ack 1000 1500 1500 1500 1500 1500 confirm\n'
    printf 'calibrate 5 %s 1500 confirm\n' "$a"
    sleep 1.55
    i=1
    while test "$i" -le 10; do
        if test $((i % 2)) -eq 1; then pulse=$b; else pulse=$a; fi
        printf 'calibrate 5 %s 2000 confirm\n' "$pulse"
        sleep 2.05
        printf '%s\n' "$i" > "$progress"
        i=$((i + 1))
    done
    printf 'status\noff\nstatus\nquit\n'
} | /usr/bin/armctl --hardware --bank2 /dev/gpiochip2 --bank3 /dev/gpiochip3 \
        --config "$config" > "$log" 2>&1 &
controller=$!
printf '%s\n' "$controller" >/tmp/base-10-controller.pid
trap 'kill -TERM "$controller" 2>/dev/null; wait "$controller"; exit 130' INT TERM HUP
wait "$controller"
result=$?
printf 'EXIT_CODE=%s\n' "$result" >> "$log"
exit "$result"
