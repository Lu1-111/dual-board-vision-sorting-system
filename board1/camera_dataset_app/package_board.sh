#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
qt=${QTDIR:-/home/uisrc/uisrc-lab-qt/images/qt5-arm64-5.13.2}
preview=${CAMERA_PREVIEW_DIR:-$here/../mipi_hdmi_preview}
fonts=${CAMERA_FONTS_DIR:-$here/../ui_app/fonts}
stage=$(mktemp -d "$here/release/camera-package-XXXXXX")
app="$stage/camera_dataset_app"
mkdir -p "$app/qt_lib/lib" "$app/qt_lib/plugins/platforms" "$app/qt_lib/plugins/imageformats" "$app/fonts" "$app/modules"
install -m 0755 "$here/release/camera_dataset_app" "$app/"
install -m 0755 "$here/run_camera.sh" "$app/"
install -m 0755 "$here/run_pc_camera.sh" "$here/serial_bridge.sh" "$app/"
cp "$here/LIVE_STREAM.md" "$app/"
cp "$here/README.md" "$app/README.md"
if test -f "$here/VALIDATION.md"; then cp "$here/VALIDATION.md" "$app/"; fi
for lib in Core Gui Widgets DBus; do cp -a "$qt/lib/libQt5$lib.so"* "$app/qt_lib/lib/"; done
cp "$qt/plugins/platforms/libqlinuxfb.so" "$qt/plugins/platforms/libqoffscreen.so" "$app/qt_lib/plugins/platforms/"
cp "$qt/plugins/imageformats/libqjpeg.so" "$app/qt_lib/plugins/imageformats/"
# Keep the current board's tested BGR24 linuxfb plugin, not its pre-fix backup.
echo 'a89215a91d999e37a99e1dbe98e7a55b4b28c79ca6c5fbd1489e58111c15324a'"  $app/qt_lib/plugins/platforms/libqlinuxfb.so" | sha256sum -c -
cp "$fonts/DroidSansFallbackFull.ttf" "$fonts/DejaVuSans.ttf" "$fonts/LICENSE.txt" "$fonts/DEJAVU_LICENSE.txt" "$app/fonts/"
cp "$preview/release/mipicam.ko" "$app/modules/"
test "$(modinfo -F vermagic "$app/modules/mipicam.ko")" = '6.1.111-rt42 SMP preempt mod_unload aarch64'
mkdir -p "$app/licenses/qt"
find "$qt" -maxdepth 1 -type f -iname '*license*' -exec cp -t "$app/licenses/qt" -- {} +
qt_sources=${QT_SOURCE_DIR:-/home/uisrc/uisrc-lab-qt/sources/qt5-arm64-5.13.2}
find "$qt_sources" -maxdepth 1 -type f -iname '*license*' -exec cp -t "$app/licenses/qt" -- {} +
test -n "$(find "$app/licenses/qt" -type f -print -quit)"
mkdir -p "$app/licenses/qt-platform-source"
cp "$qt_sources/qtbase/src/plugins/platforms/linuxfb/qlinuxfbscreen.cpp" "$qt_sources/qtbase/src/plugins/platforms/linuxfb/qlinuxfbscreen.h" "$app/licenses/qt-platform-source/"
cp -a "$here/vendor" "$app/licenses/camera-source-attribution"
(cd "$app"; find . -type f ! -name SHA256SUMS.txt -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS.txt)
tar -C "$stage" -czf "$here/release/camera_dataset_app_board.tar.gz" camera_dataset_app
echo "PACKAGE=$here/release/camera_dataset_app_board.tar.gz"
echo "STAGE=$app"
