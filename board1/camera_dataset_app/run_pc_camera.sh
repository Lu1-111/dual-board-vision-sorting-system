#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0
exec "$here/run_camera.sh" --pc-only --open-camera "$@"
