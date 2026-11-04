# HDMI 预览与照片直接传电脑

已部署到第一块控制板（本次为 CH342-A / COM9）。HDMI 使用原相机采集界面，照片通过现有 USB 串口传到电脑。

## 当前使用方式

- HDMI 界面点击 **拍一张并传到电脑**；也可使用定时连拍。
- 照片只保存到电脑 `D:\PRJ\camera_photos`，文件名采用电脑北京时间，旁边的 JSON 保留采集信息和 SHA-256。
- 板端最多保留一张 JPEG 在内存中，电脑完整接收、校验并写入成功后才释放；屏幕的“电脑已接收”计数随确认增加。传输期间跳过新的连拍节拍。
- 当前链路为 115200 波特率。首次实拍 1920×1080 JPEG，169769 字节，传输约 25 秒。HDMI 仍持续预览。
- 本次未检测到板端鼠标或触摸输入设备；点击界面需要给第一块板连接 USB 鼠标。
- 接收脚本在电脑后台运行，无独立电脑图形界面。运行期间占用第一块板串口，PuTTY 等终端不能同时打开该端口。

## 启动与恢复

板端先退出占用屏幕的程序，再运行：

```sh
/home/uisrc/camera_dataset_app/run_pc_camera.sh
# 如无鼠标，可在启动时自动拍一张：
/home/uisrc/camera_dataset_app/run_pc_camera.sh --capture-first
```

此入口固定启用 `--pc-only --open-camera`。原 `run_camera.sh` 的普通本地数据集模式仍保留；本任务使用的入口是 `run_pc_camera.sh`。

电脑沿用已有的串口 Python 环境：

```powershell
$env:PYTHONIOENCODING='utf-8'
& D:\PRJ\sdk\pc_ai_bridge\.venv\Scripts\python.exe D:\PRJ\sdk\camera_dataset_app\receive_photos.py --port COM9
```

COM 号可能变化，应选择第一块板 CH342-A。`--once` 接收一张后退出；`--capture --once` 可在相机已打开时请求拍一张并接收。Ctrl+C 正常退出接收，恢复串口终端。

接收中断时，保持板端相机软件运行并重新启动接收即可；尚未确认的照片仍在内存。关闭相机软件、重启或断电会丢失尚未传完的照片，板端不落盘恢复。

板端程序支持 SIGTERM 安全关闭采集。切回原控制界面时先退出相机软件，再执行 `/home/uisrc/qt_play/run_hdmi_ui.sh`。未修改开机自启，因此开发板重启后仍按原有设置启动。

## 验证

- 配套 Qt 5.13.2 / AArch64 工具链构建成功。
- 原有模拟流程测试通过：PNG/JPEG、连拍、关闭重开、保存中关闭，共 5 张、7 阶段。
- 内存传输测试通过：两次拍照、错误编号/偏移/校验拒绝、重复读取、完整 SHA-256、确认幂等、确认后释放、无照片目录创建。
- 串口 RPC 适配器与 SIGTERM 安全退出验证通过。
- 第一块板真实 IMX415 采集通过，电脑照片 SHA-256：`750204b4fa51d74be478cd2711bff022a5787053c4b4cefdb02e4c6f96808797`。
- 板端确认 `delivered=1`、`pending=false`、`camera_ready=true`；原数据集仍为 6 个文件，文件清单未改变。前期抓拍检查产生的两份 `/tmp` 临时图片/元数据已清理，仅清理本次创建的文件。
- 板端时钟尚未校准，图像元数据中的板端拍摄时间可能显示 1970；电脑保存文件名和接收时间使用电脑时钟。

板端原程序备份：`/home/uisrc/camera_dataset_app/camera_dataset_app.pre-pc-20261030`。本次程序 SHA-256：`81d5c88392136b2ad355b9170764d0bcc5faaf6bda2428aafecde11c96cb750b`。未更改 FPGA 位流、设备树或机械臂配置。
