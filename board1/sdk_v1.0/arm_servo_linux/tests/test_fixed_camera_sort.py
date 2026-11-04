import copy
import pathlib
import sys
import unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import fixed_camera_sort as s

class FixedCameraTests(unittest.TestCase):
    def setUp(self):
        self.cfg = s.load(pathlib.Path(s.__file__).with_name('fixed_camera_sort.example.json'))
        self.frame = dict(coordinate_frame='camera_pixel', image_width=1920, image_height=1080,
                          objects=[dict(class_name='red', cx=960, cy=540, detection_confidence=.9)])
    def test_projection(self):
        self.assertEqual(s.target(self.frame, self.cfg)[1], [18, 0])
        xy = s.project(self.cfg['camera'], [1050, 630], 3)
        self.assertAlmostEqual(xy[0], 14.3)
        self.assertAlmostEqual(xy[1], -3.7)
    def test_yaw(self):
        self.cfg['camera']['yaw_deg'] = 90
        xy = s.project(self.cfg['camera'], [1050, 540], 3)
        self.assertAlmostEqual(xy[0], 21.7)
        self.assertAlmostEqual(xy[1], 0)
    def test_ambiguity(self):
        self.frame['objects'] *= 2
        with self.assertRaises(ValueError): s.target(self.frame, self.cfg)
    def test_rejections(self):
        for field, value in [('class_name','unknown'), ('detection_confidence', .1), ('cx', 1919), ('cy', float('nan'))]:
            f = copy.deepcopy(self.frame); f['objects'][0][field] = value
            with self.assertRaises(ValueError): s.target(f, self.cfg)
        self.frame['image_width'] = 640
        with self.assertRaises(ValueError): s.target(self.frame, self.cfg)
    def test_common_destination(self):
        for zone in self.cfg['zones'].values(): self.assertEqual(zone['place_xyz_cm'], [15,8,2])
    def test_hardware_guard(self):
        state = dict(connected=True, enabled=True, initialPoseAcknowledged=True,
                     coordinateFrameCompatible=True, mode='hardware', visionReference={'pulses':[1500]*6})
        with self.assertRaises(ValueError): s.command(self.cfg, state, {'class_name':'red'}, [18,0], True)
        state['mode'] = 'simulation'
        self.assertEqual(s.command(self.cfg, state, {'class_name':'red'}, [18,0])['action'], 'fixed-sort-preview')

if __name__ == '__main__': unittest.main()
