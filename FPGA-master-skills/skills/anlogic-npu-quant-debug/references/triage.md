# 故障定位与替代路径

事实来源ID见 [证据索引](evidence.md)。本文件中的定位方法属于工程建议，不意味着已对所有DR1版本执行验证。

## A. 算子不支持和“无法量化”分开处理

先给每层建记录：ONNX op、group/Cin/Cout、kernel/stride/pad/dilation、输入输出shape、dtype/量化轴、parser结果、优化后backend、实板结果。按首个失败步骤归类：

1. **图导出失败**：原框架运算语义、opset、shape、常量、控制流或后处理不兼容。先导出最小图，比较float；不要直接换量化参数。
2. **量化器失败**：reader输入/shape不匹配、浮点图不合法、中间shape缺失、被量化op不支持或工具版本冲突。保留精确异常，不仅截图“量化失败”。
3. **量化CPU精度下降**：图可运行但校准/范围/rounding使输出恶化。走B。
4. **厂商parser失败**：CPU ORT可执行，但当前ArmNN图解析不支持、类型/shape契约不满足，或版本缺陷。走C。
5. **后端不能执行**：parser成功而Optimize/Load/Enqueue失败、CPU回退、SoftNPU超时。走D。

不要用“深度卷积神经网络”代替具体算子。普通多层CNN已经存在可量化案例（E5）和板端卷积证据（E2）。Depthwise通常为group=Cin；V1提示其不能由ALNPU加速；但该脚本把显式group=1且Cin=1也标为DWConv，单输入通道退化形态须单独实测，不能从警告推广到所有普通卷积。普通grouped Conv中1<group<Cin有V2拆分实现，但引入Split/Concat、额外copy及兼容性条件。

### 替代路径及条件

| 路径 | 适用情况 | 必须证明 |
|---|---|---|
| CPU保留不支持层 + NPU子图 | 允许混合执行，量化/算子限制仅影响部分图 | 分区边界dtype/shape/layout/scale，桥接等价、总时延与内存 |
| Group拆成普通Conv后Concat | 厂商脚本覆盖的静态group形态 | 原图/改写float等价，量化后误差，Split/Concat实际可执行 |
| Depthwise改成稀疏普通Conv | 有硬核支持需求，能承受放大的权重与MAC成本 | 数学映射正确、shape/内存/带宽可承受、量化和硬件结果；这是候选方案，资料未给通用成功实现 |
| 蒸馏/重训NPU友好结构 | 必须全硬核，CPU回退不满足性能 | 新float自身任务精度先过，再量化与实板；不是对旧模型的等价变换 |
| QAT或重新校准 | 支持性已满足而PTQ误差超限 | 目标部署格式可接受QAT输出，数据独立性和全套任务指标 |
| PL自定义算子 | 现有SoftNPU/CPU均不满足目标，允许开发新硬件 | RTL数值模型、接口/时序、DMA/cache/中断、驱动和ArmNN分区接入；本包不声称有现成自动转换器 |

R1说明有SoftNPU，不意味着所有板都已实例化正确版本，或任意op属性都支持。不要把某一次SoftNPU超时推广成整个系列永久不支持该算子；同样不要因手册列出算子就跳过实测。

## B. CPU量化精度差

1. 用同一float模型与同一输入张量比较导出和量化；先排除RGB/BGR、Pillow/OpenCV灰度公式、resize/letterbox、归一化、layout、输出顺序及后处理差异。
2. 验证校准reader实际消费的样本、预处理和输入范围。V5默认letterbox/最多800/未排序；目录包含更多图片不代表都已消费。选择覆盖亮暗、类别和工作场景的校准数据，不用test调优。
3. 在首个偏差明显层统计min/max、分位数、饱和率、按通道范围、相对scale的LSB误差，以及误差如何传播。per-tensor激活受离群通道影响时，不能只看平均误差。
4. 检查权重和bias：signedness、对称/非对称、per-channel轴、input_scale×weight_scale、INT32范围；检查ReLU/Clip/BN融合前后语义。
5. 保留业务阈值与类顺序，报告每样本新增误报/漏检和阈值margin。float本身错则先修float，不能归罪量化。E5的test140/142、量化相对float零新增错误正是这种情况；它未达该工程相对原模型“不新增误报漏检”的业务门槛，不能解释成量化或parser失败，也不是所有任务都须100%正确。
6. 一次比较一个变化：reader、校准策略、等价融合、目标支持的QAT/混合精度等。改动图/权重后重新生成量化参数、IO契约、bias及后端专用补偿；不存在通用“把所有模型改INT8一定更好”的结论。

## C. ORT能跑、厂商parser不能跑

