"""Reproducible CUDA training and same-lineage development validation."""
import argparse, csv, hashlib, json, random, time
from pathlib import Path
from collections import Counter
import numpy as np
from PIL import Image
import torch
from torch import nn
from torch.utils.data import Dataset, DataLoader, WeightedRandomSampler
from torchvision import transforms as T
from model import WorkcellTinyCNN, CLASSES, letterbox, scores

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / 'data'


def make_split(rows, seed):
    # Conservative near-duplicate grouping, in addition to exact SHA checks.
    # This cannot create independent scenes: all rows have one original lineage.
    thumbs, dhashes = [], []
    for row in rows:
        with Image.open(DATA / row['path']) as im:
            thumbs.append(np.asarray(im.convert('RGB').resize((64, 36)), dtype=np.float32))
            g = np.asarray(im.convert('L').resize((9, 8)), dtype=np.int16)
            dhashes.append(g[:, 1:] > g[:, :-1])
    parent = list(range(len(rows)))
    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    links = []
    for i in range(len(rows)):
        for j in range(i):
            if rows[i]['class_id'] != rows[j]['class_id']:
                continue
            distance = int(np.count_nonzero(dhashes[i] != dhashes[j]))
            mae = float(np.mean(np.abs(thumbs[i] - thumbs[j])))
            if rows[i]['sha256'] == rows[j]['sha256'] or (distance <= 2 and mae < 3.0):
                parent[find(i)] = find(j)
                links.append([rows[i]['sample_id'], rows[j]['sample_id'], distance, mae])
    groups = {}
    for i in range(len(rows)):
        groups.setdefault(find(i), []).append(i)
    rng = random.Random(seed)
    val = []
    for c in range(3):
        cg = [g for g in groups.values() if int(rows[g[0]]['class_id']) == c]
        target = round(sum(len(g) for g in cg) * .2)
        best, best_cost = None, float('inf')
        workflows = Counter(rows[i]['workflow_group'] for g in cg for i in g)
        for _ in range(1000):
            rng.shuffle(cg)
            picked = []
            for group in cg:
                if abs(len(picked) + len(group) - target) < abs(len(picked) - target):
                    picked.extend(group)
            actual = Counter(rows[i]['workflow_group'] for i in picked)
            cost = abs(len(picked)-target)*10 + sum(abs(actual[w] - n*.2) for w,n in workflows.items())
            if cost < best_cost and 0 < len(picked) < sum(len(g) for g in cg):
                best, best_cost = picked[:], cost
        if best is None:
            raise RuntimeError(f'No usable grouped split for class {c}')
        val.extend(best)
    val_set = set(val)
    for i, row in enumerate(rows):
        row['split'] = 'internal_val' if i in val_set else 'train'
        row['near_duplicate_group'] = f'cluster_{find(i):04d}'
    return rows, links


