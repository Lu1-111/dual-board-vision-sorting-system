#!/bin/bash
set -eu
SRC=/mnt/d/PRJ/sdk/monitor_app
exec bash "$SRC/tests/run_roi_ui_tests.sh" "${1:-$SRC/release/fusion-ui-smoke}"
