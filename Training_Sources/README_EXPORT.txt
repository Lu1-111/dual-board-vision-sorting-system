训练源码导出说明

一、来源与版本
缺角模型：/home/lululu/triangle-defect，正式 V28 版本。
已按用户指定排除 triangle_defect_npu_retrain_20261103 及其他后续重训工程。
V28 模型参数：input_size=320、threshold=0.88、selected_epoch=95。
工况模型：/home/lululu/blue_conveyor_state，按远端当前目录原样快照，含尚未提交 Git 的最新训练交付源码。

二、入口
缺角 V28：triangle-defect/src/triangle_defect/train.py
模型定义：triangle-defect/src/triangle_defect/model.py
原训练参数：triangle-defect/reports/evidence/training_config.json
启动入口：triangle-defect/start.sh、run.py

工况最新交付训练：blue_conveyor_state/deliverables/four_models_holdout18/training/
含 efficientnet_b0 和 other_models 子目录；配套 infer.py、训练清单和数据划分记录保持原目录结构。
工况基础模型：blue_conveyor_state/src/train.py、src/optimize.py、src/model.py
比较训练：blue_conveyor_state/runs/four_model_comparison/train_compare.py

三、打包范围与运行准备
源码和配套文本原样复制，无训练代码修改；包含 .py/.sh、依赖清单、JSON/JSONL/YAML 配置、CSV 数据索引、说明和文本报告。
不含图片数据集、已训练权重、预训练权重、ONNX、虚拟环境、缓存、大型原始模型目录、训练日志和 Git 历史。
因此本包是源码归档，不是已包含全部资源的即开即训工程。运行前需补齐原数据和所需权重。
V28 需要 data/images 及 models/initialization/mobilenet_v3_features.pt；来源与哈希见模型目录中的 provenance.json。
工况最新训练脚本保留原始 /root/lfm-workcell 等绝对路径，运行前需根据实际数据和输出目录调整。
请先阅读各项目 README 和 requirements；归档未安装依赖或执行训练。

四、核对
SOURCE_MANIFEST.json 记录远端路径、Git 提交和工作区状态、每个源文件的大小、修改时间及 SHA-256。
SOURCE_SHA256SUMS.txt 可校验所有原样复制的源文件。
外部同名 .sha256 文件用于验证整个 ZIP。
