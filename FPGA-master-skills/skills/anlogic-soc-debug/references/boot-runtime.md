# 启动与系统运行排障

## 以最后一个可信阶段定位

| 最后证据 | 下一步检查 | 不足以证明的结论 |
|---|---|---|
| Windows无当前串口设备 | USB枚举、CH342通道、线缆与VM透传归属 | 不能说明PL配置坏了 |
| 串口无字节 | 当前串口/波特率/占用、上电/复位、对应PSIO和硬件连接 | 不能单凭静默判定CPU未启动 |
| FSBL日志停止 | 实际FSBL/平台、DDR/PLL、PL配置握手、下一镜像读取 | 不能只因ELF构建通过认定DDR正确 |
| 已进入U-Boot | bootcmd/bootargs、镜像/DTB加载范围、介质与文件、交接入口 | U-Boot可运行不证明Linux的DT有效 |
| 内核已启动但根文件系统失败 | root参数/UUID、存储与文件系统驱动、DT和分区 | 不应先重生成无关PL |
| shell可用但设备/应用失败 | 运行DT、驱动绑定、资源、ABI、库、服务环境 | 不等于整板功能通过 |

[S1] 的 BootROM→FSBL→SSBL说明及 [S2] Linux移植链给出分层依据。具体日志因版本不同会变化，不靠固定字符串做唯一判据。

## 一组有界的只读 Linux 取证

已连接用户指定板卡时可按需要执行，而非每次全量收集：

```sh
uname -a
cat /proc/version
cat /proc/cmdline
mount
cat /proc/interrupts
cat /proc/iomem
lsmod
dmesg | tail -n 120
```

若存在且允许读取，可核对 `/proc/device-tree/model`、`/proc/device-tree/compatible` 或 `/sys/firmware/devicetree/base`；设备树属性可能是 NUL 分隔/二进制，不能当普通数字文本直接比较。需要反编译时，确认板上或主机有匹配 `dtc`，并从实际运行树或启动 DTB 取证。日志和驱动可能存在读不到或工具缺失情形，准确报告即可。

U-Boot 可先查看当前环境与启动日志，不为排查盲目执行 `saveenv`、擦除Flash或重分区。JTAG/FD 调试先检查 OpenOCD扫描、platform init、bitstream加载、ELF加载四阶段，避免把调试器错误归为APP错误。

## 驱动与应用

- 无 probe：核对实际 DT 节点 `status/compatible`、驱动是否编入或模块加载，以及当前总线设备是否出现。
- probe 延迟/失败：记录 errno 和 dmesg，检查时钟/复位/IRQ/内存资源与依赖设备；区别延迟探测和永久错误。
- module 格式错误：比较运行内核、配置、符号版本、编译器/ABI与模块；不要靠强制加载绕过不匹配。
- 应用崩溃：保留最小输入、stderr、信号和堆栈；先验证ABI/动态库/设备权限，再追踪数据长度、对齐与资源生命周期。
- 外设不通：按电源/电平/连接 → pinmux → clock/reset → 控制器 → 驱动 → 应用排查。网口再分PHY/link与IP协议；I²C/SPI读写要先识别设备地址/模式及可能产生动作的寄存器，不能无差别扫描或写入。

## 反复出现的兼容性陷阱

1. 更新 bitstream，却沿用旧 HPF/BSP/FSBL/DTB。
2. 把芯片家族资料当成某器件实际具备的资源，尤其 ARM 与 RISC-V、ACP 与普通 HP 通路。
3. 把教材的盘符、GPIO号、中断ID、内存范围、MAC/IP、设备节点名作为全局默认。
4. 启动脚本或 U-Boot 环境从另一分区加载旧产物，导致“部署新文件却无变化”。
5. 断点/大量日志/关闭cache使故障消失，却未恢复实际运行条件回归。

恢复验证至少包括原始复现、一次冷启动或任务要求的复位方式、重复运行；涉及DMA/IRQ时追加长度边界/背靠背事件/超时恢复。不要把随机运行几次无错误写成所有边界均正确。