固定崩溃前模型hash、目标共享库hash/ABI、opset、完整stack/log与最小子图。`onnx.checker` 与shape inference是检查手段，不能掩盖未知动态维；无法推导的维度要显式补充来源，不填任意1。

- 检查graph input/output/value_info是否完整，Conv必要属性与权重是否可读。E5 §5记录量化后shape/value_info补全与目标parser崩溃修复，不能断言所有SIGSEGV均由这个原因造成。
- QDQ外部float输入和目标bindings量化类型须同时记录。零点dtype、axis、多输出顺序和常量shape都可能导致解析/运行不同。
- 用真实目标ARM64运行库的parse-only程序进行QEMU测试，可以提前定位解析层；QEMU一般无实际NPUbackend，不能把parse-only成功称为Alnpu Optimize/Load通过。
- 单文件混合图失败时，只有在边界语义明确、允许CPU分区的前提下才尝试分图。E3是FP32 features + UINT8 head双图成功特例，需显式桥接 `q=clip(round(x/s)+z)`；不要随意把多个子图当不同业务模型。

## D. 板端运行/性能故障

从当前正在运行的boot链查起：实际bit、HPF/FSBL、DTB、内核driver、rootfs和ArmNN库是否同一发布组合。查看设备文件、dmesg、CMA、NPU中断、优化图与执行profile。设备树地址/时钟/中断应来自本设计，不直接照抄E1地址。

- **设备缺失/初始化失败**：E1是ArmNN32.1初始化依赖SoftNPU且DT缺节点的具体案例；先证明PL已有SoftNPU再修DT，不能用虚假节点消除报错。
- **模型加载成功但慢**：记录逐层backend、copy/reshape、CPU线程、CPU-NPU桥接与反复加载。`Alnpu,CpuAcc`允许后端选择；只有profile可证明哪些层真上NPU。INT8/UINT8文件名本身无此证明力。
- **CMA持续增长**：区分buffer池warmup稳定占用与单调泄漏；检查网络切换、CPU workspace是否误用自定义CMA allocator、load/unload与线程安全。E7分析了某libarmnn.so.32.1的所有权缺陷，E3后续修复使CPU workspace用默认堆、NPU与外部IO保留CMA；不能移除NPU必需的DMA buffer。
- **复用/并发异常**：明确每个buffer在哪个线程、哪个帧/目标ID上使用，DMA/CPU读取前后同步；先串行最小复现，再恢复异步并验证不会读写同一buffer。
- **超时**：保留第一次超时及中断/驱动信息，确认bit、reset/clock、地址和CMA后再定位；不要无限重试淹没首个失败证据。

持续运行验收记录负载、warmup后CMA/RSS曲线、迭代次数、失败率、延迟分布与回收行为。不要把E3历史900视频帧作为所有任务固定门槛；按当前业务运行期设计。

## E. CPU QDQ正确、硬件数值有偏差

先生成固定输入张量文件，传输两端校验hash并读取原始输出，不先做摄像头集成。依次比较binding、第一层Conv、子图边界、pool/reshape、FC和后处理；找到第一处系统性误差。

历史ArmNN32.1案例E2：

- HardNPU UINT8 Conv/FC的 `floor(acc*K/2^S)` 与K截断由两张诊断图逐层原始输出及SDK反汇编支持，导致系统性偏低。
- CpuAcc平均池化先输入尺度舍入再输出尺度重标定，PC语义路径只在输出尺度舍入一次。
- 该模型用由实际K/S导出的INT32 bias半步补偿，加CPU整数池化/FC头修正；133图板端类别/状态一致，但仍最大1 LSB残差。

处理方法：写目标版本的整数算术参考，显式处理accumulator、zero-point correction、K/S、舍入和饱和；用不同数值范围/边界输入验证假设。若需要硬件补偿，逐层从本模型导出且处理K=0、overflow及符号等约束。补偿模型与运行器/backend/hash绑定，不允许无提示CPU fallback。**禁止复制E2的六层bias增量到新模型，也不能承诺逐字节零误差**。

池化/Flatten数值缩幅时核对前后scale/zero point是否一致。仅修改边界scale不够时还需重算相关bias及后续量化；对原图语义和板端分别验证。

## 报告最低内容

记录失败归类、首个失配阶段与张量、输入/模型/运行库hash、原始错误、最小复现、已证实原因、修复、float/quant/parser/board各阶段结果、业务误报漏检和性能边界。使用“来源报告”“本轮静态检查”“本轮实板验证”等准确标签。无法上板时继续完成静态/CPU工作并保留明确待验证项，不能补写硬件成功结论。

