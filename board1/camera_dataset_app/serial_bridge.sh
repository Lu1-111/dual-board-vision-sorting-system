#!/bin/sh
# Camera UI keeps JPEG bytes in RAM; this adapter only relays local RPC over UART.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
settings=$(stty -g)
trap 'stty "$settings"' EXIT HUP INT TERM
stty raw -echo
export LD_LIBRARY_PATH="$here/qt_lib/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$here/camera_dataset_app" --serial-bridge
