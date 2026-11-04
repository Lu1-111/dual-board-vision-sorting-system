"""Export a checkpoint and calibrate INT8 using its training split only."""
import argparse
import collections
import hashlib
import json
import random
from pathlib import Path

import numpy as np
import onnx
import torch
from onnxruntime.quantization import CalibrationDataReader, QuantFormat, QuantType, quantize_static

from infer import make_session
from model import WorkcellTinyCNN, CLASSES, tensor_input, scores

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / 'data'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--checkpoint', type=Path, default=ROOT/'models/best.pt')
    parser.add_argument('--manifest', type=Path, default=DATA/'manifest.json')
    parser.add_argument('--output', type=Path, default=ROOT/'runs/export')
    args = parser.parse_args()
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    fp = out/'workcell_tiny_fp32.onnx'
    quantized = out/'workcell_tiny_int8_qdq.onnx'
    if fp.exists() or quantized.exists():
        raise RuntimeError('Output already contains exported models; choose a new --output directory.')
    torch.set_num_threads(4)
    checkpoint = torch.load(args.checkpoint, map_location='cpu', weights_only=False)
    model = WorkcellTinyCNN().eval()
    model.load_state_dict(checkpoint['state_dict'])
    torch.onnx.export(model, torch.zeros(1,3,224,224), str(fp),
        input_names=['images'], output_names=['logits'], opset_version=13,
        dynamo=False, do_constant_folding=True)
    onnx.save(onnx.shape_inference.infer_shapes(onnx.load(str(fp))), str(fp))
    onnx.checker.check_model(str(fp))
    records = json.loads(args.manifest.read_text())
    rng = random.Random(checkpoint['config']['seed'])
    calibration = []
    for c in range(3):
        pool = [r for r in records if r['split']=='train' and int(r['class_id'])==c]
        if not pool:
            raise RuntimeError(f'Missing training calibration images for {CLASSES[c]}')
        rng.shuffle(pool)
        calibration.extend(pool[:40])

    class Reader(CalibrationDataReader):
        def __init__(self):
            self.items = iter(calibration)

        def get_next(self):
            row = next(self.items, None)
            return None if row is None else {'images':tensor_input(DATA/row['path'])[None]}

    quantize_static(str(fp), str(quantized), Reader(), quant_format=QuantFormat.QDQ,
        activation_type=QuantType.QInt8, weight_type=QuantType.QInt8,
        per_channel=True, op_types_to_quantize=['Conv','Gemm'],
        extra_options={'ActivationSymmetric':False,'WeightSymmetric':True})
    onnx.checker.check_model(str(quantized))
    fp_session, int8_session = make_session(fp), make_session(quantized)
    labels, fp_preds, int8_preds = [], [], []
    max_error = 0.0
    with torch.no_grad():
        for row in records:
            if row['split'] != 'internal_val':
                continue
            x = tensor_input(DATA/row['path'])[None]
            expected = model(torch.from_numpy(x)).numpy()
            fp_output = fp_session.run(None, {'images':x})[0]
            int8_output = int8_session.run(None, {'images':x})[0]
            max_error = max(max_error, float(np.max(np.abs(expected-fp_output))))
            labels.append(int(row['class_id']))
            fp_preds.append(int(fp_output.argmax()))
            int8_preds.append(int(int8_output.argmax()))
    if max_error >= 1e-4:
        raise RuntimeError(f'PyTorch and ONNX disagree: {max_error}')
    report = {
        'checkpoint_sha256':hashlib.sha256(args.checkpoint.read_bytes()).hexdigest(),
        'parameters':sum(p.numel() for p in model.parameters()),
        'calibration_count':len(calibration),
        'calibration_sample_ids':[r['sample_id'] for r in calibration],
        'pytorch_onnx_max_abs_logit_error':max_error,
        'fp32_operators':dict(collections.Counter(n.op_type for n in onnx.load(str(fp)).graph.node)),
        'internal_val_fp32':scores(labels, fp_preds),
        'internal_val_int8':scores(labels, int8_preds),
        'fp32_bytes':fp.stat().st_size,
        'int8_onnx_bytes':quantized.stat().st_size,
    }
    (out/'export_report.json').write_text(json.dumps(report, indent=2)+'\n')
    (out/'input_contract.json').write_bytes((ROOT/'models/input_contract.json').read_bytes())
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
