"""CUDA fine-tuning with stronger photometric augmentation and a disclosed dev set.

Batch02 is now development data for checkpoint selection, never a fresh test.
Its pixels are not used for gradient updates. The original split is unchanged.
"""
import argparse
import hashlib
import json
import random
import time
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image
import torch
from torch import nn
from torch.utils.data import Dataset, DataLoader, WeightedRandomSampler
from torchvision import transforms as T
from torchvision.transforms import functional as F

from model import CLASSES, WorkcellTinyCNN, letterbox, scores

ROOT = Path(__file__).resolve().parents[1]


class Images(Dataset):
    def __init__(self, records, augment=False):
        self.records = records
        self.images = [letterbox(ROOT/r['project_path']) for r in records]
        self.augment = augment
        self.jitter = T.ColorJitter(brightness=.4, contrast=.25, saturation=.25, hue=.012)

    def __len__(self):
        return len(self.records)

    def __getitem__(self, index):
        im = self.images[index].copy()
        if self.augment:
            # Change illumination inside the image; preserve the input-contract padding.
            row = self.records[index]
            with Image.open(ROOT/row['project_path']) as source:
                w,h = source.size
            scale = 224/max(w,h)
            nw,nh = (max(1,int(v*scale+.5)) for v in (w,h))
            left,top = (224-nw)//2,(224-nh)//2
            body = im.crop((left,top,left+nw,top+nh))
            if random.random()<.85:
                body = self.jitter(body)
                body = F.adjust_gamma(body, random.uniform(.65,1.65))
                x = np.asarray(body,dtype=np.float32)/255
                temperature = random.uniform(-.20,.20)
                x *= np.array([1+temperature,random.uniform(.94,1.06),1-temperature],dtype=np.float32)
                if random.random()<.35:
                    # A smooth spatial illumination field, without changing scene geometry.
                    field = np.array([[random.uniform(.55,1.1) for _ in range(2)] for _ in range(2)],dtype=np.float32)
                    shade = np.asarray(Image.fromarray(field).resize((nw,nh),Image.Resampling.BILINEAR))
                    x *= shade[:,:,None]
                if random.random()<.2:
                    x += np.random.normal(0,random.uniform(.002,.012),x.shape).astype(np.float32)
                body = Image.fromarray(np.uint8(np.clip(x,0,1)*255))
                if random.random()<.15:
                    body = F.gaussian_blur(body,3,random.uniform(.1,.8))
            im.paste(body,(left,top))
        return F.to_tensor(im),int(self.records[index]['class_id'])


def evaluate(model, loader):
    model.eval(); labels=[]; predictions=[]; probabilities=[]; loss=0
    with torch.inference_mode():
        for x,y in loader:
            x,y=x.cuda(),y.cuda()
            logits=model(x)
            loss+=F_torch.cross_entropy(logits,y,reduction='sum').item()
            labels.extend(y.cpu().tolist())
            predictions.extend(logits.argmax(1).cpu().tolist())
            probabilities.extend(logits.softmax(1).cpu().tolist())
    metric=scores(labels,predictions)
    metric.update(loss=loss/len(labels),uncertain_count=sum(max(p)<.8 for p in probabilities))
    return metric,[dict(label=y,prediction=p,probabilities=prob) for y,p,prob in zip(labels,predictions,probabilities)]


