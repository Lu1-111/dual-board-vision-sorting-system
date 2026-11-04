#!/bin/sh
set -eu
app=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
bin=$app/triangle_defect_http
out=/tmp/triangle-live
lock=/tmp/triangle-live.lock
mkdir -p "$out"
starttime() {
    test -r "/proc/$1/stat" || return 1
    sed 's/^.*) //' "/proc/$1/stat" | awk '{print $20}'
}
worker_identity() {
    test -r "$out/worker.pid" || return 1
    read -r worker stamp < "$out/worker.pid" || return 1
    case "$worker:$stamp" in *[!0-9:]*|:|*:|:*) return 1;; esac
    test "$(starttime "$worker")" = "$stamp" || return 1
    test "$(readlink "/proc/$worker/exe")" = "$bin"
}
supervisor_identity() {
    test -r "$out/supervisor.pid" || return 1
    read -r supervisor stamp < "$out/supervisor.pid" || return 1
    case "$supervisor:$stamp" in *[!0-9:]*|:|*:|:*) return 1;; esac
    test "$(starttime "$supervisor")" = "$stamp" || return 1
    tr '\000' '\n' < "/proc/$supervisor/cmdline" | grep -F -x "$app/run_triangle.sh" >/dev/null
}
is_locked() { ! flock -n "$lock" true; }
do_status() {
    if worker_identity; then
        echo "RUNNING worker=$worker"
        if supervisor_identity; then echo "supervisor=$supervisor"; else echo "supervisor unavailable; worker retains service lock"; fi
        return 0
    fi
    if is_locked; then echo "STARTING/STOPPING or worker identity unavailable (lock held)"; return 1; fi
    echo "STOPPED"; return 1
}
do_start() {
    if worker_identity; then echo "Already running: $worker"; return 0; fi
    if is_locked; then echo "Service lock is held; refusing duplicate start" >&2; return 1; fi
    test -x "$app/run_triangle.sh" || { echo "run_triangle.sh is not executable" >&2; return 1; }
    # This file holds supervisor diagnostics only; worker stdout is bounded separately.
    nohup "$app/run_triangle.sh" </dev/null > "$out/launcher.log" 2>&1 &
    attempt=0
    while test "$attempt" -lt 15; do
        sleep 1
        if worker_identity; then echo "Started worker=$worker; model loading may still be in progress"; return 0; fi
        if ! is_locked; then cat "$out/launcher.log"; return 1; fi
        attempt=$((attempt + 1))
    done
    echo "Start is not confirmed; inspect $out/launcher.log and worker.log" >&2; return 1
}
do_stop() {
    if supervisor_identity; then
        echo "Requesting supervisor $supervisor to stop"
        kill -TERM "$supervisor"
    elif worker_identity; then
        echo "Requesting orphan worker $worker to stop"
        kill -TERM "$worker"
    else
        if is_locked; then echo "Lock held but identities unverified; no signal sent" >&2; return 1; fi
        echo "Already stopped"; return 0
    fi
    attempt=0
    while test "$attempt" -lt 30; do
        sleep 1
        if ! is_locked; then echo "Stopped"; return 0; fi
        attempt=$((attempt + 1))
    done
    echo "Graceful stop not complete after 30s; lock retained, no forced kill issued" >&2
    return 1
}
test "$#" -eq 1 || { echo "Usage: $0 start|stop|restart|status"; exit 2; }
case "$1" in
    start) do_start;;
    stop) do_stop;;
    restart) do_stop; do_start;;
    status) do_status;;
    *) echo "Usage: $0 start|stop|restart|status"; exit 2;;
esac
