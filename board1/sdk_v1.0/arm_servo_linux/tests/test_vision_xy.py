"""Calibration, detector schema and one-shot socket pipeline, without hardware."""
import importlib.util
import json
import math
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "vision_xy.py"
spec = importlib.util.spec_from_file_location("vision_xy", str(SCRIPT))
vision = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vision)


class VisionTests(unittest.TestCase):
    def setUp(self):
        self.points = [{"pixel": [u,v], "xyCm": [10+u/20, 5-v/20]}
                       for u,v in ((0,0),(200,0),(200,200),(0,200),(100,100))]
        self.model = vision.fit(self.points, 0.01)
        self.calibration = {"version": 1, "imageSize": [640,480], "inputFraction": 1,
                            "projection": self.model, "visionReference": {"pulses": [1500]*6},
                            "zCm": 2, "pitchDeg": -45}
        self.frame = {"coordinate_frame": "camera_pixel", "image_width": 640, "image_height": 480,
                      "input_fraction": 1, "timestamp_ms": time.time()*1000,
                      "objects": [{"class_name": "green_good", "color": "green", "defect": False,
                                   "detection_confidence": 0.95, "cx": 100, "cy": 100}]}

    def test_projection(self):
        x,y = vision.transform(self.model, [60,80])
        self.assertAlmostEqual(x, 13)
        self.assertAlmostEqual(y, 1)
        # Non-affine perspective mapping and an independent withheld point.
        points = [{"pixel": [u,v], "xyCm": [(u+10)/(1+u/1000), (v-20)/(1+u/1000)]}
                  for u,v in ((0,0),(200,0),(200,200),(0,200),(100,80))]
        model = vision.fit(points, 1e-6)
        x,y = vision.transform(model, [75,125])
        self.assertAlmostEqual(x, 85/1.075)
        self.assertAlmostEqual(y, 105/1.075)

    def test_invalid_calibration(self):
        for points in (self.points[:3], [{"pixel": [i,i], "xyCm": [i,i]} for i in range(5)],
                       [{"pixel": [None,0], "xyCm": [0,0]}]*4):
            with self.assertRaises(ValueError):
                vision.fit(points, 0.1)
        points = json.loads(json.dumps(self.points))
        points[-1]["xyCm"][0] += 5
        with self.assertRaises(ValueError):
            vision.fit(points, 0.01)
        with self.assertRaises(ValueError):
            vision.number(float("nan"))

    def test_detector_schema(self):
        obj, xy = vision.select_target(self.frame, self.calibration, "green", "green_good")
        self.assertEqual(obj["class_name"], "green_good")
        self.assertAlmostEqual(xy[0], 15)
        self.assertAlmostEqual(xy[1], 0)
        for edit in ({"image_width": 1280}, {"input_fraction": 0.5}, {"objects": []}):
            frame = dict(self.frame, **edit)
            with self.assertRaises(ValueError):
                vision.select_target(frame, self.calibration)
        self.frame["objects"] *= 2
        with self.assertRaises(ValueError):
            vision.select_target(self.frame, self.calibration)
        self.assertEqual(vision.select_target(self.frame, self.calibration, index=1)[1], xy)
        self.frame["objects"][0]["cx"] = 300
        with self.assertRaises(ValueError):
            vision.select_target(self.frame, self.calibration, index=0)

    def test_observation_and_stale_data(self):
        state = {"connected": True, "enabled": True, "initialPoseAcknowledged": True,
                 "coordinateFrameCompatible": True, "visionReference": self.calibration["visionReference"]}
        self.assertEqual(vision.observation(state), self.calibration["visionReference"])
        with self.assertRaises(ValueError):
            vision.observation(dict(state, moving=True))
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/"latest.json"
            self.frame["timestamp_ms"] = time.time()*1000-5000
            path.write_text(json.dumps(self.frame), encoding="utf-8")
            with self.assertRaises(ValueError):
                vision.fresh_frame(str(path), 0, wait_seconds=0)

    @unittest.skipUnless(hasattr(socket, "AF_UNIX") and sys.platform == "linux", "Linux board socket")
    def test_end_to_end_socket(self):
        with tempfile.TemporaryDirectory(prefix="vision-xy-") as folder:
            root = Path(folder)
            calibration_path, latest = root/"calibration.json", root/"latest.json"
            points_path = root/"points.json"
            points_path.write_text(json.dumps({"points": self.points}), encoding="utf-8")
            path = str(root/"arm.sock")
            requests, errors = [], []
            running = threading.Event()
            running.set()

            def write_frames():
                while running.is_set():
                    self.frame["timestamp_ms"] = time.time()*1000
                    temporary = root/"new.json"
                    temporary.write_text(json.dumps(self.frame), encoding="utf-8")
                    os.replace(str(temporary), str(latest))
                    time.sleep(0.02)

            def serve():
                try:
                    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
                        server.bind(path)
                        server.listen(1)
                        server.settimeout(5)
                        for _ in range(6):
                            peer,_ = server.accept()
                            with peer:
                                data = b""
                                while b"\n" not in data:
                                    data += peer.recv(4096)
                                command = json.loads(data)
                                requests.append(command)
                                reply = {"ok": True, "connected": True, "enabled": True,
                                         "initialPoseAcknowledged": True, "coordinateFrameCompatible": True,
                                         "visionReference": self.calibration["visionReference"]}
                                peer.sendall((json.dumps(reply)+"\n").encode())
                except Exception as error:
                    errors.append(error)

            writer = threading.Thread(target=write_frames)
            server = threading.Thread(target=serve)
            writer.start()
            server.start()
            try:
                for _ in range(100):
                    if os.path.exists(path) and latest.exists():
                        break
                    time.sleep(0.01)
                result = subprocess.run([sys.executable, str(SCRIPT), "--socket", path, "calibrate",
                                         "--points", str(points_path), "--latest", str(latest),
                                         "--output", str(calibration_path), "--z-cm", "2", "--pitch-deg", "-45",
                                         "--allow-pitch-search"], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        universal_newlines=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(json.loads(calibration_path.read_text())["allowPitchSearch"])
                for move in (False, True):
                    command = [sys.executable, str(SCRIPT), "--socket", path, "target",
                               "--calibration", str(calibration_path), "--latest", str(latest)]
                    if move:
                        command.append("--move")
                    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                            universal_newlines=True, timeout=5)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    xy = json.loads(result.stdout)["xyCm"]
                    self.assertAlmostEqual(xy[0], 15)
                    self.assertAlmostEqual(xy[1], 0)
            finally:
                running.clear()
                writer.join(2)
                server.join(6)
            self.assertFalse(errors, errors)
            self.assertEqual([c["action"] for c in requests], ["status","status","status","xy-preview","status","xy-move"])
            self.assertEqual(requests[-1]["visionReference"], self.calibration["visionReference"])
            self.assertEqual(requests[-1]["zCm"], 2)
            self.assertTrue(requests[-1]["allowPitchSearch"])


if __name__ == "__main__":
    unittest.main()
