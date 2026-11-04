# EfficientNet-B0 工况识别模型

本包为已微调的三分类模型，参数量 4,011,391。以 ImageNet 预训练权重初始化，使用 495 张图片训练 10 轮：空带 121 张、物块在位 149 张、人工干预 225 张。全部参与训练，没有独立验证集。

## 文件
- models/efficientnet_b0/model.pt：PyTorch 训练后权重，包含结构信息、输入规范和类别顺序。
- models/efficientnet_b0/model_fp32.onnx：导出的 FP32 ONNX，输入固定为 1×3×224×224。
- models/efficientnet_b0/metrics.json：训练指标。
- infer.py：配套 ONNX CPU 推理程序，默认使用 EfficientNet-B0。
- examples/：三类示例图片。
- training/：原始训练及 ONNX 验证脚本，云端路径需要按实际环境调整；未包含训练数据。

## 使用
建议 Python 3.11，在解压后的目录执行：

```bash
python -m pip install -r requirements.txt
python infer.py examples/01_empty.png
python infer.py /path/to/your_image.jpg
```

输出类别概率与预测结果。类别顺序：empty（空带）、object_present（物块在位）、hand_present（人工干预）；有手时优先归入人工干预。概率未校准，不等同于整体准确率。

## 输入处理
RGB，保持长宽比缩放后居中填充至 224×224，填充值 RGB (114,114,114)。像素除以 255，再以均值 [0.485,0.456,0.406]、标准差 [0.229,0.224,0.225] 标准化。输入名 images，输出名 logits，输出三个类别的 logits。

## 部署状态
ONNX 已完成与 PyTorch 的数值一致性检查。本包尚未量化或转换为 MLK-F3P 板端格式，未在 1 GB NPU 上验证内存和速度。模型文件大小不等于运行时内存占用。
