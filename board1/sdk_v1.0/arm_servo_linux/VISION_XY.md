# 从视觉 XY 到机械臂定位

现有 `color_block_detection/npu_main.cpp` 输出 `coordinate_frame="camera_pixel"`、`objects[].cx/cy`、`image_width/image_height` 和 Unix 毫秒 `timestamp_ms`。`cx/cy` 是恢复到原图分辨率后的检测框中心，单位是像素，不能直接传给以厘米为单位的机械臂逆解。

本次在原有三维解析逆解上增加了固定高度的 XY 接口，以及直接读取 NPU JSON 的板端桥接程序。完整过程为：

```text
NPU 检测中心 (cx,cy) 像素
  → 实测平面单应变换 → (X,Y) cm
  → 配置的尖端高度 Z、末端俯仰 pitch
  → 底座偏航 + 肩/肘/腕解析逆解
  → 关节限位、正解回算、控制器权限检查
  → 六轴平滑运动，保持夹爪和腕旋
```

相机随机械臂运动，因此单个平面映射只在**同一拍摄姿态、同一镜头与同一目标平面**下成立。本实现采用“在固定姿态识别一次，再运动到目标”的方式；第二次识别定位前须返回原拍摄姿态。本版没有实现任意姿态手眼标定、视觉伺服、碰撞检测、直线路径规划或自动抓放状态机。

## 1. C++ 逆解接口

`robot_arm.hpp/.cpp` 新增：

```cpp
arm::XYPlane plane(targetHeightCm, targetPitchDeg); // 数值须由实际工作台配置
arm::IKResult result = robot.previewXY(worldXcm, worldYcm, plane);
if (result.ok) {
    // nowMs 使用单调时钟；原有 20 ms 定时器继续调用 controller.tick(nowMs)。
    if (!robot.moveXY(worldXcm, worldYcm, plane, 1000, nowMs)) {
        // 读取 robot.error()；此调用不会自动连接、启能或复位。
    }
}
```

`X/Y/Z` 均使用当前机械臂坐标系：未设零时为底座坐标，设零后为相对原点的平移坐标；+X 向前、+Y 向左、+Z 向上。`pitch` 是相对水平面的绝对俯仰角，不随坐标设零改变。只有 XY 无法唯一确定末端高度和姿态，因此高度、俯仰必须由调用方提供。

默认严格保持 `pitch`，无解就返回失败。需要原有的俯仰搜索时，明确使用 `XYPlane(z, pitch, true)`；高度仍固定，实际俯仰见结果的 `achieved.pitchDeg`。解析几何关系为：

```text
yaw = atan2(Y, X)
r_w = sqrt(X²+Y²) - L_tool*cos(pitch)
z_w = Z - H_base - L_tool*sin(pitch)
cos(elbow) = (r_w²+z_w²-L_upper²-L_fore²)/(2*L_upper*L_fore)
shoulder = atan2(z_w,r_w) - atan2(L_fore*sin(elbow),L_upper+L_fore*cos(elbow))
wrist = pitch - shoulder - elbow
```

现有求解器同时检查肘部两分支、负径向候选和关节限位，并用当前命令角选取较近解。XYZ 奇异点沿用种子底座角。默认几何 2.89/10.43/8.9/17.7 cm 来自参考代码，实机尺寸、舵机零偏和方向仍需测量。回算误差是数学模型残差，不能当作实机定位精度；PL 脉宽仍按 7.84 μs 量化，系统没有位置反馈。

CLI 支持先设置固定平面，再只输入 XY：

```text
xy-plane 2.89 0
preview-xy 37.03 0
xy 37.03 0 1000
```

以上是模型的水平伸展示例，适合模拟验证；实机须使用实际工作点。`xy-plane z pitch search` 明确开启俯仰搜索。初始命令确认和使能流程沿用原有 CLI。

## 2. 与现有 Qt 会话连接

使用现有的 `/tmp/hdmi-ui-arm.sock`，不另建 GPIO 控制器。程序只操作已经连接、已经使能的当前会话。增加的请求为：

```json
{"action":"xy-preview","xy":[15,0],"zCm":2,"pitchDeg":-45,"allowPitchSearch":true}
{"action":"xy-move","xy":[15,0],"zCm":2,"pitchDeg":-45,"allowPitchSearch":true,"durationMs":1000,"confirm":true}
```

`xy` 是厘米，不是像素；缺失高度/俯仰、非数值、无解、未使能或时间不在 20～60000 ms 都会被拒绝。`allowPitchSearch` 缺省为 false。返回 `achievedCm`、`achievedPitchDeg`、`jointAnglesDeg` 和回算误差。

