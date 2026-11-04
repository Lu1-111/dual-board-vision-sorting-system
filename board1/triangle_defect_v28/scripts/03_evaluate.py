import argparse
import json
import onnx
from onnx import numpy_helper
from common import *

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--model', default='triangle_v28_uint8.onnx')
    ap.add_argument('--split', choices=['val', 'test', 'both'], default='both')
    args = ap.parse_args()
    fp = session(ART/'triangle_v28_float.onnx')
    quant_path = ART/args.model
    qp = session(quant_path)
    model = onnx.load(str(quant_path))
    onnx.checker.check_model(model)
    rows = []
    for n, rec in enumerate(r for r in records() if r['split'] != 'train' and (args.split=='both' or r['split']==args.split)):
        x = load_input(rec)
        a = fp.run(None, {'input': x})[0]
        b = qp.run(None, {'input': x})[0]
        rows.append(dict(id=rec['id'], split=rec['split'], label=rec['label'], float_score=score(a), quant_score=score(b)))
        if (n+1)%40 == 0:
            print(f'evaluated {n+1}/288', flush=True)
    report = {'model': args.model, 'sha256': sha(quant_path), 'bytes': quant_path.stat().st_size,
              'threshold': THRESHOLD, 'threshold_changed': False, 'calibration_train_only': True,
              'board_verified': False, 'metrics': {}, 'rows': rows}
    for split in ['val', 'test']:
        rr = [r for r in rows if r['split']==split]
        if not rr:
            continue
        report['metrics'][split] = {'float': metrics(rr, 'float_score'), 'quant': metrics(rr, 'quant_score'),
            'label_agreement': sum((r['float_score']>=THRESHOLD)==(r['quant_score']>=THRESHOLD) for r in rr),
            'flips': [r for r in rr if (r['float_score']>=THRESHOLD)!=(r['quant_score']>=THRESHOLD)]}
    report['mean_absolute_score_delta'] = float(np.mean([abs(r['float_score']-r['quant_score']) for r in rows]))
    report['max_absolute_score_delta'] = max(abs(r['float_score']-r['quant_score']) for r in rows)
    values = {x.name: numpy_helper.to_array(x) for x in model.graph.initializer}
    iq = next((n for n in model.graph.node if n.op_type=='QuantizeLinear' and n.input[0]=='input'), None)
    oq = next(n for n in model.graph.node if n.op_type=='DequantizeLinear' and n.output[0]=='local_logits')
    report['io'] = {'input': {'name':'input', 'shape':[1,3,320,320], 'dtype':'uint8' if iq else 'float32', 'scale':float(values[iq.input[1]]) if iq else None, 'zero_point':int(values[iq.input[2]]) if iq else None},
                    'output': {'name':'local_logits', 'shape':[1,1,40,40], 'scale':float(values[oq.input[1]]), 'zero_point':int(values[oq.input[2]])}}
    write_json(REPORTS / (quant_path.stem+'_evaluation.json'), report)
    print(json.dumps({k:v for k,v in report.items() if k!='rows'}, indent=2), flush=True)

if __name__ == '__main__':
    main()
