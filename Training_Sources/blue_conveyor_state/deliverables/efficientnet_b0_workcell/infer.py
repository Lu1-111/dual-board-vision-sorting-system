"""Run an exported workcell classifier with ONNX Runtime on CPU."""
import argparse
import json
from pathlib import Path

import numpy as np
import onnxruntime as ort
from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parent
CLASSES = ['empty', 'object_present', 'hand_present']
MODEL_NAMES = ['efficientnet_b0']

def prepare(path):
    with Image.open(path) as opened:
        image = ImageOps.exif_transpose(opened).convert('RGB')
    w,h=image.size
    scale=min(224/w,224/h)
    image=image.resize((max(1,round(w*scale)),max(1,round(h*scale))),Image.Resampling.BILINEAR)
    canvas=Image.new('RGB',(224,224),(114,114,114))
    canvas.paste(image,((224-image.width)//2,(224-image.height)//2))
    array=np.asarray(canvas,dtype=np.float32)/255.0
    array=(array-np.array([0.485,0.456,0.406],dtype=np.float32))/np.array([0.229,0.224,0.225],dtype=np.float32)
    return np.ascontiguousarray(array.transpose(2,0,1)[None])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image',type=Path)
    parser.add_argument('--model',choices=MODEL_NAMES,default='efficientnet_b0')
    args=parser.parse_args()
    options=ort.SessionOptions()
    options.intra_op_num_threads=2
    session=ort.InferenceSession(str(ROOT/'models'/args.model/'model_fp32.onnx'),sess_options=options,providers=['CPUExecutionProvider'])
    logits=session.run(['logits'],{'images':prepare(args.image)})[0][0]
    probs=np.exp(logits-logits.max());probs/=probs.sum()
    print(json.dumps({'model':args.model,'image':str(args.image),'prediction':CLASSES[int(probs.argmax())],
                      'probabilities':{label:round(float(p),6) for label,p in zip(CLASSES,probs)}},ensure_ascii=False))

if __name__=='__main__':main()
