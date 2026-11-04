# 第二块板监控界面

目标硬件：MLK-F3P / DR1M90、eMMC Linux、MPI7002 1024×600 HDMI USB 触摸屏。
当前是实际运行的 Qt Widgets 程序，不是静态测试图。2026-10-25 已部署到第二块板，用户确认中文、计数显示正常，触摸切换页面及坐标准确。

**2026-10-25 CAN 更新：** 已新增 SocketCAN 接收、会话/心跳/计数/告警协议、应用层接收确认和超时显示。第二块板当前默认运行 CAN 控制器内部回环演示，用户确认“模拟断线／恢复接收”正常。CAN 线尚未连接，不能视为两块板之间通信已验证。完整协议、第一块板发送程序和后续接线操作见 [CAN 工程说明](../can_monitor/README.md)。

## 已实现与边界

- **监控总览**：模拟运行状态、模拟心跳、物料计数、当前物料及置信度、活跃告警。
- **告警记录**：事件历史、类型筛选、恢复状态、清空显示。
- **AI 分析**：根据选中的事件生成离线规则建议，明确标为模拟；尚未调用外部模型。
- 场景选择：自动循环、正常分拣、颜色不符、物料滞留、缺料。自动循环一轮 60 拍，默认每秒一拍。
- 当前回环演示的生产数据均为模拟值，外部 AI 未接入；实际板间线缆未连接。五种物料类别只是演示定义，不代表视觉模型已训练完成。
- `IDataSource` 已有本地模拟和 CAN 两种实现；线协议由 `sdk/can_monitor/protocol.h` 明确定义，不能直接发送含 QDateTime 的 C++ 结构体。
- 外部模型接口尚待实现，没有保存 API 密钥，也没有可用的在线服务配置页。

## 屏幕和触摸接线

HDMI 接第二块板；屏幕两个标记 Touch 的 micro USB 接口功能相同，任选一个，用数据线接板子的 USB HOST，另一个留空。该线同时供电和传输触摸数据。若需要额外供电，使用带独立电源的 USB 集线器，不将屏幕两个 USB 口分别接充电器和主机。

厂商说明：https://www.lcdwiki.com/7inch_HDMI_Display-C

板上已识别 USB HID 触摸屏 `0484:5750 MPI7002`，驱动 `hid-multitouch`，本次设备为 `/dev/input/event0`。设备编号可能随连接顺序变化，启动脚本没有写死编号。

当前 Qt 插件通过 evdev 在启动时发现输入设备。**请在开机前接好触摸 USB；运行中再接入或重新插拔后，如触摸无响应，重启界面服务。**

## 板上运行

程序：`/opt/monitor-ui/monitor_ui`；启动脚本：`/opt/monitor-ui/run_monitor_ui.sh`；服务：`/etc/init.d/S99monitor-ui`。

```sh
/etc/init.d/S99monitor-ui status
/etc/init.d/S99monitor-ui restart
# 切回原来的手动场景 UI 演示
/opt/monitor-ui/switch_mode.sh local
# 恢复 CAN 内部回环演示（保存模式，下次开机仍生效）
/opt/monitor-ui/switch_mode.sh can-loopback
tail -n 30 /var/log/monitor-ui.log
cat /proc/bus/input/devices
```

使用板上现有 Qt **5.15.8** 运行库、文泉驿中文字体和同版本私有 linuxfb 插件。插件已修正 24 位 framebuffer 的 BGR888 颜色顺序，画面为 1024×600。`S99board2-display` 保留为启动阶段测试画面，随后由 UI 覆盖。

本机日期未校准时，界面提示“本机时钟未校准”；告警时间仍取系统时间。接入真实数据前需校准系统时钟。

## 本地编译和发布

Windows 源码：`D:\PRJ\sdk\monitor_app`。已配置的 WSL Ubuntu 工作目录：`/home/luyuxiang/monitor-build`。

交叉编译使用 Linaro GCC 7.5 / glibc 2.25 工具链及 Qt 5.15.8，不能直接使用要求更新 glibc 的工具链输出。Qt 源码来自官方 5.15.8 qtbase 包。工具链工作副本的链接修复见 `board/repair_toolchain_links.py`，24 位显示修复见 `board/patch_linuxfb_5158.py`。

```sh
bash /mnt/d/PRJ/sdk/monitor_app/board/build_app.sh
```

输出 `release/monitor_ui`。完整部署包为 `release/monitor-ui-board.tar.gz`，其中包含程序、CAN 发送器/模式脚本、启动脚本、服务脚本和已验证的显示插件；不包含 Qt 系统库、字体或启动固件。目标板必须具备上述现有运行环境。

