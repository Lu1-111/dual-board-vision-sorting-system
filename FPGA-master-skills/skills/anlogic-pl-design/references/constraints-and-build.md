# 约束、构建和时序诊断

证据编号对应 [evidence.md](evidence.md)。以下带“通用建议”的内容是数字设计方法，不是宣称某条安路工具命令可用。

## 先读取工程

有 MCP 时按顺序：

1. `anlogic_td.environment_info()` 确认可执行文件；用 `list_projects` 查目标目录。
2. `project_info(project=<实际 .al>)` 读取器件、顶层、源文件和 run。
3. 在设计改动前记录当前可复现基线；在用户工作副本编辑源与约束，不写参考 ZIP 原件，不依赖 ZIP 中旧 run 产物验收新设计。

无 MCP 时从 `.al` 的 XML 读取 E2 所列字段，并在 TD GUI 打开同一工程。新工程通过 TD 的创建向导设置实际器件，先从官方/本地兼容示例复制结构；不要把旧 `.al` 内的绝对 `F:/...` Path 当作新目录。

## ADC

资料证实的基本形式是 `set_pin_assignment {端口} { LOCATION = 管脚; IOSTANDARD = 电平标准; }` [E2, E5]。给每个外部端口建可追溯表：RTL 名称/方向 → 原理图网名/连接器 → FPGA pin → bank/VCCIO → 外设电平 → ADC 值。

- 完成 Read Design 后用 IO 约束向导核验端口和合法选项；未见端口时先查顶层、源文件启用状态和拼写 [E1]。
- 示例的 LVCMOS33 时钟和 LVCMOS18 LED 表明不同 IO 可以有不同标准，不能“全板统一 LVCMOS33”。
- 物理 IO delay 属性 `INDEL/OUTDEL` 和 SDC 的外部输入/输出延迟描述不同的事，不可相互替代。接口需要什么延迟须由外设/板级时序确定（通用建议）。
- 无原理图、封装或 bank 证据时，可以产出待填端口表及纯逻辑设计；不生成看似完整但臆造的 pin constraint。

## SDC

先确认实际输入频率；例如经硬件确认是 100 MHz 且顶层确有 `I_sysclk` 时，E3 的实例才适用：

```tcl
create_clock -name {sys_clk} -period 10.000 -waveform {0.000 5.000} [get_ports {I_sysclk}]
derive_clocks
```

`derive_clocks` 和示例 `rename_clock` 是已在本地 TD 6.2.190657 工程出现的命令。更换 TD/器件/IP 后检查实际生成时钟对象和报告；不要照抄 `X_ui_pll/...clkc[0]` 层次路径，不能将 `derive_clocks` 运行无报错视为所有时钟都已覆盖。

通用设计建议：

- 列出基准时钟、PLL 输出、逻辑分频、外设返回时钟和它们的关系；修改 PLL 后同步更新期望频率。
- 同步外设输入/输出应从外设 setup/hold、板级偏斜和采样关系推导 min/max 约束；实际 SDC 参数语法先查当前 TD 帮助或现有验证文件。
- false-path、异步时钟关系、multicycle 都要记录原因、起终点及设计保证；CDC 电路仍需单独验证。不能对所有跨域一刀切，也不能仅为报表变绿添加例外。
- 本资料核验未覆盖各 TD 版本完整 `create_generated_clock`、`set_clock_groups`、CDC 检查或接口时序选项语义；任务需要时先查当前版本帮助，不把其他厂商语法直接套用。

## 构建入口

MCP 调用参数示例（将工程、数据库、run 替换为读取到的真实值，不原样使用占位文本）：

```json
{"project":"<实际绝对路径>.al","stage":"elaborate","device_database":"<目标器件数据库>","synthesis_run":"<活动综合run>"}
```

之后按所需阶段调用 `build_project` 的 `synthesis`、`implementation`、`bitstream`。只传接口实际支持的字段；`rebuild` 会重置所选保存 run 结果，只有需要重建时使用。DR1 可使用接口的 DR1 默认数据库；其他系列需先确认，不套默认。

保存返回的 `job_id`，用 `job_status` 检查，再用 `read_job_log` 的 offset/next_offset 读所需日志段；不要不断重启同一失败任务。首错为缺源/黑盒时先修源集合/IP；引脚、电平或时钟错误回到约束依据；综合后无逻辑先核验顶层和输出可观察性。

`run_tcl` 只执行已检查且属于当前任务的 Tcl。E6 的 run Tcl 依赖 TD 内部变量，适合阅读和定位报告，不是脱离工程的启动脚本。TD TCL 进程具有本机权限，脚本可能修改工程或硬件，先读其内容和 include/source 路径。

## 报告判读

用 `list_reports(directory=<工程目录>)` 发现当前产物，再读取内容、时间戳、顶层、器件与 Constraint File，确认来自此次 run。E6 的文件名为版本示例，不假定所有工程都有相同文件名。

| 报告 | 要读取的证据 | 下一步 |
|---|---|---|
| `*_pr.timing` | Confidence=Routed、检查角、STA coverage、Clock Summary、SWNS/STNS/HWNS/HTNS、最差路径 | 核对目标周期与跨域路径，判断 setup/hold 违反 |
| `*_chk.timing` | 缺时钟、无效约束、PLL 频率、无/部分 IO delay、组合环 | 每个非零项修复或按接口说明可接受原因 |
| `*_exception.timing` | dominated/shadowed/ignored 例外、实际覆盖路径 | 排除无效或过宽例外 |
| `*_phy.area` | 逻辑、寄存器、RAM/DSP/IO 等资源及利用率 | 查异常增长、调试 IP 成本、器件容量 |
| `route_clock_utilization.txt` | 实际时钟资源使用 | 查预期时钟是否走正确资源 |

最终报表先给需求频率下 setup/hold 统计和覆盖缺口，再列单域 Fmax。Fmax 是特定路径集合的估计，不能证明整个多时钟系统可按该频率工作。调试 IP、流水级或约束任何一项变化后都应重做受影响阶段和对应功能验证。
