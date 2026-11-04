#!/bin/sh
cd /home/uisrc/qt_play || exit 1
QTLIB=/home/uisrc/qt_play/qt_lib
export LD_LIBRARY_PATH="$QTLIB/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$QTLIB/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$QTLIB/plugins/platforms"
export QT_QPA_FONTDIR="$QTLIB/lib/fonts"
exec env QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0 ./hdmi_ui "$@"
