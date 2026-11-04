# DR1M90 相机数据集采集 APP

运行在 DR1M90 板载 Linux 上的独立 Qt 应用，复用现有 FPGA MIPI / ISP / FDMA 通路与 IMX415。支持实时取景、单张拍照、定时连拍、JPEG/PNG、按场景保存、照片计数、最近照片和剩余空间。用于先采集图片、再到电脑标注检测框。

## 板端运行

将 `release/camera_dataset_app_board.tar.gz` 复制到开发板，在希望安装的目录解压：

```sh
tar -xzf camera_dataset_app_board.tar.gz
cd camera_dataset_app
sha256sum -c SHA256SUMS.txt
./run_camera.sh
```

发布包带有 AArch64 程序、配套 Qt 5.13.2 运行库、已修正 BGR24 颜色的 linuxfb 插件、JPEG 插件、中文字体及现有相机驱动。无需重新编译 PL、内核或设备树。首次加载包内 `mipicam.ko` 时要求内核 `6.1.111-rt42`；已有驱动仍须支持当前缓冲区与缓存同步 ioctl ABI。当前硬件接口为 `/dev/mipicam`、`/dev/i2c-0`、I²C 地址 `0x1a`、PL 控制地址 `0x80300000`，相机图像为 1920×1080 BGRA。

真实采集须有相机、当前 FPGA 配置、`/dev/fb0` 和输入设备。启动前退出原 `hdmi_ui`、`mipi_hdmi_preview`、`color_block_npu` 等占用屏幕或相机的应用。启动脚本会检查常见冲突，不会自动停止其他程序。实际相机打开失败会显示具体原因，不会切换到模拟画面。若已启动 DMA 的帧完成等待异常，现有驱动没有取消/复位接口，程序会要求重启开发板并阻止再次打开；同次系统启动中的保护不会因重启 APP 清除。

可从串口指定保存位置与场景，不需要在屏幕上输入长路径：

```sh
./run_camera.sh --output /home/uisrc/datasets \
    --dataset object_training --scene desk_daylight
```

默认全屏，使用鼠标/触摸点击“打开相机”，确认画面后点“拍一张”。也可以按空格（文本输入框有焦点时保留输入空格）。选择间隔后点“开始连拍”；保存繁忙时跳过节拍，不积压拍照队列。“停止连拍”停止新请求，正在进行的一张会完成。关闭相机或退出会等待采集结束/1500 ms 超时及当前文件写入，禁止直接拔相机排线或强制卸载驱动。

## 数据文件

保存位置、数据集名、场景名可配置；默认为 `/home/uisrc/datasets/vision_dataset/images/unlabeled/`：

```text
datasets/
  object_training/
    images/
      desk_daylight/
        20261018T080000123Z_<uuid>.jpg
        20261018T080000123Z_<uuid>.json
```

- 保存的是新完成的一帧，原始尺寸 1920×1080，取景框、按钮、文字不会烧录到真实照片中。
- JPEG 默认质量 95，可调；PNG 为无损。PNG 文件通常更大、写盘更慢。
- 每张照片的 JSON 记录 UTC 拍照请求/帧完成时间、实际尺寸、场景、数据集、帧序号、来源、格式和 `annotation_status: "unannotated"`。帧序号为十进制字符串；`image` 路径相对输出根目录，包含数据集名。
- “场景/批次”仅是整理目录，不是目标类别或检测框。应用不会生成虚构的 YOLO/COCO 标注。
- 时间使用 UTC 命名、UUID 避免同名；板卡时间未校准时仍可避免覆盖，但时间字段取决于板卡系统时钟。
- 图像与元数据采用原子文件提交；若元数据写入失败，尝试回滚本次新照片并报告异常。已有照片不会被常规重复拍摄覆盖。断电恰好发生在两个文件提交之间仍可能留下孤立图片，拷贝数据时可核对同名 JSON。
- 只统计本次程序运行中成功保存的照片。目录保留历史照片；再次启动不会清空数据。
- 保存前检查空间，写盘失败会停止连拍并显示错误。请选用持久存储目录，使用 `df -h /home/uisrc/datasets` 确认存储介质和空间，不要把正式数据集保存在 `/tmp`。

采集完成后退出程序，把整个数据集目录复制到电脑，保留图片与同名 JSON。再使用标注工具为图片画检测框、分配目标类别并导出所需格式；建议按采集场景/批次划分训练与验证集，避免连续近似帧同时进入两个集合。拍摄时变化背景、距离、角度、光照，并采集不含目标的背景照片。

## 模拟与界面验证

显式选择 `--simulate` 才会生成测试画面，不会打开相机、I²C 或 GPIO。模拟画面带醒目标识，文件前缀为 `SIM_`，JSON 来源为 `simulation`；不要把它们当成真实训练数据。

```sh
QT_QPA_PLATFORM=offscreen ./run_camera.sh --simulate \
    --output /tmp/camera-sim --dataset simulation_test \
    --screenshot /tmp/camera-ui.png --quit-after 3000
```

`--screenshot` 保存的是界面截图，仅用于界面检查；正式训练图片必须通过拍照按钮产生。`--quit-after` 安全关闭程序。宿主机原生构建也支持相同参数。真实模式默认不自动打开相机；模拟模式会自动开始预览。

