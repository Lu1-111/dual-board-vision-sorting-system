"""Evaluate a reviewed synthetic test batch without modifying model or threshold."""
import argparse
import csv
import hashlib
import html
import json
import os
import sys
import time
from collections import Counter
from pathlib import Path

import numpy as np
import onnxruntime as ort
from PIL import Image, __version__ as pillow_version

from infer import CLASSES, make_session, preprocess

ROOT = Path(__file__).resolve().parents[1]
DISPLAY = {'empty':'空带', 'object_present':'物块在位', 'hand_present':'人工干预', 'uncertain':'不确定'}


def metrics(rows, kind):
    matrix = np.zeros((3, 3), dtype=np.int64)
    for row in rows:
        matrix[CLASSES.index(row['expected_class']), CLASSES.index(row[kind]['prediction'])] += 1
    per_class = []
    for i, name in enumerate(CLASSES):
        tp = int(matrix[i, i])
        precision = tp/max(1, int(matrix[:, i].sum()))
        recall = tp/max(1, int(matrix[i, :].sum()))
        f1 = 2*precision*recall/max(1e-12, precision+recall)
        per_class.append(dict(class_name=name, support=int(matrix[i].sum()),
                              correct=tp, precision=precision, recall=recall, f1=f1))
    accepted = [r for r in rows if r[kind]['state']!='uncertain']
    return dict(n=len(rows), correct=int(np.trace(matrix)),
        accuracy=float(np.trace(matrix)/max(1,len(rows))),
        macro_f1=float(np.mean([r['f1'] for r in per_class])),
        confusion_matrix=matrix.tolist(), per_class=per_class,
        uncertain_count=len(rows)-len(accepted), coverage=len(accepted)/max(1,len(rows)),
        high_confidence_error_count=sum(not r[kind]['correct'] for r in accepted),
        minimum_max_probability=min((r[kind]['confidence'] for r in rows), default=None))


