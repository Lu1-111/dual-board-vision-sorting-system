#!/bin/sh
# Run in the configured Qt 5.13.2 build tree with its cross compiler on PATH.
set -eu
qtbase=${1:?Usage: build_linuxfb.sh /path/to/configured/qtbase /output/directory}
output=${2:?Output directory required}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
qtbase=$(CDPATH= cd -- "$qtbase" && pwd)
python3 "$here/patch_linuxfb.py" "$qtbase/src/plugins/platforms/linuxfb"
python3 "$here/patch_damage.py" "$qtbase"
make -C "$qtbase/src/platformsupport/fbconvenience" -j2
make -C "$qtbase/src/plugins/platforms/linuxfb" -j2
mkdir -p "$output"
output=$(CDPATH= cd -- "$output" && pwd)
cp "$qtbase/plugins/platforms/libqlinuxfb.so" "$output/libqlinuxfb.so"
aarch64-linux-gnu-strip "$output/libqlinuxfb.so"
aarch64-linux-gnu-gcc -Os -Wall -Wextra "$here/fb_color_check.c" -o "$output/fb_color_check"
aarch64-linux-gnu-strip "$output/fb_color_check"
mkdir -p "$output/.probe-build"
(
    cd "$output/.probe-build"
    "$qtbase/bin/qmake" "$here/qt_fb_probe.pro"
    make -j2
    aarch64-linux-gnu-strip qt_fb_probe
    cp qt_fb_probe "$output/qt_fb_probe"
)
sha256sum "$output/libqlinuxfb.so" "$output/fb_color_check" "$output/qt_fb_probe"
