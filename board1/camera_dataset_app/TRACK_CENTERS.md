# Optional preview center tracking

Enable `--stream --track-centers` with the existing camera arguments. The default
output is `/tmp/triangle-track/latest.json`; `--tracking-output-dir` selects a
dedicated absolute directory for testing. The existing `--stream-only` behavior
is preserved. No additional camera owner, HTTP client, motor command, GPIO write,
CAN command or arm command is introduced.

The preview encoder owns a FastTracker instance. `fast_tracker.hpp` is copied
unchanged from the restored V28 board source (SHA256
`e5bd1fc9ab68619787f11e4db9c0374eb4550efe9ba60a3a4345fd8d2410122c`).
The original 1920x1080 BGRA pixels are sampled with target width 320, matching the
released tracker setting. Boxes and centers are reported in original pixels.
The 640x360 preview is detached before drawing the box, ID, center cross and
`#id (x,y)` text. The original QImage and full-resolution inference JPEG are
never annotated. An uncertain (partial/ambiguous) track is marked `?`.

Tracking does not classify defects. JSON objects contain only `track_id` (string),
`bbox_xyxy`, `center_xy`, `partial`, and `ambiguous`. There are no NORMAL/DEFECT
labels or confidence scores. More than 128 objects invalidates the tracking
publication rather than truncating or selecting a subset silently.

The JSON contains schema=1, status, source_session, string frame_sequence,
published_monotonic_ms, source_frame_age_ms, source, width, height, objects,
tracking_ms and fps. Frame/session IDs match the corresponding preview JPEG
headers. The source remains `simulation` in tests and `imx415` for the camera.
The encoded preview and its tracking calculation use the same owned full frame.
QSaveFile atomically replaces the JSON after successful preview encoding, while
holding the session mutex. Stopping/restarting the camera invalidates the JSON;
an old in-flight encode cannot republish a stopped session. A separate file lock
rejects multiple writers of this tracking directory.

Age is measured from the capture thread offering its completed frame to the
server. This is not an exposure timestamp or a certified capture-latency bound.
At publication, age >500 ms produces source_stale with an empty object list.
Consumers must additionally add `now_monotonic - published_monotonic_ms` to the
published source age; a frozen JSON must not remain fresh indefinitely.
Only real-source, fresh, unambiguous, nonpartial detections are even candidates
for subsequent control logic; this change performs no control itself.

The tracker was originally a display localization helper, not a calibrated stop
sensor. At 320 sampling width the 1920x1080 grid spacing is approximately 6x6
pixels. Shape/morphology can add error; an arbitrary real-world center accuracy
has not been established. Merges, occlusion, partial objects and motion can change
identity. A tracking ID must never be treated as an original V28 classification
ID. Region margins, motion stopping distance, latency and the association of a
stationary object with a newer V28 frame require separate validation.

Build dependencies: Qt GUI (already used) plus OpenCV core/imgproc. For the board:

```
export CAMERA_OPENCV_ROOT=/home/uisrc/npu-deploy-sdk-20261023/app/face_detection/libs/ffmpeg_opencv4.7.0_aarch64
qmake camera_dataset_app.pro
make -j2
```

For native extracted libraries use `OPENCV_INCLUDE_DIR` and `OPENCV_LIB_DIR`.
They take priority over CAMERA_OPENCV_ROOT. Otherwise include/library search paths
come from `pkg-config opencv4`. Only core/imgproc are linked by this change.

Native tests:

```
qmake tests/tracking_centers_test.pro && make -j2
QT_QPA_PLATFORM=offscreen ./tracking_centers_test
# In a separate build directory:
qmake tests/tracking_stream_fixture.pro && make -j2
python3 tests/tracking_stream_integration.py --fixture ./tracking_stream_fixture
```

The unit test covers a synthetic blue block at center (1020,540), source-image
immutability, preview overlay, stable/new IDs, boundary partial, merger ambiguity,
500 ms staleness and session invalidation. The integration test compares full
JPEG bytes with tracking disabled/enabled, confirms preview changes, checks
preview/JSON frame IDs, and rejects resurrection after stop. These are host tests;
they do not claim board timing, real-world localization accuracy or safe stopping.
