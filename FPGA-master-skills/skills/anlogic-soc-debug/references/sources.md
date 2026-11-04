# 联调资料来源

整理时间：2026-11-04。路径相对于原参考资料根 `D:/PRJ/referrence`；迁移后替换根目录。行号引用同名文本提取文件，非 PDF 页码；无文本时使用同名 PDF 对应章节。随包只保留摘要。

| 编号 | 相对路径 | 定位 |
|---|---|---|
| D1 / S1 | `03_demo/3-0_book_all/3_2_01_米联客2024版FPSoc_SDK入门篇.txt` | 793–835 FD调试阶段与ISR断点；920–989启动；3755–3758 PL中断；4687–4721 AXI通路；4770–4848 cache、宽度、转换器 |
| D2 | `03_demo/3-4_ex_soc_linux/ebook/3-4-02_米联客2024版安路F3P-CZ02-FPSoC Linux驱动开发篇.txt` | 1376–1378 MMIO；6530–6552硬件/DT；6580–6631 IRQ取得与驱动 |
| S2 | `03_demo/3-4_ex_soc_linux/ebook/3-4-01_米联客2024版安路F3P-CZ02-FPSoc Linux基础入门篇.txt` | 180–235 Linux移植和启动；1082–1099串口启动信息 |
| D3 | `02_hardware/hardware_MLK_F3P_CZ02_DR1_20250526/hardware_MLK_F3P_CZ02_DR1_20250526/06_芯片手册/01_DR1/UG1214_安路科技DR1系列FPSoC产品说明书.txt` | 393–480器件差异；1122–1140 APU/cache/ACP；1276–1277 GIC；1400–1470 RPU |

工程证据 P1：`D:/PRJ/soc_sdk/soc_base/dr1m90_fpsoc_cpu0/inc/al_cache.h`，53–73 行。签名和 dirty-line 行为来自此实例头文件；未在本次整理中执行板上实验或验证汇编实现。

已知资料陷阱：

- D2 的 DTS `<0 82 4>` 与驱动 `IRQF_TRIGGER_RISING` 触发语义不一致。常用 Linux IRQ 值需与当前内核 dt-bindings/interrupt-controller/irq.h 复核，实施时统一定义。
- D1 的64位传输示例用8字节步长读32位值，不能用这个示例证明所有有效字节正确。
- D1 的“读前invalidate、写后flush”是简单裸机场景摘要；P1 的invalidate注释提示dirty行会先flush，DMA并发必须先解决缓冲区所有权。
- D1 当前表格/文字不等同于所有生成IP参数，地址和AXI宽度应复核HPF与实际连线。

检查清单、Linux DMA API选择、缓冲区状态机、分层假设检验与回归矩阵均为工程建议，不冒充厂商特定版本的完整支持声明。此技能没有承诺自动修复未知板卡、未知IP或未提供源码的二进制驱动。
