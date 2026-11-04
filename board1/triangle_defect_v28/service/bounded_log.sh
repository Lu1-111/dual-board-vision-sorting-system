#!/bin/sh
# Bounded byte chunks, including arbitrarily long lines; logger does not own flock.
set -eu
export LC_ALL=C
test "$#" -eq 1 || exit 2
log=$1
case "$log" in /tmp/triangle-live/worker.log) ;; *) echo "Unexpected log path" >&2; exit 2;; esac
limit=262144
chunk=$log.chunk.$$
trap 'rm -f "$chunk"' EXIT HUP INT TERM
for existing in "$log" "$log.1"; do
    if test -f "$existing" && test "$(wc -c < "$existing")" -gt "$limit"; then
        tail -c "$limit" "$existing" > "$chunk"
        mv -f "$chunk" "$existing"
    fi
done
bytes=0
if test -f "$log"; then bytes=$(wc -c < "$log"); fi
while :; do
    dd bs=4096 count=1 of="$chunk" 2>/dev/null
    count=$(wc -c < "$chunk")
    test "$count" -ne 0 || break
    if test "$((bytes + count))" -gt "$limit"; then
        test ! -f "$log" || mv -f "$log" "$log.1"
        : > "$log"
        bytes=0
    fi
    cat "$chunk" >> "$log"
    bytes=$((bytes + count))
done
