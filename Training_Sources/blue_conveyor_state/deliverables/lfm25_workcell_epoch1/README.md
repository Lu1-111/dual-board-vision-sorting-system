# LFM2.5-VL-450M 工况识别：第一轮

本包包含原始 BF16 基座、第一轮 LoRA 权重、推理代码、三张生成测试图片以及训练脚本和日志。适配器必须与包内基座一起使用。

## 推理

建议 Python 3.11；在有 A100 等 NVIDIA GPU 的环境中创建虚拟环境，然后在本目录运行：

```bash
python -m pip install -r requirements.txt
python infer.py examples/01_empty.png
python infer.py examples/02_object_present.png
python infer.py examples/03_hand_present.png
python infer.py /path/to/your/image.jpg
```

程序自动选择 CUDA；可以用 `--device cpu` 或 `--device cuda` 指定。CPU 模式使用 FP32，内存需求较高。模型文件本地加载，不需要再下载权重。不同机器的 NVIDIA 驱动和 PyTorch 构建需自行匹配。

输出 JSON 的 `prediction` 为 `empty`（空带）、`object_present`（物块在位）或 `hand_present`（人工干预）。`valid_label` 只表示输出是否属于这三种标签，不是置信度。手和物块同时出现时，手的类别优先。

## 模型与限制

- `model/base`：LiquidAI/LFM2.5-VL-450M 基座，含原始许可文件。
- `model/adapter`：使用 495 张图训练 1 轮的 LoRA 适配器，以及对应处理器。
- `training`：本轮训练脚本、日志和可修改的配置示例；示例假定从本目录执行，并自行准备 `data/train.jsonl`。
- `examples`：训练后生成的三张测试图；它们以一张训练集中已有的实拍图为参考，不能代表独立实拍准确率。

若需要重新训练，请额外安装 `trl==1.17.1` 和 `datasets==5.1.0`，准备数据并修改配置，再从本目录执行 `python training/train_epoch1.py --config training/train_config.example.json`。包内未包含训练图片。

本包是 PyTorch + Transformers 推理包，未量化、未转换为安路 NPU 格式，也没有在 1 GB 板端运行验证；原始权重、运行时及中间数据的总内存可能超过板卡可用内存。需要确认目标 NPU 支持的算子、量化方式和编译工具后再做板端适配。

使用基座模型需遵守 `model/base/LICENSE` 中的许可条款。
