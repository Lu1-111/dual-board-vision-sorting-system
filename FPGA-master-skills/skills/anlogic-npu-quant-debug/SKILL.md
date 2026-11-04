---
name: anlogic-npu-quant-debug
description: 诊断安路 DR1/DR1M90 NPU 的算子不支持、ONNX/ArmNN 解析失败、PTQ 精度下降、CPU与硬NPU数值差异、SoftNPU/CMA故障及性能回退。区分 depthwise/group卷积限制与量化误差，不把历史SDK行为泛化到所有型号。
---

# DR1 NPU 量化与运行时排障

先读 [证据与支持边界](references/evidence.md)。按 [故障定位流程](references/triage.md) 找到首个失配阶段，保留原模型、固定输入及日志；只改被证据支持的因素。

## 先分类

| 现象 | 首要检查 | 不能据此下的结论 |
|---|---|---|
| 厂商检查器提示 DWConv 不能加速 | group、Cin/Cout、kernel/stride/pad、目标后端 | 深度 CNN 都不能量化 |
| 量化器生成失败 | ONNX合法性、shape/value_info、校准 reader、工具版本 | 硬件一定不支持整个模型 |
| CPU QDQ 与 FP32 不同 | 校准分布、饱和、逐层误差、离群通道、阈值 margin | 是 NPU 驱动问题 |
| QEMU/parser 通过、板端失败 | Optimize/Load 阶段、DT/bit/driver/runtime、CMA | 已经完成 NPU 部署 |
| CPU QDQ 正常、板端数值错 | 相同输入字节、bindings、量化边界、舍入/乘数/偏置 | 重新训练就一定能解决 |
| 能跑但慢 | workload放置、CPU回退、桥接、copy/cache、重复加载 | 模型文件是 UINT8 就是全硬核 |

## 核心约束

- 普通 Conv 的量化/部署已有本地证据；depthwise 的 ALNPU 加速限制由当前厂商检查器证实。分组卷积拆分引入 Split/Concat 后仍要验证运行库支持和数据搬运成本。
- 用同一模型 hash、同一输入字节、同一预处理和同一业务阈值比较。每次改变权重、校准或图结构，重新导出绑定与 scale/zero point、bias；不复用旧模型常量。
- QDQ 图外部 float32 类型并不代表板端应喂 float32；从实际 ArmNN parser 的 binding 信息核对。量化定义为 q=clip(round(x/s)+z)，反量化 x=(q-z)s；rounding、量化轴、饱和区间须与目标路径一致。
- 不把历史“半步 bias 补偿”作为通用量化补丁。仅在目标 SDK 算术、逐层原始输出和整数模拟一致时，才派生该模型/该后端专用补偿，并绑定运行器防止 CPU 回退误用。
- 工程旧报告可能包含“全 NPU”推断；没有目标板实际优化图和执行证据时，只能写“静态检查通过/解析通过”。

## 验证与结束条件

最小复现包括最先失败的张量/节点、shape/dtype/量化参数、完整原始错误、工具链与运行库版本及成功基线。数值修复需要张量误差（含 LSB、偏置/饱和率）和业务判定一起验证；运行时修复需要持续运行的 CMA/RSS/线程/延迟证据。训练失败、量化精度失败、解析失败和硬件执行失败分别报告，不能调阈值掩盖平台误差或把单样本成功当全部支持。
