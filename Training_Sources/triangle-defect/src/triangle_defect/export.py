"""Export the frozen V28 model as a batch-one TorchScript score function."""
import argparse
from pathlib import Path
import torch
from torch import nn

from .paths import ROOT
from .predict import Predictor


class Score(nn.Module):
    def __init__(self, model):
        super().__init__()
        self.model = model

    def forward(self, x):
        return self.model.score(x)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "runs/model_float32.torchscript.pt")
    args = parser.parse_args()
    predictor = Predictor()
    model = Score(predictor.model).eval()
    example = torch.zeros(1, 1, 320, 320)
    with torch.inference_mode():
        traced = torch.jit.trace(model, example)
        torch.testing.assert_close(traced(example), model(example))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    traced.save(str(args.output))
    print(f"已导出: {args.output}")
