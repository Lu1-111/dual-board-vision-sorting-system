# 蓝色物块定位与三角缺角识别 · V28

这个文件夹包含正式数据集、V28 模型参数、训练和识别源码、测试报告，可整体移动。识别范围是原数据集中的**三角缺角**，缺角尺寸与断面纹理可以变化。

现在可以直接输入原传送带场景的**彩色整图**：程序定位蓝色物块、画框、输出原图像素坐标，再自动裁剪并判断三角缺角。彩色只用于定位；缺角判定继续使用冻结的灰度 V28，权重和阈值 0.88 均未改变。

## 从这里开始

在本文件夹打开终端，运行：

```bash
./start.sh
```

会打开图片识别界面。默认“原始彩色整图”模式，可选择图片、批量识别文件夹、查看检测框及中心坐标、导出 CSV / JSON、保存原分辨率标注图。已有单工件裁剪图请切换到“已裁剪单工件”模式。本机启动脚本可使用已安装的训练环境；也可以设置 `TRIANGLE_PYTHON` 指定 Python，或者使用项目内的 `.venv`。

在新电脑上，需要 Python 3.10–3.12、匹配的 PyTorch/torchvision 和依赖；图形界面还需要系统提供 Tk。已验证环境为 Python 3.11、PyTorch 2.7.1+cu128、torchvision 0.22.1+cu128。GPU 不是识别的必需条件。

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
./start.sh
```

图形界面默认使用 CPU。训练默认选择可用的 CUDA，否则使用 CPU。若另行安装 GPU 版本，torch 与 torchvision 必须匹配。

## 文件放在哪里

```text
triangle-defect/
├── data/
│   ├── images/{train,val,test}/{normal,triangle_defect}/
│   ├── manifest.json            唯一的训练/验证/测试清单，含标签和分组
│   ├── index.csv                可查看的图片索引
│   └── originals/               101 张原始采集照片及裁剪/审核记录
├── models/
│   ├── v28/
│   │   ├── weights.pt           正式模型权重
│   │   ├── config.json          输入规格与固定阈值
│   │   ├── model_float32.torchscript.pt  同一模型的便携推理格式
│   │   └── release.json         权重校验值和版本说明
│   └── initialization/          离线重新训练所需的预训练特征参数
├── src/triangle_defect/         模型、训练、识别、界面、校验、评估、导出源码
├── config/blue_locator.json    蓝色物块定位参数（不属于缺角模型权重）
├── tests/                     定位、坐标、导出、异常处理和界面功能测试
├── reports/
│   ├── evaluation.html         可直接打开的逐图测试报告
│   ├── overview.png            测试概览
│   ├── predictions.csv         逐图预测
│   ├── evidence/              原始评估和整理核验记录
│   ├── generated-test-20261102/ 20 张新增生成图的测试依据和报告
│   └── fullframe-validation-20261102/ 101 张彩色原图的定位与识别报告
├── run.py                      统一命令入口
├── start.sh                    启动脚本
└── requirements.txt            已验证的依赖版本
```

正式缺陷模型只保留 V28。TorchScript 是相同参数的另一种保存格式；`initialization` 仅用于重新训练的初始特征，不是另一个缺陷模型。

## 数据集

共 **1,009 张正式图片**：961 张主数据和 48 张保留原三角缺角形状的光照变体。不按实拍或 AI 来源区分训练权重。标签为正常 0、三角缺角 1。

| 划分 | 正常 | 三角缺角 | 合计 |
| --- | ---: | ---: | ---: |
| train 训练 | 342 | 379 | 721 |
| val 验证 | 69 | 77 | 146 |
| test 内部回归测试 | 67 | 75 | 142 |
| 总计 | 478 | 531 | 1,009 |

同图变体及已识别的相似图片按组划分，118 个组不跨集合。来源与预测结果不参与划分。原主数据完整保留，未按模型是否预测正确删图。任意形状缺口的 P/Q 补充批次已经退出项目。

`originals` 保存原始采集依据，不是另一套训练集。其裁剪样本已经计入正式清单；其中 4 张原先无法判定的照片只留作原始记录，不重复训练或强行标注。所有训练、验证、测试只读取 `manifest.json`。

## 识别、校验和测试

```bash
./start.sh verify
./start.sh detect data/originals/blue_051.jpg
./start.sh detect data/originals --output runs/originals-inspection
./start.sh predict data/images/test/normal/blue_004_historic.png
./start.sh predict data/images/test/triangle_defect --output runs/predictions.csv
./start.sh evaluate --compare-release
./start.sh evaluate --split val
```

`detect` 输入彩色整图或图片文件夹，输出目录中包含 `annotated/` 原分辨率标注图、`results.csv` 坐标表、`results.json` 完整结果和 `errors.json` 无法处理的文件记录。默认目录为 `runs/detect-日期-时间`；自定义输出目录必须尚不存在。损坏图片不影响其余图片处理，有失败时命令返回非零状态。`predict` 继续用于已有裁剪图，不负责物块定位。

坐标为**原图像素**，原点在左上角，x 向右、y 向下。`bbox_xyxy = [x1,y1,x2,y2]` 的 x2/y2 不包含在框内；`center_xy = [(x1+x2)/2,(y1+y2)/2]` 是检测框中心，不是经标定的机械臂或抓取坐标。框表示整个蓝色物块，并不是缺角区域的分割轮廓。

定位使用 HSV 蓝色分割和连通区域形状过滤，忽略现有场景中的红色、黄色物块。每个物块按框外扩 15% 后裁剪，先转灰度并双线性插值到 224×224，再按 V28 插值到 320×320，保持原始训练裁剪图的处理顺序。已有裁剪图仍直接灰度插值到 320。程序不会将“未找到物块”当作正常件；接触画面边缘的物块保留定位框，但跳过缺角分类并标记 `needs_review`。

定位只覆盖当前蓝色物块和传送带场景；相互分离的多物块坐标输出已通过软件测试，但这 101 张实图每张只有一个蓝色物块。接触、遮挡或复杂蓝色背景需要另行验证，现有形状过滤不能保证识别所有遮挡情况。

101 张原始照片均检测到 1 个蓝色物块。97 张有标签照片的框和中心均落在原人工裁剪范围内，缺角判定 **96/97 正确**，正常误报 0/50、缺角漏检 1/47；其中原训练划分 42/42、原验证划分 26/26、原测试划分 28/29。保留的漏检仍是 `blue_075.jpg`。另外 4 张缺角无法判定的原图仅参加定位检查。人工裁剪包含背景，不是精确物体边界，所以没有宣称 mAP、IoU 或像素定位误差。这是内部回归，包含训练来源照片，不是独立验收。

打开 `reports/fullframe-validation-20261102/report.html` 可查看全部原图结果；同目录有逐图坐标、正常与缺角标注示例、复测脚本。使用项目 Python 环境运行 `reports/fullframe-validation-20261102/evaluate.py` 可复现报告，运行 `python -m unittest discover -s tests -v` 可检查软件功能（界面测试需要 Tk 和显示环境）。

`verify` 检查全部正式图片、原始照片、分组隔离和模型文件校验值。`evaluate` 默认使用固定 V28 和阈值 0.88，结果写入 `runs/evaluation.json`；`--compare-release` 对比整理前的逐图结果及 TorchScript 输出。它不会修改权重或阈值。

V28 验证集 146/146 正确；内部回归测试 140/142 正确，准确率 **98.59%**，缺角召回率 **97.33%**，正常样本误报 0/67。保留的两张漏检是 `blue_075_historic` 和 `S10`，未隐藏或删除。

2026-11-02 补充了 20 张参考原场景新生成的测试图（10 张正常、10 张三角缺角），固定 V28 和阈值 0.88，主测试及 224→320 缩放对照均为 20/20。它们属于相关的合成测试样本，变化幅度有限，不代表独立实拍验收；图片未加入训练。原图、完整提示词、预测前标签、逐图得分与可视化报告集中在 `reports/generated-test-20261102/`，打开其中的 `report.html` 即可查看。

复测该批图片：`./start.sh` 使用的 Python 环境执行 `reports/generated-test-20261102/evaluate.py`。该脚本只进行识别和记录结果；重建展示报告的 `build_report.py` 另需 Matplotlib。

这 142 张图片在前期开发中已经被查看，因此这些指标是内部回归结果，不能当作未见过的独立现场验收。整理后的逐图输出与原 V28 完全一致，详情见 `reports/evidence/reorganization_evaluation.json`。

## 重新训练

```bash
./start.sh train --output runs/my-training
```

默认使用 V28 的训练配方：100 轮、batch 32、随机种子 20261128、灰度双线性插值输入。每轮遍历全部训练样本；仅用验证集选阈值和权重，训练过程不加载测试图。每 5 轮及最后一轮验证，最优检查点写入该次运行目录的 `best.pt`，日志为 `epochs.json`。预训练特征已经随项目保存，训练无需下载初始权重。

新训练结果单独保存，不自动替换正式 V28。不同硬件或库版本可能造成训练数值差异。`reports/evidence/training_config.json` 和 `training_epochs.json` 保存 V28 的历史训练记录；历史清单校验值对应整理前的路径文本，整理后的清单校验值见迁移核验记录。

模型为 MobileNetV3-Small 前 9 个特征模块加局部分类头，250,649 个参数。单通道灰度在网络内复制为 3 通道并归一化，不引入彩色信息。最大 4 个局部 logits 取均值后 sigmoid，得分 ≥ 0.88 判为缺角。裁剪模式下的红圈是最强响应位置，不是分割边界；得分未经概率校准。整图模式显示物块框和中心十字。

## 导出与版本管理

```bash
./start.sh export
```

可重新导出正式 V28 至 `runs/model_float32.torchscript.pt`。TorchScript 输入为已预处理的 float32 `[1,1,320,320]`，范围 0–1，输出为 `[1]` 缺角得分，使用同一阈值 0.88。随项目交付的模型仍为 FP32，后续量化与 FPGA 部署尚未包含。

本项目已建立本地 Git 仓库，代码、正式数据、原始采集依据、参数和报告均纳入管理。`runs/`、Python 缓存和虚拟环境不纳入 Git。目前没有配置或推送远程仓库。
