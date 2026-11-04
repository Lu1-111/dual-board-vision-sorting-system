# Independent preview endpoint

`--stream` enables both the existing `/v1/frame.jpg` and the new `/v1/preview.jpg`.
Without `--stream`, neither HTTP service nor preview encoder is started.

Existing inference/photo behavior is retained:
- `/v1/frame.jpg`: original full resolution, `--stream-fps 3`, JPEG `--stream-quality 95` defaults, demand lease unchanged.
- Photos and local HDMI video retain their existing paths; the local GUI refresh cap remains 10 FPS.
- Frame delivery to network caches is now directly from the capture thread after its existing owned DMA `.copy()`, independently of the GUI's 10 FPS refresh. No camera is reopened, no extra DMA copy is introduced.

Preview defaults: `--preview-fps 15 --preview-width 640 --preview-quality 75`.
FPS range is 1–30; maximum edge range is 160–1280; quality range is 1–100. Invalid values are rejected. `preview-width` means maximum edge, preserving aspect ratio and avoiding upscaling: 1920x1080 becomes 640x360.

The preview uses its own background encoder and latest JPEG cache. HTTP requests do not trigger JPEG encoding. Each encoder has at most one in-flight frame and one replaceable pending frame, and both share immutable owned QImages from capture. Scaling/encoding never holds the capture object or cache mutex. The existing four-client cap and bounded HTTP I/O remain unchanged.

Preview downscaling uses Qt FastTransformation to reduce resize cost on the board. This trades display-only resampling quality for throughput; the full-resolution inference endpoint never enters the scaling branch. There is no explicit format conversion or additional full-frame deep copy in either encoder. The capture-owned copy remains required for DMA lifetime safety.

Both endpoints use the camera's native frame sequence, capture UTC and session UUID. Header width/height describe the actual endpoint JPEG; frame age uses monotonic receipt time. Stop/reopen invalidates both caches, including in-flight old-session results. `/v1/status.json` retains existing full-stream keys and adds a `preview` object with its own size, quality, encoder period/counters, pending count and age.

15 FPS is a configured upper bound, not a measured guarantee. Actual source capture, resize/encoding speed, photo saves, GUI/socket servicing and network throughput can reduce it. Preview and inference are asynchronous; the UI must not present an older inference as a prediction of the current preview frame.

Native test instructions (existing Qt host build): build `tests/live_stream_fixture.pro`; run existing `tests/live_stream_integration.py <fixture>` and new `tests/preview_stream_integration.py <fixture>`. The new test verifies preview caching without HTTP demand, independent full-resolution quality/rate, bounded pending frames, native metadata, stale/stop/reopen handling and invalid preview configuration. Target-board FPS, frame age, memory/CPU and capture/photo concurrency must still be measured.
