# 传送带控制：BTS7960 + JGB37-520 + AB 编码器

2026-11-04：新增 A=Y6 / B=V10 外部编码器接线候选、固件能力检查及独立有界速度 PI 测试入口。尚未部署或完成实测标定，详见 [单轴闭环接入](ENCODER_CLOSED_LOOP_20261104.md)。

2026-11-02 最新现场结果：用户在满占空比短点动后确认传送带已运动，并要求停止后续测量；最终读回 Features=6、status=0、duty=0。可调范围 1..100%，单次 20..900 ms。此前无动作的具体原因未独立确定，仍为整板时序未合格的调试镜像。

2026-11-01：点动范围扩为 **1..100%**（20..900 ms），须使用配套 `Features=6` 的 FPGA 位流与新 `conveyor-jog`。硬件保留 250 ms 看门狗和连续运行 1 秒截止，100% 时选定方向持续输出高电平。旧 `Features=2` 位流仍只允许 1..25%，新 CLI 会在写寄存器前拒绝更高请求并提示更新位流。已于 2026-11-01 部署并读回 Features=6；50% 和首次 100% 各 500 ms 点动现场均反馈完全没动，寄存器与停机读回正常，继续排查实际电气链路；构建和离线测试记录见 `../../tmp/conveyor-range-20261101/README.md`。以下日期段落为历史记录。

2026-10-28：新增独立受限点动工具 `conveyor_jog.cpp`，通过 `build_jog.ps1 -Board` 编译。仅在 `features=2` 的配套 JOG_ONLY 位流上接受 `--uio /dev/uioN --jog 10 500 forward`，占空比最多 25%、时长最多 900 ms；硬件另限连续运行 1 秒并保留 250 ms 看门狗。`--status` 只读，`--stop` 禁能，`--clear-fault` 显式清错且保持禁能。生产控制器拒绝 JOG_ONLY。FPGA r3 点动实验位流已加载，仍存在整板时序违例和覆盖率问题；板端寄存器已回读，但尚未确认真实运动。本轮新增活动占空比核对与 CLI→GP0 回放，新点动程序尚未部署；接线和验证见 [实物调试记录](../../docs/CONVEYOR_BRINGUP_20261028.md)。下面的 2026-10-24 记录保留历史背景。

2026-10-24：源代码在 `E:/26FPGA/sdk_v1.0/conveyor_linux`，PL 在 `E:/26FPGA/prj_v1.0/soc_prj/src/conveyor`。已加入主系统 GP0 AXI-Lite 路由和 Qt 传送带页面，UI/CLI 模拟流程及像素视觉回归已在开发板通过。整板候选已生成，但 75 MHz 布线后时序复核未通过，未刷写新启动位流。主系统版本为输出锁定的 bench 模式，不引出电机控制引脚；实物电机、编码器及定位精度尚未测试。集成和部署记录见 `../ui_app/deploy/conveyor-integration-20261024.md`。

用户确认的硬件为 BTS7960 模块、JGB37-520 12 V/60 转档减速编码电机。按用户提供的商家参数表，该档减速比为 169:1，减速输出轴空载标称 60 rpm（表列 ±10%），额定负载转速 46 rpm；这些是商家标称值，尚未实测。用户确认电机同步轮 20 齿、滚筒同步轮 31 齿、主动滚筒外径约 20 mm，估算空载带速为 60×20/31×π×2/60≈4.05 cm/s。编码器具体线数及有效厘米/计数仍待实测，软件不猜测每转脉冲数，可以直接通过实际带面位移标定厘米/计数。

```text
检测中心像素 (u,v) → 固定平面标定 → (X,Y) cm
                                     ↓
PS：沿 X 到抓取线的位置控制 → 目标带速 cm/s → 编码器速度 PI → PWM 占空比
                                     ↑                         ↓
                               AB 四倍频计数 ← PL ← AXI4-Lite / Linux UIO
                                                               ↓
                                                BTS7960 RPWM/LPWM/R_EN/L_EN
```

