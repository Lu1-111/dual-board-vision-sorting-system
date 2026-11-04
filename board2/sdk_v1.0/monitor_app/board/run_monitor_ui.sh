#!/bin/sh
# 板2 监控终端启动脚本（部署在 /opt/monitor-ui/run_monitor_ui.sh）。
# 使用板上 Qt 5.15.8 和同版本的 BGR888 修正 linuxfb 插件。
BASE=/opt/monitor-ui
cd "$BASE" || exit 1
export QT_PLUGIN_PATH="$BASE/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$BASE/plugins/platforms"
export QT_QPA_FONTDIR=/usr/share/fonts/wqy-zenhei
export QT_AUTO_SCREEN_SCALE_FACTOR=0
export QT_SCALE_FACTOR=1
export QT_FONT_DPI=96
export XDG_RUNTIME_DIR=/tmp/monitor-runtime
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
if [ "$#" -eq 0 ]; then
    MODE=$(cat /etc/monitor-ui-mode 2>/dev/null || echo local)
    case "$MODE" in
        local) ;;
        can-loopback) set -- --can can0 --loopback-test;;
        can-udp) set -- --can-udp 39001;;
        can) set -- --can can0;;
        *) echo "Unknown monitor mode: $MODE"; exit 2;;
    esac
fi
exec env QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0 "$BASE/monitor_ui" "$@"