class Images(Dataset):
    def __init__(self, rows, augment):
        self.rows = rows
        self.images = [letterbox(DATA / r['path']) for r in rows]
        self.transform = T.Compose(([T.ColorJitter(.25,.2,.15,.015),
             T.RandomApply([T.GaussianBlur(3, sigma=(.1,.7))], p=.15)] if augment else []) + [T.ToTensor()])
    def __len__(self): return len(self.rows)
    def __getitem__(self, i):
        return self.transform(self.images[i]), int(self.rows[i]['class_id'])


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--epochs',type=int,default=140)
    parser.add_argument('--seed',type=int,default=20261102)
    parser.add_argument('--output',type=Path,default=ROOT/'runs'/'train_v1'); args=parser.parse_args()
    out=args.output; out.mkdir(parents=True,exist_ok=True)
    if (out/'frozen_model.json').exists():
        raise RuntimeError('Frozen run exists; use a new run directory for further experiments.')
    random.seed(args.seed); np.random.seed(args.seed); torch.manual_seed(args.seed)
    torch.set_num_threads(4)
    assert torch.cuda.is_available(), 'CUDA GPU is required; no CPU fallback.'
    torch.cuda.manual_seed_all(args.seed)
    torch.cuda.set_per_process_memory_fraction(.18)
    torch.backends.cudnn.benchmark=False
    torch.backends.cudnn.deterministic=True
    rows=json.loads((DATA/'manifest.json').read_text())
    assert len(rows)==331 and all(r['review_status']=='approved' for r in rows)
    rows, links=make_split(rows,args.seed)
    (out/'split_manifest.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2))
    (out/'near_duplicate_groups.json').write_text(json.dumps(links,indent=2))
    train=[r for r in rows if r['split']=='train']; val=[r for r in rows if r['split']=='internal_val']
    counts=Counter(int(r['class_id']) for r in train)
    generator=torch.Generator().manual_seed(args.seed)
    sampler=WeightedRandomSampler([1/counts[int(r['class_id'])] for r in train],len(train),replacement=True,generator=generator)
    tl=DataLoader(Images(train,True),batch_size=24,sampler=sampler,num_workers=2,pin_memory=True)
    vl=DataLoader(Images(val,False),batch_size=24,shuffle=False,num_workers=0,pin_memory=True)
    model=WorkcellTinyCNN().cuda()
    optimizer=torch.optim.AdamW(model.parameters(),lr=.002,weight_decay=.008)
    scheduler=torch.optim.lr_scheduler.CosineAnnealingLR(optimizer,T_max=args.epochs,eta_min=2e-5)
    criterion=nn.CrossEntropyLoss(label_smoothing=.03)
    config=dict(seed=args.seed,max_epochs=args.epochs,batch_size=24,learning_rate=.002,weight_decay=.008,
        optimizer='AdamW',scheduler='cosine',label_smoothing=.03,balanced_sampling=True,
        gpu=torch.cuda.get_device_name(0),torch=torch.__version__,cuda=torch.version.cuda,
        train_count=len(train),internal_val_count=len(val),train_class_counts=dict(counts),
        val_class_counts=dict(Counter(int(r['class_id']) for r in val)),
        validation_limitation='Same original lineage; development diagnostics only, not independent real-world validation.',
        parameter_count=sum(p.numel() for p in model.parameters()),classes=CLASSES)
    (out/'config.json').write_text(json.dumps(config,indent=2))
    print(json.dumps(config),flush=True)
    best_key=(-1,float('-inf')); best_epoch=0; history=[]; start=time.time()
    for epoch in range(1,args.epochs+1):
        model.train(); running=0.; n=0
        for x,y in tl:
            x,y=x.cuda(non_blocking=True),y.cuda(non_blocking=True)
            optimizer.zero_grad(set_to_none=True)
            logits=model(x); loss=criterion(logits,y)
            loss.backward(); nn.utils.clip_grad_norm_(model.parameters(),5.0); optimizer.step()
            running+=loss.item()*len(y); n+=len(y)
        model.eval(); ys=[]; preds=[]; probabilities=[]; val_loss=0
        with torch.no_grad():
            for x,y in vl:
                x,y=x.cuda(),y.cuda(); logits=model(x)
                val_loss+=nn.functional.cross_entropy(logits,y).item()*len(y)
                ys.extend(y.cpu().tolist()); preds.extend(logits.argmax(1).cpu().tolist())
                probabilities.extend(logits.softmax(1).cpu().tolist())
        metric=scores(ys,preds); vl_value=val_loss/len(val)
        entry=dict(epoch=epoch,train_loss=running/n,val_loss=vl_value,accuracy=metric['accuracy'],macro_f1=metric['macro_f1'],elapsed_seconds=time.time()-start)
        history.append(entry)
        # Primary criterion is macro-F1. Cross entropy breaks ties.
        key=(round(metric['macro_f1'],8),-vl_value)
        if key>best_key:
            best_key=key; best_epoch=epoch
            torch.save({'state_dict':{k:v.cpu() for k,v in model.state_dict().items()},'config':config,'epoch':epoch},out/'best.pt')
            (out/'internal_validation.json').write_text(json.dumps(dict(metrics=metric,epoch=epoch,predictions=[dict(sample_id=r['sample_id'],label=y,prediction=p,probabilities=prob) for r,y,p,prob in zip(val,ys,preds,probabilities)]),indent=2))
        scheduler.step()
        (out/'history.json').write_text(json.dumps(history,indent=2))
        if epoch%5==0 or epoch==1: print(json.dumps(entry),flush=True)
        if epoch>=60 and epoch-best_epoch>=40: break
    artifact=out/'best.pt'
    frozen=dict(selected_epoch=best_epoch,trained_epochs=epoch,training_seconds=time.time()-start,
        checkpoint_sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),
        cuda_peak_allocated_mib=torch.cuda.max_memory_allocated()/2**20,
        cuda_peak_reserved_mib=torch.cuda.max_memory_reserved()/2**20,
        frozen_unix_time=time.time(),test_images_used_for_training_or_selection=False)
    (out/'frozen_model.json').write_text(json.dumps(frozen,indent=2))
    print('FROZEN '+json.dumps(frozen),flush=True)

if __name__=='__main__': main()
