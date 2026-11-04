# 调试来源与已验证边界

所有路径相对 `referrence/`；PDF 页码是 1-based 物理页，ZIP 条目行号是原始文本的 1-based 行。资料的存在和阅读不等同于本机实际跑过仿真/硬件验证。

## D1 — TD 仿真与在线调试教程

`01_start/01_start_fpga/00_for_starter/米联客-安路FPGA TD软件快速入门课程.pdf`，Rev2026，2026-04-03。

- p6、p57、p66：TD_6.2.2_Release_2026.1_SP2_200067、Windows 10/11 64bit；p57 的示例仿真器为 ModelSim win64 10.6d se，不要求用户更换成此旧版。
- p57–59：仿真前安装 ModelSim；TD_SIM_MODEL 指向 TD 仿真库目录。
- p60–65：cmd.do、compile.f、modelsim.ini、sim_top_tb.v 的职责；进入仿真目录、执行 do cmd.do、View → Wave、拖入信号、10us 单步观察。
- p66–70：ChipWatcher 可设置采样时钟、深度、trigger；Tools → IP Catalog → Debug → ChipWatcher；probe/深度/流水和数据/触发使能参数。
- p71：Watch 前完成 Generate Bitstream，下载包含当前 ChipWatcher 的 bit，否则无法触发并报错。
- p72–75：Add Signals/Add Triggers、Idle/Wait for trigger/Reading data/Finished、窗口/触发位置和 AND/OR；只有 Trigger Enable 的信号出现在触发列表。
- p21–23 是教程的器件识别和 bit 下载章节入口，本技能未验证下载器底层 Tcl API。

## D2 — 实际基础 demo

ZIP：`03_demo/3-1_ex_fpga/3_1_01_fpga_base/3_1_01_fpga_base.zip`。

| ZIP 条目 | 精确定位及证据 |
|---|---|
| `03_td_modelsim_sim/uisrc/02_sim/cmd.do` | L1–9 vlib/vmap/vlog/vsim；L15–17 开窗；L20 的 run -all 注释 |
| `03_td_modelsim_sim/uisrc/02_sim/compile.f` | L1–3 RTL/TB/ui_pll；L7–8 ph1 模型路径 |
| `03_td_modelsim_sim/uisrc/02_sim/sim_top_tb.v` | L1 timescale；L9–13 端口；L15–23 激励和 #20 时钟 |
| `03_td_modelsim_sim/uisrc/01_rtl/pll_test.v` | L2–6 有 I_sysrst_n；L13–18 接 PLL reset；L20–25 lock 控制计数 |
| `03_td_modelsim_sim/uisrc/03_ip/ui_pll/ui_pll.v` | L12–17 输入100MHz输出200MHz；L41 起实例化 dr1_phy_pll_ui_pll；L51 FIN=100.0 |
| `03_td_modelsim_sim/uisrc/03_ip/ui_pll/RTL/dr1_phy_pll_ui_pll.v` | 独立底层 wrapper 存在；仿真源集应核查它的解析来源 |
| `04_use_ChipWatcher/uisrc/01_rtl/pll_test.v` | L33–36 顶层只有时钟/LED；L60–65 三个 probe 和 clk 连接 |
| `04_use_ChipWatcher/al_ip/cwc/cwc.v` | L11–25 probe0/1/2 宽度分别1/2/26，实例化 ChipWatcher_cwc |
| `02_fpga_start/fpga_prj_Runs/phy_1/fpga_prj_pr.timing` | L1–13版本/目标/覆盖率/时序角；L25–43时钟与SWNS/HWNS；L55–57单域Fmax限制 |
| `02_fpga_start/fpga_prj_Runs/phy_1/fpga_prj_chk.timing` | L3–16检查类别；L93–98两个LED输出没有output delay |

已核验不一致：03 TB 输入25MHz与PLL FIN100MHz不同；ph1 模型命名和 DR1 wrapper 的组合需要当前 TD 库绑定核验；不同示例的复位接口不同。以上是待修复/验证项，不据此宣称整个示例都无法运行。完整 ModelSim 会话尚未在制作本技能时执行。

## D3 — PLL 极性与锁定

`02_hardware/hardware_MLK_F3P_CZ02_DR1_20250526/hardware_MLK_F3P_CZ02_DR1_20250526/06_芯片手册/01_DR1/UG1206_安路科技DR1系列FPSoC PLL用户手册-米联客.pdf`，v0.1，2024-01。

- PDF p8 / 正文p7，§2.2：pllreset 高有效，drp_rstn 低有效。
- PDF p19 / 正文p18，§2.5.2：LOCK 表示参考和反馈相位/频率对齐；输入停止或相位/频率变化可导致失锁。

## 制作时验证范围

已阅读以上 PDF 指定页及 ZIP 中原始工程、约束、RTL、仿真文件、生成 Tcl 和时序报告；只以简短流程和来源定位形式整理，没有复制完整手册。当前可用 TD MCP 的环境探测证明 TD 可执行文件存在；未进行本技能样例编译、ModelSim 仿真、JTAG 下载或实板波形捕获。CDC/复位同步/协议断言的部分建议属于通用数字设计方法，未冒称来自上述教程的完整 CDC 规范。
