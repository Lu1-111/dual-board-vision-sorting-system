# 四个工况识别模型

四个模型以 ImageNet 预训练权重为起点，在同一批 495 张工况图片上分别微调 10 轮；训练时四进程并行使用一张 A100。数据包括空带 121 张、物块在位 149 张、人工干预 225 张。全部数据参与训练，没有独立验证集。

| 模型目录 | 微调后参数量 | FP32 ONNX 大小约 |
|---|---:|---:|
| mobilenet_v3_small | 1,520,931 | 5.9 MiB |
| mobilenet_v3_large | 4,205,875 | 17 MiB |
| efficientnet_b0 | 4,011,391 | 16 MiB |
| resnet18 | 11,178,051 | 43 MiB |

每个模型目录中，`model.pt` 是带结构信息、输入规范和类别顺序的 PyTorch 权重；`model_fp32.onnx` 为固定 `1×3×224×224` 的 FP32 ONNX；`metrics.json` 保存训练损失和预测结果。

## 试用 ONNX 模型

在可安装 ONNX Runtime 的 Python 3.11 环境中，进入本目录执行：

```bash
python -m pip install -r requirements.txt
python infer.py examples/01_empty.png --model mobilenet_v3_small
python infer.py examples/02_object_present.png --model mobilenet_v3_large
python infer.py examples/03_hand_present.png --model efficientnet_b0
python infer.py /path/to/your.jpg --model resnet18
```

类别为 `empty`（空带）、`object_present`（物块在位）和 `hand_present`（人工干预）；有手时优先归入人工干预。输出概率不是经过校准的可靠度，不应单独用于安全决策。

预处理：RGB，保持长宽比缩放并居中填充为 224×224，填充值 RGB 114；像素除以 255，再用 ImageNet 均值 `[0.485, 0.456, 0.406]` 和标准差 `[0.229, 0.224, 0.225]` 标准化。ONNX 输出为三类 logits。

## 验证与局限

四个模型在参与训练的 495 张上均为 495/495，在另外三张合成测试图上均为 3/3。合成测试图由训练中已使用的场景图编辑而来，样本很少，不能代表真实现场准确率。需要在后续新拍、未参与训练和调参的实拍图上检验。

四个 ONNX 均通过结构检查；同一组三张测试图上，ONNX 与 PyTorch 输出的最大绝对偏差小于 0.00004。`training/` 保存训练脚本和日志；脚本中的云服务器路径需要根据实际环境修改，训练图片未包含在此包中。

ONNX 是通用 FP32 格式，尚未量化，也未使用安路工具链编译或在 1 GB NPU 板端测试。具体算子支持、板端内存和速度须实测。