坐标和距离统一用厘米，速度用厘米/秒，定时用单调时钟毫秒。`axis_x=1, axis_y=0` 表示带子前进增加 X，`origin_y` 对应带面中心线，Y 只参与走廊范围检查。传送带不能主动修正物体的 Y 偏差。Z 固定且不参与单轴输送控制；视觉标定点必须与跟踪特征位于同一高度平面。

## 文件

| 文件 | 内容 |
| --- | --- |
| `conveyor.hpp/.cpp` | 寄存器驱动、模拟后端、视觉位置状态机、编码器速度 PI、参数解析 |
| `uio_backend.cpp` | Linux UIO 用户态设备驱动；使用系统已有 UIO 内核驱动 |
| `conveyorctl.cpp` | 非阻塞 CLI、模拟带面、纯编码器监视模式 |
| `conveyor.hardware.template.ini` | 厘米硬件配置模板，编码器比例为 0，故意拒绝运行 |
| `conveyor.cm.simulation.ini` | 厘米视觉 + 编码器联合仿真的虚构参数 |
| `conveyor.example.ini` | 原始占空比/像素单位基准仿真，不用于真实电机 |
| `tools/belt_calibration.py` | 像素平面标定、投影、实测编码器比例换算 |
| `tests/` | 状态机、错误输入、速度环、CLI 及标定测试 |

## 构建与离线运行

本机 PowerShell：

```powershell
cd E:\26FPGA\sdk_v1.0\conveyor_linux
.\build_windows.ps1 -Board
.\build\conveyorctl.exe --simulate --config conveyor.cm.simulation.ini --demo
.\build\conveyorctl.exe --simulate --config conveyor.cm.simulation.ini
```

`-Board` 使用 `E:/FD/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-g++.exe`，生成 `release/conveyorctl-aarch64`。Linux/配套 VM 可运行 `make test` 和 `bash build_board.sh`，通过 `CXX` 或 `TOOLCHAIN_BIN` 指定工具链。标准为 C++11，不依赖 Qt、OpenCV 或 libgpiod。

厘米联合演示模拟 80 ms 惯性和虚构编码器比例，物体从 X=5 cm 输送到 X≈19.91 cm 后稳定等待，目标线为 20 cm、示例容差为 0.2 cm。该结果只证明模拟场景下的控制流程，不能作为电机性能、实际定位精度或最终整定参数。

## 状态与放行

| 状态 | 行为 |
| --- | --- |
| `idle` | 输出禁能，等待 `arm` |
| `waiting` | 已准备，未运动；可提交首个可见目标，或明确执行 `feed` 进料 |
| `feeding` | 按最大配置速度进料，等待下一个目标；受最大周期时间约束 |
| `tracking` | 锁定目标 ID，随目标接近抓取线而减速 |
| `settling` | 两路使能拉低，等待视觉和编码器同时确认稳定 |
| `holding` | `ready=true`，继续监视位置与带速，等待外部抓取完成/放行 |
| `picking` | 抓取已开始，带面继续禁能；暂停视觉超时检查，继续检查带面移动和最大周期时间 |
| `fault` | 尝试禁能并锁存原因；须先 `reset` 再 `arm` |

`beginPick` 只允许在 `holding` 调用，消费 ready 并进入 `picking`；相机随机械臂移动时不再把抓取中的视觉中断当作丢失目标。`release` 只允许在 `holding` 或 `picking` 调用，表示机械臂已经离开运动范围并允许传送带继续，随后进入 `feeding`。刚放行的目标 ID 不会立刻再次触发停带，新物体必须使用新 ID。`arm` 不自动进料；初次启动可用 `arm` 后接 `feed`。

相机报告空场景时，用 `valid=false`/`lost`。进料且尚未锁定物体时允许空场景；锁定后丢失、目标 ID 改变、超过视觉时限、越过停止容差或超出 Y 范围都会停机。源帧序号必须全局递增，拍摄时间必须递增；重复/乱序帧不刷新有效反馈时间。检测器重启后需要 `stop`、`arm` 开始新的序列。

视觉位置控制采用速度前馈的停止提前量和分段减速；速度内环每约 20 ms 运行一次，含前馈、PI、输出限幅和积分抗饱和。编码器反向、异常带速、非法 AB 跳变及持续无脉冲会进入故障。`holding` 是观测到的到位状态，电机处于禁能滑行，不是主动保持制动。

