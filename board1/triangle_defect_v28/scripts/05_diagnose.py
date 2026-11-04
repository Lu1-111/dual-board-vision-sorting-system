import onnx
from onnx import helper, TensorProto
from common import *

def main():
    f = onnx.load(str(ART/'triangle_v28_float_sim.onnx'))
    q = onnx.load(str(ART/'triangle_v28_uint8.onnx'))
    q_outputs = {o for n in q.graph.node for o in n.output}
    names = [n.output[0] for n in f.graph.node if n.op_type in ['Conv','Mul','Add','Resize','Concat']]
    pairs=[]
    for name in names:
        qname = name+'_DequantizeLinear_Output' if name+'_DequantizeLinear_Output' in q_outputs else name
        if qname not in q_outputs:
            continue
        pairs.append((name,qname))
    del f.graph.output[:]
    del q.graph.output[:]
    for a,b in pairs:
        f.graph.output.append(helper.make_tensor_value_info(a,TensorProto.FLOAT,None))
        q.graph.output.append(helper.make_tensor_value_info(b,TensorProto.FLOAT,None))
    onnx.save(f,str(ART/'diag_float.onnx'))
    onnx.save(q,str(ART/'diag_quant.onnx'))
    fp=session(ART/'diag_float.onnx'); qp=session(ART/'diag_quant.onnx')
    rec=next(r for r in records() if r['id']=='blue_053_historic')
    x=load_input(rec)
    aa=fp.run(None,{'input':x});bb=qp.run(None,{'input':x})
    rows=[]
    for (name,_),a,b in zip(pairs,aa,bb):
        a=a.astype(np.float64);b=b.astype(np.float64)
        error=np.mean((a-b)**2); power=np.mean(a*a)
        row={'name':name,'range':[float(a.min()),float(a.max())], 'quant_range':[float(b.min()),float(b.max())],
             'relative_rmse':float(np.sqrt(error/(power+1e-20))), 'mean_delta':float(np.mean(b-a))}
        rows.append(row)
        print(name, 'relerr',round(row['relative_rmse'],4), 'fp',np.round(row['range'],3),'q',np.round(row['quant_range'],3),flush=True)
    write_json(REPORTS/'layer_error.json',rows)

if __name__=='__main__':main()
