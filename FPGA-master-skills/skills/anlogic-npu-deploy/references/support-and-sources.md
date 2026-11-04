# 来源、支持矩阵与版本边界

本文件是 2026-11-04 的资料整理，不是本轮上板测试报告。路径相对于原资料根 `D:/PRJ`；迁移后以新的工程/资料根替换。各 skill 内的总结可离线阅读，原 PDF、厂商 SDK、模型和工程日志未复制到技能目录。

## 证据索引

| ID | 相对路径 | 已阅读定位与证据范围 |
|---|---|---|
| R1 | `referrence/01_start/02_start_Linux/05_application_demo/05-5_NPU/05-5_米联客-安路F3P-CZ02开发板NPU开发.txt`（同名 PDF） | §1.4 / txt 71–83：512 MAC、Hard/SoftNPU、INT8/UINT8；§2.2 / 85–95：PL/PS视觉链路；§2.3–2.6 / 96–190：HPF、FSBL、系统与应用构建。PDF封面时间2025/12/1，REV2024；命令针对文中演示环境。 |
| V1 | `sdk/cube_defect_quant_v1/scripts/vendor_code/check_onnx_model.py` | 89–119 Conv筛查；121–129 Gemm；131–165 MaxPool；181–219 Add/Concat/Resize；221–225显式不支持与默认警告。属于工程中保存的厂商脚本副本，需记录hash并与实际SDK核对。 |
| V2 | `sdk/cube_defect_quant_v1/scripts/vendor_code/split_convFC.py` | 122–145静态形状/权重与1<group<Cin判断，后续分组拆成多个Conv并拼接；不是depthwise通用支持实现。 |
| V3 | `sdk/cube_defect_quant_v1/scripts/vendor_code/AL_onnx_pass.py` | 11–29实参；47–89 simplify/shape/YOLO裁剪/split/opset/检查/量化。 |
| V4 | `sdk/cube_defect_quant_v1/scripts/vendor_code/run_quant.py` | 30–41补shape；57–106 UINT8/INT8两种QDQ分支。 |
| V5 | `sdk/cube_defect_quant_v1/scripts/vendor_code/data_reader.py` | 8–46 letterbox参数；49–100 BGR→RGB及(mean,scale)处理；109–119 reader，最多800样本、os.listdir原序。 |
| E1 | `sdk/blue_conveyor_state/board_validation/BOARD_VALIDATION.md` | §平台问题、§数值与性能：ArmNN32.1初始依赖SoftNPU设备节点；实板7个HardNPU workload，Pool/Reshape走CpuAcc；历史初始数值不合格。 |
| E2 | `sdk/blue_conveyor_state/board_validation/PRECISION_FIX.md` | 3–15 SDK特定floor/乘数截断与双重舍入修正；19–40最终133图板端回归及1 LSB残差；63–70原始证据索引。后续结论优先于E1。 |
| E3 | `sdk/triangle_defect_npu_v28/README.md` | 13–18浮点特征+UINT8头拆分、全网PTQ精度损失及实板3个HardNPU事件；部署特例。 |
| E4 | `sdk/triangle_defect_npu_v28/reports/board_debug/tmp/triangle_image_003.log` | 3–9：FP32 features被解析为CPU辅助，包含DepthwiseConvolution2d；日志后续多处depthwise。不是“depthwise数学上不能量化”的证据。 |
| E5 | `sdk/triangle_defect_npu_retrain_20261103/reports/FINAL_REPORT.md` | §3–5普通9Conv全网UINT8；§6 val146/146、test140/142，量化相对float无新增错误；§7 QEMU parser通过但实板未执行。报告摘要的“全NPU”需受§7证据边界约束。 |
| E6 | `sdk/cube_threecolor_quant_v1/reports/QUANTIZATION_REPORT.md` | 3–9 YOLOv8n六头UINT8，Add/MaxPool/Resize/Concat需辅助，PC量化对比通过，尚未上板。 |
| E7 | `sdk/triangle_defect_npu_v28/reports/board_vectors/cma_leak_analysis.md` | §Verified SDK implementation和§Validation boundary：ArmNN32.1特定库的CPU自定义CMA allocator所有权分析；该分析本身未做板端修复验收。 |

