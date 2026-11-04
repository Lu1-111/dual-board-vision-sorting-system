import onnx
from onnx import numpy_helper
from common import *

def main():
    path=ART/'float_features.onnx'
    model=onnx.load(str(path))
    out=ROOT/'parser_probes';out.mkdir(exist_ok=True)
    for i in [0,1,2,3,4,5,6,9,15,25,50]:
        node=model.graph.node[i]
        onnx.utils.extract_model(str(path),str(out/f'{i:02}_{node.op_type}.onnx'),['input'],[node.output[0]])
    changed=[]
    # Broadcast scalar arithmetic constants as rank-four NCHW tensors.
    for node in model.graph.node:
        if node.op_type not in ['Mul','Add']:continue
        for name in node.input:
            for init in model.graph.initializer:
                if init.name!=name or len(init.dims)!=0:continue
                value=numpy_helper.to_array(init).reshape(1,1,1,1)
                init.CopyFrom(numpy_helper.from_array(value,name))
                changed.append(name)
    onnx.checker.check_model(model)
    onnx.save(model,str(out/'broadcast4.onnx'))
    model=onnx.load(str(path))
    node=next(n for n in model.graph.node if n.op_type=='Resize')
    print('Resize',node)
    model.graph.initializer.append(numpy_helper.from_array(np.array([1,1,4,4],np.float32),'resize_scales_4x'))
    del node.input[1:]
    node.input.extend(['','resize_scales_4x'])
    onnx.checker.check_model(model)
    onnx.save(model,str(out/'resize_scales.onnx'))
    onnx.utils.extract_model(str(path),str(out/'pre_head.onnx'),['input'],['/Concat_1_output_0'])
    onnx.utils.extract_model(str(path),str(out/'head_only.onnx'),['/Concat_1_output_0'],['local_logits'])
    print(changed)
if __name__=='__main__':main()
