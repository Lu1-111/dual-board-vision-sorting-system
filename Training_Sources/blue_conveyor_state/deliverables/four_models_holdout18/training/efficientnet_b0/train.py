"""Train and compare four pretrained classifiers on the same workcell dataset."""
import json
import os
import random
import time
from collections import Counter
from pathlib import Path

import numpy as np
import onnx
import torch
from PIL import Image, ImageOps
from torch import nn
from torch.utils.data import DataLoader, Dataset
from torchvision import models, transforms
from torchvision.transforms import functional as F

ROOT = Path('/root/lfm-workcell')
OUT = ROOT / 'cnn_compare' / 'run_holdout18'
TRAIN = ROOT / 'data/epoch1/train_holdout18.jsonl'
TEST = ROOT / 'new_test/images'
CLASSES = ['empty', 'object_present', 'hand_present']
MEAN = [0.485, 0.456, 0.406]
STD = [0.229, 0.224, 0.225]
SEED = 42
EPOCHS = 10
BATCH = 32
SIZE = 224

def letterbox(image):
    image = ImageOps.exif_transpose(image).convert('RGB')
    width, height = image.size
    scale = min(SIZE / width, SIZE / height)
    resized = image.resize((max(1, round(width*scale)), max(1, round(height*scale))), Image.Resampling.BILINEAR)
    canvas = Image.new('RGB', (SIZE, SIZE), (114, 114, 114))
    canvas.paste(resized, ((SIZE-resized.width)//2, (SIZE-resized.height)//2))
    return canvas

class Workcell(Dataset):
    def __init__(self, rows, training):
        self.rows = rows
        self.training = training
        self.jitter = transforms.ColorJitter(brightness=0.12, contrast=0.12, saturation=0.08)
        self.images = []
        for row in rows:
            with Image.open(row['path']) as image:
                self.images.append(letterbox(image))
    def __len__(self): return len(self.rows)
    def __getitem__(self, i):
        image = self.images[i]
        if self.training:
            image = self.jitter(image)
        return F.normalize(F.to_tensor(image), MEAN, STD), CLASSES.index(self.rows[i]['label'])

def build(name):
    if name == 'mobilenet_v3_small':
        model = models.mobilenet_v3_small(weights=models.MobileNet_V3_Small_Weights.DEFAULT)
        model.classifier[-1] = nn.Linear(model.classifier[-1].in_features, 3)
    elif name == 'mobilenet_v3_large':
        model = models.mobilenet_v3_large(weights=models.MobileNet_V3_Large_Weights.DEFAULT)
        model.classifier[-1] = nn.Linear(model.classifier[-1].in_features, 3)
    elif name == 'efficientnet_b0':
        model = models.efficientnet_b0(weights=models.EfficientNet_B0_Weights.DEFAULT)
        model.classifier[-1] = nn.Linear(model.classifier[-1].in_features, 3)
    elif name == 'resnet18':
        model = models.resnet18(weights=models.ResNet18_Weights.DEFAULT)
        model.fc = nn.Linear(model.fc.in_features, 3)
    else: raise ValueError(name)
    return model

def metrics(model, dataset, device):
    loader = DataLoader(dataset, batch_size=BATCH, shuffle=False, num_workers=0)
    matrix = torch.zeros((3,3), dtype=torch.int64)
    preds = []
    model.eval()
    with torch.inference_mode():
        for images, labels in loader:
            logits = model(images.to(device))
            probs = logits.softmax(-1).cpu()
            for label, probability in zip(labels.tolist(), probs):
                pred = int(probability.argmax())
                matrix[label,pred] += 1
                preds.append({'expected': CLASSES[label], 'predicted': CLASSES[pred],
                              'confidence': round(float(probability[pred]), 6), 'correct': label == pred})
    scores=[]
    for i in range(3):
        tp=int(matrix[i,i]); fp=int(matrix[:,i].sum())-tp; fn=int(matrix[i,:].sum())-tp
        scores.append((2*tp/(2*tp+fp+fn)) if 2*tp+fp+fn else 0.0)
    return {'accuracy': float(matrix.diag().sum()/max(1,matrix.sum())),
            'macro_f1': sum(scores)/3, 'confusion_matrix': matrix.tolist(), 'predictions': preds}

def main():
    if not torch.cuda.is_available(): raise RuntimeError('CUDA required')
    OUT.mkdir(parents=True, exist_ok=True)
    rows=[]
    for line in TRAIN.read_text().splitlines():
        r=json.loads(line); rows.append({'path': str(TRAIN.parent/r['image']), 'label':r['label']})
    assert len(rows)==477 and all(Path(r['path']).exists() and r['label'] in CLASSES for r in rows)
    tests=[{'path':str(ROOT/'holdout18'/r['image']), 'label':r['label']} for r in json.loads((ROOT/'holdout18/test.json').read_text())]
    assert len(tests)==18 and not ({Path(r['path']).name for r in rows} & {Path(r['path']).name for r in tests})
    counts=Counter(r['label'] for r in rows)
    print('DATA',json.dumps({'train_count':len(rows),'counts':dict(counts),'epochs':EPOCHS,'batch_size':BATCH}),flush=True)
    train_data=Workcell(rows,True)
    train_eval=Workcell(rows,False)
    test_data=Workcell(tests,False)
    weights=torch.tensor([len(rows)/(3*counts[c]) for c in CLASSES],device='cuda')
    all_results=[]
    for name in ['efficientnet_b0']:
        random.seed(SEED); np.random.seed(SEED); torch.manual_seed(SEED); torch.cuda.manual_seed_all(SEED)
        loader=DataLoader(train_data,batch_size=BATCH,shuffle=True,num_workers=0,
                          pin_memory=True,generator=torch.Generator().manual_seed(SEED))
        model=build(name).to('cuda')
        optimizer=torch.optim.AdamW(model.parameters(),lr=0.0003,weight_decay=0.0001)
        scheduler=torch.optim.lr_scheduler.CosineAnnealingLR(optimizer,T_max=EPOCHS)
        criterion=nn.CrossEntropyLoss(weight=weights)
        torch.cuda.reset_peak_memory_stats()
        start=time.monotonic(); history=[]
        for epoch in range(EPOCHS):
            model.train(); running=0; total=0
            for x,y in loader:
                x,y=x.to('cuda',non_blocking=True),y.to('cuda',non_blocking=True)
                optimizer.zero_grad(set_to_none=True)
                with torch.autocast('cuda',dtype=torch.bfloat16):
                    logits=model(x); loss=criterion(logits,y)
                if not torch.isfinite(loss): raise RuntimeError(f'{name}: non-finite loss')
                loss.backward(); optimizer.step()
                running+=loss.item()*len(y);total+=len(y)
            scheduler.step()
            item={'epoch':epoch+1,'train_loss':running/total,'samples':total}
            history.append(item)
            print('EPOCH',name,json.dumps(item),flush=True)
        torch.cuda.synchronize()
        duration=time.monotonic()-start
        train_metrics=metrics(model,train_eval,'cuda')
        test_metrics=metrics(model,test_data,'cuda')
        model_dir=OUT/name;model_dir.mkdir(exist_ok=True)
        state={k:v.detach().cpu() for k,v in model.state_dict().items()}
        torch.save({'architecture':name,'classes':CLASSES,'state_dict':state,
                    'preprocessing':{'size':SIZE,'letterbox_rgb':[114,114,114], 'mean':MEAN,'std':STD},
                    'seed':SEED,'epochs':EPOCHS},model_dir/'model.pt')
        model=model.cpu().eval()
        torch.onnx.export(model,torch.zeros(1,3,SIZE,SIZE),str(model_dir/'model_fp32.onnx'),
                          input_names=['images'],output_names=['logits'],opset_version=17,
                          dynamic_axes=None,do_constant_folding=True)
        onnx.checker.check_model(str(model_dir/'model_fp32.onnx'))
        result={'model':name,'parameters':sum(p.numel() for p in model.parameters()),
                'training_seconds':round(duration,2),'peak_gpu_gib':round(torch.cuda.max_memory_allocated()/2**30,3),
                'history':history,'train':train_metrics,'heldout_real_test':test_metrics,
                'train_count':len(rows),'epochs':EPOCHS,'batch_size':BATCH}
        (model_dir/'metrics.json').write_text(json.dumps(result,ensure_ascii=False,indent=2))
        all_results.append(result)
        (OUT/('summary_'+name+'.json')).write_text(json.dumps(all_results,ensure_ascii=False,indent=2))
        print('FINISHED',name,json.dumps({'seconds':round(duration,2),'train_acc':train_metrics['accuracy'],
                'test_acc':test_metrics['accuracy']}),flush=True)
        del model,optimizer,scheduler,criterion
        torch.cuda.empty_cache()
    print('ALL_DONE',flush=True)

if __name__ == '__main__': main()
