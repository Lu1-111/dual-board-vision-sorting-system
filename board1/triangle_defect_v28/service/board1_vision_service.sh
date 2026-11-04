#!/bin/sh
# Vision-only boot lifecycle. Never enables mechanical outputs.
set -eu
camera=/home/uisrc/camera_dataset_app/camera_dataset_app
camera_pid_file=/tmp/camera-preview.pid
starttime() { sed 's/^.*) //' "/proc/$1/stat" | awk '{print $20}'; }
camera_matches() {
    test -r "$camera_pid_file" || return 1
    read -r camera_pid camera_start < "$camera_pid_file" || return 1
    case "$camera_pid" in ''|*[!0-9]*) return 1;; esac
    test "$(readlink "/proc/$camera_pid/exe")" = "$camera" || return 1
    test -n "${camera_start:-}" && test "$(starttime "$camera_pid")" = "$camera_start" || return 1
    tr '\000' '\n' <"/proc/$camera_pid/cmdline" | grep -Fx -- '--stream-only' >/dev/null
}
start() {
    exec 8>/tmp/board1-vision-start.lock
    flock -n 8 || { echo 'Vision startup already in progress'; return 0; }
    if test -r /opt/triangle-live/parking_reference.json; then
        mkdir -p /tmp/triangle-track
        cp /opt/triangle-live/parking_reference.json /tmp/triangle-track/reference.json.new
        mv /tmp/triangle-track/reference.json.new /tmp/triangle-track/reference.json
    fi
    if ! camera_matches; then
        if pidof camera_dataset_app >/dev/null 2>&1; then
            echo 'Another camera application is present; leaving it untouched' >&2; return 1
        fi
        tries=0
        until ip -4 addr show dev eth0 | grep -F '172.22.203.1/30' >/dev/null; do
            test "$tries" -lt 20 || { echo 'Camera stream network address unavailable' >&2; return 1; }
            sleep 1; tries=$((tries+1))
        done
        nohup env QT_QPA_PLATFORM=offscreen /home/uisrc/camera_dataset_app/run_camera.sh \
            --stream-only --track-centers --tracking-output-dir /tmp/triangle-track --open-camera --output /home/uisrc/datasets --dataset sd_capture_20261102 --scene manual \
            --stream --stream-address 172.22.203.1 --stream-port 47003 --stream-fps 3 --stream-quality 95 \
            --preview-fps 15 --preview-width 640 --preview-quality 75 \
            >/tmp/camera-dual-live.log 2>&1 </dev/null 8>&- &
        camera_pid=$!
        sleep 3
        test "$(readlink "/proc/$camera_pid/exe")" = "$camera" || { tail -n 12 /tmp/camera-dual-live.log; return 1; }
        printf '%s %s\n' "$camera_pid" "$(starttime "$camera_pid")" >"$camera_pid_file"
    fi
    /opt/triangle-live/triangle_service.sh start 8>&-
    echo "Board1 vision running, camera=$camera_pid"
}
stop() {
    /opt/triangle-live/triangle_service.sh stop
    if camera_matches; then
        kill -TERM "$camera_pid"
        tries=0
        while test -e "/proc/$camera_pid"; do
            test "$tries" -lt 25 || { echo 'Camera still shutting down; no forced kill' >&2; return 1; }
            sleep 1; tries=$((tries+1))
        done
        rm -f "$camera_pid_file"
    fi
}
case "${1:-}" in
    start) start;;
    stop) stop;;
    restart) stop; start;;
    status) camera_matches && echo "CAMERA_RUNNING $camera_pid"; /opt/triangle-live/triangle_service.sh status;;
    *) echo "Usage: $0 start|stop|restart|status" >&2; exit 2;;
esac
