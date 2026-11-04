# 任务管理器（Linux / Qt 5）

2026-10-31：新增独立的 `fixed-sort-preview/run` 控制接口，复用九帧抓放执行器，配合 [固定相机联调工具](../arm_servo_linux/FIXED_CAMERA_SORT.md) 可跳过传送带。此入口通过脚本使用，未新增界面按钮；旧文中“未接入自动分拣”仅描述早期版本。

本工程位于 E:\26FPGA\sdk_v1.0\ui_app，是面向 DR1 开发板 HDMI 屏幕的 Qt Widgets 应用。

- **待办任务**：新增、编辑、搜索、筛选、完成/恢复和删除任务；支持说明、优先级及可选截止日期。修改后自动保存为 JSON。
- **系统监控**：从 Linux /proc 和根文件系统读取 CPU、内存、磁盘、运行时间及进程列表，每 2 秒刷新。进程列表可以按 PID 或名称搜索，目前只读，不结束进程。
- **机械臂控制**：六轴角度/微秒手动运动、夹爪、腕旋、复位、坐标逆解预览和运动、动作组录制/保存/播放/停止，以及每轴校准和几何尺寸配置。默认模拟，不申请 GPIO；未接入视觉识别或自动分拣。
- **传送带联动**：共用机械臂会话，显示厘米坐标、停带状态与速度，支持模拟到位/虚拟抓取/放行、持续视觉 JSON 读取、动作组互锁和 UIO 后端。默认模拟；实物带面与动态抓放路径尚未验证。集成、部署与边界见 [2026-10-24 集成记录](deploy/conveyor-integration-20261024.md)。

## 机械臂操作（2026-10-12）

`armpage.*` 调用 `../arm_servo_linux` 的独立 C++ 关节与运动学层。Qt 使用单线程 20 ms 定时器推进插值和动作队列，不用 sleep 等待动作完成。PS 内部保存 double 角度/微秒值，发送到现有 PL 的六个 8 位通道时才量化，步进约 7.84 μs。界面显示的是最后发送的命令，没有编码器位置反馈。

进入机械臂页后默认连接模拟后端，PWM 关闭。点击“确认初始姿态并启用”后，可以先模拟关节运动、坐标预览和动作播放。启动不会执行复位。重复连接同一后端只刷新状态，保留当前运动和回位任务。离开机械臂页、切换模式、修改 GPIO 路径或退出应用时取消队列并保留最后 PWM。新硬件后端重连后保持控制锁定，需手动关闭 PWM、再次确认初始六轴脉宽后重新使能。“停止并保持”取消当前插值、后续动作及待执行的关闭，保持最后命令。UI 的“回位后关闭 PWM”先取消动作，沿用“复位姿态”的当前模式映射和复位角度，按关节页运动时间（至少 3000 ms）插值回位，最终命令提交后保持 300 ms，再关闭输出。回位期间按钮显示“立即关闭 PWM”，再次点击可直接禁能；未使能或失联锁定时直接禁能，不自动重新使能。回位时禁用标签页内的操作，停止和关闭按钮始终可用。控制台 `off` 仍立即执行，不增加回位；会话释放不自动禁能，通信故障保持并锁定控制。PL 在下一 PWM 周期边界关闭，最长约 20 ms。回位完成指命令插值完成，没有实际关节到位反馈；停止 PWM 后不能保证维持姿态。

连接硬件前，在“校准”页设置逻辑轴到 PL 通道 0–5 的映射、方向、角度限位、物理脉宽上下限、脉宽偏移和复位角度。轴1夹爪、轴2腕旋、轴3–5臂部关节、轴6底座。通道必须一一对应；`offsetUs` 是角度映射的微秒偏移，最终值仍须落在物理脉宽范围内。方向和限位必须在本机逐轴实测后才勾选“已实测”和六轴确认。

