#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
face_root=${FACE_DETECTION_ROOT:-/home/uisrc/app/face_detection}
tool_bin=${TOOLCHAIN_BIN:-/home/uisrc/uisrc-lab-anlogic/tools/aarch64-linux/bin}
cmake -S "$here" -B "$here/build" -G Ninja \
  -DFACE_DETECTION_ROOT="$face_root" \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_C_COMPILER="$tool_bin/aarch64-linux-gnu-gcc" \
  -DCMAKE_CXX_COMPILER="$tool_bin/aarch64-linux-gnu-g++" \
  -DArmnn_DIR="$face_root/libs/armnn_lib" \
  -DOpenCV_DIR="$face_root/libs/ffmpeg_opencv4.7.0_aarch64/lib/cmake/opencv4"
cmake --build "$here/build" --target triangle_defect_npu -j 4
file "$here/build/triangle_defect_npu"
