"""Split the validated mixed graph at its feature/head boundary for the vendor parser."""
import json
import onnx
from onnx import numpy_helper
from common import *

def main():
    root=ROOT/'package/models';root.mkdir(parents=True,exist_ok=True)
    source=ART/'float_features.onnx'
    feature=root/'triangle_v28_features_fp32.onnx'
    head=root/'triangle_v28_head_uint8.onnx'
    boundary='/Concat_1_output_0'
    onnx.utils.extract_model(str(source),str(feature),['input'],[boundary])
    onnx.utils.extract_model(str(source),str(head),[boundary],['local_logits'])
    fs=session(feature);hs=session(head);ms=session(source)
    model=onnx.load(str(head));values={x.name:numpy_helper.to_array(x) for x in model.graph.initializer}
    node=next(n for n in model.graph.node if n.op_type=='QuantizeLinear' and n.input[0]==boundary)
    scale=float(values[node.input[1]]);zero=int(values[node.input[2]])
    max_error=0.;agreement=0;rows=[]
    for i,r in enumerate(rec for rec in records() if rec['split']!='train'):
        x=load_input(r);intermediate=fs.run(None,{'input':x})[0]
        # Explicit CPU-to-NPU tensor bridge, same nearest-even rounding used by C++.
        u8=np.clip(np.rint(intermediate/scale)+zero,0,255).astype(np.uint8)
        dequant=(u8.astype(np.float32)-zero)*scale
        separate=hs.run(None,{boundary:dequant})[0]
        mixed=ms.run(None,{'input':x})[0]
        err=float(np.max(np.abs(separate-mixed)));max_error=max(err,max_error)
        equal=(score(separate)>=THRESHOLD)==(score(mixed)>=THRESHOLD)
        agreement+=equal
        rows.append(dict(id=r['id'],split=r['split'],label=r['label'],score=score(separate),max_logit_delta=err))
        if (i+1)%60==0:print(f'split equivalence {i+1}/288',flush=True)
    assert agreement==288
    contract={'version':'V28', 'deployment':'FP32 CpuAcc feature extractor + UINT8 Alnpu classification head',
        'threshold':THRESHOLD,'parameter_changes':False,'feature_input':{'name':'input','shape':[1,3,320,320],'dtype':'float32'},
        'bridge':{'name':boundary,'shape':[1,72,40,40],'dtype':'uint8','scale':scale,'zero_point':zero,'rounding':'nearest ties to even, saturate 0..255'},
        'output':{'name':'local_logits','shape':[1,1,40,40],'dtype':'uint8','scale':0.10496816039085388,'zero_point':96},
        'aggregation':'Dequantize all 1600 logits, mean of four largest, sigmoid, compare >=0.88',
        'preprocessing':'PIL-compatible BGR to gray, full-frame crop -> bilinear224 -> bilinear320; crop-only -> bilinear320; /255, repeat3, ImageNet mean/std',
        'models':[{'file':p.name,'bytes':p.stat().st_size,'sha256':sha(p)} for p in [feature,head]],
        'calibration':'721 training images only','split_equivalence':{'n':288,'label_agreement':agreement,'max_logit_error':max_error},
        'board_verified':False,'camera_verified':False,'display_verified':False}
    write_json(ROOT/'package/deployment_contract.json',contract)
    write_json(REPORTS/'split_equivalence.json',dict(contract=contract,rows=rows))
    print(json.dumps(contract,indent=2),flush=True)

if __name__=='__main__':main()