选择“硬件 GPIO”，填写低/高32位 GPIO 字符设备，再主动点击“连接”。默认输入 `/dev/gpiochip2` 与 `/dev/gpiochip3` 仅为路径模板，实际编号以板卡为准；后端须验证其设备树身份分别为 `gpio@f8411200` 与 `gpio@f8411300`，不提供绕过开关。初次启用或校准测试会要求确认关节页里的全部六轴初始微秒值，因为现有 PL 是六轴全局启用，其余五轴也会收到命令。普通硬件模式在未完成实测标记时只允许明确确认的单轴脉宽校准测试；另可明确开启下文“临时全范围调试”会话。

配置默认保存在 `AppDataLocation/arm_config.conf`，通常为 `~/.local/share/DR1/hdmi_ui/arm_config.conf`，是与 CLI 共用的 `key=value` 格式。启动载入已保存配置，硬件模式仍需手动连接。保存成功才替换内存配置；保存或载入失败显示错误并保留有效配置。应用配置会先关闭输出，硬件需重新连接和确认初始姿态。

启动脉宽预填值以当前配置的“复位姿态”为准（2026-10-25 用户指定）。未完成六轴校准时按全范围调试映射计算，保留通道和方向，临时范围为 0～180°、500～2499.2 μs、偏移为 0；完成六轴校准后使用常规配置的复位脉宽。输入框保留一位小数，与复位命令对应同一 PL 输出码。替代之前固定的 2026-10-23 停止姿态预置，不更改底层驱动、复位角度或校准标志。预填或重载不会启用硬件 PWM，输入框不代表位置反馈或笛卡尔零位。

坐标页采用 cm，X向前、Y向左、Z向上，俯仰单位为度。逆解先尝试指定俯仰，再在可行俯仰范围中搜索；界面显示实际回算坐标与俯仰，不能将预览结果当成路径碰撞保证。普通硬件模式必须在校准页填写并确认本机实测几何后才允许坐标运动；临时全范围调试可使用尚未确认的当前几何。先验证少量已知坐标；软件没有碰撞检测。

### 尖端坐标原点（2026-10-23）

在当前会话已使能、运动与动作组均停止时，点击坐标页的“以当前尖端设零点”。程序按最后发送的六轴命令和当前几何计算尖端在基坐标中的位置，将它作为新 XYZ 原点，并原子保存到校准页指定的配置文件。设零本身不发送运动命令，不修改关节角度、俯仰、角度映射、限位或校准确认标志。坐标轴保持 X向前、Y向左、Z向上；只平移原点。此位置由命令和模型推算，不是位置传感器测量。

设零后，XYZ 输入框回到 `(0,0,0)`，俯仰输入保留记录值。输入 `(5,0,-2)` 表示相对该原点向前5 cm、向下2 cm；内部先加上原点偏移，再做逆解，预览和状态报告会换算回相对坐标。零点目标先尝试精确俯仰，确保高于默认俯仰搜索上限的初始姿态也能保留其姿态。精确姿态无解时，仍采用已有的俯仰范围搜索；请核对预览里的实际俯仰。

“恢复基坐标”清除偏移并保存。原点与记录时的常规/全范围调试映射绑定，换模式后运动须回到记录模式或重新设零；关闭输出时仍可按记录的映射做只读预览。修改连杆长度、通道、方向、角度/脉宽范围或偏移并应用配置，会清除旧原点。改变初始姿态不会自动移动坐标原点。

原有 version=1 配置仍可载入。新保存的配置增加完整的可选块：`origin_enabled`、`origin_debug_mapping`、`origin_x_cm`、`origin_y_cm`、`origin_z_cm`、`origin_pitch_deg`。缺少整个块时按基坐标工作，部分缺失或非有限值会被拒绝。

夹爪输入为开合角 0–90°，底层转换为逻辑轴1的 `90-open`；腕旋输入为相对中心角 -90–90°，转换为逻辑轴2的 `90-roll`。手动六轴角度输入直接使用各轴校准后的伺服角度。复位仅由“复位姿态”按钮发起，使用配置中的每轴复位角。

