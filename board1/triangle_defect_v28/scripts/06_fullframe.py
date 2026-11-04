"""Post-selection full-frame regression using the frozen blue locator."""
import argparse
import sys
import json
from PIL import Image
from common import *
sys.path.insert(0, str(SOURCE/'src'))
from triangle_defect.detect import BlueLocator

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--model',required=True);args=ap.parse_args()
    qp=session(ART/args.model);fp=session(ART/'triangle_v28_float.onnx')
    locator=BlueLocator()
    original=json.loads((SOURCE/'reports/fullframe-validation-20261102/results.json').read_text(encoding='utf-8'))
    rows=[]
    for path in sorted((SOURCE/'data/originals').glob('*.jpg')):
        with Image.open(path) as im:
            rgb=im.convert('RGB');boxes=locator.locate(rgb)
            for obj in boxes:
                if obj['partial_at_frame_border']:continue
                crop=rgb.crop(obj['crop_bbox_xyxy']).convert('L').resize((224,224),Image.Resampling.BILINEAR)
                x=normalized(gray(crop))
                a=score(fp.run(None,{'input':x})[0]);b=score(qp.run(None,{'input':x})[0])
                rows.append({'image':path.name,'bbox':obj['bbox_xyxy'],'crop':obj['crop_bbox_xyxy'],
                             'float_score':a,'quant_score':b,'same_label':(a>=THRESHOLD)==(b>=THRESHOLD)})
    report={'model':args.model,'n':len(rows),'label_agreement':sum(r['same_label'] for r in rows),
            'flips':[r for r in rows if not r['same_label']], 'rows':rows,
            'limitation':'Original images include training sources; internal regression only.'}
    write_json(REPORTS/'fullframe_quant_regression.json',report)
    print(json.dumps({k:v for k,v in report.items() if k!='rows'},indent=2))

if __name__=='__main__':main()
