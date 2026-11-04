"""Evaluate labelled board photographs with the frozen ONNX models."""
import argparse
import csv
import hashlib
import html
import json
import os
import platform
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
import onnxruntime as ort
from PIL import Image, __version__ as pillow_version

from infer import CLASSES, make_session, preprocess

ROOT = Path(__file__).resolve().parents[1]
DISPLAY = {'empty':'空带', 'object_present':'蓝块在位', 'hand_present':'人工干预', 'uncertain':'不确定'}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def metrics(rows, kind):
    matrix = [[0 for _ in CLASSES] for _ in CLASSES]
    for row in rows:
        matrix[CLASSES.index(row['expected_class'])][CLASSES.index(row[kind]['prediction'])] += 1
    accepted = [row for row in rows if row[kind]['state'] != 'uncertain']
    return {
        'count': len(rows),
        'top1_correct': sum(row[kind]['correct'] for row in rows),
        'top1_accuracy': sum(row[kind]['correct'] for row in rows) / len(rows),
        'uncertain': len(rows) - len(accepted),
        'explicit_correct': sum(row[kind]['correct'] for row in accepted),
        'explicit_wrong': sum(not row[kind]['correct'] for row in accepted),
        'confusion_matrix_rows_actual_columns_predicted': matrix,
        'per_class': {name: {'count': sum(row['expected_class'] == name for row in rows),
                             'top1_correct': sum(row['expected_class'] == name and row[kind]['correct'] for row in rows),
                             'uncertain': sum(row['expected_class'] == name and row[kind]['state'] == 'uncertain' for row in rows)}
                      for name in CLASSES},
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', type=Path, default=ROOT/'test')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, output = args.input.resolve(), args.output.resolve()
    if output == source or source in output.parents:
        raise ValueError('Keep outputs separate from the source photos')
    if output.exists() and any(output.iterdir()):
        raise FileExistsError('Choose an empty output directory')
    images = sorted(p for p in source.iterdir() if p.is_file() and p.suffix.lower() in {'.jpg', '.jpeg', '.png'})
    if not images:
        raise ValueError('No input photographs')
    model_dir = ROOT/'models'
    expected_hashes = json.loads((model_dir/'artifact_hashes.json').read_text())
    assert all(sha256(model_dir/name)==expected for name,expected in expected_hashes.items()), 'Model hash mismatch'
    contract = json.loads((model_dir/'input_contract.json').read_text())
    assert contract['classes'] == CLASSES
    threshold = contract['provisional_uncertain_threshold']
    sessions = {k:make_session(model_dir/f'workcell_tiny_{k}{suffix}.onnx') for k,suffix in
                [('fp32',''),('int8','_qdq')]}
    records, seen = [], set()
    for path in images:
        metadata_path = path.with_suffix('.json')
        metadata = json.loads(metadata_path.read_text(encoding='utf-8'))
        task, photo = metadata['task'], metadata['photo']
        assert task['id'] == path.stem and task['expected_class'] in CLASSES
        assert metadata['label_review_status'] == 'visually_confirmed'
        actual_hash = sha256(path)
        assert actual_hash == photo['sha256'], f'Image hash mismatch: {path}'
        assert actual_hash not in seen, f'Duplicate photo: {path}'
        seen.add(actual_hash)
        with Image.open(path) as im:
            width, height = im.size
            im.verify()
        assert (width,height) == (photo['width'],photo['height'])
        item = dict(id=task['id'], path=os.path.relpath(path,ROOT), expected_class=task['expected_class'],
                    lighting=task['lighting'], instruction=task['instruction'], review_note=metadata['review_note'],
                    sha256=actual_hash, metadata_sha256=sha256(metadata_path), width=width, height=height,
                    host_received_at=metadata.get('host_received_at'),
                    metadata_recorded_freeze_fp32_sha256=metadata.get('model_freeze',{}).get('fp32_sha256'))
        tensor = preprocess(path)
        for kind, session in sessions.items():
            logits = session.run(['logits'], {'images':tensor})[0][0]
            probabilities = np.exp(logits-logits.max())
            probabilities /= probabilities.sum()
            index = int(probabilities.argmax())
            prediction, confidence = CLASSES[index], float(probabilities[index])
            item[kind] = dict(prediction=prediction, confidence=confidence,
                              probabilities=dict(zip(CLASSES,probabilities.tolist())),
                              state=prediction if confidence >= threshold else 'uncertain',
                              correct=prediction==item['expected_class'])
        records.append(item)

    training = json.loads((ROOT/'data/manifest.json').read_text())
    exact_training_duplicates = sum(r['sha256'] in {x['sha256'] for x in training} for r in records)
    training_freeze = json.loads((ROOT/'reports/optimization_v2/training/frozen_model.json').read_text())['frozen_unix_time']
    host_times_after_freeze = all(r['host_received_at'] and datetime.fromisoformat(r['host_received_at']).timestamp()>training_freeze for r in records)
    freeze_matches = all(r['metadata_recorded_freeze_fp32_sha256']==expected_hashes['workcell_tiny_fp32.onnx'] for r in records)
    result = dict(dataset=str(source), n=len(records), threshold=threshold,
                  evaluated_at_utc=datetime.now(timezone.utc).isoformat(),
                  environment={'provider':'CPUExecutionProvider','onnxruntime':ort.__version__,
                               'numpy':np.__version__,'pillow':pillow_version,'python':platform.python_version()},
                  model_hashes=expected_hashes,
                  metadata_recorded_fp32_hashes=sorted({r['metadata_recorded_freeze_fp32_sha256'] for r in records if r['metadata_recorded_freeze_fp32_sha256']}),
                  metadata_freeze_matches_current_fp32=freeze_matches,
                  exact_duplicate_training_images=exact_training_duplicates,
                  host_received_after_model_training_freeze=host_times_after_freeze,
                  note='Uses the project full-frame preprocessing and frozen threshold. Timestamp and exact-hash checks do not establish independence from all possible related images.',
                  fp32=metrics(records,'fp32'), int8=metrics(records,'int8'),
                  per_lighting={light:{kind:metrics([r for r in records if r['lighting']==light],kind) for kind in sessions}
                                for light in sorted({r['lighting'] for r in records})},
                  fp32_int8_agreement=sum(r['fp32']['prediction']==r['int8']['prediction'] for r in records),
                  predictions=records)
    output.mkdir(parents=True)
    (output/'report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    with (output/'predictions.csv').open('w',encoding='utf-8-sig',newline='') as f:
        fields=['id','lighting','expected_class','fp32_prediction','fp32_confidence','fp32_state',
                'int8_prediction','int8_confidence','int8_state','int8_correct','path']
        writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader()
        for row in records:
            flat={key:row[key] for key in ['id','lighting','expected_class','path']}
            for kind in sessions:
                flat.update({f'{kind}_{key}':row[kind][key] for key in ['prediction','confidence','state']})
            flat['int8_correct']=row['int8']['correct']
            writer.writerow(flat)
    cards=[]
    for row in records:
        status = 'uncertain' if row['int8']['state']=='uncertain' else ('correct' if row['int8']['correct'] else 'error')
        picture = html.escape(os.path.relpath(ROOT/row['path'],output))
        predictions=''.join(f'<p>{kind.upper()}：最高类别 {DISPLAY[row[kind]["prediction"]]} {row[kind]["confidence"]:.2%}；输出 {DISPLAY[row[kind]["state"]]}</p>' for kind in sessions)
        cards.append(f'<article data-status="{status}"><a href="{picture}"><img loading="lazy" src="{picture}" alt="{row["id"]}"></a><div><b>{row["id"]} · {html.escape(row["lighting"])}</b><p>实际：{DISPLAY[row["expected_class"]]}</p>{predictions}<small>{html.escape(row["review_note"])}</small></div></article>')
    summary=f'INT8 最高类别正确 {result["int8"]["top1_correct"]}/{len(records)}，明确正确 {result["int8"]["explicit_correct"]}，明确错误 {result["int8"]["explicit_wrong"]}，不确定 {result["int8"]["uncertain"]}。'
    page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>板端实拍测试</title><style>body{font:16px system-ui;margin:28px;color:#172333;background:#f6f8fb}select{padding:8px;font:inherit}main{display:grid;grid-template-columns:repeat(auto-fill,minmax(330px,1fr));gap:20px;margin-top:20px}article{background:#fff;border:1px solid #d9e0e8;border-radius:10px;overflow:hidden}article[data-status=error]{border:3px solid #d94040}article[data-status=uncertain]{border:3px solid #cc9827}article img{width:100%;aspect-ratio:16/9;object-fit:contain}article div{padding:14px}p{line-height:1.5;margin:7px 0}small{color:#596875}[hidden]{display:none!important}</style><h1>板端实拍测试</h1><p>'''+summary+'''</p><p>保留预设 0.8 阈值；照片及标签原文件保持不变。推理在电脑上运行，不代表 FPGA 实测性能。</p><label>显示 <select id="filter"><option value="all">全部</option><option value="error">INT8 明确错误</option><option value="uncertain">INT8 不确定</option><option value="correct">INT8 明确正确</option></select></label><main>'''+''.join(cards)+'''</main><script>document.querySelector('#filter').onchange=e=>document.querySelectorAll('article').forEach(x=>x.hidden=e.target.value!=='all'&&x.dataset.status!==e.target.value)</script></html>'''
    (output/'results.html').write_text(page,encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='predictions'},ensure_ascii=False,indent=2))
    print('Per-image:')
    for row in records:
        print(row['id'],row['lighting'],row['expected_class'],
              'FP32',row['fp32']['prediction'],round(row['fp32']['confidence'],4),row['fp32']['state'],
              'INT8',row['int8']['prediction'],round(row['int8']['confidence'],4),row['int8']['state'])


if __name__=='__main__':
    main()
