import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import unittest

spec = importlib.util.spec_from_file_location("belt_calibration", Path(__file__).resolve().parents[1] / "tools/belt_calibration.py")
belt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(belt)


class CalibrationTests(unittest.TestCase):
    def setUp(self):
        self.points = [{"pixel": [u, v], "xyCm": [u/20, v/20]}
                       for u, v in [(20, 20), (400, 20), (400, 300), (20, 300), (200, 150)]]

    def test_measured_mapping(self):
        cal = belt.calibrate(self.points, 640, 480, 2, .01)
        x, y = belt.project(cal, 200, 100)
        self.assertAlmostEqual(x, 10)
        self.assertAlmostEqual(y, 5)

    def test_outside_region(self):
        cal = belt.calibrate(self.points, 640, 480, 2, .01)
        with self.assertRaises(ValueError):
            belt.project(cal, 500, 400)

    def test_collinear_rejected(self):
        with self.assertRaises(ValueError):
            belt.calibrate([{"pixel": [i, i], "xyCm": [i, i]} for i in range(4)], 640, 480, 2, .1)

    def test_encoder_scale(self):
        self.assertEqual(belt.encoder_scale(10, 100, 1100)["cm_per_count"], .01)
        self.assertEqual(belt.encoder_scale(10, 1100, 100)["encoder_sign"], -1)

    def test_wrapped_encoder(self):
        self.assertEqual(belt.encoder_scale(1, 0xfffffffe, 2)["cm_per_count"], .25)
        with self.assertRaises(ValueError):
            belt.encoder_scale(1, 2, 2)

    def test_unfilled_template(self):
        with self.assertRaises(ValueError):
            belt.calibrate([{"pixel": [None, None], "xyCm": [None, None]}]*4, 640, 480, 2, .1)

    def test_cli_encoder_scale(self):
        result = subprocess.run(
            [sys.executable, str(spec.origin), "encoder-scale", "--travel-cm", "10",
             "--before", "100", "--after", "1100"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["cm_per_count"], .01)

    def test_cli_requires_command(self):
        result = subprocess.run([sys.executable, str(spec.origin)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                universal_newlines=True, timeout=5)
        self.assertEqual(result.returncode, 2)
        self.assertIn("required", result.stderr)


if __name__ == "__main__":
    unittest.main()
