"""Diagnostic variants selected on validation only; use frozen weights/threshold."""
import argparse
import importlib
import shutil
import onnxruntime as ort
from onnxruntime.quantization import quantize_static, QuantFormat, QuantType, CalibrationMethod
from common import *

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('variant', choices=['per_channel', 'conv_only', 'conv_per_channel', 'float_stem', 'float_early', 'float_early_conv', 'float_early_per_channel', 'float_early4', 'float_early4_conv', 'float_features'])
    args = ap.parse_args()
    DataReader = importlib.import_module('02_quantize').DataReader
    kwargs = {}
    if 'conv' in args.variant:
        kwargs['op_types_to_quantize'] = ['Conv']
    if args.variant.startswith('float_'):
        import onnx
        model = onnx.load(str(ART/'triangle_v28_float_sim.onnx'))
        prefixes = ['/features.0/']
        if 'early' in args.variant:
            prefixes += ['/features.1/']
        if 'early4' in args.variant:
            prefixes += ['/features.2/', '/features.3/']
        if args.variant == 'float_features':
            prefixes = ['/features.', '/Resize', '/Concat']
        kwargs['nodes_to_exclude'] = [n.name for n in model.graph.node if any(n.name.startswith(p) for p in prefixes)]
    # ORT creates an inferred graph beside its input; separate names avoid concurrent clobbering.
    input_path = ART/f'{args.variant}_source.onnx'
    shutil.copyfile(ART/'triangle_v28_float_sim.onnx', input_path)
    quantize_static(str(input_path), str(ART/f'{args.variant}.onnx'),
                    DataReader(), quant_format=QuantFormat.QDQ, activation_type=QuantType.QUInt8,
                    weight_type=QuantType.QUInt8, per_channel='per_channel' in args.variant,
                    extra_options={'ForceQuantizeNoInputCheck': True}, **kwargs)
    print('VARIANT_COMPLETE', args.variant, flush=True)

if __name__ == '__main__':
    main()
