import torch
from torch import nn
from torch.nn import functional as F
from torchvision.models import mobilenet_v3_small

class DefectNet(nn.Module):
    """Grayscale multi-scale local classifier using pretrained visual features."""

    def __init__(self, depth=0):
        super().__init__()
        base = mobilenet_v3_small(weights=None)
        self.features = base.features[:9]
        self.head = nn.Sequential(nn.Conv2d(24 + 48, 64, 3, padding=1, bias=False), nn.BatchNorm2d(64), nn.ReLU(), nn.Conv2d(64, 32, 3, padding=1, bias=False), nn.BatchNorm2d(32), nn.ReLU(), nn.Conv2d(32, 1, 1))
        self.register_buffer('mean', torch.tensor([0.485, 0.456, 0.406])[None, :, None, None])
        self.register_buffer('std', torch.tensor([0.229, 0.224, 0.225])[None, :, None, None])

    def forward(self, x):
        x = (x.repeat(1, 3, 1, 1) - self.mean) / self.std
        early = None
        for i, layer in enumerate(self.features):
            x = layer(x)
            if i == 3:
                early = x
        x = F.interpolate(x, size=early.shape[-2:], mode='bilinear', align_corners=False)
        return self.head(torch.cat([early, x], 1))

    def score(self, x):
        return self(x).flatten(1).topk(4, dim=1).values.mean(1).sigmoid()
