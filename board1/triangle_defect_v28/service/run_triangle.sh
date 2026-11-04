#!/bin/sh
# Single persistent HTTP worker. No camera, framebuffer, GPIO or module setup.
set -eu
umask 022
app=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
bin=$app/triangle_defect_http
out=/tmp/triangle-live
models=/home/uisrc/triangle_defect_v28/models
runtime=/home/uisrc/triangle_defect_v28
lock=/tmp/triangle-live.lock
features_sha=108d66f9faeea396a22adba1d039c0b4bbeb4c547d48018589532b951f0ccbf7
head_sha=dcbe3b45e1eb3c0bb797aa93eb147ecd5c6f119f833abb588bb1f6c8cf0d2168
mkdir -p "$out"
exec 9>"$lock"
if ! flock -n 9; then echo "Triangle HTTP service is already locked/running" >&2; exit 3; fi
test -x "$bin" || { echo "Missing executable: $bin" >&2; exit 1; }
test -x "$app/bounded_log.sh" || { echo "Missing bounded logger" >&2; exit 1; }
test -f "$app/triangle_defect_http.sha256" || { echo "Missing pinned binary checksum" >&2; exit 1; }
read -r binary_sha binary_name < "$app/triangle_defect_http.sha256"
test "$binary_name" = triangle_defect_http || { echo "Unexpected binary checksum filename" >&2; exit 1; }
test "${#binary_sha}" -eq 64 || { echo "Invalid binary checksum length" >&2; exit 1; }
case "$binary_sha" in *[!0-9a-f]*) echo "Invalid binary checksum" >&2; exit 1;; esac
printf '%s  %s\n' "$binary_sha" "$bin" | sha256sum -c - > "$out/binary-checksums.log"
command -v nice >/dev/null
for device in cma_mem hard_npu soft_npu; do
    test -c "/dev/$device" || { echo "Missing /dev/$device; leave driver setup to board deployment" >&2; exit 1; }
done
# Old direct-camera V28 must be stopped by its owner before this service starts.
for process in triangle_defect_npu triangle_defect_http color_block_npu; do
    if pidof "$process" >/dev/null 2>&1; then echo "Existing NPU program $process; refusing parallel board1 runtime" >&2; exit 1; fi
done
printf '%s  %s\n' "$features_sha" "$models/triangle_v28_features_fp32.onnx" "$head_sha" "$models/triangle_v28_head_uint8.onnx" | sha256sum -c - > "$out/model-checksums.log"
export LD_LIBRARY_PATH="$runtime/lib/armnn:$runtime/lib/opencv:$runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export CUBE_CPU_THREADS=1
# Continuous profiling retains events; normal service never enables it.
unset CUBE_PROFILE_PATH CUBE_PROFILE_MAX_RUNS CUBE_CPU_OPS
export CUBE_CMA_TRACE=${CUBE_CMA_TRACE:-0}
case "$CUBE_CMA_TRACE" in 0|1) ;; *) echo "CUBE_CMA_TRACE must be 0 or 1" >&2; exit 2;; esac
starttime() {
    test -r "/proc/$1/stat" || return 1
    sed 's/^.*) //' "/proc/$1/stat" | awk '{print $20}'
}
matches() {
    test -n "$1" && test -n "$2" && test -r "/proc/$1/stat" || return 1
    test "$(starttime "$1")" = "$2" || return 1
    test "$(readlink "/proc/$1/exe")" = "$bin"
}
super_start=$(starttime $$)
printf '%s %s\n' $$ "$super_start" > "$out/supervisor.pid.tmp"
mv -f "$out/supervisor.pid.tmp" "$out/supervisor.pid"
fifo=$out/worker-output.$$.fifo
mkfifo "$fifo"
worker=
worker_start=
logger_pid=
stop_requested=0
request_stop() {
    stop_requested=1
    if matches "$worker" "$worker_start"; then kill -TERM "$worker" 2>/dev/null || true; fi
}
cleanup() {
    # The lock inode is never unlinked; a surviving worker retains descriptor 9.
    rm -f "$fifo"
    if test -r "$out/supervisor.pid"; then
        read -r p t < "$out/supervisor.pid" || true
        if test "$p" = "$$" && test "$t" = "$super_start"; then rm -f "$out/supervisor.pid"; fi
    fi
    if test -r "$out/worker.pid"; then
        read -r p t < "$out/worker.pid" || true
        if test "$p" = "$worker" && test "$t" = "$worker_start"; then rm -f "$out/worker.pid"; fi
    fi
}
trap request_stop TERM INT HUP
trap cleanup EXIT
"$app/bounded_log.sh" "$out/worker.log" < "$fifo" 9>&- &
logger_pid=$!
nice -n 8 "$bin" --features "$models/triangle_v28_features_fp32.onnx" --model "$models/triangle_v28_head_uint8.onnx" \
    --http-source http://172.22.203.1:47003/v1/frame.jpg --http-output-dir "$out" --infer-interval-ms 333 --quiet > "$fifo" 2>&1 &
worker=$!
worker_start=$(starttime "$worker") || worker_start=
if test -z "$worker_start"; then
    echo "Worker failed before PID identity could be recorded" >&2
    wait "$worker" || true
    wait "$logger_pid" || true
    exit 1
fi
printf '%s %s\n' "$worker" "$worker_start" > "$out/worker.pid.tmp"
mv -f "$out/worker.pid.tmp" "$out/worker.pid"
echo "Triangle worker PID=$worker CPU_THREADS=1 nice=8 source=full-frame-http"
test "$stop_requested" -eq 0 || request_stop
rc=0
wait "$worker" || rc=$?
while matches "$worker" "$worker_start"; do wait "$worker" || rc=$?; done
wait "$logger_pid" || true
echo "Triangle worker exited: $rc"
exit "$rc"
