"""Evaluate four frozen ONNX workcell models on the known T01–T18 photographs."""
import csv
import hashlib
import html
import json
import sys
from pathlib import Path

import numpy as np
import onnxruntime as ort

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT/'deliverables/four_workcell_models'))
from infer import prepare, CLASSES, MODEL_NAMES

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    train = [json.loads(line) for line in (ROOT/'runs/dataset_audit/cloud_train.jsonl').read_text().splitlines()]
    training_labels = {Path(r['image']).name:r['label'] for r in train}
    inventory = {r['file']:r['sha256'] for r in json.loads((ROOT/'runs/dataset_audit/inventory.json').read_text())}
    rows=[]
    for image in sorted((ROOT/'test').glob('T[0-9][0-9].jpg')):
        meta=json.loads(image.with_suffix('.json').read_text())
        label=meta['task']['expected_class']
        assert label in CLASSES and meta['label_review_status']=='visually_confirmed'
        sha=digest(image)
        assert meta['photo']['sha256']==sha
        rows.append({'id':image.stem,'image':str(image.relative_to(ROOT)), 'sha256':sha,
                     'expected':label,'lighting':meta['task']['lighting'],
                     'in_training': image.name in training_labels and sha==inventory.get(image.name),
                     'training_label': training_labels.get(image.name)})
    assert len(rows)==18 and all(r['in_training'] and r['expected']==r['training_label'] for r in rows)
    input_arrays=[prepare(ROOT/r['image']) for r in rows]
    summaries={}
    for name in MODEL_NAMES:
        model=ROOT/'deliverables/four_workcell_models/models'/name/'model_fp32.onnx'
        options=ort.SessionOptions();options.intra_op_num_threads=2
        sess=ort.InferenceSession(str(model),sess_options=options,providers=['CPUExecutionProvider'])
        confusion=np.zeros((3,3),dtype=int)
        for row,array in zip(rows,input_arrays):
            logits=sess.run(['logits'],{'images':array})[0][0]
            probs=np.exp(logits-logits.max());probs/=probs.sum()
            predicted=CLASSES[int(np.argmax(probs))]
            row[name]={'prediction':predicted,'correct':predicted==row['expected'],
                       'probabilities':{c:round(float(p),7) for c,p in zip(CLASSES,probs)}}
            confusion[CLASSES.index(row['expected']),CLASSES.index(predicted)]+=1
        summaries[name]={'correct':int(np.trace(confusion)),'total':len(rows),
                         'per_class':{c:{'correct':int(confusion[i,i]),'total':int(confusion[i].sum())}
                                      for i,c in enumerate(CLASSES)},
                         'confusion_matrix':confusion.tolist(),'model_sha256':digest(model)}
    report={'warning':'全部18张原始实拍照片对应的缩放重编码副本均参与了本轮训练；本报告为训练集回看，不是独立测试。',
            'classes':CLASSES,'rows':rows,'models':summaries,'training_membership_verified':18}
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    with (OUT/'results.csv').open('w',newline='',encoding='utf-8-sig') as f:
        fields=['id','lighting','expected','in_training']+[x for n in MODEL_NAMES for x in (n+'_prediction',n+'_correct')]
        w=csv.DictWriter(f,fieldnames=fields);w.writeheader()
        for row in rows:
            r={k:row[k] for k in ['id','lighting','expected','in_training']}
            for name in MODEL_NAMES:
                r[name+'_prediction']=row[name]['prediction'];r[name+'_correct']=row[name]['correct']
            w.writerow(r)
    names={'empty':'空带','object_present':'物块在位','hand_present':'人工干预'}
    items=[]
    for row in rows:
        parts=[]
        for name in MODEL_NAMES:
            r=row[name]; parts.append(f"<li>{html.escape(name)}：{names.get(r['prediction'],html.escape(r['prediction']))} {'✓' if r['correct'] else '✗'}</li>")
        items.append(f"<article><a href='../../{html.escape(row['image'])}'><img loading='lazy' src='../../{html.escape(row['image'])}'></a><h2>{row['id']} · {html.escape(row['lighting'])}</h2><p>实际：{names[row['expected']]}</p><ul>{''.join(parts)}</ul></article>")
    (OUT/'results.html').write_text('''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>四模型实拍回看</title><style>body{font:16px system-ui;background:#f1f4f6;margin:20px} .notice{background:#fff2c2;padding:16px} main{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:15px}article{background:white;padding:12px;border-radius:8px}img{width:100%}h2{font-size:17px}li{margin:5px 0}</style><h1>四模型实拍回看</h1><p class="notice">这18张照片的缩放副本参与了四个模型的训练；本页结果不能当作独立测试准确率。</p><main>'''+''.join(items)+'</main></html>')
    print(json.dumps(summaries,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