通过 `sdk/color_block_detection/deploy/send_serial_file.ps1` 传输并校验压缩包后，在板上执行：

```sh
mkdir -p /tmp/monitor-install
tar -xzf /tmp/monitor-ui-board.tar.gz -C /tmp/monitor-install
/etc/init.d/S99monitor-ui stop
mkdir -p /opt/monitor-ui/plugins/platforms
cp /tmp/monitor-install/monitor_ui /opt/monitor-ui/
cp /tmp/monitor-install/run_monitor_ui.sh /opt/monitor-ui/
cp /tmp/monitor-install/can_sender /tmp/monitor-install/setup_can.sh /tmp/monitor-install/prepare_monitor_can.sh /tmp/monitor-install/switch_mode.sh /opt/monitor-ui/
cp /tmp/monitor-install/plugins/platforms/libqlinuxfb.so /opt/monitor-ui/plugins/platforms/
cp /tmp/monitor-install/S99monitor-ui /etc/init.d/
chmod 755 /opt/monitor-ui/monitor_ui /opt/monitor-ui/can_sender /opt/monitor-ui/*.sh /etc/init.d/S99monitor-ui
sync
/etc/init.d/S99monitor-ui start
```

可用 `--sim-interval 250` 加速模拟，`--windowed` 用于桌面调试。生产数据和诊断仍为模拟。

## 验证记录

- 桌面自动检查覆盖三页导航、告警触发及恢复、选中事件的离线分析、缺料/滞留显示、超过 255 个事件的编号及恢复。
- 已交叉编译并通过串口传输校验，在板上运行；实际 framebuffer 回读图为 `release/board-ui-live.png`。
- 用户已目视确认 UI 显示，并确认真实手指触摸导航准确。
- 重启、冷启动的最终结果见 `release/verification.txt`，未完成的验证不可据此推断成功。

## 启动故障修复与回退

本次遇到的另一项问题是 FSBL 在 eMMC 最后一项调相失败时提前退出，导致 Linux 和 UI 都无法启动。已备份原始 BOOT.bin，并安装仅修正该边界判断的启动镜像；没有修改已验证的 HDMI FPGA 配置或 system.dtb。

复现脚本及地址校验：`board/prepare_boot_fix.py`。产物及校验记录：`release/boot-fix/`。此补丁只针对脚本验证的原始版本，不应直接用于其他固件。

- 板上原镜像备份：`/root/monitor-ui-backup-20261025/BOOT.before-ui.bin`
- 原始 BOOT SHA256：`923c43987cbc834ee2c669428cc6065565eeb28477f7a901d4730270f7b03bcd`
- 修复后 SHA256：`9b50c3bc5c118e1f1529d0d07d4d459b67c80840a9e24d2a5494660678e5e981`

UI 回退可停止服务并将 `/etc/init.d/S99monitor-ui` 移出开机脚本目录，保留 `S99board2-display`。BOOT 镜像回退须先核对备份校验值，再复制回启动分区并同步；恢复旧镜像会重新引入已观察到的 eMMC 启动失败风险。
# 异常图片与电脑 AI 桥接更新（2026-10-25）

现已支持告警绑定JPEG预览，以及屏幕点击后向电脑提交图片和文字、显示返回分析结果。AI页保留最近100条告警；其他告警页面原有逻辑保持不变。

使用说明见 `../pc_ai_bridge/README.md`；板1图片提交接口见 `evidence_bridge/README.md`；本次实机验证与边界见 `evidence_bridge/VERIFIED_20261025.md`。当前真实外部API尚未选择，CAN仍为内部回环演示。不要将已通过的图片网线通道误认为真实CAN已经联通。

# 运行状态和北京时间修正（2026-10-25）

本地模拟和CAN回环模拟中，三类视觉告警不再自动改写生产线运行状态；颜色不符时继续模拟运行和不合格计数。真实CAN心跳明确上报的暂停、故障、待机仍会正常显示；通信超时仍显示未知。页面切换及AI分析只操作监控界面与分析队列。

两块板时区已设置为Asia/Shanghai（北京时间）。当前系统没有可用的`/dev/rtc*`，不能保证断电后保时。电脑桥接启动时自动校准板2；也可在关闭桥接/串口终端后运行`../pc_ai_bridge/sync_board_clocks.ps1`校准两块板。未校时前不创建AI分析请求，避免校时跳变使待办请求立即过期。