动作组 JSON 格式为 `{"version":1,"frames":[{"anglesDeg":[90,90,90,90,90,90],"durationMs":1000,"holdMs":0}]}`。此处角度仅说明结构，不代表本机安全姿态。每组 1–1000 帧，每帧六个有限角度且不超过当前校准限位，运动时间为整数20–60000 ms、停留为整数0–60000 ms。录制取最后发送的姿态与关节页时间；录制时须先停止或完成运动。载入/保存动作前会停止并保持，保存采用 `QSaveFile` 原子提交。播放启动失败、GPIO写入失败或用户停止时立即取消后续帧，不会自动继续。

坐标页“加入动作组”保存当前输入的相对 XYZ 和俯仰，并保存当时夹爪、腕旋的舵机角度。坐标帧在播放时逐帧进行逆解，和工具角度一起组成一次六轴运动。运动/停留时间取关节页。纯角度动作仍保存 version=1；包含坐标帧时保存 version=2，并记录完整的 `coordinateFrame`，旧角度帧可以混排。播放前核对原点和映射，任一坐标帧无解时取消后续帧。这里只规划各关节随时间插值，不保证尖端走直线。

下面只说明 version=2 文件结构，原点和目标均为示例，不能直接用于本机运动：

```json
{"version":2,"coordinateFrame":{"enabled":true,"originBaseCm":[12,0,20],"debugMapping":true,"referencePitchDeg":-45},"frames":[{"type":"coordinate","coordinate":[0,0,0,-45],"toolAnglesDeg":[0,90],"durationMs":1000,"holdMs":0}]}
```

`coordinate` 的顺序为 `[X_cm,Y_cm,Z_cm,pitch_deg]`；`toolAnglesDeg` 为夹爪、腕旋的舵机角度，沿用六轴角度数组的前两项含义，不是夹爪开口角或腕旋相对角。混排的角度帧添加 `"type":"angles"`，其余字段与 version=1 相同。

LeArm 原厂代码转换的 20 个离线动作模板位于 [`action_groups/learm_reference`](action_groups/learm_reference/README.md)，含复位、准备和三色/六个距离分档；来源、换算与风险见该目录说明及 `manifest.json`。这些文件按模板正常角度映射生成，尚未实机验证，不能直接用于当前未完成校准的机械臂或全范围调试映射。

关节、机械臂坐标和动作组 API 可供颜色/距离/视觉服务调用。2026-10-23 增加 `xy-preview` / `xy-move` 固定平面逆解接口，以及 `status.visionReference` 拍摄姿态核对；像素标定与现有 NPU JSON 桥接见 [视觉 XY 定位说明](../arm_servo_linux/VISION_XY.md)。本版本没有自动抓取或分拣状态机。

独立 Qt 测试工程：`tests/tests.pro` 编译动作 JSON/播放状态机测试，`tests/ui_smoke.pro` 编译模拟界面烟测。可在具有兼容 Qt 运行库的环境执行 `QT_QPA_PLATFORM=offscreen ./arm_ui_smoke preview.png /path/to/DroidSansFallbackFull.ttf`，不会申请 GPIO。测试使用完整1024×640工作台，并生成 `preview-tab0.png` 至 `preview-tab3.png` 四个机械臂标签页截图；字体路径可省略，默认同生产入口的应用目录 fonts 路径。实际方向、机械限位、启动和 PWM 电平仍需板卡验证。

`tests/park_tests.pro` 编译 `arm_park_tests`。使用隔离配置和模拟后端，验证常规/全范围调试两种映射下的使能与复位输出码一致、至少 3 秒回位、停留后关闭、停止取消、再次点击立即关闭、控制台立即关闭以及重复连接保持当前会话的流程。

原项目图片已复制到 assets/mascot.jpeg，通过 resources.qrc 嵌入程序并显示在侧栏。当前版本不接入摄像头视频。

## 文件

