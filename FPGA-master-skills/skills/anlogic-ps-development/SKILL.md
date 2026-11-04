---
name: anlogic-ps-development
description: 开发安路 DR1 FPSoC 的 PS 裸机、BSP、FSBL 或 Linux 系统与驱动，核对 HPF、FD、启动镜像和设备树的一致性；适用于 MLK F3P-CZ02/DR1M90 工程，其他 DR1 型号先确认处理器与板卡差异。
---

# 安路 PS 开发

从当前硬件设计建立软件平台和可复现产物。此技能总结随包参考资料中的工作流；示例路径和旧工具版本是定位线索，不能替代当前工程配置。资料定位见 [来源索引](references/sources.md)。

## 先确定软件契约

读取实际工程而非从板卡简称推断：器件完整型号、ARM/RISC-V、CPU 核、裸机或 Linux、32/64 位 ABI、运行内存、工具版本、目标外设、HPF 来源。UG1214 区分 DR1M 的 APU 与 DR1V 的 RISC-V；不要把 M 系列的 ARM 链接脚本或 GIC 设置用于 V 系列。

检查 `.al`、导出的 `.hpf`、FD `.project/.cproject`、`system.mss`、应用 Makefile/链接脚本以及平台初始化文件，建立链条：

`TD 硬件工程 → HPF/bitstream → FD Platform/BSP → APP/FSBL → 启动镜像 → 板端运行证据`

HPF 地址/时钟/PSIO/CPU 发生变化时，先更新 FD 的硬件引用并重新生成、构建平台，再构建应用。`Change Reference HPF` 需要与平台创建时的架构一致。只重新 make 旧 APP 不会自动完成硬件到 BSP 的更新。

## 选择工作流

- 新建或修订裸机/BSP、OpenOCD/GDB 调试、生成 FSBL：读 [裸机与 FD](references/baremetal-fd.md)。
- Linux 移植、U-Boot/内核/DT/rootfs、驱动和应用：读 [Linux 与启动产物](references/linux-boot.md)。
- 寄存器读不通、DMA 数据错、PL 中断异常或系统在某启动阶段停住：使用同包 `anlogic-soc-debug`；未安装时按上述链条和故障阶段进行证据定位。

## 使用已配置的 FD MCP

若 `anlogic-fd` MCP 可用，先用 `environment_info` 确认安装路径，再用 `list_projects` 与 `project_info` 选择现有 FD 工程、平台引用和构建配置；根据实际 ABI 选 `aarch64`、`aarch32`、`aarch64_linux` 或 `riscv`。只有需要运行编译时才 `build_project`，通过 `job_status/read_job_log` 读结果，再核对 ELF/链接输出与时间戳。用工具返回的 job ID，避免猜测。

此 MCP 构建的是已有生成 Makefile，不能代替从 HPF 生成 BSP。`dry_run` 使用 `make -n`，Makefile 展开和递归 make 仍可能执行动作；仅对已检查和信任的工程使用。硬件平台重生成使用当前 FD 功能或已验证脚本，不虚构 MCP 接口。编译通过只证明产物生成，不等于已经下载或运行成功。

## 交付可核对的结果

按任务规模保留：硬件/HPF 与 BSP 的对应关系、工具和编译配置、构建日志、产物路径与散列、链接内存范围、已执行测试及结果。涉及启动时记录启动介质和板端实际启动链；缺少上板环境时明确只完成构建。外设开发的最小验收包括正常传输、超时/错误路径、重复运行和复位后恢复。