F_torch = nn.functional


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--epochs',type=int,default=100)
    parser.add_argument('--seed',type=int,default=20261103)
    parser.add_argument('--lr',type=float,default=.0005)
    parser.add_argument('--initial-checkpoint',type=Path,default=ROOT/'models/best.pt')
    args=parser.parse_args();out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=True)
    assert not (out/'best.pt').exists(), 'Choose a new run directory'
    assert torch.cuda.is_available(), 'CUDA GPU is required'
    random.seed(args.seed);np.random.seed(args.seed);torch.manual_seed(args.seed)
    torch.cuda.manual_seed_all(args.seed);torch.set_num_threads(4)
    torch.backends.cudnn.benchmark=False;torch.backends.cudnn.deterministic=True
    torch.cuda.set_per_process_memory_fraction(.25)
    rows=json.loads((ROOT/'data/manifest.json').read_text())
    train=[dict(r,project_path='data/'+r['path']) for r in rows if r['split']=='train']
    internal=[dict(r,project_path='data/'+r['path']) for r in rows if r['split']=='internal_val']
    dev=[dict(r,project_path='data/test_batches/batch02/'+r['path'],class_id=CLASSES.index(r['expected_class']))
         for r in json.loads((ROOT/'data/test_batches/batch02/manifest.json').read_text()) if r['suite']=='classification']
    assert (len(train),len(internal),len(dev))==(264,67,24)
    assert not {r['sha256'] for r in train}&{r['sha256'] for r in internal+dev}
    counts=Counter(int(r['class_id']) for r in train)
    sampler=WeightedRandomSampler([1/counts[int(r['class_id'])] for r in train],len(train),replacement=True,
        generator=torch.Generator().manual_seed(args.seed))
    tl=DataLoader(Images(train,True),batch_size=24,sampler=sampler,num_workers=2,pin_memory=True,persistent_workers=True)
    vl=DataLoader(Images(internal),batch_size=24,num_workers=0)
    dl=DataLoader(Images(dev),batch_size=24,num_workers=0)
    initial=args.initial_checkpoint.resolve()
    checkpoint=torch.load(initial,map_location='cpu',weights_only=False)
    model=WorkcellTinyCNN().cuda();model.load_state_dict(checkpoint['state_dict'])
    optimizer=torch.optim.AdamW(model.parameters(),lr=args.lr,weight_decay=.005)
    scheduler=torch.optim.lr_scheduler.CosineAnnealingLR(optimizer,args.epochs,eta_min=1e-5)
    criterion=nn.CrossEntropyLoss(label_smoothing=.015)
    config=dict(seed=args.seed,max_epochs=args.epochs,batch_size=24,learning_rate=args.lr,weight_decay=.005,
        label_smoothing=.015,optimizer='AdamW',scheduler='cosine',gpu=torch.cuda.get_device_name(0),
        torch=torch.__version__,cuda=torch.version.cuda,parameter_count=sum(p.numel() for p in model.parameters()),
        classes=CLASSES,train_count=264,internal_val_count=67,known_dev_count=24,
        initial_checkpoint_sha256=hashlib.sha256(initial.read_bytes()).hexdigest(),
        selection='Maximize minimum internal/dev macro-F1, then mean macro-F1, then minimize mean cross entropy.',
        known_dev='batch02 classification images, previously tested; used for checkpoint selection, not gradient updates.',
        augmentation=dict(brightness=.4,contrast=.25,saturation=.25,hue=.012,gamma=[.65,1.65],
            red_blue_temperature=[-.2,.2],smooth_shadow_probability=.35,blur_probability=.15),
        original_split_unchanged=True,new_test_used=False)
    (out/'config.json').write_text(json.dumps(config,indent=2)+'\n')
    (out/'split_manifest.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(config),flush=True)
    history=[];best_key=(-1,-1,float('-inf'));best_epoch=0;start=time.time()
    for epoch in range(args.epochs+1):
        running=0;n=0
        if epoch:
            model.train()
            for x,y in tl:
                x,y=x.cuda(non_blocking=True),y.cuda(non_blocking=True)
                optimizer.zero_grad(set_to_none=True)
                loss=criterion(model(x),y);loss.backward()
                nn.utils.clip_grad_norm_(model.parameters(),5)
                optimizer.step();running+=loss.item()*len(y);n+=len(y)
            scheduler.step()
        iv,ip=evaluate(model,vl);dv,dp=evaluate(model,dl)
        key=(round(min(iv['macro_f1'],dv['macro_f1']),8),round((iv['macro_f1']+dv['macro_f1'])/2,8),-(iv['loss']+dv['loss'])/2)
        entry=dict(epoch=epoch,train_loss=running/max(n,1),internal=iv,known_dev=dv,elapsed_seconds=time.time()-start)
        history.append(entry)
        if key>best_key:
            best_key=key;best_epoch=epoch
            torch.save(dict(state_dict={k:v.detach().cpu() for k,v in model.state_dict().items()},config=config,epoch=epoch),out/'best.pt')
            (out/'validation.json').write_text(json.dumps(dict(epoch=epoch,internal=iv,known_dev=dv,
                internal_predictions=[dict(p,sample_id=r['sample_id']) for r,p in zip(internal,ip)],
                known_dev_predictions=[dict(p,id=r['id']) for r,p in zip(dev,dp)]),indent=2)+'\n')
        (out/'history.json').write_text(json.dumps(history,indent=2)+'\n')
        if epoch%10==0:
            print(json.dumps(dict(epoch=epoch,train_loss=entry['train_loss'],internal_accuracy=iv['accuracy'],
                dev_accuracy=dv['accuracy'],dev_loss=dv['loss'],dev_uncertain=dv['uncertain_count'],best_epoch=best_epoch)),flush=True)
    frozen=dict(selected_epoch=best_epoch,trained_epochs=args.epochs,training_seconds=time.time()-start,
        checkpoint_sha256=hashlib.sha256((out/'best.pt').read_bytes()).hexdigest(),
        cuda_peak_allocated_mib=torch.cuda.max_memory_allocated()/2**20,
        cuda_peak_reserved_mib=torch.cuda.max_memory_reserved()/2**20,frozen_unix_time=time.time(),
        known_batch02_used_for_selection=True,new_test_images_used_for_training_or_selection=False)
    (out/'frozen_model.json').write_text(json.dumps(frozen,indent=2)+'\n')
    print('FROZEN '+json.dumps(frozen),flush=True)


if __name__=='__main__':
    main()