R=参考资料；V=本地厂商脚本副本；E=已有工程证据。下表的“已证实”严格限定于记录版本、图与阶段；不能提升成全DR1系列永久承诺。

## 算子与路径矩阵

| 类型 | 当前证据 | 可作的判断 | 下一步验证 |
|---|---|---|---|
| 普通Conv，group=1 | R1；V1；E2板端六层卷积；E5仅parser | **条件支持且有具体实板成功案例**；多层CNN并非天然不能量化 | 逐层shape/属性→当前parser→实际Alnpu放置→数值 |
| Depthwise Conv | V1 93–95提示DWConv不能被ALNPU加速；E4 CPU辅助 | **该检查器的硬核加速不支持**；不等于CPU QDQ不能生成或运行 | 可接受CPU时测分区；全硬核要求则等价改写或重训并复测 |
| Group Conv，1<group<Cin | V2拆成多个Conv、Split、Concat | **转换实现存在，端到端条件支持待验证**；不能保证新节点受后端支持 | 比较拆分前后float及量化语义、parser、copy/带宽和分配 |
| Gemm/FC | R1；V1要求transB=1、输入通道>2038提示拆分；E1有HardNPU FC | **条件支持，有特定实板路径** | bias scale、reshape边界、K/S数值与输出尺度 |
| Add/Concat/Pooling/Upsample/PReLU/YOLO相关 | R1是SoftNPU能力概述；V1只覆盖部分属性；E6需要辅助 | **不能当作全HardNPU支持**；当前PL、驱动、SDK和融合形态决定路径 | 检查真实SoftNPU IP/DT及当前backend；无法执行时CPU分区 |
| Reshape/Flatten | V1允许进入检查分支；E1实际Reshape对Alnpu不支持而走CpuAcc | **parser认可不等于硬核执行** | 保持量化尺度；确认视图/拷贝与布局转换语义 |
| Clip/ReLU6/融合激活 | E5 §7探针能解析Clip为Activation，但无实板证明 | **解析已证实，目标NPU执行未验证** | 精确探针+目标板；不要把ReLU融合经验扩展到任意Clip |
| InstanceNormalization/Erf | V1 221–223直接报该ArmNN不支持 | **当前检查器拒绝** | 查新版本或证明等价分解；没有证据时明确缺口 |
| 动态shape、RNN/Transformer/任意新增算子 | R1泛述不能提供具体层/shape/runtime支持证据 | **未验证** | 导出静态小图、当前工具检查、parser与实板 |

## V1 的 Conv 静态筛查条件（非充分支持判据）

- input/output/filter均4维；输入H/W为1..1023；Cin>4095提示拆分。
- dilation若声明须为[1,1]；核为方形1/3/5/7；stride为[1,1]或[2,2]，stride2时核须为3。
- 必须明确kernel_shape/pads/strides。pads检查允许全方向(k−1)/2，或 `[0,(k−1)/2,0,(k−1)/2]`；不能简写成“所有padding均支持”或“仅对称padding”。
- DWConv分支仅比较显式group与Cin，故group=1且Cin=1的单输入通道卷积也会被该脚本标为DWConv。这是退化形态的判定边界：不能据此断言所有group=1灰度卷积无法加速，也不能保证其支持；需记录原始属性并用当前backend的小图实测。
- 检查器只查部分规则、有分支遗漏/拼写差异、对前后继索引有假设，也可能只打印Warning而退出0。必须读日志并结合实际运行库，不用退出0证明全模型可部署。

INT8/UINT8都在R1产品概述中。V4的UINT8分支是权重/激活QUInt8 QDQ；INT8分支是QInt8、per_channel=True、对称选项。它们不是相同方案；是否被目标parser/backend接受仍需验证。不要把通用ORT或昇腾CANN文档当作安路SDK依据。