- ui_app.pro：Qt 5 / qmake 工程，目标程序名 hdmi_ui。
- dashboardwindow.*：全屏窗口、页面导航和侧栏。
- taskpage.*：待办任务界面与本地保存。
- systempage.*：Linux 状态读取与进程列表。
- armpage.*：机械臂页面；arm_actions.*：严格动作JSON和非阻塞播放队列。
- main.cpp：应用入口和中文字体加载。
- release/hdmi_ui：已用配套 Qt 5.13.2 AArch64 工具链构建的程序。
- fonts/DroidSansFallbackFull.ttf：中文字体；DejaVuSans.ttf：拉丁字母、数字和单位回退字体；部署整个 fonts 目录并保留 LICENSE.txt 与 DEJAVU_LICENSE.txt。

## 任务数据

任务文件使用 Qt 的 AppDataLocation，程序底部会显示实际路径。应用名为 hdmi_ui，组织名为 DR1；通常会存到当前运行用户的 ~/.local/share/DR1/hdmi_ui/tasks.json。首次运行从空列表开始。请用同一用户启动程序，否则会看到不同用户各自的任务文件。

保存采用临时文件提交，避免写入中断损坏原文件。如果启动时发现 JSON 格式错误，程序会先尝试复制一份带 .invalid-时间戳 后缀的备份，再从空列表开始；若备份失败，程序拒绝覆盖原文件。

## 板卡显示前置条件

在板卡 Linux 终端确认：

~~~sh
ls -l /dev/fb0
cat /sys/class/graphics/fb0/virtual_size
cat /sys/class/graphics/fb0/bits_per_pixel
~~~

项目设备树 ../anlogic-dr1m90-softip.dts 选择 1920×1080 RGB888，帧缓冲驱动 ../../driver/anlogic-vdmafb.c 配置 24 bpp。实际输出以板卡命令为准。若 /dev/fb0 不存在，需要先将现有 framebuffer、VDMA 和动态像素时钟驱动及对应设备树集成到内核。应用本身不会创建显示设备。

### 24 位颜色修正（2026-10-13）

实机 framebuffer 为 1920×1080、24 bpp、行宽 5760 字节，R/G/B 位偏移分别为 16/8/0。在 AArch64 小端内存中，每像素实际排列是 B、G、R。原 Qt 5.13.2 linuxfb 插件将它作为字节排列 R、G、B 的 `QImage::Format_RGB888` 直接写入，导致黄色变青色、红色变蓝色。直接按 framebuffer 位偏移绘制的六色条已由现场确认正确，故修正在 Qt 输出边界完成。

`display/patch_linuxfb.py` 修复 Qt 平台插件：保持内部 RGB 图像，仅在需要 BGR 的 24 位 framebuffer 写入处转换；截图读取时反向转换。16/32 位路径保持原实现。局部重绘始终从内部图像读取，避免重复交换。源文件旁保留 `.pre-bgr24` 备份。

配套 Qt 5.13.2 的已验证插件是 `release/display/libqlinuxfb.so`，SHA-256 为 `a89215a91d999e37a99e1dbe98e7a55b4b28c79ca6c5fbd1489e58111c15324a`。已更新当前 SD 系统的 `/home/uisrc/qt_play/qt_lib/plugins/platforms/libqlinuxfb.so` 并重启界面；同目录旧插件保存为 `libqlinuxfb.so.pre-bgr24-20261013`。仅适用于这套配套 Qt 运行库，不可混装到其他 Qt 版本。

重新编译时将配套交叉编译器加入 PATH，执行 `sh display/build_linuxfb.sh /path/to/configured/qtbase /path/to/output`。`release/display/fb_color_check` 可读取实际格式；附加 1–180 秒参数时会暂时绘制六色条并恢复原像素，运行前需暂停占用屏幕的程序。`release/display/qt_fb_probe` 使用同一 Qt 环境运行，覆盖全屏、局部改色、重复局部刷新和邻区完整性，并同时校验 framebuffer 与截图。

板端回归结果：旧插件第一阶段 24 点中 16 点错误；修复后四阶段共 96 点全部通过，退出码为 0。启动日志应包含 `packed BGR24 conversion enabled`。此更新只替换显示插件，无需重新生成 FSBL、设备树、内核或 PL bitstream。回退时先退出 Qt，再恢复备份插件并重启界面。

