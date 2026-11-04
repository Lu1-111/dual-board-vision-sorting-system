from pathlib import Path
import hashlib
import json
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT.parent / 'triangle-defect' / 'triangle-defect'
ART = ROOT / 'artifacts'
REPORTS = ROOT / 'reports'
THRESHOLD = 0.88
MEAN = np.array([0.485, 0.456, 0.406], np.float32)[None, :, None, None]
STD = np.array([0.229, 0.224, 0.225], np.float32)[None, :, None, None]

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding='utf-8')

def records():
    return json.loads((SOURCE / 'data/manifest.json').read_text(encoding='utf-8'))['records']

def gray(image):
    return np.asarray(image.convert('L').resize((320, 320), Image.Resampling.BILINEAR), np.float32)[None, None] / 255.0

def normalized(g):
    return np.ascontiguousarray((np.repeat(g, 3, axis=1) - MEAN) / STD)

def load_input(record):
    with Image.open(SOURCE / record['path']) as im:
        return normalized(gray(im))

def score(logits):
    top = np.sort(np.asarray(logits, dtype=np.float64).reshape(-1))[-4:]
    return float(1 / (1 + np.exp(-top.mean())))

def session(path):
    import onnxruntime as ort
    options = ort.SessionOptions()
    options.intra_op_num_threads = 4
    options.inter_op_num_threads = 1
    return ort.InferenceSession(str(path), options, providers=['CPUExecutionProvider'])

def metrics(rows, key):
    tn = sum(r['label'] == 0 and r[key] < THRESHOLD for r in rows)
    fp = sum(r['label'] == 0 and r[key] >= THRESHOLD for r in rows)
    fn = sum(r['label'] == 1 and r[key] < THRESHOLD for r in rows)
    tp = sum(r['label'] == 1 and r[key] >= THRESHOLD for r in rows)
    return {'n': len(rows), 'correct': tn+tp, 'accuracy': (tn+tp)/len(rows),
            'confusion_actual_by_predicted': [[tn, fp], [fn, tp]],
            'defect_recall': tp/(tp+fn), 'false_alarms': fp, 'misses': fn}
