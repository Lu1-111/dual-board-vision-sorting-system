"""Standalone ONNX inference; requires numpy, pillow, onnxruntime (not PyTorch)."""
import argparse,json
from pathlib import Path
import numpy as np
from PIL import Image
import onnxruntime as ort

CLASSES=['empty','object_present','hand_present']


def make_session(path):
    opts=ort.SessionOptions(); opts.intra_op_num_threads=2; opts.inter_op_num_threads=1
    return ort.InferenceSession(str(path),sess_options=opts,providers=['CPUExecutionProvider'])


def preprocess(path):
    with Image.open(path) as src:
        image=src.convert('RGB'); scale=224/max(image.size)
        w,h=[max(1,int(v*scale+.5)) for v in image.size]
        image=image.resize((w,h),Image.Resampling.BILINEAR)
        canvas=Image.new('RGB',(224,224),(114,114,114))
        canvas.paste(image,((224-w)//2,(224-h)//2))
        return np.asarray(canvas,dtype=np.float32).transpose(2,0,1)[None]/255.


def main():
    p=argparse.ArgumentParser(); p.add_argument('image'); p.add_argument('--model',default=str(Path(__file__).resolve().parents[1]/'models/workcell_tiny_int8_qdq.onnx'))
    p.add_argument('--threshold',type=float,default=.8); args=p.parse_args()
    session=make_session(args.model)
    logits=session.run(['logits'],{'images':preprocess(args.image)})[0][0]
    probabilities=np.exp(logits-logits.max()); probabilities/=probabilities.sum()
    i=int(probabilities.argmax())
    print(json.dumps(dict(predicted_class=CLASSES[i],confidence=float(probabilities[i]),
        state=CLASSES[i] if probabilities[i]>=args.threshold else 'uncertain',
        probabilities=dict(zip(CLASSES,probabilities.tolist()))),ensure_ascii=False,indent=2))

if __name__=='__main__': main()
