# ModelSim 与 ChipWatcher 可执行流程

来源编号见 [evidence.md](evidence.md)。菜单和参数范围来自 TD 6.2.2 时代教程；当前安装版本有差异时以当前 UI/帮助为准。

## ModelSim

教程 [D1] 指定 `TD_SIM_MODEL` 指向 **实际 TD 安装的仿真库目录**，不是 TD 根目录。优先给本次进程设置变量；无需为一次任务改变机器全局环境。先验证模型文件存在、器件系列正确，再启动 ModelSim。

在副本的 `uisrc/02_sim` 工作目录，核查 `compile.f` 后运行教程流程：

```tcl
do cmd.do
```

本地 demo 的 cmd.do 实际执行如下核心步骤 [D2]：

```tcl
vlib work
vmap work work
vlog -timescale 1ps/1ps -sv -f compile.f
vsim -voptargs=+acc work.sim_top_tb
```

这是该测试顶层的入口，不是要求所有工程顶层都叫 sim_top_tb。原 cmd.do 仅打开窗口，`run -all` 被注释；教程用 10 us 单步观察。根据实际启动/协议时间选择仿真窗口，存在持续时钟的 TB 应有完成条件/超时，避免无条件 `run -all` 永久运行。

03 示例的已知核验点：

1. TB timescale=1ns，`always #20` 为 25 MHz；其 PLL 生成 wrapper 标注/FIN 为 100 MHz → 200 MHz。确认本次目标后同步修改 TB/PLL，不能把时序错配造成的锁相问题当业务逻辑故障。
2. `compile.f` 引用 `$TD_SIM_MODEL/ph1/ph1_phy_pll.v` 与 `ph1_logic_clk.v`，但该目录示例为 DR1。该现象需检查当前 TD 模型中实际 module 定义和兼容关系；**不能仅因路径名为 ph1 判定不可用，也不能直接假定可用。**
3. wrapper 实例化独立 `dr1_phy_pll_ui_pll`，清单只显式列 ui_pll.v、RTL、TB 及两个模型。查看是否通过 include/库配置提供底层模块；没有时补充当前生成的 `RTL/dr1_phy_pll_ui_pll.v` 及其所需器件模型。不能在模型报错时写一个空模块骗过 elaboration。
4. 03 顶层具有 `I_sysrst_n`，同 demo TB 与其匹配；02/04 顶层没有该端口。变量名虽带 `_n`，03 将它接到 PLL reset，高低语义必须沿 wrapper/原语核验。

排错顺序：首个编译错误 → module binding/库 → 时钟/复位 → IP lock/ready → 业务状态机 → 外部响应。波形包含最低限度因果链：输入请求、接受握手、内部状态、输出响应和错误状态。

## ChipWatcher

1. **准备可实现工程。** 教程要求先完成编译，然后 Tools → IP Catalog → Debug → ChipWatcher [D1 p67]。保存独立 IP 路径/名称。
2. **选择采样与探针。** 采样时钟选能持续运行、与待观察协议相关的域。多域信号的同步与采样混叠是设计问题；单一采样时钟不能保证重建所有异步边沿（通用建议）。若要看 PLL 未锁期间，优先用仍在运行的参考域观察同步后的状态；用未启动的 PLL 输出采样时，可能根本没有数据。
3. **按假设设置宽度/深度。** 教程范围：1–256 probes、每 probe 1–1024 bit、深度128–65536；较深采样消耗更多 RAM。Input/Output Pipe Stages 最多6级，增加可缓解插入后的时序压力但消耗寄存器。当前器件、版本和剩余资源仍会约束可实现配置 [D1 p69]。
4. **生成并连接。** 当前生成的 wrapper 是端口真值。基础示例 cwc probe0=1bit lock、probe1=2bit LED、probe2=26bit counter、clk=clk0 [D2]。这些只是示范；不要向新设计强加相同探针。
5. **重建并查 STA。** 重新 Generate Bitstream，阅读本次时序和资源；必要时减少 probe/深度或调整流水。Pipe Stages 会影响观测延迟，记录配置后再比较数据时间关系（通用分析建议）。
6. **绑定 bit。** 打开 ChipWatcher 的 Watch，下载包含当前 ChipWatcher 配置的 bit。教程明确旧/不匹配 bit 会无法触发并报错 [D1 p71]。核对当前文件哈希/时间与工程，不凭文件名相同作判断。
7. **观察与触发。** Add Signals 添加观测信号；Add Triggers 中仅会出现启用了 Trigger Enable 的 probe。先用一个已知会发生的 high/low/edge 条件，确认能从 Wait for trigger 进入 Reading data/Finished，再添加复杂条件。
8. **设置窗口。** 教程提供 Number of windows、Window of depth、Trigger Position in window、Basic AND/OR。触发位置应留下足够触发前的原因和触发后的响应；多窗口将总深度分摊。实际窗口深度/位置范围以当前 UI 为准 [D1 p73–74]。
9. **复核故障。** 在波形中标出输入事件、首个违反预期的周期和后续后果；保留配置/bit 身份。若全常值，分辨没有运行、重置未释放、probe 接错、数据采集未启用或条件从未成立，而不是立即扩大深度。

## 工具边界

当前已提供的安路 TD MCP 可以构建和读报告，未提供 ChipWatcher probe 编辑、JTAG 扫链、下载与导出波形接口。可通过 TD GUI 完成上述步骤，或使用该版本已验证的现有 Tcl；没有文档证据时不要发明 Tcl 下载命令。缺少现场连接时，只报告已完成的工程配置和构建，板测待实际硬件。