## AArch64 编译

在已有 /home/uisrc/uisrc-lab-qt 的 Ubuntu 开发环境中：

~~~sh
source /home/uisrc/uisrc-lab-qt/scripts/qt_cfg.sh
cd /path/to/sdk/ui_app
export QMAKESPEC="$QTDIR/mkspecs/$ARCHXPLATFOR"
"$QTDIR/bin/qmake" ui_app.pro
make -j2
file hdmi_ui
~~~

配套环境使用 Qt 5.13.2，编译结果应是 AArch64 ELF。也可以直接部署 release/hdmi_ui。修改代码后需重新编译，并用新程序替换板卡上的旧版本。

## 部署与运行

### 开机自动进入 UI（2026-10-23）

当前开发板已安装 `/etc/init.d/S99hdmi-ui`，源文件为 `deploy/S99hdmi-ui`。Buildroot 的 `rcS` 会在启动时调用它，无需 PuTTY、串口登录或电脑发送启动命令。连接 HDMI 显示器和 USB 鼠标，开发板上电后即可等待进入界面。

启动项最多等待 `/dev/fb0` 30 秒，以 root 的原数据目录运行 `/home/uisrc/qt_play/run_hdmi_ui.sh`，日志追加到 `/tmp/hdmi-ui-start.log`。已运行时复用现有进程；用户退出 UI 后不会自动拉起。脚本不发送机械臂连接、使能或运动命令。

维护命令：`/etc/init.d/S99hdmi-ui start`、`stop`、`status`。禁用自启可执行 `chmod -x /etc/init.d/S99hdmi-ui`；恢复执行 `chmod +x /etc/init.d/S99hdmi-ui`。更新 UI 程序仍使用原启动入口。

验证：板端 shell 语法检查和传输哈希一致；已用 `env -i PATH=/sbin:/bin:/usr/sbin:/usr/bin /etc/init.d/S99hdmi-ui start` 验证不依赖登录环境，确认 UI 进程及 linuxfb 输出正常，并验证重复启动不产生第二个进程。此次未执行开发板重启，断电上电后的完整启动流程尚待现场验证。

程序更新默认只保留最新 UI 可执行文件，不另存旧程序备份。2026-10-22 已清理本机两个 SDK 目录和开发板中的旧版 UI 程序、旧发布包及临时归档；当前本机发布程序为 `sdk_v1.0/ui_app/release/hdmi_ui`，开发板启动程序为 `/home/uisrc/qt_play/hdmi_ui`。

### 当前 SD 卡（2026-10-10）

已使用 `/home/uisrc/uisrc-lab-anlogic/boards/DR1M/buildroot/images/boot/` 下的 `BOOT.bin`、`system.dtb`、`uImage.lz4`、`uInitrd.lz4`，以及同一 Buildroot 构建的 `output/rootfs.tar.gz`，将 29.2 GiB SD 卡制作成两个分区：

- `BOOT`：255 MiB FAT32；`boot.scr` 已设为从第二分区启动，原版内存根文件系统脚本保存在 `boot-initrd.scr`。
- `rootfs`：约 28.9 GiB ext4；PARTUUID 为 `1a366705-02`，已包含任务管理器、中文字体和 Qt 运行库。

板卡上的启动入口为 `/home/uisrc/qt_play/run_hdmi_ui.sh`。SD 卡写入后已卸载并通过 FAT/ext4 文件系统检查。**仍需在开发板上验证系统启动和 `/dev/fb0` 输出。**

本次刷卡前的原 BOOT 分区及卡首 1 MiB 备份分别保存在 `D:\PRJ\sdk\sdcard_boot_backup_20261010.tar` 和 `D:\PRJ\sdk\sdcard_first_mib_backup_20261010.bin`。`flash_sd_once.sh` 是带设备标识和备份校验的一次性刷卡脚本，不能直接用于另一张卡。

之后只更新程序时，在 Ubuntu 虚拟机中确认 SD 卡设备名，挂载它的 ext4 分区并替换 `/home/uisrc/qt_play/hdmi_ui`，无需重新分区或重刷系统。替换后执行 `sync` 并卸载分区，再取出 SD 卡。

