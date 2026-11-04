"""Small RGB workcell classifier; standard convolution operators only."""
from pathlib import Path
import numpy as np
from PIL import Image
import torch
from torch import nn

CLASSES = ["empty", "object_present", "hand_present"]
INPUT_SIZE = 224


class WorkcellTinyCNN(nn.Module):
    def __init__(self):
        super().__init__()
        layers = []
        channels = [3, 16, 24, 32, 48, 64, 64]
        for i, (a, b) in enumerate(zip(channels, channels[1:])):
            layers.extend([nn.Conv2d(a, b, 3, stride=2 if i < 5 else 1,
                                     padding=1, bias=False),
                           nn.BatchNorm2d(b), nn.ReLU(inplace=False)])
        self.features = nn.Sequential(*layers)
        self.pool = nn.AvgPool2d(7)
        self.head = nn.Linear(64, 3)

    def forward(self, x):
        return self.head(self.pool(self.features(x)).flatten(1))


def letterbox(path):
    """RGB bilinear, round-half-up dimensions, centered (114,114,114) padding."""
    with Image.open(path) as source:
        im = source.convert("RGB")
        scale = INPUT_SIZE / max(im.size)
        size = tuple(max(1, int(v * scale + 0.5)) for v in im.size)
        resized = im.resize(size, Image.Resampling.BILINEAR)
        out = Image.new("RGB", (INPUT_SIZE, INPUT_SIZE), (114, 114, 114))
        out.paste(resized, ((INPUT_SIZE - size[0]) // 2, (INPUT_SIZE - size[1]) // 2))
        return out


def tensor_input(path):
    return np.asarray(letterbox(path), dtype=np.float32).transpose(2, 0, 1) / 255.0


def scores(labels, predictions):
    cm = np.zeros((3, 3), dtype=np.int64)
    for y, p in zip(labels, predictions):
        cm[int(y), int(p)] += 1
    per_class = []
    for i, name in enumerate(CLASSES):
        tp = int(cm[i, i])
        precision = tp / max(1, int(cm[:, i].sum()))
        recall = tp / max(1, int(cm[i].sum()))
        f1 = 2 * precision * recall / max(1e-12, precision + recall)
        per_class.append(dict(class_name=name, precision=precision, recall=recall,
                              f1=f1, support=int(cm[i].sum())))
    return dict(n=int(cm.sum()), accuracy=float(np.trace(cm) / max(1, cm.sum())),
                macro_f1=float(np.mean([x['f1'] for x in per_class])),
                confusion_matrix=cm.tolist(), per_class=per_class)
