# PL 资料证据与适用范围

下列路径均相对用户的资料根目录 `referrence/`。PDF 页码为阅读器的 1-based 物理页，部分手册印刷页不同。ZIP 内路径用 `!` 分隔，行号为 UTF-8 解码后的 1-based 行。原手册与完整 demo 未复制进技能；源文件不存在时应请目标环境定位对应资料，不能声称再次核验。

## E1 — TD 图形工作流

来源：`01_start/01_start_fpga/00_for_starter/米联客-安路FPGA TD软件快速入门课程.pdf`，Rev2026，2026-04-03。

- p6：教程软件版本为 `TD_6.2.2_Release_2026.1_SP2_200067`，Windows 10/11 64bit。仅用于版本溯源。
- p25：示例选择 DR1 / DR1M90GEG400；不是用户新工程的默认器件。
- p32–35：IP Catalog 创建 PLL，生成 `.v`，右键 `.v` 的 Show Instantiation Template 可取例化模板。
- p36–39：Add Sources 支持 Verilog/SystemVerilog/VHDL、IP ipc/xml 和 adc/sdc；IO 向导先做 Read Design，根据原理图填写 Location、IOStandard 等。
- p44–49：时钟约束向导、PLL 时钟约束、保存 `timing.sdc`。
- p50–51：编译、Log Tree、资源利用及 STA 报告入口。
- **勘误：p44 文字将 25 MHz 与 10 ns/5 ns 翻转并列。** 按频率公式 25 MHz 应为 40 ns 周期；10 ns 对应 100 MHz。先确定实际时钟来源，不从矛盾文字选择一个值。

## E2 — 工程组织与 IO 示例

来源 ZIP：`03_demo/3-1_ex_fpga/3_1_01_fpga_base/3_1_01_fpga_base.zip`。

- `!02_fpga_start/fpga_prj.al`：L5 TD 6.2.190657；L7–11 Family/Device/Speed；L12–53 分类引用 RTL、ADC、SDC、IP xml；L60–64 顶层 `pll_test`；L71–87 run 为 `syn_1`/`phy_1`。
- `!02_fpga_start/uisrc/04_pin/fpga_pin.adc` L1–3：`set_pin_assignment` 的 Location、IOSTANDARD、PULLTYPE、DRIVESTRENGTH 语法；示例时钟 H16/LVCMOS33，LED T19/R19/LVCMOS18。只证明此 demo 使用这些值，迁移必须对照实际板卡和封装。
- `!02_fpga_start/uisrc/01_rtl/pll_test.v` L33–60：PLL wrapper 连接、lock 与计数器。该教学例程不是经过本技能完整质量验收的生产模板，尤其不要把直接异步释放或复位分支阻塞赋值当作统一最佳实践。

## E3 — SDC 与仿真一致性证据

同一基础 ZIP：

- `!02_fpga_start/uisrc/04_pin/timing.sdc` L1–5：实际出现 `create_clock -name {sys_clk} -period 10.000 -waveform {0.000 5.000} [get_ports {I_sysclk}]`、`derive_clocks`、`rename_clock`。最后的层次对象与当前 IP 实例有关，不可跨工程硬编码。
- `!03_td_modelsim_sim/uisrc/02_sim/sim_top_tb.v` L1、L23：timescale 为 1ns，`always #20`，输入频率为 25 MHz。
- `!03_td_modelsim_sim/uisrc/03_ip/ui_pll/ui_pll.v` L12–17、L51：生成信息为输入 100 MHz、输出 200 MHz、FIN="100.0"。与上述 TB 不一致，应按本次设计修复后运行。
- 03 示例的 TB 有 `I_sysrst_n` 端口，其自身 `uisrc/01_rtl/pll_test.v` L2–6 也有该端口；02/04 顶层只有时钟和 LED。不能混用不同示例的 TB 与 RTL。

## E4 — DR1 PLL

来源：`02_hardware/hardware_MLK_F3P_CZ02_DR1_20250526/hardware_MLK_F3P_CZ02_DR1_20250526/06_芯片手册/01_DR1/UG1206_安路科技DR1系列FPSoC PLL用户手册-米联客.pdf`，UG1206 v0.1，2024-01。

