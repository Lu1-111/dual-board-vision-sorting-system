# Linux、启动镜像与驱动

## 保持整条启动链可追溯

[S3] 第三章说明 `uisrc-lab-anlogic` 管理 U-Boot、kernel、DTS、rootfs、镜像和 SD 部署。教程标注的 TD/FD 版本（185–186 行）仅是该教程验证组合；迁移时记录现用版本及差异。

维护一张小表：

| 项 | 核对内容 |
|---|---|
| PL/HPF | `.al`、顶层、IP、地址、时钟、bitstream、导出时间与散列 |
| FSBL | 来源 Platform/HPF、CPU/ABI、DDR/PSIO 初始化、实际 ELF |
| U-Boot | 配置、设备树、启动脚本/环境、镜像和 DTB 加载地址 |
| 内核/DTB | kernel 配置、设备树输入、驱动版本、实际启动 DTB |
| rootfs | libc/ABI、所需动态库/固件、模块与内核版本 |
| 启动介质 | SD/eMMC/QSPI 的实际选择、分区、文件名、部署位置 |

修改 PL 地址、IRQ、时钟或复位后，同步修改相关驱动/设备树并重建必要产物；仅换 bitstream 可能使 Linux 继续按旧 DT 操作硬件。U-Boot DT 与内核 DT 分开核对。不要把某次成功的 PARTUUID、网卡名称、IP、盘符或存储地址固化为通用规则。

## 复现教程构建链

资料事实：[S3] 195–229 行把 `soc_fsbl.elf`、`system.bit` 及 DTS 输入交给开发包，随后配置环境并依次构建。以下是**从教程提炼的步骤**，执行前先定位、阅读本机对应脚本和参数，不自动运行一个拼接好的烧录命令：

1. 在当前 TD/FD 版本生成匹配的 HPF、Platform 和 FSBL。
2. 定位实际 `uisrc-lab-anlogic` 根目录；确认其 `boards/files` 输入目录、`scripts/drcfg.sh` 和脚本版本。教程约定 FSBL 名称与路径可能被脚本引用，改名须查调用方。
3. 在该根目录的 `scripts` 中查看并 `source ./drcfg.sh`，按当前包说明调用 `move_files.sh`、`make_rootfs.sh`、`make_uboot.sh`、`make_kernel.sh`、`create_image.sh`。选择与本次启动布局一致的 parameter 文件；教程 `parameter-tf-fat.txt` 不是全部工程通用配置。
4. 检查镜像内容、大小、地址重叠和文件时间，保留构建日志。构建成功与烧录成功分别记录。
5. 只有执行上板部署任务时才考虑 `make_parted.sh`/`deploy_image.sh`；先由设备枚举、容量、型号、挂载点与连接变化确认目标。教程的 `sdb` 是示例，不能照抄到用户主机。分区和格式化会清空目标盘。

[S5] 2566 行出现 `source /scripts/drcfg.sh`，而 [S3] 218 行给的是在包内 scripts 工作目录执行 `source drcfg.sh`。前者可能丢失路径上下文；以实际存在的包路径验证，不盲目执行绝对 `/scripts`。

## 驱动开发

设备树节点的 `compatible` 与驱动 `of_match_table` 对应；核对 `status`、`reg` 与上层 `#address-cells/#size-cells`、`interrupt-parent/interrupts`、时钟、复位、pinctrl 和 DMA 所需属性。资源地址必须来自当前设计/绑定；不能把教程中硬编码的基地址变成产品常量。

[S5] 第 6 章示范设备树平台驱动，第 16 章使用 `platform_get_irq()` 获取 Linux IRQ。驱动实现按当前内核 API 从平台资源取得 MMIO 和 IRQ，检查每个错误返回；不要把物理地址当作普通用户态指针解引用。教程给了 `ioremap/iounmap`，生产实现可按内核版本选择管理式资源 API。

在**与板端运行内核匹配的构建树、配置和工具链**下编译模块。上板诊断可先读 `uname -a`、`cat /proc/version`、`dmesg`、`lsmod`；若工具存在，检查 `modinfo` 的 vermagic。模块加载失败先看符号/版本/ABI错误；probe 未执行先看 DT 匹配和资源；probe 后失败再看时钟/复位、地址和 IRQ。`insmod`、驱动解绑或卸载属于运行状态变更，应在用户本次调试范围内执行，先确认没有被正在运行的应用使用。

## 应用与系统故障（工程建议）

- 应用“存在却无法执行”：检查 ELF 架构、动态加载器与库，再看挂载 `noexec` 和权限，不直接重编整个内核。
- 启动找不到 rootfs：对照实际 bootargs、分区 UUID、内核存储/文件系统驱动和挂载失败日志。
- 串口能进入 shell，但界面/网络没启动：分开验证设备节点、依赖库、启动环境与服务日志，不能把用户态失败归因于 bitstream。
- 性能验证同时记录稳态延迟、吞吐和 CPU/内存使用；剥离加载、初始化、图像拷贝和网络传输等成本。

只输出已验证阶段；没有连接板卡就写“构建/静态核对完成，待上板”，不要把历史教程演示作为此次执行证据。
