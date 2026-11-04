"""V28 training recipe. Test images are never loaded during training."""
from pathlib import Path
from datetime import datetime
from zoneinfo import ZoneInfo
import argparse, json, math, time, random, hashlib
import numpy as np
from PIL import Image
import torch
from torch.nn import functional as F
from .model import DefectNet
from .paths import ROOT, MANIFEST, INITIALIZATION, project_path

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--dataset', type=Path, default=MANIFEST)
    p.add_argument('--output', type=Path, default=ROOT / 'runs' / datetime.now().strftime('%Y%m%d-%H%M%S'))
    p.add_argument('--epochs', type=int, default=100)
    p.add_argument('--seed', type=int, default=20261128)
    p.add_argument('--batch', type=int, default=32)
    p.add_argument('--device', default='cuda' if torch.cuda.is_available() else 'cpu')
    p.add_argument('--check-every', type=int, default=5)
    args = p.parse_args()
    if min(args.epochs, args.batch, args.check_every) < 1:
        p.error('epochs, batch and check-every must be positive')
    out = args.output
    out.mkdir(exist_ok=False, parents=True)
    torch.set_num_threads(4)
    torch.manual_seed(args.seed)
    random.seed(args.seed)
    np.random.seed(args.seed)
    torch.backends.cudnn.benchmark = False
    plan = json.loads(args.dataset.read_text())
    rows = plan['records']
    train = [i for i, r in enumerate(rows) if r['split'] == 'train']
    val = [i for i, r in enumerate(rows) if r['split'] == 'val']
    device = args.device
    size = 320
    active = train + val
    remap = {i: j for j, i in enumerate(active)}
    data = [rows[i] for i in active]
    train = [remap[i] for i in train]
    val = [remap[i] for i in val]
    arr = []
    pm = []
    nm = []
    yy, xx = np.mgrid[0:size, 0:size]
    xx = (xx + 0.5) / size
    yy = (yy + 0.5) / size
    for r in data:
        with Image.open(project_path(r['path'])) as im:
            arr.append(np.asarray(im.convert('L').resize((size, size), Image.Resampling.BILINEAR), np.float32) / 255)
        pos = np.zeros((size, size), np.float32)
        neg = np.ones_like(pos) if not r['label'] or 'bbox' in r else np.zeros_like(pos)
        if r['label'] and 'bbox' in r:
            x0, y0, x1, y1 = r['bbox']
            cx = (x0 + x1) / 2
            cy = (y0 + y1) / 2
            rx = max((x1 - x0) * 0.26, 0.012)
            ry = max((y1 - y0) * 0.26, 0.012)
            pos[((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2 <= 1] = 1
            neg[(xx >= x0 - 0.025) & (xx <= x1 + 0.025) & (yy >= y0 - 0.025) & (yy <= y1 + 0.025)] = 0
        pm.append(pos)
        nm.append(neg)
    images = torch.tensor(np.stack(arr), device=device)[:, None]
    positives = torch.tensor(np.stack(pm), device=device)[:, None]
    negatives = torch.tensor(np.stack(nm), device=device)[:, None]
    labels = torch.tensor([r['label'] for r in data], device=device, dtype=torch.float32)
    config = {'created_at': datetime.now(ZoneInfo('Asia/Shanghai')).isoformat(), 'seed': args.seed, 'depth': 0, 'epochs': args.epochs, 'validation_interval': args.check_every, 'device': args.device, 'batch': args.batch, 'size': size, 'dataset_sha256': hashlib.sha256(args.dataset.read_bytes()).hexdigest(), 'counts': plan['counts'], 'sampling': 'Uniform shuffled pass over every training image per epoch, no real/AI source distinction; class-weighted BCE only. Known optional approximate boxes provide auxiliary localization.', 'selection': 'At each validation interval (default5 epochs) and final epoch: optimize threshold on validation grid0.10..0.95 step0.01 for balanced accuracy, then fewer FN, then largest nearest score margin. Select checkpoint by balanced accuracy, fewer FN, then validation BCE. Test inaccessible during training.', 'initialization': 'Torchvision official MobileNetV3Small ImageNet1K_V1 features0..8, grayscale replicated3 channels; lower learning rate0.1x for backbone; local8x and16x feature fusion; no RGB information', 'evaluation_scope': 'Train/validation/test image groups remain unchanged after dropping the whole out-of-scope supplemental batch. These are internal grouped regression/holdout measurements; former model results were inspected during development, so not independent field validation.', 'aggregation': 'Mean of4 highest local logits then sigmoid; full image scored; standard pretrained backbone padding.'}
    (out / 'config.json').write_text(json.dumps(config, ensure_ascii=False, indent=2))
    model = DefectNet()
    model.features.load_state_dict(torch.load(INITIALIZATION, map_location='cpu', weights_only=True))
    model = model.to(device)
    opt = torch.optim.AdamW([{'params': model.features.parameters(), 'lr_scale': 0.1}, {'params': model.head.parameters(), 'lr_scale': 1.0}], lr=0.001, weight_decay=0.001)
    print('CONFIG', config, 'parameters', sum((p.numel() for p in model.parameters())), flush=True)
    axis = torch.linspace(-1, 1, size, device=device)
    gy, gx = torch.meshgrid(axis, axis, indexing='ij')

    def augment(x, p, n):
        b = len(x)
        angle = torch.randint(4, (b,), device=device) * math.pi / 2 + (torch.rand(b, device=device) - 0.5) * math.pi / 9
        co = angle.cos()
        si = angle.sin()
        scale = (0.76 + 0.24 * torch.rand(b, device=device)) / (co.abs() + si.abs())
        th = torch.zeros(b, 2, 3, device=device)
        th[:, 0, 0] = co / scale
        th[:, 0, 1] = si / scale
        th[:, 1, 0] = -si / scale
        th[:, 1, 1] = co / scale
        th[:, :, 2] = (torch.rand(b, 2, device=device) - 0.5) * 0.06
        th[:, 0, :] *= torch.where(torch.rand(b, device=device) > 0.5, 1.0, -1.0)[:, None]
        grid = F.affine_grid(th, x.shape, align_corners=False)
        x = F.grid_sample(x, grid, padding_mode='border', align_corners=False)
        p = F.grid_sample(p, grid, align_corners=False)
        n = F.grid_sample(n, grid, padding_mode='border', align_corners=False)
        shape = (b, 1, 1, 1)
        x = x.pow(0.7 + 0.7 * torch.rand(shape, device=device)) * (0.7 + 0.6 * torch.rand(shape, device=device))
        x *= 1 + (torch.rand(shape, device=device) - 0.5) * 0.3 * gx + (torch.rand(shape, device=device) - 0.5) * 0.3 * gy
        if torch.rand(()) < 0.25:
            x = 0.7 * x + 0.3 * F.avg_pool2d(x, 3, 1, 1)
        return ((x + torch.randn_like(x) * torch.rand(shape, device=device) * 0.003).clamp(0, 1), p, n)

    def evaluate():
        model.eval()
        probs = []
        with torch.inference_mode():
            for k in range(0, len(val), 16):
                probs += model.score(images[val[k:k + 16]]).cpu().tolist()
        y = [data[i]['label'] for i in val]
        trials = []
        for t in [round(0.1 + j * 0.01, 2) for j in range(86)]:
            cm = [[sum((yy == a and int(pp >= t) == b for yy, pp in zip(y, probs))) for b in [0, 1]] for a in [0, 1]]
            tn, fp = cm[0]
            fn, tp = cm[1]
            ba = 0.5 * (tn / (tn + fp) + tp / (tp + fn))
            margin = min((abs(pp - t) for pp in probs))
            trials.append(dict(threshold=t, confusion=cm, ba=ba, key=[ba, -fn, margin]))
        chosen = max(trials, key=lambda r: tuple(r['key']))
        loss = float(np.mean([-math.log(max(pp if yy else 1 - pp, 1e-07)) for yy, pp in zip(y, probs)]))
        return dict(chosen, loss=loss, ids=[data[i]['id'] for i in val], probabilities=probs)
    best = None
    logs = []
    start = time.time()
    n0 = sum((data[i]['label'] == 0 for i in train))
    n1 = len(train) - n0
    classweight = torch.tensor([len(train) / (2 * n0), len(train) / (2 * n1)], device=device)
    for epoch in range(1, args.epochs + 1):
        model.train()
        order = [train[j] for j in torch.randperm(len(train)).tolist()]
        losses = []
        for k in range(0, len(order), args.batch):
            ids = order[k:k + args.batch]
            x, p, n = augment(images[ids], positives[ids], negatives[ids])
            z = model(x)
            p = F.interpolate(p, size=z.shape[2:], mode='area')
            n = F.interpolate(n, size=z.shape[2:], mode='area')
            y = labels[ids]
            logit = z.flatten(1).topk(4, dim=1).values.mean(1)
            mil = (F.binary_cross_entropy_with_logits(logit, y, reduction='none') * classweight[y.long()]).mean()
            pos = (F.softplus(-z) * p).sum() / p.sum().clamp_min(1)
            negpixel = F.softplus(z) * n
            valid = (n.flatten(1).sum(1) > 0).float()
            hard = negpixel.flatten(1).topk(8, dim=1).values.mean(1)
            neg = (hard * valid).sum() / valid.sum().clamp_min(1)
            mean = negpixel.sum() / n.sum().clamp_min(1)
            loss = mil + 0.6 * pos + 0.3 * neg + 0.1 * mean
            opt.zero_grad(set_to_none=True)
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 5)
            opt.step()
            losses.append(float(loss))
        lr = 2e-05 + (0.001 - 2e-05) * 0.5 * (1 + math.cos(math.pi * epoch / args.epochs))
        for g in opt.param_groups:
            g['lr'] = lr * g['lr_scale']
        if epoch % args.check_every == 0 or epoch == args.epochs:
            ev = evaluate()
            key = (ev['ba'], -ev['confusion'][1][0], -ev['loss'])
            entry = dict(epoch=epoch, train_loss=float(np.mean(losses)), validation=ev, elapsed_s=time.time() - start)
            logs.append(entry)
            if best is None or key > best:
                best = key
                torch.save({'state_dict': {k: v.cpu().clone() for k, v in model.state_dict().items()}, 'depth': 0, 'input_size': size, 'threshold': ev['threshold'], 'epoch': epoch, 'validation': ev, 'parameter_count': sum((p.numel() for p in model.parameters()))}, out / 'best.pt')
            (out / 'epochs.json').write_text(json.dumps(logs, indent=2))
            print('EPOCH', epoch, 'LOSS', round(float(np.mean(losses)), 4), 'VAL', {k: v for k, v in ev.items() if k not in ['ids', 'probabilities', 'key']}, 'SECONDS', round(time.time() - start, 1), flush=True)
    print('DONE', out, flush=True)
if __name__ == '__main__':
    main()
