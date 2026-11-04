# 裸机、BSP 与 FD

## 从硬件平台到应用

资料事实：[S1] §01 的 445–463、563–617 行给出 TD→HPF→FD 流程；[S4] §4–5 确认 Platform 是 Makefile 项目、通常产生静态库，Application 依赖 Platform、通常产生 ELF。

1. 从当前 TD 工程确认正确顶层、PS IP、DDR/PLL/PSIO、PL 地址分配与外设连接。需要 PL 时完成 bitstream；导出 `Project → Export Hardware Platform`，确认是否勾选 `Include bitstream`。
2. 在 FD 新建 Platform Project，选择实际 HPF、CPU 类型和 OS 类型。通过 `system.mss` 的 BSP View 检查 OS、Drivers、Library；需要 lwIP 等库时先在平台启用，随后创建对应 APP。
3. 优先由当前 BSP 的 `Import Examples` 找到实际驱动样例，保留其版本和配置。不要把教程中的驱动名称、枚举值或函数参数原样移植到不同版本 SDK。
4. 修改硬件后，用 `Change Reference HPF` 更新同架构平台并构建平台和 APP；比较生成地址宏、初始化代码、链接脚本与 HPF。若架构改变，创建匹配平台，不用宏强行覆盖旧 BSP。
5. 查 APP Makefile 和 `.cproject` 的 BSP 路径、CPU、ABI、异常级别、链接内存、库配置。工具链 `--version` 和实际编译命令是证据，界面工程名不是 ABI 证据。

工程佐证 [P1] 中现有 FSBL Makefile 使用 `DOWNLOAD=ocm`、`ARMv8_EL=EL3`、按芯片与 ABI 选择链接脚本；这些只证明该工程的配置，不能成为全部 APP 的默认值。检查 ELF 的机器类型、入口和段布局，确保与加载位置、DDR 初始化状态、堆栈和保留区一致。

## JTAG/OpenOCD/GDB 的调试顺序

[S1] 793–835 行描述：调试器扫描 → 可选 platform init → 可选 FPGA 编程 → GDB 加载 APP → 停在入口。

- 先确认物理下载器、驱动、实际启动模式与连接占用。教程 One Cable/AL-LINK 路径与 CH342 串口控制台是不同功能，不因串口能打印就认定 JTAG 可用。
- 从当前工程选择 ELF、符号与 bitstream；用 FD 自带调试配置核查 OpenOCD/GDB 路径和 config。不要凭其他厂商经验生成替代配置。
- DDR 中 APP 首次调试前需要已正确初始化平台。教程通过 `Run Platform Init` 执行平台的 `soc_plat.tcl` 初始化时钟、DDR、PSIO；仅在已确认应用运行于非 DDR 且自己正确调用初始化等条件时，才省略该阶段。
- 先验证能停在 APP 入口，随后逐步验证串口、定时器、目标外设。读取内存失败时先暂停 CPU，再核对地址映射和初始化，而不是反复写同一寄存器。
- 教程版本明确提醒 ISR 断点会触发 GDB 堆栈解析/OpenOCD 异常（[S1] 826–827）。对这些版本用计数器、事件标志、缓冲区或中断外断点采样；是否已在新版本修复需实测。

**版本冲突必须显式处理。** 旧 [S4] 187–189 行写 One Cable 的 bitstream 预下载选项尚未实现；[S1] 638、806–820 行介绍该选项。查实际 TD/FD 版本和当前调试日志决定操作路径，不能声称所有版本都支持或都不支持。

## FSBL 与持久启动

[S1] 920–989 行：BootROM 选择启动设备并加载 FSBL；FSBL 从片内内存运行、初始化 DDR/PLL，必要时配置 PL，再交给下一阶段（例如 U-Boot 或用户 APP）。创建 FSBL 用当前平台的 FSBL 模板；教程出现过 `FSBL_V1_0`，模板名以安装版本为准。

使用 FD `Create Boot Image` 前核对每个分区的类型、处理器、加载/执行地址、bitstream、ELF 和先后关系；沿用已验证项目描述文件并审查差异，不依据文件扩展名猜测镜像布局。导出产物清单和 SHA-256，保留上一份可用镜像。真正写入 Flash/SD/eMMC 前，要处于当前用户已授权的上板任务范围并确认目标设备；文档整理本身不执行烧录。

## 外设开发判据（工程建议）

对 GPIO/定时器/UART/I²C/SPI/SD/MMC/CAN/Ethernet 等先追踪 `system.mss → 生成的设备配置 → 当前 HAL API → 最小 demo`。逐项核对：实例号、PSIO/PLIO pinmux、外部电平、时钟/复位、驱动模式、传输长度和超时、中断/轮询、错误返回。测试至少有一个外部可观察结果，不能只依赖函数返回成功。

网络应用先验证 MAC/PHY 时钟、复位、MDIO 与实际链路，再验证 IP/ARP/ICMP，最后测 TCP/UDP；lwIP throughput 需区分 CPU、缓存维护、DMA 和网络协议开销。高级 SDK 资料 [S2] 含 FDMA、lwIP TCP/UDP、视频/采集链路，可按任务查对应章，不能从单个 demo 的吞吐推断全板峰值。