### 首次部署说明

在板卡首次运行前，将 hdmi_ui（或 release/hdmi_ui）和本工程的整个 fonts 目录放到同一应用目录，例如：

~~~text
/home/uisrc/qt_play/
├── hdmi_ui
├── fonts/
│   ├── DroidSansFallbackFull.ttf
│   └── LICENSE.txt
└── qt_lib/
~~~

若板卡已有兼容的 Qt 5 运行库，可以使用现有库。否则可按项目配套 Qt 示例部署 qt_play/qt_lib；其中需要 plugins/platforms/libqlinuxfb.so 和用于侧栏图片的 plugins/imageformats/libqjpeg.so。图片已嵌入 hdmi_ui，无需单独复制 JPEG。

在板卡终端运行：

~~~sh
cd /home/uisrc/qt_play
chmod +x ./hdmi_ui
export QTLIB="$PWD/qt_lib"
export LD_LIBRARY_PATH="$QTLIB/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$QTLIB/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$QTLIB/plugins/platforms"
export QT_QPA_FONTDIR="$QTLIB/lib/fonts"
QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0 ./hdmi_ui
~~~

按 Esc 或右上角的“退出”结束程序。任务数据随运行用户保存在板卡上，替换程序时不会因覆盖可执行文件而清除。

## 常见问题

| 现象 | 检查 |
| --- | --- |
| 没有 /dev/fb0 | 核对 FPGA 配置、设备树、framebuffer 驱动和 VDMA。 |
| 找不到 Qt 平台插件 | 检查 libqlinuxfb.so 与 QT_QPA_PLATFORM_PLUGIN_PATH；可设置 QT_DEBUG_PLUGINS=1。 |
| 找不到 Qt 共享库 | 检查 LD_LIBRARY_PATH、Qt 库和程序均为 AArch64。 |
| 中文显示方框 | 检查 hdmi_ui 同级的 fonts/DroidSansFallbackFull.ttf。 |
| 侧栏图片不显示 | 检查 Qt 运行包的 plugins/imageformats/libqjpeg.so。 |
| 系统监控显示“不可用” | 确认应用运行在 Linux，且 /proc 已挂载、相关文件可读。 |
| 任务无法保存 | 检查当前用户的应用数据目录是否可写，以及弹出的错误提示。 |

参考资料：../../referrence/01_start/02_start_Linux/05_application_demo/05-1_QT环境搭建及demo演示/05-1_米联客-安路F3P-CZ02开发板QT环境搭建及demo演示.pdf。


## 手动硬件调试与串口入口（2026-10-16）

未校准时，关节页按当前配置的复位角度和全范围调试映射预填初始脉宽，不将这些值标记为实测校准。选择“硬件 GPIO”并首次连接后读取实际输出状态、保持锁定，不改变已有 PWM；需人工关闭解除锁定后才能再次使能。“全范围调试使能”须明确确认六路初始命令，临时开放六轴（包括夹爪）**500～2499.2 μs** 和 **0～180°**。角度、复位、夹爪、腕旋、坐标、单轴校准及动作组可在该会话中操作，运动时间为 20～60000 ms。离页停止队列；重复连接保持会话；关闭才禁能；通信故障保持并锁定。`--arm-page` 只显示机械臂页，不自动连接或使能。

UI 的“全范围调试使能”直接按当前配置的复位角度与调试映射计算六轴使能命令，确认后直接发送这组六轴目标并保持 PWM 开启，不再安排起终点相同的空复位运动，也不宣称使能时能从未知实物姿态缓慢回位。手动输入框中残留的其他目标不会作为本次使能姿态；使能成功后输入框同步到复位目标。拒绝确认不修改输入、不使能或运动。显式控制台 `debug-enable` 仍按调用方确认的 `baseline` 执行，供独立脉宽调试使用，不自动复位。程序无实际位置反馈，使能瞬间仍可能产生运动，命令一致不能代替实物姿态核对。

