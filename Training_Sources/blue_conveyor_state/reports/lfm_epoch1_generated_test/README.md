# LFM 第一轮：生成图片测试

三张图片在训练后生成，以参与过训练的 T01 实拍照片为编辑底图；画面高度相关，不是独立实拍测试。三张均由人工检查类别，且与 504 张源图片无 SHA-256 完全重复。未参与训练。

| 图片 | 预期 | 输出 | 结果 |
|---|---|---|---|
| 01_empty.png | empty | empty | 正确 |
| 02_object_present.png | object_present | object_present | 正确 |
| 03_hand_present.png | hand_present | hand_present | 正确 |

A100 上加载第一轮 LoRA 和基座，使用训练提示词、确定性生成和训练时的处理器，最大生成 12 token。三张结果不能推断真实部署准确率。详情见 results.json。