def confusion_svg(report, kind):
    matrix = report[kind]['confusion_matrix']
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="470" height="340" viewBox="0 0 470 340">',
           '<rect width="470" height="340" fill="white"/>',
           f'<text x="235" y="28" text-anchor="middle" font-family="sans-serif" font-size="20">{kind.upper()} · n={report[kind]["n"]}</text>']
    labels = ['Empty', 'Object', 'Hand']
    maximum = max(1, max(max(row) for row in matrix))
    for i, name in enumerate(labels):
        svg.append(f'<text x="106" y="{106+i*70}" text-anchor="end" font-family="sans-serif" font-size="15">{name}</text>')
        svg.append(f'<text x="{155+i*90}" y="300" text-anchor="middle" font-family="sans-serif" font-size="15">{name}</text>')
        for j in range(3):
            value = matrix[i][j]; shade = value/maximum
            color = f'rgb({int(239-204*shade)},{int(246-151*shade)},{int(255-102*shade)})'
            svg.append(f'<rect x="{110+j*90}" y="{65+i*70}" width="90" height="70" fill="{color}" stroke="white"/>')
            svg.append(f'<text x="{155+j*90}" y="{108+i*70}" text-anchor="middle" font-family="sans-serif" font-size="24" fill="{"white" if shade>.5 else "#18243a"}">{value}</text>')
    svg.append('<text x="245" y="329" text-anchor="middle" font-family="sans-serif" font-size="14">Predicted class (rows: actual class)</text></svg>')
    return ''.join(svg)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--batch', type=Path, required=True)
    parser.add_argument('--model-dir', type=Path, default=ROOT/'models')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--allow-model-update', action='store_true', help='Explicit regression evaluation; requires a separate output directory.')
    parser.add_argument('--comparison', action='store_true', help='Label an explicit model override as a fixed baseline comparison.')
    args = parser.parse_args(); batch = args.batch.resolve()
    model_dir = args.model_dir.resolve()
    assert not args.comparison or args.allow_model_update
    plan = json.loads((batch/'plan.json').read_text())
    manifest = json.loads((batch/'manifest.json').read_text())
    # Normalize the original batch's older metadata without rewriting its history.
    for row in manifest:
        if 'visual_review' in row:
            row.setdefault('review_status','accepted' if row['visual_review'].startswith('accepted') else 'unreviewed')
            row.setdefault('reviewed_before_inference','before model inference' in row['visual_review'])
            row.setdefault('review_note',row['visual_review'])
            row.setdefault('title',row['id'])
            if row['suite']=='out_of_scope': row['suite']='boundary'
    manifest.sort(key=lambda row:row['id'])
    assert len(manifest)==len(plan['tests']), 'Unexpected batch size'
    assert {x['id'] for x in manifest}=={x['id'] for x in plan['tests']}, 'Incomplete batch'
    assert all(r['review_status']=='accepted' and r['reviewed_before_inference'] for r in manifest), 'Complete visual review before evaluation'
    planned = {r['id']:r for r in plan['tests']}
    assert all(r['expected_class']==planned[r['id']]['expected_class'] and r['suite']==planned[r['id']]['suite'] for r in manifest), 'Labels differ from test design'
    threshold = plan['threshold']
    contract = json.loads((model_dir/'input_contract.json').read_text())
    assert CLASSES==contract['classes']
    assert threshold==contract['provisional_uncertain_threshold']
    model_hashes = {name:hashlib.sha256((model_dir/name).read_bytes()).hexdigest() for name in plan['model_hashes']}
    changed = model_hashes!=plan['model_hashes']
    if args.allow_model_update:
        assert args.output and args.output.resolve()!=ROOT/'reports'/plan['batch'], 'Use a separate regression output directory'
    else:
        assert not changed, 'Model changed; use explicit regression mode and separate output'
    known = set()
    for file in [ROOT/'data/manifest.json', ROOT/'data/test/manifest.json']:
        if file.resolve()!=(batch/'manifest.json').resolve():
            known.update(r['sha256'] for r in json.loads(file.read_text()))
    known.update(r['sha256'] for r in json.loads((ROOT/'data/reference/manifest.json').read_text())['samples'])
    for previous in (ROOT/'data/test_batches').glob('*/manifest.json'):
        if previous.parent.resolve()!=batch:
            known.update(r['sha256'] for r in json.loads(previous.read_text()))
    sessions = {kind:make_session(model_dir/name) for kind,name in
                [('fp32','workcell_tiny_fp32.onnx'),('int8','workcell_tiny_int8_qdq.onnx')]}
    predictions = []; hashes = set()
    for record in manifest:
        path = batch/record['path']; sha = hashlib.sha256(path.read_bytes()).hexdigest()
        assert sha==record['sha256'] and sha not in known and sha not in hashes, 'Duplicate/corrupt image'
        if not args.allow_model_update and 'frozen_unix_time' in plan:
            assert record['generated_unix_time']>plan['frozen_unix_time'], 'Image predates final model freeze'
        hashes.add(sha)
        with Image.open(path) as image:
            width,height = image.size; image.verify()
        item = {k:record[k] for k in ['id','title','expected_class','suite','condition','path','review_note']}
        item.update(sha256=sha,width=width,height=height)
        tensor = preprocess(path)
        for kind,session in sessions.items():
            logits = session.run(['logits'], {'images':tensor})[0][0]
            probabilities = np.exp(logits-logits.max()); probabilities /= probabilities.sum()
            index = int(probabilities.argmax()); confidence = float(probabilities[index])
            label = CLASSES[index]
            item[kind] = dict(prediction=label,confidence=confidence,
                probabilities=dict(zip(CLASSES,probabilities.tolist())),
                state=label if confidence>=threshold else 'uncertain',
                correct=label==record['expected_class'] if record['expected_class'] else None)
        predictions.append(item)
    regular = [r for r in predictions if r['suite']=='classification']
    boundaries = [r for r in predictions if r['suite']=='boundary']
    report = dict(batch=plan['batch'],scope=plan['scope'],n=len(predictions),
        classification_count=len(regular),boundary_count=len(boundaries),threshold=threshold,
        plan_sha256=hashlib.sha256((batch/'plan.json').read_bytes()).hexdigest(),
        manifest_sha256=hashlib.sha256((batch/'manifest.json').read_bytes()).hexdigest(),
        model_hashes=model_hashes,planned_model_hashes=plan['model_hashes'],model_or_threshold_changed=changed,
        evaluation_mode='comparison' if args.comparison else ('regression' if args.allow_model_update else 'frozen_test'),
        environment=dict(python=sys.version.split()[0],numpy=np.__version__,pillow=pillow_version,onnxruntime=ort.__version__,provider='CPUExecutionProvider'),
        exact_duplicates_with_training_reference_or_previous_test=0,
        fp32=metrics(regular,'fp32'),int8=metrics(regular,'int8'),
        fp32_int8_prediction_agreement=float(np.mean([r['fp32']['prediction']==r['int8']['prediction'] for r in predictions])),
        boundary_int8_states=dict(Counter(r['int8']['state'] for r in boundaries)),
        evaluated_unix_time=time.time(),predictions=predictions)
    output = args.output.resolve() if args.output else ROOT/'reports'/plan['batch']
    output.mkdir(parents=True, exist_ok=True)
    gallery = output/'results.html' if args.allow_model_update else batch/'results.html'
    (output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    with (output/'predictions.csv').open('w',encoding='utf-8-sig',newline='') as f:
        keys=['id','title','suite','expected_class','fp32_prediction','fp32_confidence','fp32_state',
              'int8_prediction','int8_confidence','int8_state','int8_correct','path']
        writer=csv.DictWriter(f,fieldnames=keys,lineterminator='\n'); writer.writeheader()
        for row in predictions:
            flat={k:row[k] for k in ['id','title','suite','expected_class']}
            flat['path']=str((batch/row['path']).relative_to(ROOT))
            for kind in sessions:
                flat.update({f'{kind}_{key}':row[kind][key] for key in ['prediction','confidence','state']})
            flat['int8_correct']=row['int8']['correct']; writer.writerow(flat)
    for kind in sessions:
        (output/f'confusion_{kind}.svg').write_text(confusion_svg(report,kind))
    cards=[]
    for row in predictions:
        correct=row['int8']['correct']
        status='boundary' if correct is None else ('correct' if correct else 'error')
        states=''.join(f'<p>{kind.upper()}：最高类别 {DISPLAY[row[kind]["prediction"]]} · {row[kind]["confidence"]:.1%} · 最终输出 {DISPLAY[row[kind]["state"]]}</p>' for kind in sessions)
        picture = html.escape(os.path.relpath(batch/row['path'],gallery.parent))
        cards.append(f'<article data-status="{status}" data-class="{row["expected_class"] or "boundary"}"><a href="{picture}"><img loading="lazy" src="{picture}" alt="{html.escape(row["title"])}"></a><div><b>{html.escape(row["title"])}</b><p>预期类别：{DISPLAY.get(row["expected_class"], "边界工况，不计入三分类准确率")}</p>{states}<small>{html.escape(row["review_note"])}</small></div></article>')
    summary=f'三分类 {len(regular)} 张：FP32 {report["fp32"]["correct"]}/{len(regular)}，INT8 {report["int8"]["correct"]}/{len(regular)}。另有 {len(boundaries)} 张边界工况。'
    summary += f' 按预设 {threshold:.0%} 阈值，FP32 有 {report["fp32"]["uncertain_count"]} 张、INT8 有 {report["int8"]["uncertain_count"]} 张输出不确定。边界图片单独展示，不计入三分类准确率。'
    page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>蓝色方块 · 第二批测试</title><style>body{font:16px system-ui;margin:28px;color:#172333;background:#f6f8fb}h1{font-size:28px}select{padding:8px;font:inherit}main{display:grid;grid-template-columns:repeat(auto-fill,minmax(320px,1fr));gap:20px;margin-top:20px}article{background:white;border:1px solid #d9e0e8;border-radius:10px;overflow:hidden}article[data-status=error]{border:3px solid #d94040}article[data-status=boundary]{border-color:#c69a3a}article img{width:100%;aspect-ratio:16/9;object-fit:contain}article div{padding:14px}p{line-height:1.55;margin:7px 0}small{color:#596875}[hidden]{display:none!important}</style><h1>蓝色方块 · 第二批固定背景测试</h1><p>'''+summary+'''</p><p>先固定测试设计与模型，再生成和目视审核，最后统一推理；测试结果未用于训练或调阈值。</p><label>显示 <select id="filter"><option value="all">全部</option><option value="error">INT8 分类错误</option><option value="boundary">边界工况</option><option value="empty">空工位</option><option value="object_present">物块在位</option><option value="hand_present">人工干预</option></select></label><main>'''+''.join(cards)+'''</main><script>document.querySelector('#filter').onchange=e=>document.querySelectorAll('article').forEach(x=>x.hidden=e.target.value!=='all'&&x.dataset.status!==e.target.value&&x.dataset.class!==e.target.value)</script></html>'''
    page = page.replace('第二批固定背景测试',html.escape(plan['batch'])+' 固定背景测试').replace('第二批测试',html.escape(plan['batch'])+' 测试')
    if args.allow_model_update:
        page = page.replace('先固定测试设计与模型，再生成和目视审核，最后统一推理；测试结果未用于训练或调阈值。','已有图片的回归评估；此结果不作为新的独立测试。原始结果另行保留。')
    if args.comparison:
        page = page.replace('已有图片的回归评估；此结果不作为新的独立测试。原始结果另行保留。','固定版本基线对照；使用同一批图片比较模型，测试中不修改权重或阈值。')
    gallery.write_text(page,encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='predictions'},ensure_ascii=False,indent=2))
    print('Errors / boundaries:')
    for row in predictions:
        if row['suite']=='boundary' or not row['fp32']['correct'] or not row['int8']['correct']:
            print(json.dumps(row,ensure_ascii=False))


if __name__=='__main__':
    main()