全范围调试使用临时角度映射和当前几何配置，保留每轴方向和通道，临时脉宽偏移为 0；不会勾选或保存实测校准标志。坐标仍需通过可达性与数值检查。关闭调试后恢复普通模式的校准和几何确认要求。角度动作 JSON 记录舵机角度，切换映射后需重新核对动作结果；坐标动作另保存原点和映射模式，播放时会核对这些信息。

没有鼠标时，可通过串口向正在显示的同一个 Qt 会话发送命令。客户端不创建第二个界面或 GPIO 控制器：

```sh
export LD_LIBRARY_PATH=/home/uisrc/qt_play/qt_lib/lib
/home/uisrc/qt_play/hdmi_ui --arm-command '{"action":"status"}'
/home/uisrc/qt_play/hdmi_ui --arm-command '{"action":"connect","mode":"hardware"}'
# 只有现场明确同意这组初始命令后才发送使能：
/home/uisrc/qt_play/hdmi_ui --arm-command '{"action":"debug-enable","confirm":true,"baseline":[1000,1500,1500,1500,1500,1656]}'
/home/uisrc/qt_play/hdmi_ui --arm-command '{"action":"debug-pulses","pulses":[1000,1510,1500,1500,1500,1656],"durationMs":1000}'
/home/uisrc/qt_play/hdmi_ui --arm-command '{"action":"stop"}'
/home/uisrc/qt_play/hdmi_ui --arm-command '{"action":"off"}'
```

本机 Unix socket `/tmp/hdmi-ui-arm.sock` 仅允许同 UID 访问（0600），支持 show/status/connect/debug-enable/debug-pulses/stop/off，以及 load-actions/play-actions 和下列坐标请求。`load-actions` 使用 `path` 指定动作 JSON，仅载入、不使能或播放；`play-actions` 要求 `confirm=true` 且当前会话已启用。状态包含动作文件、帧数和播放进度。`debug-pulses` 的六个值均须处于 500～2499.2 μs、最多一位小数，`durationMs` 须为 20～60000 的整数。角度和复位通过 Qt 控件操作。请求仍限制为 4096 字节，且有响应超时。命令更新当前控制页的连接、状态和目标值。退出程序会释放 GPIO；异常终止仍不能替代硬件急停。

坐标控制增加以下请求，均操作当前界面会话：

```json
{"action":"coordinate-zero"}
{"action":"coordinate-base"}
{"action":"coordinate-preview","coordinate":[0,0,0,-45]}
{"action":"coordinate-move","coordinate":[0,0,0,-45],"durationMs":1000,"confirm":true}
```

设零与恢复基坐标会保存参数，不驱动舵机；设零要求已使能且停止。`coordinate-preview` 只解算，`coordinate-move` 复用界面相同的运动权限和几何检查。`status` 包含原点的基坐标、参考俯仰、映射兼容状态和当前相对命令坐标。`HDMI_UI_ARM_SOCKET` 可指定独立的绝对 socket 路径，用于隔离模拟测试；默认路径不变，仍核对同 UID、权限与路径归属。

新增验证覆盖核心全范围、量化边界、调试授权退出与普通模式确认门，另有 Qt 模拟界面、Unix socket 和完整 GUI/串口入口模拟联调。部署仅替换 `hdmi_ui`，保留运行库、字体和已修复的 BGR24 平台插件。

2026-10-24 继续集成：传送带页新增“分类放置区”和自动分拣入口，支持五类目标独立放置、完整动作预检、返回观察姿态与放行。板上五类模拟抓放和真实 MIPI 时间字段验证通过，UI 已更新。PL 新位流仍受时序问题阻塞；实物参数未确认时不能进行真实自动抓放。详见 [集成记录](deploy/conveyor-integration-20261024.md)。

同日代码复核已修复确认窗口竞争、中断误放行和停带累计漂移漏检；增加动作任务编号及状态，修订界面已通过板上模拟测试并部署。检查范围、证据与剩余限制见 [代码检查记录](deploy/code-review-20261024.md)。