- PDF p5–6（正文 p4–5），§1–2：PL PLL、7 路输出及锁定状态等功能介绍。
- PDF p8（正文 p7），§2.2 表2-3：`pllreset` 高有效；`drp_rstn` 低有效；不要根据业务 RTL 信号名猜原语极性。
- PDF p19（正文 p18），§2.5.2：LOCK 指示相位/频率对齐；上电自动锁定，输入停止/相位或频率改变可能失锁。此信息不能证明用户电路复位释放已跨域同步。
- 动态重配置/相移的完整驱动与时序尚未在本技能中验证；需求涉及这些特性时继续读手册对应章节及当前生成 IP，不给未核验寄存器值。

## E5 — DR1 IO

来源：同上 `06_芯片手册/01_DR1/UG1210_安路科技DR1系列FPSoC IO用户手册-米联客.pdf`，UG1210 v0.1，2024-01。

- PDF p8（正文 p5），§2.3：bank VCCIO 需要匹配配置的 IO 电平标准。
- PDF p8–11（正文 p5–8），§2.5：位置、IOSTANDARD、SLEWRATE、DRIVESTRENGTH、PULLTYPE、PACKREG、INDEL、OUTDEL 的约束入口。
- PDF p4（正文 p1）的注释：使用 ODELAY 需启用 IO 内寄存器；PL 全局时钟引脚 T 端不能用 IDELAY；PS IO 的延迟能力不同。适用于该手册版本，实际器件仍需核验。
- **封装边界：PDF p7（正文 p4），§2.2 的 BANK 表列出 EG484 器件；不能把该分布直接用于 EG400。** 新引脚任务需目标封装 pin list 和原理图。
- 文档个别例句拼作 `LVCOMS33`，而示例 ADC 使用 `LVCMOS33`；约束枚举以当前 TD 接受值和实际 IP/示例交叉核对，避免原样复制排版错误。

## E6 — TD 报告与命令证据

基础 ZIP 中：

- `!04_use_ChipWatcher/fpga_prj_Runs/phy_1/fpga_prj.tcl` L45、L97、L123–149、L172–179、L193–224：read_design/opt_rtl/opt_gate/opt_place/opt_route/bitgen 的生成流程；`report_area`、`report_timing_status`、`report_timing_summary`、`report_timing_exception`、`check_timing -verbose`、`report_clock_utilization`。这是生成的内部 flow，依赖 settings.cfg、运行变量和数据库，不能当作独立通用脚本直接 source。
- `!02_fpga_start/fpga_prj_Runs/phy_1/fpga_prj_pr.timing` L1–13：版本、器件、STA coverage=83.13%、Constraint File、Confidence=Routed、Both Slow/Fast；L25–43：SWNS/STNS/HWNS/HTNS 与各时钟；L45–57：Fmax/最小周期仅为时钟域内统计，不能替代跨域总时序；L75–87：路径的 start/end、setup、cell/net 延迟占比。
- 同目录 `fpga_prj_chk.timing` L3–16、L88–98：无时钟、无效约束、PLL/clock buffer、组合环、软 FIFO、例外覆盖、IO delay 检查，以及两个无 output delay 的 LED 端口。
- 同目录 `fpga_prj_phy.ts` L1–2：正 setup/hold slack；与上述覆盖率/遗漏项并存，说明不能只读 `.ts`。

## 工具能力来源

当前会话提供 `anlogic_td` MCP：`environment_info`、`list_projects`、`project_info`、`build_project`、job/log/report 读取和 `run_tcl`。2026-11-04 环境检查发现 `D:/FPGA/TD/bin/td_commands_prompt.exe` 与 `td.exe`；这是本机观测，不作为安装路径要求。构建接口支持 elaborate/synthesis/implementation/bitstream，成功不保证时序闭合。未提供仿真、ChipWatcher 捕获或烧写的专用 MCP 调用；不能虚构这些接口。
