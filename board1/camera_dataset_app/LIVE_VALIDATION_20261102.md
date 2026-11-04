# 直播版本本地验证（2026-11-02）

新增 `latest_frame_server.*`，连接现有 `MainWindow::receiveFrame`。未修改 `capture_worker.*`、`dataset_store.*`、`camera_console.*` 的相机 DMA、保存、Unix RPC 和 ACK 实现。新增显式 `--stream` 开关及地址/端口/FPS/质量参数，默认关闭。源码和构建仅在工作目录及固定开发 VM 中完成，没有访问 COM3/COM9，没有部署、重启或停止当前板上采集。

## 构建

- 固定 Ubuntu VM `/home/uisrc/camera-live-20261102`。
- native Qt 5.13.2，g++，release 构建通过。
- 板端 Qt 5.13.2，`aarch64-linux-gnu-g++`，`build_board.sh` 通过。
- aarch64 主程序 327872 字节，SHA256：`427260589d76073167aa62e3072b93f4e1dd36c738ef3b7c37c8badd2275a4c8`。
- ELF NEEDED 仍为 Qt5Widgets/Gui/Core、pthread、stdc++、m、gcc_s、c；没有增加 QtNetwork、OpenCV 或其他板端依赖。

## 已通过的检查

1. **native HTTP 生命周期/资源边界**：1920×1080 随机噪声 JPEG 2381484 字节；首帧、新帧序号、JPEG 头尾、长度、来源、原尺寸；无订阅停编码；单待编码槽覆盖旧帧；暂停导致 stale 503；停止立即 503；重开 UUID 改变；未知路由/POST/过长头分别 404/405/431；4 连接上限；不完整头 2 秒超时；不读图片的慢客户端 3 秒发送超时释放，其他请求仍响应。
2. **aarch64 QEMU 相同 HTTP 检查**：与 native 完整复用 `live_stream_integration.py`，全部通过；2.38 MiB JPEG 最后一次编码约 255 ms（QEMU 指标，不代表板端速度）。
3. **直播 + 既有照片/引导并发**：20 次成功直播请求、12 个不同真实发布帧号；引导倒计时期间不自动拍照；非法引导命令被拒绝；两张 JPEG 的 SHA256、分块重传、错误 ACK 拒绝、正确 ACK 幂等、RAM 清理、计数递增全部通过。未产生照片文件。
4. **既有 GUI smoke**：7 个阶段，PNG/JPEG、连拍、开关重开、拍照待完成时安全退出、原尺寸与元数据完整性通过。
5. 独立代理只读审查确认：`m_photoPending` 不会阻止直播；编码时不持共享锁；慢连接不会阻塞采集；重开后的旧会话不能发布；析构等待线程时已释放 mutex。

native 复现：`bash tests/run_live_native.sh`。QEMU 使用同一 fixture 的 aarch64 构建，Qt 与 sysroot 路径与 `build_board.sh` 一致，`qemu-aarch64 -L <toolchain>/aarch64-linux-gnu/libc` 加目标 Qt lib/plugin 环境。

## 实板验证边界

这些是本地模拟/ABI 验证，没有测量真机 JPEG CPU 占用、曝光、拍照间隔或两板网络延迟。主任务在第 18 张照片保存且 ACK 完成后负责安全换版、确认 `source=imx415`、JPEG 原尺寸和摄像头继续工作，再启动板 2 客户端。真实模型在冻结 18 张实拍中全部预测 hand、4 模型均 6/18；直播 UI 必须保留现有可靠性提示，不能将高置信度解释为正确。
