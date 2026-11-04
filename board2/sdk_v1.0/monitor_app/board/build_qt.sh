#!/bin/bash
set -eu
WORK="$HOME/monitor-build"
SRC="$WORK/qtbase-everywhere-src-5.15.8"
TOOL="$WORK/toolchain/bin"
export PATH="$TOOL:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
mkdir -p "$WORK/qt-board"
cd "$WORK/qt-board"
if [ ! -f Makefile ]; then
    "$SRC/configure" -prefix "$WORK/qt-runtime" -xplatform linux-aarch64-gnu-g++ \
      -device-option CROSS_COMPILE="$TOOL/aarch64-linux-gnu-" \
      -opensource -confirm-license -release -nomake examples -nomake tests \
      -no-opengl -no-xcb -no-dbus -no-icu -no-openssl -no-cups -no-glib \
      -no-fontconfig -no-eglfs -no-kms -no-directfb -linuxfb \
      -qt-freetype -qt-harfbuzz -qt-libpng -qt-zlib -qt-pcre \
      QMAKE_LFLAGS+=-fno-use-linker-plugin
fi
make -j6
make install
