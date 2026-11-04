Triangle V28 persistent HTTP service (board1)

Install triangle_defect_http and these three scripts together in /opt/triangle-live:
run_triangle.sh, triangle_service.sh, bounded_log.sh. Set executable permissions.
Place triangle_defect_http.sha256 beside the executable, containing its validated
build digest followed by two spaces and triangle_defect_http. The launcher checks
both this binary pin and the two model pins before starting; regenerate the binary
pin only from the final validated build. A missing or malformed pin refuses start.
Keep original models and runtime libraries under /home/uisrc/triangle_defect_v28.
This is a separate service; it does not remove old files or edit init/boot scripts.

Commands: /opt/triangle-live/triangle_service.sh start|status|stop|restart
Status confirms executable/PID identity, not successful inference; inspect
/tmp/triangle-live/latest.json for status, session/frame age and advancing results.
The camera must already serve http://172.22.203.1:47003/v1/frame.jpg.
This source is the unchanged full-resolution, quality95 endpoint, not preview.jpg.
The address is the board1 camera bind address; loopback127.0.0.1 is not assumed.

The worker has only --http-source/--http-output-dir source arguments. There is no
--camera or --display argument and no module loading, framebuffer mapping or GPIO
setup in these scripts. Required NPU device nodes must already exist. Stop any
old direct-camera V28 detector using its existing graceful procedure before start;
the scripts refuse known old NPU processes instead of killing them.

Two model SHA256 values are checked before launch; failure does not spawn a worker.
V28 preprocessing, threshold0.88, top4 reduction and model files remain unchanged.
Libraries follow original runtime.sh: lib/armnn, lib/opencv, then lib beneath the
original deployment directory. CUBE_CPU_THREADS=1 and nice+8 initially preserve
camera/UI headroom; actual preview and inference performance need measurement.
Normal service unsets profiling and custom CPU-op overrides. CUBE_CMA_TRACE=1 can
be supplied for a bounded diagnostic run; default0. Logs rotate worker.log and .1
at256KiB each using at most4096-byte chunks, even if there are no line breaks.

Supervisor and worker inherit one flock descriptor; the logger closes that fd.
Never unlink /tmp/triangle-live.lock. Worker identity includes /proc start time and
resolved executable. The supervisor PID additionally must contain run_triangle.sh
in its cmdline. Stop waits30seconds and never uses kill-9. If NPU shutdown hangs,
the service stays locked and reports failure; inspect state rather than launching
another instance. Restart is stop then start, so a failed stop cannot duplicate.
PID files have TWO fields (PID,starttime); do not pass their whole contents to kill.

Test_service_native.py provides safe synthetic-worker POSIX tests for duplicate
start, lock inheritance, stale metadata, orphan recovery, model hash rejection
and byte-bounded logging. No target device/model is opened by that fixture.
The test uses Python3.6-compatible subprocess PIPE/universal_newlines arguments.
Native execution and actual-board smoke are pending; scripts have not been deployed.

Rollback: gracefully stop this service and verify lock release. The original
triangle_defect_npu and models are untouched. Restoring its direct-camera mode
requires coordinating exclusive camera ownership; do not start it while the
current camera_dataset_app owns /dev/mipicam. This service never stops that camera.
