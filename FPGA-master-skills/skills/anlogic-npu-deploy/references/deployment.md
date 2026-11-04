# 部署步骤与交付契约

本文步骤是工程建议；R/V/E出处见 [来源索引](support-and-sources.md)。命令中的模型、校准目录和工具位置按实际工程解析，先确认文件存在；不要复制历史板卡地址、COM号或业务阈值。

## 1. 冻结可比较的模型

记录模型文件、训练权重、图改写脚本和数据清单SHA-256；记录 Python/onnx/onnxruntime/onnxsim 与厂商库版本。至少保存框架float输出、原始ONNX输出和同一预处理后的输入张量。检测模型还要冻结框坐标、类别顺序、score和NMS语义。

- 静态shape：V3按输入[1,C,H,W]推导，不能默认处理任意动态batch/输入数/序列图。
- fold BN/eval导出后先核对float语义，再量化；不把训练模式BN直接带进推理。
- 模型后处理裁剪必须保留完整图与外部后处理的等价测试。V3确有YOLO类型选项，但这不保证全部YOLO架构或每个新版本都支持。
- 如果 `onnx.checker` 通过但厂商parser崩溃，核对所有中间tensor的shape/value_info、算子属性、外部权重与runtime ABI；参考E5 §5的value_info修复经验。不要用checker成功代替parser测试。

## 2. 量化及预处理

V3 已存在的 CLI 示例（只有当默认reader与模型预处理完全一致时适用）：

```powershell
python AL_onnx_pass.py --input_model model_fp32.onnx --calibrate_dataset calibration --quant_type uint8 --input_mean 0 0 0 --input_scale 0.00392156862745098 0.00392156862745098 0.00392156862745098
```

`--input_scale` 在V5中实现 `(pixel-mean)*scale`，不是最终量化张量的scale。默认reader先letterbox（填充值114、不放大）、BGR→RGB、逐通道归一化、转NCHW；它取 `os.listdir` 原序前800张，非确定排序的全集。用于灰度、直接resize、裁剪ROI、其他归一化或大于800的校准集时，写匹配的reader并冻结实际消费样本清单；不要仅凭目录名声称全部样本已参与校准。

V3/V4流程包含simplify、shape、split、检查、QDQ与量化后shape inference。脚本中的内存图修改并不一定被保存到实际传给量化器的文件，尤其是未发生split时：核对每一步真实文件的opset、shape、节点与hash。脚本可继续运行的Warning也要记录。

UINT8与INT8采用不同权重/激活及per-channel配置。默认先沿用目标SDK已验证的量化路径；改变per-channel、signedness、校准方法或QAT方案时重做parser和实板门槛。QAT可以改善误差，但不会自动增加硬核算子支持。

## 3. 建立部署契约

每个IO tensor都记录以下字段，不硬编码成示例模型值：

| 字段 | 必须澄清的内容 |
|---|---|
| 身份 | 模型hash、tensor名、binding ID、输入输出顺序 |
| 存储 | dtype、shape、NCHW/NHWC、连续性、字节数、buffer stride/alignment |
| 数值 | scale、zero point、axis、signedness、rounding及clip范围 |
| 图像 | RGB/BGR/灰度算法、resize/letterbox、插值、padding、mean/std或scale |
| 输出 | logits/概率/框编码、解码公式、类别顺序、sigmoid/softmax、NMS、阈值 |
| 分区 | CPU/NPU桥接张量、是否显式重标定、物理buffer与cache所有权 |

对标量affine量化，`q=clip(round(x/s)+z)`，`x=(q-z)*s`。只有确认输入语义为pixel/255、s=float32(1/255)、z=0且dtype=uint8时，才可直接把像素字节填入NPU。INT32 bias的量化尺度应与相应输入scale×权重scale一致，per-channel时逐输出通道处理。

通用QDQ ONNX可能有float32外部IO，而ArmNN读取量化边界为QAsymmU8。需同时记录ONNX图与实际 `GetNetworkInputBindingInfo` / `GetNetworkOutputBindingInfo` 的结果；不能只看文件扩展名或量化脚本输出。

