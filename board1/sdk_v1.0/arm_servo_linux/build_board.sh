#!/bin/bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
compiler=${CXX:-${TOOLCHAIN_BIN:-/home/uisrc/uisrc-lab-anlogic/tools/aarch64-linux/bin}/aarch64-linux-gnu-g++}
make -C "$source_dir" -j2 CXX="$compiler" BUILD=.build-board all
mkdir -p "$source_dir/release"
install -m 0755 "$source_dir/.build-board/armctl" "$source_dir/release/armctl-aarch64"
file "$source_dir/release/armctl-aarch64"
