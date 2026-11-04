"""Vendor UINT8 QDQ flow, calibrated on the 721 training images only."""
import sys
import types
import runpy
import shutil
import json
from common import *

VENDOR = ROOT.parent / 'cube_defect_quant_v1/scripts/vendor_code'

class DataReader:
    def __init__(self, *args, **kwargs):
        self.samples = [r for r in records() if r['split'] == 'train']
        assert len(self.samples) == 721
        other = [r for r in records() if r['split'] != 'train']
        assert not ({r['group'] for r in self.samples} & {r['group'] for r in other})
        assert not ({r['sha256'] for r in self.samples} & {r['sha256'] for r in other})
        for r in self.samples:
            assert sha(SOURCE / r['path']) == r['sha256'], r['path']
        write_json(ROOT / 'calibration/manifest.json', {'source_manifest_sha256': sha(SOURCE/'data/manifest.json'),
                    'count': len(self.samples), 'train_only': True, 'records': self.samples})
        self.rewind()
    def get_next(self):
        if self.index >= len(self.samples):
            return None
        rec = self.samples[self.index]
        self.index += 1
        if self.index % 50 == 0:
            print(f'Calibration {self.index}/{len(self.samples)}', flush=True)
        return {'input': load_input(rec)}
    def rewind(self):
        self.index = 0

def main():
    module = types.ModuleType('data_reader')
    module.DataReader = DataReader
    sys.modules['data_reader'] = module
    sys.path.insert(0, str(VENDOR))
    entry = VENDOR / 'AL_onnx_pass.py'
    sys.argv = [str(entry), '--input_model', str(ART/'triangle_v28_float.onnx'),
                '--calibrate_dataset', str(SOURCE/'data/images/train'), '--quant_type', 'uint8']
    runpy.run_path(str(entry), run_name='__main__')
    source = ART/'triangle_v28_float_sim_quant_uint8.onnx'
    assert source.is_file()
    shutil.copyfile(source, ART/'triangle_v28_uint8.onnx')
    print('QUANTIZATION_COMPLETE', sha(ART/'triangle_v28_uint8.onnx'), flush=True)

if __name__ == '__main__':
    main()
