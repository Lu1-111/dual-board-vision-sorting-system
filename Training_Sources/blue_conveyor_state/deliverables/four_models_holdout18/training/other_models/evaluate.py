import csv,hashlib,json,sys
from pathlib import Path
import numpy as np
import onnxruntime as ort
ROOT=Path(__file__).resolve().parents[2]
PACKAGE=ROOT/'deliverables/four_models_holdout18'
sys.path.insert(0,str(PACKAGE))
from infer import prepare,CLASSES,MODEL_NAMES

def main():
    tests=json.loads((ROOT/'runs/efficientnet_b0_holdout18/test.json').read_text())
    report={'train_count':477,'test_count':18,'epochs':10,'classes':CLASSES,'models':{}}
    table=[]
    for name in MODEL_NAMES:
        opts=ort.SessionOptions();opts.intra_op_num_threads=2
        model=PACKAGE/'models'/name/'model_fp32.onnx'
        session=ort.InferenceSession(str(model),sess_options=opts,providers=['CPUExecutionProvider'])
        rows=[];matrix=np.zeros((3,3),dtype=int)
        for test in tests:
            path=ROOT/'test'/test['image']
            logits=session.run(['logits'],{'images':prepare(path)})[0][0]
            probs=np.exp(logits-logits.max());probs/=probs.sum()
            prediction=CLASSES[int(probs.argmax())]
            matrix[CLASSES.index(test['label']),CLASSES.index(prediction)]+=1
            row={**test,'prediction':prediction,'correct':prediction==test['label'],'probabilities':dict(zip(CLASSES,map(float,probs)))}
            rows.append(row)
            table.append({'model':name,**{k:v for k,v in row.items() if k!='probabilities'}})
        metrics=json.loads((model.parent/'metrics.json').read_text())
        assert [r['prediction'] for r in rows]==[r['predicted'] for r in metrics['heldout_real_test']['predictions']]
        result={'correct':int(matrix.trace()),'total':18,'accuracy':float(matrix.trace()/18),'confusion_matrix':matrix.tolist(),'rows':rows,'model_sha256':hashlib.sha256(model.read_bytes()).hexdigest()}
        report['models'][name]=result
        print(json.dumps({'model':name,'correct':result['correct'],'confusion_matrix':matrix.tolist(),'errors':[r['image']+':'+r['prediction'] for r in rows if not r['correct']]},ensure_ascii=False))
    (PACKAGE/'evaluation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
    with (PACKAGE/'evaluation.csv').open('w',newline='',encoding='utf-8-sig') as f:
        writer=csv.DictWriter(f,fieldnames=['model','image','label','prediction','correct']);writer.writeheader();writer.writerows(table)
    readme='''# 四模型：排除18张实拍照片重新训练

四个模型均从 ImageNet 预训练权重开始，未载入旧工况分类权重。原495张训练图片排除T01–T18的对应副本后，使用477张训练10轮：空带115张、物块在位143张、人工干预219张。18张实拍图片只在训练结束后评估，不用于梯度更新或挑选训练轮次。EfficientNet-B0沿用本次排除18张后已完成的新模型，另三个模型并行重新训练。

配置：seed 42，batch 32，AdamW，学习率0.0003，weight decay 0.0001，余弦调度，BF16。保存最后一轮权重。

## 测试结果

|模型|正确数量|准确率|
|---|---:|---:|
'''
    for name,result in report['models'].items():
        readme+=f"|{name}|{result['correct']}/18|{result['accuracy']:.2%}|\n"
    readme+='''
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
'''
    (PACKAGE/'README.md').write_text(readme)
if __name__=='__main__':main()
