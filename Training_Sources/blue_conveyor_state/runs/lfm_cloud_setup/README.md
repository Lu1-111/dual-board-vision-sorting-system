# LFM2.5-VL-450M 工况微调环境

工作目录 /root/lfm-workcell，Python 环境 .venv；模型在 models/LFM2.5-VL-450M。

已验证：A100 40 GB、Python 3.11.13、PyTorch 2.6.0+cu124、Transformers 5.22.1、PEFT 0.21.2、TRL 1.17.1、Accelerate 1.15.0。
依赖版本见 requirements.lock.txt，pip check 无冲突。
模型权重 SHA256：2f6deb5dd43707de5cfe3c59470d3bccf4c3112a810a74570499f4728d412eea。

## 数据

上传新采集图片至 data/，建立 data/train.jsonl 和 data/val.jsonl。
每行是 image、label 两个字段的 JSON，路径相对于清单所在目录；格式见 data/manifest.example.jsonl。
标签为 empty / object_present / hand_present，优先级 hand_present > object_present > empty。
同一拍摄批次的相邻帧不要跨训练和验证集合。
如所有图片都参与训练，train_config.json 中 val_manifest 可设为 null；此时没有验证选优，保存最终参数。
测试集应另行采集并保留，不写入训练清单。

## 配置与运行

train_config.json：3 轮、BF16 LoRA r16 alpha32 dropout0.05，学习率 1e-4，batch 4，累积 4（有效 batch16），cosine，warmup 5%，冻结视觉编码器及投影层，只对语言部分线性层添加 LoRA。关闭图片切分，使用原模型处理器保持长宽比。
仅助手答案计入损失。每轮验证并保存，以验证损失选择检查点；工况分类指标仍需独立实拍测试。

启动：bash /root/lfm-workcell/start_train.sh
日志：tail -f /root/lfm-workcell/train.log
输出：/root/lfm-workcell/outputs/workcell_lora（LoRA 适配器及处理器，需结合基座模型使用）
环境检查：.venv/bin/python train.py --smoke

## 已完成的检查

使用两张人工构造的 512x288 图片完成 1 个优化步，检查答案掩码、非零梯度、有限损失及 LoRA 保存。
耗时约 1 秒（不含加载），峰值张量显存 1.25 GiB。该检查只验证运行，不验证识别效果或正式训练速度。
详细日志 smoke.log，适配器 outputs/smoke，不能作为工况成品模型。
PyTorch 参考卷积路径可以正常训练；本配置未安装可选 causal-conv1d 加速扩展。

云盘 /mnt 仅 5 GB；当前环境保存在 /root，实例释放前需下载训练成果并确认平台存储保留规则。
