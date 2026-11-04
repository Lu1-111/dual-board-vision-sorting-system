"""Exercise PC batch completion, old-photo handling and cancellation without hardware."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from guided_capture import load_plan, run_batch


class SimLink:
    def __init__(self):
        self.pending = False
        self.acks = self.captures = 0
        self.events = []
        self.guidance = ''
        self.cancel_download = False

    def rpc(self, action, **fields):
        self.events.append(action)
        if action == 'status':
            return dict(guidance_protocol=1, camera_ready=True, pending=self.pending,
                        busy=self.pending, guidance_seconds=0, photo=getattr(self, 'photo', None))
        if action == 'guide':
            # Never tell the user to move before the captured frame is frozen.
            if fields['phase'] == 'transfer':
                assert self.pending
            self.guidance = fields['phase']
        if action == 'capture':
            assert not self.pending and self.guidance == 'capture'
            self.captures += 1
            self.pending = True
            self.data = b'\xff\xd8' + str(self.captures).encode() + b'\xff\xd9'
            self.photo = dict(photo_id=str(uuid.uuid4()), source='simulation', width=1920,
                              height=1080, sha256=hashlib.sha256(self.data).hexdigest(),
                              size=len(self.data), format='jpg')
        if action == 'ack':
            assert self.pending and fields['sha256'] == self.photo['sha256']
            self.pending = False
            self.acks += 1
        return {'ok': True}

    def download(self, photo, should_stop):
        if self.cancel_download or should_stop():
            raise KeyboardInterrupt()
        return self.data


class GuidedTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        plan_path = Path(__file__).resolve().parents[2] / 'color_block_detection/field_training/20261030_v1/guided_pilot_01.json'
        self.plan = load_plan(plan_path)
        self.plan['steps'] = self.plan['steps'][:2]
        self.link = SimLink()

    def run_batch(self):
        return run_batch(self.link, self.plan, self.root / 'photos', self.root / 'batch', lambda: False,
                         prepare_seconds=0, hold_seconds=0, next_prepare_seconds=0, allow_simulation=True)

    def test_bounded_batch_is_saved_but_not_approved(self):
        self.assertEqual(self.run_batch(), 2)
        self.assertEqual((self.link.captures, self.link.acks, self.link.guidance), (2, 2, 'done'))
        self.assertEqual(len(list((self.root / 'photos').glob('*.jpg'))), 2)
        row = json.loads((self.root / 'batch/FT-001.json').read_text(encoding='utf-8'))
        self.assertFalse(row['training_eligible'])
        self.assertEqual(row['visual_review'], 'pending')

    def test_old_photo_not_mislabeled(self):
        self.link.pending = True
        with self.assertRaisesRegex(RuntimeError, '旧照片'):
            self.run_batch()
        self.assertEqual(self.link.captures, 0)
        self.assertFalse((self.root / 'batch').exists())

    def test_interrupted_transfer_remains_in_ram(self):
        self.link.cancel_download = True
        with self.assertRaises(KeyboardInterrupt):
            self.run_batch()
        self.assertTrue(self.link.pending)
        self.assertEqual((self.link.captures, self.link.acks, self.link.guidance), (1, 0, 'idle'))
        self.assertFalse((self.root / 'batch/FT-001.json').exists())


if __name__ == '__main__':
    unittest.main()
