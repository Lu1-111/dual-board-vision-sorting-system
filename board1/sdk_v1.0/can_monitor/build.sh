#!/bin/bash
set -eu
SRC=/mnt/d/PRJ/sdk/can_monitor
TOOL="$HOME/monitor-build/toolchain/bin"
export PATH="$TOOL:/usr/bin:/bin"
mkdir -p "$SRC/release"
aarch64-linux-gnu-g++ -std=c++11 -O2 -Wall -Wextra -Werror -pthread "$SRC/sender.cpp" -o "$SRC/release/can_sender"
aarch64-linux-gnu-strip "$SRC/release/can_sender"
