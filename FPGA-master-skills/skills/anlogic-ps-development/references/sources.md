# 来源与适用范围

整理时间：2026-11-04。主要来源根目录为原工作区 `D:/PRJ/referrence`；迁移后由用户提供的新资料根目录替代。下面路径均相对于该根目录。行号指当时同名 UTF-8 `.txt` 提取文本，不是 PDF 页号。找不到文本时按同名 PDF 的章节检索。技能包保留摘要，不附整套厂商资料。

| 编号 | 相对路径 | 已阅读证据 |
|---|---|---|
| S1 | `03_demo/3-0_book_all/3_2_01_米联客2024版FPSoc_SDK入门篇.txt` | 445–463 TD/FD/HPF；563–617 平台与 APP；755–835 BSP、初始化、调试；920–989 启动；1012–1047 FSBL/BOOT；4687–4848 AXI/cache |
| S2 | `03_demo/3-0_book_all/3_2_02_米联客2025版FPSoc_SDK高级篇.txt` | 294–301 AXI BRAM；643–672 AXI-IIC、地址；852–890 FDMA；1644–1655 cache/总线初始化；1913–1921、3623–3631 cache 与网络数据读取；章节目录列举视频、TCP/UDP与采集应用 |
| S3 | `03_demo/3-4_ex_soc_linux/ebook/3-4-01_米联客2024版安路F3P-CZ02-FPSoc Linux基础入门篇.txt` | 150–164 VM/开发包；180–235 系统移植与构建；1082–1099 串口启动日志 |
| S4 | `02_hardware/hardware_MLK_F3P_CZ02_DR1_20250526/hardware_MLK_F3P_CZ02_DR1_20250526/06_芯片手册/01_DR1/FD_DR1_User_Guide_V1.1-米联客.txt` | 130–204 Platform、APP、Debug；与较新教程的 One Cable 功能描述存在版本差异 |
| S5 | `03_demo/3-4_ex_soc_linux/ebook/3-4-02_米联客2024版安路F3P-CZ02-FPSoC Linux驱动开发篇.txt` | 1376–1378、1675 MMIO；2535–2569 DT/构建；3037–3053 匹配；6530–6635 PL IRQ及驱动 |
| S6 | `02_hardware/hardware_MLK_F3P_CZ02_DR1_20250526/hardware_MLK_F3P_CZ02_DR1_20250526/06_芯片手册/01_DR1/UG1214_安路科技DR1系列FPSoC产品说明书.txt` | 393–480 DR1M/DR1V；1122–1140 APU/cache/ACP；1276–1277 GIC；1400–1470 RPU |

补充工程证据（不是厂商手册）：

- P1：`D:/PRJ/soc_sdk/soc_fsbl/Makefile`，读取 1–119 行，表明该实例的 OCM、EL3、ABI 与链接脚本选择。路径与配置只适用于该实例。
- P2：`D:/PRJ/soc_sdk/soc_base/dr1m90_fpsoc_cpu0/inc/al_cache.h`，53–73 行给出 Start/End 形式的 cache API；invalidate 的注释明确 dirty line 会先 flush。
- P3：`D:/PRJ/soc_dts/kernel-dts/anlogic-dr1.dts`，1–65 行显示 DTS 包含基础和 softip 文件，包含 memory/CMA/串口使能等配置。不能把此文件的内存容量和保留区复制到其他板卡。

文中标为“工程建议”的缓冲区所有权、资源生命周期、产物散列和验证矩阵是基于上述资料提炼的实践建议，未声称在所有硬件/内核版本上做过实测。寄存器地址、IRQ、电压、下载器行为、SDK函数语义应回到当前生成文件和对应版本手册复核。
