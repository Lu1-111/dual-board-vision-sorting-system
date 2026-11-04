"""Export V28 local logits; exact normalization and top-four aggregation run on PS."""
import sys
import copy
import collections
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
import onnx
from PIL import Image
from common import *

sys.path.insert(0, str(SOURCE / 'src'))
from triangle_defect.predict import Predictor

class HardSigmoid(nn.Module):
    def forward(self, x):
        return torch.clamp(x * (1.0/6.0) + 0.5, 0.0, 1.0)

class HardSwish(nn.Module):
    def forward(self, x):
        return x * torch.clamp(x * (1.0/6.0) + 0.5, 0.0, 1.0)

def lower(module):
    for name, child in list(module.named_children()):
        if isinstance(child, nn.Hardswish):
            setattr(module, name, HardSwish())
        elif isinstance(child, nn.Hardsigmoid):
            setattr(module, name, HardSigmoid())
        else:
            lower(child)

class Core(nn.Module):
    def __init__(self, source):
        super().__init__()
        self.features = copy.deepcopy(source.features)
        self.head = copy.deepcopy(source.head)
        lower(self)
    def forward(self, x):
        for i, layer in enumerate(self.features):
            x = layer(x)
            if i == 3:
                early = x
        x = F.interpolate(x, size=(40, 40), mode='bilinear', align_corners=False)
        return self.head(torch.cat([early, x], 1))

def main():
    ART.mkdir(parents=True, exist_ok=True)
    release = json.loads((SOURCE / 'models/v28/release.json').read_text())
    assert sha(SOURCE / 'models/v28/weights.pt') == release['weights_sha256']
    predictor = Predictor(threads=4)
    core = Core(predictor.model).eval()
    model_path = ART / 'triangle_v28_float.onnx'
    torch.onnx.export(core, torch.zeros(1,3,320,320), str(model_path), opset_version=16,
                      input_names=['input'], output_names=['local_logits'], do_constant_folding=True)
    model = onnx.load(str(model_path))
    onnx.checker.check_model(model)
    sess = session(model_path)
    errors = []
    rows = []
    all_records = records()
    # Full validation/test parity catches export and preprocessing changes.
    for n, rec in enumerate(r for r in all_records if r['split'] != 'train'):
        with Image.open(SOURCE / rec['path']) as im:
            g = gray(im)
        with torch.inference_mode():
            reference = predictor.model(torch.from_numpy(g)).numpy()
        actual = sess.run(None, {'input': normalized(g)})[0]
        errors.append(float(np.max(np.abs(reference-actual))))
        rows.append(dict(id=rec['id'], split=rec['split'], label=rec['label'],
                         pytorch=score(reference), onnx=score(actual)))
        if (n+1) % 40 == 0:
            print(f'float parity {n+1}/288', flush=True)
    result = {'weights_sha256': release['weights_sha256'], 'float_sha256': sha(model_path),
              'ops': dict(collections.Counter(n.op_type for n in model.graph.node)),
              'input_shape': [1,3,320,320], 'output_shape': [1,1,40,40],
              'max_logit_error': max(errors),
              'max_score_error': max(abs(r['pytorch']-r['onnx']) for r in rows),
              'label_agreement': sum((r['pytorch']>=THRESHOLD)==(r['onnx']>=THRESHOLD) for r in rows),
              'n': len(rows), 'rows': rows}
    write_json(REPORTS / 'float_equivalence.json', result)
    assert max(errors) < 0.001, result['max_logit_error']
    assert result['label_agreement'] == len(rows)
    print(json.dumps({k:v for k,v in result.items() if k != 'rows'}, indent=2), flush=True)

if __name__ == '__main__':
    main()
