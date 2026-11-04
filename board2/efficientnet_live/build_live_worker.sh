#!/bin/sh
set -eu
base=/home/uisrc/npu-deploy-sdk-20261023/app/face_detection/libs
sdk=$base/armnn_lib
cv=$base/ffmpeg_opencv4.7.0_aarch64
cxx=/home/uisrc/uisrc/uisrc-lab-anlogicM-V4.0.1/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-g++
srcdir=${1:-/tmp/efficientnet_live_source}
out=${2:-/tmp/efficientnet_live_worker}
"$cxx" -std=c++14 -O2 -ffp-contract=off -Wall -Wextra -I"$sdk/include" -I"$cv/include/opencv4" -I"$srcdir" "$srcdir/live_worker.cpp" "$srcdir/preprocess.cpp" -L"$sdk/lib" -L"$cv/lib" -Wl,-rpath-link,"$sdk/lib" -Wl,-rpath-link,"$cv/lib" -larmnnOnnxParser -larmnn -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lpthread -ldl -latomic -o "$out"
file "$out"
