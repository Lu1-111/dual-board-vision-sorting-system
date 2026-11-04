# EfficientNet-B0：排除 T01–T18 重新训练

本版本从 ImageNet 预训练权重重新初始化，未载入之前的工况模型权重。原 495 张训练图片中排除了 T01–T18 的 18 张对应副本，其余 477 张参与训练：空带 115 张、物块在位 143 张、人工干预 219 张。

配置：10 轮，batch 32，AdamW，学习率 0.0003，weight decay 0.0001，余弦学习率调度，seed 42，BF16 混合精度。保存最后一轮模型；18 张实拍图片未用于梯度更新或挑选轮次。

## 使用
```bash
python -m pip install -r requirements.txt
python infer.py /path/to/image.jpg
```

models/efficientnet_b0/model.pt 为 PyTorch 权重，model_fp32.onnx 为 FP32 ONNX。类别顺序 empty（空带）、object_present（物块在位）、hand_present（人工干预），有手优先人工干预。输入处理与旧版一致：RGB，等比例缩放并居中补边至 224×224，补边 RGB 114，除以 255，ImageNet 均值和标准差标准化。

training/ 包含训练、导出验证脚本及数据划分记录，服务器路径需根据运行环境修改。evaluation.json 包含18张实拍图的逐图预测。训练图片不包含在包中。

本次排除了指定18张图及本地字节相同副本；未进行整个采集批次隔离或合成图来源隔离。因此小样本结果不能代表跨场景泛化性能。该测试集此前已被查看，应另采集全新图片用于最终评估。

ONNX 未量化，未转换为 MLK-F3P 板端格式。

本轮测试：16/18（88.89%）；空带 6/6、物块在位 4/6、人工干预 6/6。T05、T11 的物块在位被误判为空带。ONNX 与 PyTorch 的18张预测一致。
