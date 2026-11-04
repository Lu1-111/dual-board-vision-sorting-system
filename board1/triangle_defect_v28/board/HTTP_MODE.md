# Shared-camera V28 mode

This mode leaves the camera_dataset_app process as the sole MIPI/HDMI camera
owner and runs the existing V28 feature and head networks in one persistent
ArmNN runtime. It does not launch a subprocess per image. The existing
locate(), input(), infer(), 0.88 threshold, top-four-logit reduction and
image/camera entry points remain unchanged. No ROI is enabled.

```
triangle_defect_npu \
  --features /home/uisrc/triangle_defect_v28/models/triangle_v28_features_fp32.onnx \
  --model /home/uisrc/triangle_defect_v28/models/triangle_v28_head_uint8.onnx \
  --http-source http://172.22.203.1:47003/v1/frame.jpg \
  --http-output-dir /tmp/triangle-live --infer-interval-ms 333 --quiet
```

The launcher verifies the original model hashes. CUBE_CPU_THREADS=1 reduces
competition with the camera encoders. CUBE_PROFILE_PATH is opt-in; the original
runtime bounds capture using CUBE_PROFILE_MAX_RUNS and disables event collection
after that count. Profiling logic is unchanged.

Only one request/inference exists at a time. HTTP has a total two-second
deadline, 64 KiB header and 8 MiB JPEG limits. Protocol, source, session, sequence,
full-frame header dimensions, SOF dimensions and EOI are validated, followed by
OpenCV decoding. Only original 1920x1080 frames are accepted; preview images
are rejected. The 128-object bound rejects overly complex frames as unavailable.

Freshness: received frames must be at most five seconds old before inference.
Results older than thirty seconds are discarded. Age includes the server age,
request time, decode and inference. A result older than five seconds is explicitly
historical; publication time does not make its source image fresh. Sequence
duplicates are skipped without renewing freshness; sequence rollback or retired
session replay is rejected.

latest.json uses schema=1, model=V28, threshold=0.88, status, message,
published_monotonic_ms, source_frame_age_ms, historical, source_session,
frame_sequence (string), frame_path, width, height, objects, processing_ms,
inference_fps and timing_ms. Original labels/scores/boxes/centers are retained.
Timing npu_head includes the original head execution, synchronization and score
reduction; it is not a measurement of hardware kernel time alone.

Each result points to its exact unannotated source JPEG named
frame_<session>_<sequence>.jpg. JPEG is atomically published before the JSON.
Only the two most recent images created by this process are retained. Failures
publish unavailable fields (empty objects/image path), allowing the UI to retain
its explicitly historical last snapshot. No old image is given a fresh frame ID.
SIGTERM stops polling and waits for the current runtime call to return; there is
no forced termination of an executing NPU call. stopped invalidates the JSON and
normal process destruction removes only this process's two retained images.
The output directory has a separate flock protecting against duplicate writers.

Hardware-free checks:

```
g++ -std=c++17 -O2 http_transport_probe.cpp -o http_transport_probe
python3 test_http_transport.py --binary ./http_transport_probe
```

The 21 cases test HTTP bounds, protocol metadata, stale input, malformed JPEG
markers, absolute deadlines, sequence/session gates, writer lock, two-image
retention, JSON/image identity, invalidation, and preservation of unrelated files.
The SOF fixtures are structural parser inputs, not entropy-coded camera images;
these tests do not claim JPEG decoder, model accuracy or NPU validation. Board
tests must additionally compare the same full-frame JPEG in --image and HTTP
modes, exercise normal/defect examples, and observe both board workloads together.
