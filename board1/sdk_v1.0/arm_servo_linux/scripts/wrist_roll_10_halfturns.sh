#!/bin/sh
# User-authorized isolated T9 wrist-roll test: ten half-turn commands.
# Only T9 (J12 pin 5) may be connected to a servo during this script.
# 500..2499.2 us maps to 0..180 deg in the current uncalibrated template.
# It is a commanded angle, not measured position feedback.
set -u
config=/home/uisrc/qt_play/arm_config.ini
log=/tmp/wrist-roll-10.log
progress=/tmp/wrist-roll-10-progress
if pidof armctl >/dev/null 2>&1; then
    echo 'Refusing: armctl is already running' >&2
    exit 1
fi
test -x /usr/bin/armctl && test -r "$config" || exit 1
printf '0\n' > "$progress"
{
    printf 'ack 1000 1500 1500 1500 1500 1500 confirm\n'
    # Position at the low endpoint with a ramp, then perform ten half-turns.
    printf 'calibrate 1 500 1500 confirm\n'
    sleep 1.55
    i=1
    while test "$i" -le 10; do
        if test $((i % 2)) -eq 1; then pulse=2499.2; else pulse=500; fi
        printf 'calibrate 1 %s 2000 confirm\n' "$pulse"
        sleep 2.05
        printf '%s\n' "$i" > "$progress"
        i=$((i + 1))
    done
    printf 'status\noff\nstatus\nquit\n'
} | /usr/bin/armctl --hardware --bank2 /dev/gpiochip2 --bank3 /dev/gpiochip3 \
        --config "$config" > "$log" 2>&1 &
controller=$!
printf '%s\n' "$controller" > /tmp/wrist-roll-10-controller.pid
trap 'kill -TERM "$controller" 2>/dev/null; wait "$controller"; exit 130' INT TERM HUP
wait "$controller"
result=$?
printf 'EXIT_CODE=%s\n' "$result" >> "$log"
exit "$result"
