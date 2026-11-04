# 固定相机单次抓放联调

2026-10-31：独立于传送带，复用 Qt 当前机械臂会话、逆运动学和九帧动作执行器。每次调用只处理一个静止方块，不循环抓取，不申请传送带设备。需要包含 `fixed-sort-preview/run` 接口的新版 hdmi_ui。

配置在 `tools/fixed_camera_sort.example.json`，全部长度为 cm。机械臂基坐标 +X 向前、+Y 向左、+Z 向上。先假设相机固定在 (18,0,40)，光轴竖直向下，画面右侧指向 -Y、下侧指向 -X；yaw_deg 可修改水平安装朝向。fx/fy=900、图像1920×1080及主点(960,540)也是假设值。投影利用方块顶面 Z=3 与针孔射线求交，夹爪抓取 Z=2，与顶面高度分开配置。斜装相机或明显镜头畸变需要改用实测平面单应标定，不能只修改高度。

五个模型类别 red/yellow/blue/green_good/green_defect 都先放到 (15,8,2)。后续分别编辑 zones 中的 place_xyz_cm 即可按颜色/类别分区。夹爪参数是舵机角度，不是开口角或毫米；30/60只是模板，必须核对实际开合方向。

后台视觉入口沿用板上 `/home/uisrc/cube_detection/run_headless.sh 0`，输出 `/tmp/cubes_latest.json`。应先确认没有另一推理进程占用相机/NPU，避免重复启动。保持 Qt 运行；不要使用占用 HDMI 的视觉显示入口。

在包含这两个 Python 文件的目录执行：

```sh
# 默认只预检整段逆解，不运动；要求 Qt 已连接模拟后端并确认初始姿态
python3 fixed_camera_sort.py --config fixed_camera_sort.example.json --class-name red
# 完整执行一次，在模板未实测时仅允许模拟后端
python3 fixed_camera_sort.py --config fixed_camera_sort.example.json --class-name red --run
# 只检查已保存检测数据的投影，不接机械臂、不检查数据时效
python3 fixed_camera_sort.py --config fixed_camera_sort.example.json --latest sample.json --offline
```

实时入口要求连续三帧新数据、相同类别、位置变化不超过0.5 cm、置信度至少0.6；匹配多个目标时拒绝。执行前锁定该目标，不在移动途中换目标。逆解预检全部通过才执行：接近→下降→闭爪→抬升→转移→下降→开爪→抬离→返回调用前姿态。运行中可使用界面“停止并保持”或 Ctrl+C 中止脚本；脚本会尝试停止自己仍在运行的任务，不自动关闭 PWM。

实物联调前须实测相机参数、桌面/方块高度、抓放高度、夹爪开合和放置位置，再将 measured 改为 true；Qt 仍要求普通硬件模式下关节与几何均已确认，不允许用临时调试模式绕过。必须恢复机械臂基坐标，否则拒绝执行。入口不会自动连接、确认初始姿态或使能硬件。

单目图像无法凭空提供深度，此版本依赖已知静止平面。现有机械臂执行器为关节插值，无碰撞规划和抓取反馈；完成表示命令队列完成，不证明实际抓到了方块。配置的抬升高度也不保证转移整段轨迹恒高。假设值不是实物标定结果。