## 构建

在现有 Ubuntu 开发虚拟机内：

```sh
bash build_board.sh
bash package_board.sh
```

默认工具链为 `/home/uisrc/uisrc-lab-anlogic/tools/aarch64-linux/bin`，目标 Qt 为 `/home/uisrc/uisrc-lab-qt/images/qt5-arm64-5.13.2`。可设置 `TOOLCHAIN_BIN`、`QTDIR`、`BUILD_JOBS`。打包时还使用相邻 `../mipi_hdmi_preview/release/mipicam.ko` 与 `../ui_app/fonts`，可通过 `CAMERA_PREVIEW_DIR`、`CAMERA_FONTS_DIR` 调整。

原生界面验证使用 x86_64 Qt 的 qmake，在独立目录运行 qmake 后 make。存储测试入口为 `tests/store_tests.pro`；完整模拟界面/连拍/重开/退出测试入口为 `tests/app_smoke.pro`，设置 `QT_QPA_PLATFORM=offscreen`，可设置 `CAMERA_TEST_FONT_DIR` 指向字体目录。`app_smoke [截图路径]` 不使用真实硬件或用户采集设置。

vendor 中保留参考相机 ioctl 头文件、IMX415 寄存器表及原版权说明；没有修改现有相机内核驱动或机械臂程序。

实际执行的构建、测试结果与硬件验证范围见 `VALIDATION.md`。

## HDMI 引导批量采集（2026-10-30）

`guided_capture.py` 配合 `--pc-only` GUI 的 `guide` 命令，在 HDMI 显示摆放说明、移手倒计时和批次进度。PC 串口连接应由一个进程独占。先确认用户准备好，再启动有限张数的计划；命令行工具不自动请求用户确认。当前现场首批计划为 `../color_block_detection/field_training/20261030_v1/guided_pilot_01.json`，含六张四色图。

```powershell
& D:\PRJ\sdk\pc_ai_bridge\.venv\Scripts\python.exe D:\PRJ\sdk\camera_dataset_app\guided_capture.py `
  --port COM9 `
  --plan D:\PRJ\sdk\color_block_detection\field_training\20261030_v1\guided_pilot_01.json `
  --manifest D:\PRJ\sdk\color_block_detection\field_training\20261030_v1\batches\pilot01 `
  --stop-file D:\PRJ\sdk\color_block_detection\field_training\20261030_v1\batches\pilot01.stop
```

COM9 仅为本次确认的第一块板端口，重新连接时应核验。首次准备 20 秒，每拍前移手倒计时 5 秒。照片编码完成后，屏幕才允许摆下一张；上传期间可操作，下一轮至少留出 25 秒摆放时间。图片和任务对应记录持久保存到电脑后才发送 ACK，释放板端内存。照片仍为原始 JPEG，不包含 HDMI 提示。

创建对应 `--stop-file` 可在等待或分段传输间暂停。尚未传完的照片仍在板端内存，需要先用 `receive_photos.py --once` 恢复；不要关闭相机程序。每次使用新 manifest 目录，旧批次不能覆盖；恢复照片需人工核对任务归属，不能冒充新姿态。批次输出只代表收到照片，均为待审核，不自动生成或批准检测标注。

无硬件的 PC 流程测试：`tests/test_guided_capture.py`；运行中的模拟 GUI 协议/倒计时/完整性检查：`tests/guided_integration.py`。模拟数据不得纳入训练。

## 串口单张采集（2026-10-19 补充）

没有键鼠时，可用独立的 `capture_once` 小工具拍一张真实照片。它使用同一采集与存储模块，先等待若干完整帧，再保存一张 JPEG 和 JSON，不向 framebuffer 写入；已打开相机的其他进程会通过独占检查阻止它采集。

用目标 Qt 的 qmake 在独立构建目录编译 `capture_once.pro`，然后把 `capture_once` 放进板端 APP 目录。它需要同目录发布包已有的驱动和 Qt 运行库。驱动已经加载时，可在该目录运行：

```sh
LD_LIBRARY_PATH="$PWD/qt_lib/lib" \
QT_PLUGIN_PATH="$PWD/qt_lib/plugins" \
QT_QPA_PLATFORM_PLUGIN_PATH="$PWD/qt_lib/plugins/platforms" \
QT_QPA_FONTDIR="$PWD/fonts" \
./capture_once --output /home/uisrc/datasets --dataset vision_dataset --scene unlabeled
```

成功输出 `CAPTURE_OK`、`IMAGE`、`METADATA` 和尺寸后自动退出。`--simulate` 只用于显式测试，真实拍照不要添加。该工具已单独部署到当前开发板；2026-10-18 的原交付 ZIP 不包含这个后加的工具。
# 可选实时直播

新增显式 `--stream` 只读 HTTP 出口，不影响原拍照及 ACK 流程。默认关闭，启用方法、帧标识与过期约定见 [LIVE_STREAM.md](LIVE_STREAM.md)，本地验证见 [LIVE_VALIDATION_20261102.md](LIVE_VALIDATION_20261102.md)。

