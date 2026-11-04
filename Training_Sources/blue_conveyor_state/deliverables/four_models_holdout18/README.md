# 四模型：排除18张实拍照片重新训练

四个模型均从 ImageNet 预训练权重开始，未载入旧工况分类权重。原495张训练图片排除T01–T18的对应副本后，使用477张训练10轮：空带115张、物块在位143张、人工干预219张。18张实拍图片只在训练结束后评估，不用于梯度更新或挑选训练轮次。EfficientNet-B0沿用本次排除18张后已完成的新模型，另三个模型并行重新训练。

配置：seed 42，batch 32，AdamW，学习率0.0003，weight decay 0.0001，余弦调度，BF16。保存最后一轮权重。

## 测试结果

|模型|正确数量|准确率|
|---|---:|---:|
|mobilenet_v3_small|17/18|94.44%|
|mobilenet_v3_large|15/18|83.33%|
|efficientnet_b0|16/18|88.89%|
|resnet18|17/18|94.44%|

详细结果见 evaluation.json / evaluation.csv，类别顺序：empty（空带）、object_present（物块在位）、hand_present（人工干预）。有手优先人工干预。

## 使用

```bash
python -m pip install -r requirements.txt
python infer.py /path/to/image.jpg --model mobilenet_v3_small
python infer.py /path/to/image.jpg --model mobilenet_v3_large
python infer.py /path/to/image.jpg --model efficientnet_b0
python infer.py /path/to/image.jpg --model resnet18
```

models/ 各子目录包含 model.pt、model_fp32.onnx、metrics.json。training/ 保存训练脚本及划分记录；服务器路径需按实际运行环境调整，训练图片未包含在包内。

输入：RGB，等比例缩放并居中补边至224×224，补边RGB114；像素除以255，按ImageNet均值[0.485,0.456,0.406]和标准差[0.229,0.224,0.225]标准化，float32 NCHW。输入名images，输出名logits。使用配套infer.py避免预处理不一致。

本次排除了指定18张图及本地字节相同副本，未做整个采集批次或合成图来源隔离；18张样本较少，且此前已查看，不能代表全新场景的泛化准确率。

ONNX为FP32，尚未量化或转换为MLK-F3P板端格式。verification.jsonl保存ONNX与PyTorch数值验证结果。