## 4. 平台准备与ArmNN运行

参考R1演示链：TD导出包含bit的HPF → FD用该HPF建立Platform/BSP、FSBL → bit/FSBL及kernel/uboot DTS → 匹配Linux与rootfs → 应用和运行库。历史演示用 `source drcfg.sh`、`move_files.sh`、`make_uboot.sh`、`make_kernel.sh`、`make_rootfs.sh`、`create_image.sh`；按当前开发包脚本与依赖顺序执行。文中的CMake3.21.2是演示环境版本，不是已证明的最低版本。TF分区/写卡命令不可作为准备环境的默认动作。

确认PL包含当前模型需要的SoftNPU及数据通路，DT节点来自同份硬件设计。E1表明某ArmNN32.1构建会在runtime初始化时访问SoftNPU，即使计划主要使用HardNPU也可能受此影响；不能在未核实PL时凭空补节点。

可先执行只读检查：

```sh
uname -a
cat /proc/cmdline
ls -l /dev/cma_mem /dev/hard_npu /dev/soft_npu
grep -E 'CmaTotal|CmaFree' /proc/meminfo
grep -i npu /proc/interrupts
dmesg | tail -n 80
```

设备缺失时查驱动日志和DT，读不到dmesg时记录权限限制。不能仅依据设备文件存在证明硬件可用。

已存在的ArmNN实现参考：`sdk/triangle_defect_npu_v28/board/runtime_executor.cpp` 191–245，348–389，444–446。顺序为解析 → `Optimize` 指定backend → `LoadNetwork` → 获取bindings并按 `GetNumBytes()` 分配 → `EnqueueWorkload` → 检查返回状态/执行profile。复用model/runtime/buffer，明确CPU及DMA buffer所有权，按驱动约定cache同步；不要每帧重新解析加载。

## 5. 分级验证

| 层级 | 输入与比较 | 可以证明 |
|---|---|---|
| A 浮点 | 相同预处理张量，框架 vs ONNX | 导出和裁剪语义 |
| B 量化CPU | 同一张量，float vs目标QDQ | 当前量化方案的任务精度和误差分布 |
| C 厂商parser | 实际目标库，必要时ARM64 QEMU | 图能解析、binding shape/type/scale；不证明硬件 |
| D 实板固定向量 | 传输前后hash相同，原始整数输出/反量化/判定 | 数值正确、真实backend路径、运行时稳定性 |
| E 真实系统 | 相机/ROI/显示/CPU负载、持续运行 | 端到端吞吐、延迟、掉帧、内存及业务行为 |

每级单列PASS/FAIL/未执行。E5的全UINT8新模型只到C，test140/142未达到该工程“不新增误报漏检”的任务门槛（相对原模型新增1误报，量化相对自身float无新增错误），这不是parser失败或通用准确率门槛；E2的板端133图回归到D且有1 LSB残差。两者都不能改写成“所有模型实板完美运行”。

先固定输入排除相机差异，再接入实时视觉流水。R1链路为camera→PL ISP/uifdma→PS DDR→NPU→VDMA/vidout/VTG/HDMI；实际图像格式、stride、所有权、帧序号和cache必须逐边界检查，显示刷新率不能充当采集或推理FPS。

## 6. 性能与发布

先warmup，再重复采集稳定样本，保留次数/原始耗时/中位数及高分位。CPU参考使用同图、同输入、同线程条件；分别报告load、preprocess、cache/copy、Enqueue、后处理与end-to-end。多图分区要计入桥接，异步系统要计入等待和结果时效；不能只报告最快一层。

发布包包含实际模型、运行器、目标架构依赖与软链接、IO/预处理/后处理契约、hash清单、数据验证摘要、backend证据和回退方式。硬件专用补偿图绑定其运行器及指定backend。验证正式安装路径的文件hash与一次固定向量，再执行任务需要的持续测试；已有服务仅在授权范围内切换。