CLI 示例（X/Y 已标定为 cm）：

```text
arm
feed
xy 101 1 0 5.0 0.3
xy 101 2 10 5.3 0.3
status
stop
reset
quit
```

`xy OBJECT_ID FRAME_SEQUENCE CAPTURE_AGE_MS X_CM Y_CM` 中 age 是输入时距真实采集时刻的年龄，不是读取旧 JSON 的时间。上面只有两帧，随后会正常触发视觉超时；实际必须持续供帧。行未输完也不会阻塞心跳。`fault-on/fault-off` 仅用于模拟注入故障。EOF、正常退出和可处理的终止信号会尝试禁能；强制退出由 PL 看门狗兜底。

## 视觉/Qt 接入接口

```cpp
conveyor::MotorDriver motor(registers);
motor.initialize(monotonicMs());
conveyor::Controller belt(motor, measuredConfig);
belt.arm(monotonicMs());
belt.feed(monotonicMs());               // 初次明确允许进料

// 视觉采集回调：captureMs 是采集时刻，不是处理完成时刻。
belt.observe(conveyor::Observation(trackId, frameSequence, captureMs, xCm, yCm), nowMs);

// 单独的约 20 ms 定时器；不能只在视觉回调里调用。
belt.tick(monotonicMs());
if (belt.snapshot().ready) {
    // 将 snapshot().along/lateral 或已保存的实际 X/Y 交给抓取协调器。
    // 抓取完成、机械臂已避让、相机恢复正确姿态后再调用 release()。
}
belt.release(monotonicMs());
```

此库不是线程安全的；Qt 集成使用同一线程的 20 ms 定时器。不要由两个进程同时拥有 UIO。传送带使用独立 AXI 寄存器，不申请机械臂已使用的两组 GPIO。`ui_app/conveyorpage.cpp` 已接入持续 JSON 读取、平面投影、单目标关联检查和既有机械臂动作组；像素模式要求筛选区域内最多一个候选物体，并限制类别和相邻位移，尚不具备多目标遮挡后的身份恢复能力。`belt_cm` 输入必须由上游提供稳定 object_id，不能用每帧检测列表索引代替。

现有 NPU `timestamp_ms` 是 Unix 时间，库使用单调时间；生产者应在同一主机用当前 wall-clock 与单调时钟换算采集时间，并拒绝未来时间、时钟跳变和旧文件。最好由采集端直接输出单调采集时间。已有机械臂 `vision_xy.py` 是单次定位工具，不负责持续订阅或传送带调度。

## 像素到厘米的平面标定

`tools/belt_calibration.py` 复用同级 `../arm_servo_linux/tools/vision_xy.py` 的归一化单应变换数学函数，不连接机械臂会话。部署时需要同时保存这个已有模块。相机必须固定或机械臂保持同一拍摄姿态；单个标定不能补偿相机运动。

1. 在实际跟踪的固定 Z 平面布置至少四个非共线实测点，建议六至九个覆盖输送工作区。
2. 将 `tools/belt_points.template.json` 复制后填写实际像素和厘米坐标。模板的 null 不能直接运行。
3. 用实际图像尺寸、平面高度生成标定，再用独立点核对误差。图像尺寸、裁剪、镜头或拍摄姿态改变后重新标定。

```sh
python3 tools/belt_calibration.py calibrate --points measured_points.json \
  --width 1920 --height 1080 --plane-z-cm 2 --output measured_belt_plane.json
python3 tools/belt_calibration.py project --calibration measured_belt_plane.json --u 900 --v 500
```

这些数字只是命令格式示例。工具拒绝退化点、超差拟合和标定区域外的投影；拟合残差不是现场定位精度。检测框中心只近似代表物体位置，应在与目标相同的高度平面实测。

## 编码器到货后标定

完成 PL/UIO 板级接线后，先保持电机禁能。如果机构可以轻松反向带动减速器，可手动向 +X 移动带面一段实测距离；169:1 减速器可能不易反拖，不可强行推带。不能轻动时，需要另行准备受限的低速点动校准流程。当前 `--encoder-monitor` 持续发送禁能命令，不能与另一个电机点动程序同时运行：

