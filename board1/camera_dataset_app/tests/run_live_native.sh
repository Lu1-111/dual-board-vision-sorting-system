#!/usr/bin/env bash
# Run only our own offscreen simulated applications; no camera hardware involved.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
qt=${NATIVE_QT:-/opt/Qt5.13.2/5.13.2/gcc_64}
export QT_QPA_PLATFORM=offscreen
export LD_LIBRARY_PATH="$qt/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$qt/plugins"
cd "$here"
for target in native fixture smoke-native; do mkdir -p ".build-$target"; done
(cd .build-native; "$qt/bin/qmake" ../camera_dataset_app.pro CONFIG+=release; make -j2)
(cd .build-fixture; "$qt/bin/qmake" ../tests/live_stream_fixture.pro CONFIG+=release; make -j2)
(cd .build-smoke-native; "$qt/bin/qmake" ../tests/app_smoke.pro CONFIG+=release; make -j2)
python3 tests/live_stream_integration.py "$here/.build-fixture/live_stream_fixture"
.build-smoke-native/app_smoke
if test -S /tmp/camera-dataset.sock; then
    echo 'An existing photo RPC socket is present; leave it alone and stop this test.' >&2
    exit 2
fi
.build-native/camera_dataset_app --simulate --pc-only --stream --stream-address 127.0.0.1 \
    --stream-port 47994 --output /tmp/camera-no-photo-files-20261030 --quit-after 45000 \
    > test-photo-app.log 2>&1 &
app_pid=$!
trap 'kill "$app_pid" 2>/dev/null || true' EXIT
python3 tests/live_photo_concurrent.py
wait "$app_pid"
trap - EXIT
