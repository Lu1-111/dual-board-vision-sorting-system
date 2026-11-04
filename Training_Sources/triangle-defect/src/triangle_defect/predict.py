"""Single-workpiece crop inference; grayscale then bilinear320. Batch1, FP32."""
from pathlib import Path
import argparse, csv, json
import numpy as np
from PIL import Image
import torch
from .model import DefectNet
from .paths import MODEL

class Predictor:

    def __init__(self, folder=None, device='cpu', threads=4):
        folder = Path(folder) if folder else MODEL
        self.config = json.loads((folder / 'config.json').read_text())
        self.device = device
        if device == 'cpu':
            torch.set_num_threads(threads)
        ck = torch.load(folder / 'weights.pt', map_location='cpu', weights_only=True)
        self.model = DefectNet(depth=self.config['depth']).to(device)
        self.model.load_state_dict(ck['state_dict'])
        self.model.eval()

    def preprocess(self, path):
        with Image.open(path) as im:
            return self.preprocess_image(im)

    def preprocess_image(self, image):
        a = np.asarray(image.convert('L').resize((self.config['input_size'],) * 2, Image.Resampling.BILINEAR), np.float32) / 255.0
        return torch.from_numpy(a)[None, None].to(self.device)

    def predict_image(self, image):
        return self.infer_tensor(self.preprocess_image(image))

    @torch.inference_mode()
    def infer_tensor(self, x):
        z = self.model(x)[0, 0]
        values, indices = z.flatten().topk(4)
        score = float(values.mean().sigmoid())
        i = int(indices[0])
        w = z.shape[-1]
        return {'class_id': int(score >= self.config['threshold']), 'label': 'defect' if score >= self.config['threshold'] else 'normal', 'score': score, 'threshold': self.config['threshold'], 'peak_xy_normalized': [(i % w + 0.5) / w, (i // w + 0.5) / z.shape[0]]}

    def predict(self, path):
        return dict(path=str(path), **self.infer_tensor(self.preprocess(path)))

def main():
    p = argparse.ArgumentParser()
    p.add_argument('input', type=Path, help='single workpiece image or image folder')
    p.add_argument('--output', type=Path, help='save results CSV')
    p.add_argument('--device', default='cpu')
    p.add_argument('--threads', type=int, default=4)
    a = p.parse_args()
    model = Predictor(device=a.device, threads=a.threads)
    paths = [a.input] if a.input.is_file() else sorted((p for p in a.input.iterdir() if p.suffix.lower() in {'.png', '.jpg', '.jpeg', '.bmp', '.tif', '.tiff'}))
    if not paths:
        raise SystemExit('No supported images found.')
    records = [model.predict(path) for path in paths]
    if a.output:
        a.output.parent.mkdir(parents=True, exist_ok=True)
        with a.output.open('w', newline='', encoding='utf-8-sig') as f:
            w = csv.DictWriter(f, fieldnames=list(records[0]))
            w.writeheader()
            w.writerows(records)
    print(json.dumps(records[0] if len(records) == 1 else records, ensure_ascii=False, indent=2))
if __name__ == '__main__':
    main()