`status` 新增 `visionReference`，记录六轴命令脉宽、调试映射、连杆、轴校准映射和坐标原点。可把它随 XY 请求一起传入，服务器在同一次请求内核对当前状态，拒绝运动中、动作组播放中或参考不符的请求。此核对依赖命令估计，不能发现机械打滑、相机支架移动或无反馈的舵机误差。

## 3. 像素到平面的实测标定

桥接程序为 `tools/vision_xy.py`，只依赖 Python 3 标准库，运行在开发板上。使用它之前须安装本次更新的 Qt 程序；旧版不认识新接口。

1. 让机械臂稳定保持选定的拍摄姿态，保持 PWM 使能。固定相机安装、分辨率、裁剪和焦距。若使用当前尖端设零，先完成设零再标定。
2. 在目标所在的同一高度平面布置至少 4 个非共线标记，建议 6～9 个覆盖工作区。记录每个标记的图像像素位置和实际机械臂 `X/Y`（cm），填入 `tools/vision_points.template.json` 的副本。模板中的 null 必须替换成实测值，不能拿示意坐标代替标定。
3. 目标应位于标定点围成区域内。检测框中心只近似代表物体中心；应在独立的已知目标上核对定位误差。目标高度变化、明显镜头畸变、相机支架移动时，须重新标定或升级为内参/手眼与平面求交方案。
4. 检测程序持续输出 JSON。Qt 和检测视频不能同时占用 framebuffer，所以检测程序运行时省略 `--display`，保留 `--latest-json`；NPU 模型和参数沿用当前已验证配置。

```sh
# 在现有检测启动命令中追加；省略 --display，--frames 0 连续运行：
# ... --frames 0 --latest-json /tmp/cubes_latest.json

# 2 和 -45 仅示例，替换成实际期望的尖端高度与俯仰。
python3 tools/vision_xy.py calibrate \
  --points measured_points.json --latest /tmp/cubes_latest.json \
  --z-cm 2 --pitch-deg -45 --output measured_plane.json

# 只预览，选择唯一的完好绿色目标：
python3 tools/vision_xy.py target \
  --calibration measured_plane.json --latest /tmp/cubes_latest.json \
  --class-name green_good

# 同一接口执行一次运动：
python3 tools/vision_xy.py target \
  --calibration measured_plane.json --latest /tmp/cubes_latest.json \
  --class-name green_good --duration-ms 1000 --move
```

标定输出包含标准化后的单应矩阵和原拍摄姿态。拟合使用标准化最小二乘，拒绝退化点、投影奇点和超差数据；默认拟合最大残差 0.3 cm（不代表实机精度）。已有标定文件不会被覆盖。默认严格保持配置俯仰；若允许为可达性调整俯仰，在 `calibrate` 命令添加 `--allow-pitch-search`，搜索范围沿用 -90～10°，实际角度由预览返回。

程序先核对机械臂停止并保持输出，再等待至少 300 ms 后的新检测；检测时间超过 500 ms、图像尺寸/裁剪变化、目标超出标定范围、姿态或模型变化时拒绝运动。多个候选时要求 `--color`、`--class-name` 或显式 `--index`，不自动挑选一个。`--index` 从 0 起，按筛选后的 JSON 顺序计数；检测置信度至少 0.5。每次调用只处理一个目标，不自动闭爪、回零或重复运动。

运动后相机已经离开拍摄姿态，不能直接重用该映射定位。通过现有姿态/动作组接口回到标定时保存的六轴命令后，再等待新的识别。改变坐标原点、连杆或舵机映射后重新标定。

## 4. 验证

```sh
make test
```

包括原有运动学/关节/CLI 测试、新增固定平面严格俯仰和相对原点测试、像素变换与退化标定测试，以及 Linux 本地 socket 的新检测 → 预览/运动请求链路测试。Qt 的 `tests/coordinate_tests.pro` 和 `tests/console_smoke.pro` 另外覆盖接口解析、参考姿态核对和模拟运动终点。构建 Linux 原生 Qt 程序后，还可运行 `python3 tests/test_vision_qt.py /absolute/path/to/hdmi_ui` 验证真实 Qt 模拟会话与桥接程序的完整链路，测试使用隔离的配置目录与 socket。

实机准确到位仍需完成平面标定与机械臂校准。本次代码不附带虚构的现场标定结果，也没有自动操作开发板。
