# CAN0 板间实线检测：未通过

2026-10-25 北京时间23:12起。用户确认两板CAN0端子以H接H、L接L连接。终端电阻及参考地未由仪表确认。

## 实际步骤和证据

1. 动态枚举CH342 PS串口：板1 COM9、板2 COM3。两板重启后重新登录，并校准系统时间。
2. 板2原为can-loopback，本机发送器PID267。切换为`can`，停止本机发送器。之后确认原进程已退出，不将本机回显当作板间接收。
3. 两板CAN0设500000bit/s、内部loopback off；BSP需要FD数据段时序有效，仍只发标准ID、DLC8的经典帧。两板`ip -details`只显示`<FD>`，无`LOOPBACK`。
4. 板1运行现有`can_sender --can can0 --demo --ticks 30`，会话4218139179，约15秒后自动结束。
5. 板1日志为`STOP TX=250 ACK=0 ERR=0`。这里TX代表SocketCAN写入成功，ERR=0不能证明物理线上成功。
6. 板1CAN0统计TX250帧，RX0；板2CAN0 RX保持11270帧、90160字节不变。这些旧帧来自此前内部回环，本次对端传输的新增接收为0。
7. 板2没有记录会话4218139179；告警索引为空。心跳、状态、五类计数和告警均未通过真实线缆接收验证，返回确认也未通过。

原始记录：`release/board1-physical-can0.log`、`release/board2-physical-can0.log`。

## 发现的硬件工程缺项

本地硬件手册`D:/PRJ/soc_prj/doc/refs/fpgahw-b1a660-MLK_F3P_CZ02_DR1开发板硬件使用手册.pdf`第21页说明：底板只有一路CAN收发器SN65HVD232DR，连接PL BANK33，CAN0_TXD=F20，CAN0_RXD=F19，LVCMOS33。

当前本地项目`D:/PRJ/soc_prj/soc_prj.al`引用`src/system_top.v`、`src/constraint/io.adc`和`design_0/design_0.xml`。检查发现：

- `design_0/Design.xml`第48、49行，CAN0_Available和CAN0_Dot_Enable均为0。
- 实际引用的PS IP在`design_0/ip/design_0_ARM_Processor_System_0/Design.xml`第369至372行也未启用CAN0/1。
- 该IP生成RTL `RTL/ARM_Processor_System_9c4afc9a72e9.v`第519行把`can0_rx_in`接到常量`1'b0`。
- 顶层没有CAN收发端口，IO约束没有F20/F19的CAN连接。

这证明**本地工程没有建立PS CAN与底板收发器之间的PL信号通路**，是修复前必须处理的配置缺项。只打开Linux can0不会自动生成这段硬件连线。尚未将两板在用bitstream逐一还原对照，因此不将本地源码检查冒充板上全部硬件问题已穷尽；接线、终端电阻和引脚复用仍需在通路修复后复测。

板2启动分区BOOT.bin SHA256：9b50c3bc5c118e1f1529d0d07d4d459b67c80840a9e24d2a5494660678e5e981。
板1发现两份BOOT.bin：mmcblk0p1为6431d2fc21f6f6aa644655070d100153df8fac0fe0ecc6294771e2d4937557d2，mmcblk1p1为858cf78c03ae75c870587be77d70668e0ba4841448581273e5c996bf8f980706。本次没有改写启动镜像。

## 后续修复与验收范围

核对两板实际启动镜像来源；在保持原HDMI、摄像头、NPU、机械臂逻辑的工程上启用CAN0经PL引出，连接顶层TX/RX并约束F20/F19，核对PS初始化/引脚复用；重新综合实现和打包，备份后部署适配各板的镜像。随后验证物理收发、三类告警发生与恢复、同周期五类计数完整性、反向确认、超时/恢复及控制器错误统计，并确认终端电阻与参考地。

当前测试已结束、板1发送器已退出；板2保留`can`真实接收模式，未恢复内部回环。因此屏幕显示等待数据/通信超时是正确结果，不能用恢复模拟循环掩盖实线未通。CAN接口保持UP、loopback off；没有对机械臂/传送带发送控制命令。

预定监控数据格式已经实现，当前结论是**实线尚未打通，不能宣称预定内容已正常跨板传输**。异常JPEG仍使用单独的PS以太网通道，不在此次CAN帧传输范围内。
