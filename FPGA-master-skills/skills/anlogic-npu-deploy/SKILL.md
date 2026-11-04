---
name: anlogic-npu-deploy
description: 安路 DR1M90/DR1 FPSoC 的 NPU 模型导出、兼容性评估、量化、ArmNN 集成与板端验收。用于 ONNX 模型部署或 PL/PS/NPU 视觉流水线；量化误差与运行时故障可接 anlogic-npu-quant-debug，不用于昇腾 CANN。
---

# DR1 NPU 模型部署

把模型部署视为模型、数值契约、ArmNN/驱动、PL bitstream 与 PS 系统的组合。先读 [来源与支持矩阵](references/support-and-sources.md)，再按 [部署步骤](references/deployment.md) 推进。资料以 MLK F3P-CZ02 / DR1M90 为主，其他 DR1 型号、SDK 版本和自研板需重新核对。

## 接入与判断

1. 定位模型/训练代码、输入输出、训练时预处理、后处理和验收数据；记录模型 hash、opset、静态 shape、量化格式，以及允许的 CPU/SoftNPU/PL 回退。若用户要求全 NPU，则逐层记录分配证据，不能把默认 CPU 回退计为成功。
2. 读取真实图而不是从“CNN、YOLO、MobileNet”等名称推断支持性。区分普通卷积、depthwise（一般 group=Cin 且 Cout 为 Cin 倍数）与 grouped Conv。**“深度卷积神经网络不能量化”不成立**；“某 depthwise 算子不能被该版 ALNPU 加速”与“模型量化后精度不足”是两类问题。
3. 手册描述 HardNPU 为 512 MAC、加速卷积/全连接，SoftNPU 为变换类算子；这是产品能力概述，不是所有形状和导出图的部署保证。先查本机厂商工具，再查目标 ArmNN 解析器与实板 workload。
4. 校准集使用代表性训练/校准数据，保留文件清单、顺序和预处理版本。验证/测试集用于评价；不要为了让量化结果通过而用测试数据调校准或阈值。

## 推进与验收

- 浮点导出：框架与 ONNX 使用同一输入张量对齐；动态维度、Resize、后处理裁剪和 graph rewrite 必须有语义对照。
- 厂商转换：在副本上执行 simplify、shape inference、必要的 Conv/FC 拆分、检查及 QDQ 静态量化；核对脚本实际保存的图和调用参数。通用 ORT 量化产物不能自动当作厂商可部署文件。
- 明确输入契约：布局、尺寸、颜色、归一化、padding、rounding、dtype、scale/zero point、buffer stride、输出顺序。FP32 ONNX 外部 IO 与 ArmNN 识别到的 QAsymmU8 bindings 可能不同，必须分别读取。
- 按顺序留证：框架 FP32 → ONNX FP32 → CPU QDQ → 厂商 ARM64 parser（本机或 QEMU）→ 板端 Optimize/Load/Enqueue → 业务结果。解析通过只证明解析；QEMU 不证明硬 NPU 执行、CMA、时延或实板精度。
- 板端检查 boot/bit/DTB/内核驱动/运行库一致性，再用固定张量验证。历史平台有 `/dev/cma_mem`、`/dev/hard_npu`、`/dev/soft_npu`；是否需要及节点名以当前驱动为准，不直接复制历史设备树地址。
- 性能分别报告模型加载、预处理、buffer/cache 同步、NPU enqueue、CPU 后处理及端到端时间；以实际优化图/执行 profile 证明放置。IRQ 增长是旁证，不能简单把中断次数等同卷积层数。

## 交付

输出模型与运行器、依赖版本、IO/前后处理契约、来源及 hash、算子分配、分级验证结果和明确未验证项。对于未支持层，可选 CPU 分区、等价改写、NPU 友好重训或 PL 自定义实现；仅在实际完成语义/精度/性能验证后称为可部署。开发板擦写、重启和服务切换按当前任务授权实施；只要求评估或整理资料时不执行这些动作。
