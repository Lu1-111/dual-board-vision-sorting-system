# Blue Conveyor State

固定传送带、固定相机视角下的蓝色方块工况识别。当前交付为 **v2**：WorkcellTinyCNN，输出 `empty`（空带）、`object_present`（物块在位）、`hand_present`（人工干预）。只有一个活动模型，PyTorch、FP32 ONNX、INT8 ONNX 是同一套参数的交付格式。

**最新板端实拍测试未通过：**18 张照片中，FP32 和 INT8 均只识别正确 6 张，18 张全部输出人工干预，12 张无手照片均被高置信度误判。当前 v2 尚不能可靠识别这批实际工况。详见 [实拍测试报告](reports/real_board_20261102/README.md) 和 [逐图结果](reports/real_board_20261102/results.html)。

## 当前结果

| 项目 | 结果 |
|---|---|
| 输入 | RGB，1×3×224×224，像素除以 255 |
| 网络 | 6 层普通卷积 + ReLU + 平均池化 + 三分类全连接 |
| 参数量 / 计算量 | 89,827 / 27,546,816 MAC/帧 |
| FP32 / INT8 ONNX 大小 | 362,064 / 109,708 字节 |
| 原内部验证 | FP32、INT8 均为 67/67 |
| 首批分类图片回归 | FP32、INT8 均为 18/18，无不确定输出 |
| 第二批已知开发集 | FP32、INT8 均为 24/24；各 1 张不确定 |
| 第三批全新生成测试 | FP32、INT8 均为 18/18；各 17 张明确正确、1 张不确定 |
| 最新板端实拍测试 | FP32、INT8 均为 6/18；12 张明确误判 |

第二版使用 RTX 5060 Laptop GPU 继续训练，保持模型结构和大小不变，加强冷暖光、明暗变化和阴影等训练增强。共训练 100 轮，选第 20 轮；循环约 32.35 秒，峰值张量显存约 119.66 MiB。INT8 使用原训练集 120 张图片校准，未使用测试图校准。

第二批图片参与了本轮参数选择，属于已知开发集。第三批 18 张图片在最终模型冻结后才生成，未参与任何训练、选择或阈值调整。同图对照中，初版 INT8 为 17/18，第二版为 18/18；初版冷暗空带的高置信度误判已修正。仍有一张新暗光蓝块输出不确定，第二批靠下边轨的暖光蓝块也仍不确定。用户认可的下边轨卡块输出物块在位不作为验收问题。

详细信息见 [优化报告](reports/optimization_v2/README.md) 和 [第三批逐图结果](data/test_batches/batch03/results.html)。这些数字描述固定背景下本批生成图片的表现。INT8 为通用 QDQ 导出，尚未进行 FPGA 厂商编译或硬件实测。

## 项目目录与数据用途

```text
blue_conveyor_state/
├── data/
│   ├── images/                 331 张已通过审核的训练候选，按三类存放
│   ├── manifest.json           原划分保持不变：264 训练 / 67 内部验证
│   ├── usage.json              各数据集在当前模型中的实际用途
│   ├── classes.json
│   ├── prompts/                原训练候选的生成提示词
│   ├── gallery.html
│   ├── reference/              7 张认可的来源参考图，不参与训练
│   ├── test/                   首批 20 张图片；其中 18 张分类、2 张边界
│   └── test_batches/
│       ├── batch02/            28 张图片；24 张已知开发集、4 张边界
│       └── batch03/            18 张模型冻结后生成的新测试图片
├── models/
│   ├── best.pt                 当前 v2 PyTorch 权重
│   ├── workcell_tiny_fp32.onnx
│   ├── workcell_tiny_int8_qdq.onnx
│   ├── input_contract.json
│   ├── artifact_hashes.json    当前三份模型文件的 SHA-256
│   └── revisions.json          活动版本与历史版本证据；旧权重在 Git 中
├── src/
│   ├── model.py                网络结构、训练预处理及指标
│   ├── train.py                原始 CUDA 训练
│   ├── optimize.py             当前 CUDA 优化训练
│   ├── export_model.py         ONNX 导出与训练集 INT8 校准
│   ├── infer.py                独立 ONNX 推理，无需 PyTorch
│   ├── evaluate_batch.py       统一测试、回归评估与图册
│   ├── evaluate_real.py        带标签的板端实拍图片评估
│   └── verify.py               图片、模型、路径及划分完整性检查
├── reports/
│   ├── initial_training/      初版训练与首批测试历史记录
│   ├── batch02/               初版在第二批图片上的历史测试结果
│   ├── batch03/               v2 的全新测试结果
│   ├── real_board_20261102/   板端实拍测试结果
│   └── optimization_v2/       优化训练、回归结果、初版同图对照和说明
├── requirements.txt
└── requirements-inference.txt
```

原训练清单的 `path`、`prompt_file` 相对于 `data/`；来源参考清单相对于 `data/reference/`；每批测试的 `path` 相对于其所在目录，`reference_paths` 相对于项目根目录。历史结果与当前结果分别保存，不覆盖旧的测试结论。原先的重复评估脚本已由统一的 `evaluate_batch.py` 替代。

## 推理与评估

以下命令在项目根目录运行，建议使用 Python 3.11 虚拟环境。仅推理和评估无需 PyTorch：

```sh
python -m pip install -r requirements-inference.txt
python src/infer.py data/test_batches/batch03/images/b03_01_cool_dim_left.png
python src/verify.py
python src/evaluate_batch.py --batch data/test_batches/batch03
```

评估已知旧图片时，显式指定回归模式和独立输出，避免覆盖历史报告：

```sh
python src/evaluate_batch.py --batch data/test_batches/batch02 --allow-model-update --output reports/optimization_v2/batch02_regression
python src/evaluate_batch.py --batch data/test --allow-model-update --output reports/optimization_v2/first_test_regression
```

输入采用 RGB 双线性等比例缩放，尺寸按 `int(v * scale + 0.5)` 取整，灰色 `(114,114,114)` 居中补边。ONNX 输出 logits，softmax 与原有 0.8 的不确定阈值在推理程序中处理。规格见 `models/input_contract.json`。

## 训练复现

```sh
python -m pip install torch==2.7.1 torchvision==0.22.1 --index-url https://download.pytorch.org/whl/cu128
python -m pip install -r requirements.txt
```

本轮从 Git 提交 `60bdb55` 的初版权重开始。若重现本轮过程，先取回该权重，再运行：

```sh
mkdir -p runs
git show 60bdb55:models/best.pt > runs/initial_v1.pt
python src/optimize.py --initial-checkpoint runs/initial_v1.pt --output runs/reproduce_v2 --epochs 100 --seed 20261103 --lr 0.0005
python src/export_model.py --checkpoint runs/reproduce_v2/best.pt --manifest data/manifest.json --output runs/reproduce_v2/export
```

继续优化当前模型时可省略 `--initial-checkpoint`，并使用新的输出目录。训练与导出写入 `runs/`，不会自动覆盖最终模型。环境、训练临时权重和缓存不纳入 Git；正式数据、当前模型、源码和报告均纳入版本管理。本地 Git 默认分支为 `main`，没有配置远程仓库。
