#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test "$(uname -m)" = aarch64 || { echo 'This release requires AArch64 Linux.' >&2; exit 1; }
simulate=0
for arg in "$@"; do test "$arg" != --simulate || simulate=1; done
if test "${QT_QPA_PLATFORM:-linuxfb}" != offscreen; then
    for process in hdmi_ui mipi_hdmi_preview color_block_npu camera_dataset_app; do
        if pidof "$process" >/dev/null 2>&1; then
            echo "Exit $process before opening this camera application (shared camera/display)." >&2
            exit 1
        fi
    done
fi
if test "$simulate" = 0; then
    test -c /dev/fb0 || { echo '/dev/fb0 is unavailable.' >&2; exit 1; }
    test -c /dev/i2c-0 || { echo '/dev/i2c-0 is unavailable.' >&2; exit 1; }
    if test ! -c /dev/mipicam; then
        test "$(uname -r)" = 6.1.111-rt42 || { echo 'Bundled mipicam.ko requires kernel 6.1.111-rt42.' >&2; exit 1; }
        insmod "$here/modules/mipicam.ko"
    fi
fi
runtime=${CAMERA_QT_RUNTIME:-$here/qt_lib}
test -f "$runtime/lib/libQt5Widgets.so.5" || { echo "Qt runtime missing: $runtime" >&2; exit 1; }
opencv=${CAMERA_OPENCV_RUNTIME:-/home/uisrc/triangle_defect_v28/lib/opencv}
test -f "$opencv/libopencv_core.so.407" && test -f "$opencv/libopencv_imgproc.so.407" || {
    echo "OpenCV 4.7 runtime missing: $opencv; set CAMERA_OPENCV_RUNTIME to the existing library directory." >&2; exit 1;
}
export LD_LIBRARY_PATH="$runtime/lib:$opencv${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$runtime/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$runtime/plugins/platforms"
export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-linuxfb:fb=/dev/fb0}
export QT_QPA_FONTDIR="$here/fonts"
exec "$here/camera_dataset_app" --fullscreen "$@"