```sh
./conveyorctl-aarch64 --uio /dev/uioN --encoder-monitor
python3 tools/belt_calibration.py encoder-scale --travel-cm 10 --before 100 --after 1100
```

第二条示例输出 `cm_per_count=0.01`、`encoder_sign=1`，只是演示计算。使用自己测得的带面距离和前后 PL 原始计数，重复多次检查一致性；计数差按 32 位回绕处理。可以无需猜测编码器线数、减速比和滚筒直径而标定实际传动关系。

滚筒直径实测：断开电机动力电源，识别直接带动宽输送带的主动滚筒，测量其接触输送带的圆柱外径（含包胶，不含轴、挡边或同步带轮）。优先使用游标卡尺；改变测量方向至少三次并取平均。只能绕量时，用不伸长的纸条贴紧包胶外圆测周长 C，再以 D=C/π 换算。同步带轮齿顶外径不代表滚筒直径；若计算转速比，应数主动/从动轮齿数。

滚筒外径是传动估算初值。最终控制比例优先采用正常装配、正常张紧下的实际带面位移/编码器计数，吸收同步轮传动比及有效周长的影响；打滑和物体相对皮带滑动仍需视觉外环处理。

若由参数计算，`cm_per_count = 每圈输出轴对应的带面行程 / 每圈输出轴对应的 PL 四倍频计数`。假如厂商给的是减速前单通道每圈完整脉冲数 P，输出轴四倍频计数通常需要再乘减速比和 4；若已给四倍频边沿计数不能重复乘 4。实物计数定义须确认。

复制 `conveyor.hardware.template.ini`，填入比例和正方向，再测量目标 X、Y 范围及传动速度，逐步整定占空比上限、最大带速、PI、停止提前量及稳定阈值。该模板 `cm_per_count=0` 会拒绝启动；`--encoder-monitor` 不需要运动配置。`motor_reverse` 改变 BTS 输出方向，`encoder_sign` 定义带面向 +X 运动时的计数正号，两者独立配置。

```sh
./conveyorctl-aarch64 --uio /dev/uioN --config measured_conveyor.ini
```

## 验证记录与边界

2026-10-28 点动程序：`build_jog.ps1 -Board -Rtl` 编译主机和 AArch64 程序，并用真实 CLI/驱动生成的寄存器读写回放 GP0 RTL。测试后端只访问本地文件，不打开串口或真实 UIO。回放使用 75 MHz 参数，验证 20% 正向、25% 正反向、3750 tick PWM 周期、750/937 tick 高电平、双使能一致和停机后的四路低电平；另注入占空比错误、方向错误和提交丢失，检查报错后写入停止命令。波形和结果保存在 `build/jog-rtl/`。回放是短脉冲；1 秒硬截止和看门狗另由工程 RTL 回归验证，不把短回放说成 900 ms 或实物测试。

点动 CLI 允许 1..25%、20..900 ms，仍受 PL 的 25%、1 秒硬上限及 250 ms 看门狗保护。运行期间核对使能、方向和 `ActiveDuty`，停止后打印 `REGISTER_READBACK`（采样次数、时刻、周期和目标占空比）。`pin_voltage_verified=no` 明确表示寄存器读回不等于引脚电压或电机运动验证。整板时序和实际电气接口验收仍须另行通过。

2026-10-24：本机 `-Wall -Wextra -Werror` 构建通过，96 项 C++ 检查、CLI 心跳/超时测试、6 项标定测试、两组模拟流程通过；Icarus Verilog 通过 158 项核心检查和 114 项 GP0 路由/锁定模式检查；TD Release_2026.1_SP2 在 DR1M90GEG400 上完成主系统综合，布局布线及板上记录另见集成文档；AArch64 Linux 交叉编译通过。

详细寄存器、BTS7960 接线、板级集成事项见 `../../prj_v1.0/soc_prj/doc/conveyor.md`。尚无物理电机、负载、皮带打滑、相机移动、制动距离、延时抖动或现场精度的验证结果。
