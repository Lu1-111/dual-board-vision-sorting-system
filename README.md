# DR1M90 双板视觉分拣系统

基于两块 DR1M90 FPGA SoC 开发板的视觉分拣项目，面向固定相机与传送带场景中的蓝色工件。主控板完成采集、缺角识别和分拣控制，监控板显示运行状态并识别工况；两板通过 CAN 与以太网交换数据。

缺角识别采用 **V28**，工况识别采用 **EfficientNet-B0**。本仓库汇总 FPGA 工程、板端应用、模型训练源码及配套 AI 开发技能。

## 两板职责

| 单元 | 主要功能 |
| --- | --- |
| 主控板 `board1` | IMX415 相机采集、蓝色工件定位、V28 缺角识别、传送带送料、六轴机械臂抓放及状态上报 |
| 监控板 `board2` | Qt 触摸监控界面、CAN 状态与告警接收、相机预览、EfficientNet-B0 工况识别 |
| 通信 | CAN 传送状态、计数和告警；以太网传送相机图像与预览 |

分拣流程：**等待工件 → 送料定位 → 到位停带 → 缺角判定 → 抓取分区放置 → 等待清场**。相机由统一采集程序管理，定位和模型推理分别发布结果。

## 目录

| 路径 | 内容 |
| --- | --- |
| [board1/prj_v1.0](board1/prj_v1.0) | 板一 TD 工程、RTL、IP、约束、设备树、驱动及 FSBL 源码 |
| [board1/sdk_v1.0](board1/sdk_v1.0) | 主控界面、自动分拣、机械臂、传送带与 CAN 模块 |
| [board1/camera_dataset_app](board1/camera_dataset_app/README.md) | 相机采集、HTTP 图像服务与工件中心跟踪 |
| [board1/triangle_defect_v28](board1/triangle_defect_v28/board/HTTP_MODE.md) | V28 板端推理与视觉服务 |
| [board2/sdk_v1.0/monitor_app](board2/sdk_v1.0/monitor_app/README.md) | 监控界面、启动脚本与测试 |
| [board2/efficientnet_live](board2/efficientnet_live) | EfficientNet-B0 实时工况推理 |
| [Training_Sources](Training_Sources/README_EXPORT.txt) | 缺角与工况模型的训练、评估、导出源码及来源清单 |
| [FPGA-master-skills](FPGA-master-skills/README.md) | 安路 FPGA/FPSoC 开发技能、TD/FD MCP 工具及串口、虚拟机辅助技能 |

保留 `monitor_app` 与同级 `can_monitor` 的目录关系，监控应用通过相对路径引用共享协议头文件。

## 环境与使用入口

- **FPGA**：Anlogic TD，工程记录版本 `6.2.175876`，器件 `DR1M90GEG400`，顶层 `system_top`。
- **板端软件**：AArch64 Linux、配套 BSP 与驱动、GCC/G++；板一使用 Qt 5.13.2，板二使用配套 Qt 5.15.8。
- **视觉推理**：厂商 Arm NN / Alnpu SDK 与 AArch64 OpenCV；V28 构建还需 CMake 和 Ninja。
- **训练**：建议 Python 3.11；按训练目录准备 PyTorch、torchvision、ONNX 等依赖。

| 开发内容 | 入口 |
| --- | --- |
| FPGA 工程 | 用 TD 打开 [soc_prj.al](board1/prj_v1.0/soc_prj/soc_prj.al) |
| 主控界面 | [ui_app/README.md](board1/sdk_v1.0/ui_app/README.md)、[ui_app.pro](board1/sdk_v1.0/ui_app/ui_app.pro) |
| 自动分拣与送料 | [autosort/README.md](board1/sdk_v1.0/autosort/README.md)、[autosort.pro](board1/sdk_v1.0/autosort/autosort.pro) |
| 相机与缺角服务 | [相机构建脚本](board1/camera_dataset_app/build_board.sh)、[V28 服务说明](board1/triangle_defect_v28/service/README_SERVICE.txt) |
| 监控与工况推理 | [monitor_app.pro](board2/sdk_v1.0/monitor_app/monitor_app.pro)、[build_live_worker.sh](board2/efficientnet_live/build_live_worker.sh) |
| CAN 通信 | [can_monitor/README.md](board1/sdk_v1.0/can_monitor/README.md) |

首次使用需调整脚本中的工具链、SDK、模型、数据和输出路径。部分文件保留原开发机的绝对路径；子目录文档中的历史部署记录不代表当前设备状态。

## 模型训练

### V28 缺角识别

[V28 训练工程](Training_Sources/triangle-defect/README.md) 包含模型定义、训练与评估代码。采用 MobileNetV3-Small 特征模块与局部分类头，输入为 320×320 灰度图，判定阈值为 `0.88`。板端采用 FP32 特征网络与 UINT8 分类头分段推理。

补齐数据集和初始化权重后，在训练工程目录执行：

```bash
python -m pip install -r requirements.txt
python run.py train --output runs/v28_retrain
```

原始参数见 [training_config.json](Training_Sources/triangle-defect/reports/evidence/training_config.json)。本仓库保留 V28，不包含后续 NPU 重训工程。

### EfficientNet-B0 工况识别

识别 `empty`（空带）、`object_present`（物块在位）、`hand_present`（人工干预）三类状态。输入为 RGB，等比例缩放补边至 224×224，并进行 ImageNet 标准化。

训练入口见 [four_models_holdout18](Training_Sources/blue_conveyor_state/deliverables/four_models_holdout18/README.md) 及其 [EfficientNet-B0 训练目录](Training_Sources/blue_conveyor_state/deliverables/four_models_holdout18/training/efficientnet_b0)。`blue_conveyor_state` 同时保留 TinyCNN 和其他网络的历史比较代码，应按具体模型选择入口。训练导出的 ONNX 仍需适配板端运行库及分段推理程序。

## 资源范围

- 图片数据集、训练/预训练权重、板端 ONNX、厂商 SDK、第三方运行库、编译产物及可烧录镜像需另行准备。
- 训练归档来源与文件哈希见 [SOURCE_MANIFEST.json](Training_Sources/SOURCE_MANIFEST.json) 和 [SOURCE_SHA256SUMS.txt](Training_Sources/SOURCE_SHA256SUMS.txt)，仅覆盖训练源码归档。
- 板二独立 FPGA 工程及工况服务管理脚本未完整收录；当前目录不是可直接烧录的整机镜像。
- 编码器比例、PI 参数、抓取位置和动作组需按实物标定；完整整板时序与最终联合验收材料未随仓库提供。
