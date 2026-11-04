"""Drive the actual Qt simulator from synthetic detector JSON; no GPIO."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

root = Path(__file__).resolve().parents[1]
script = root/"tools"/"vision_xy.py"
spec = importlib.util.spec_from_file_location("vision_xy", str(script))
vision = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vision)
binary = os.path.abspath(sys.argv[1])

with tempfile.TemporaryDirectory(prefix="vision-qt-") as folder:
    folder = Path(folder)
    endpoint = str(folder/"arm.sock")
    environment = dict(os.environ, HDMI_UI_ARM_SOCKET=endpoint, QT_QPA_PLATFORM="offscreen",
                       XDG_DATA_HOME=str(folder/"data"), XDG_CONFIG_HOME=str(folder/"config"))
    with open(str(folder/"qt.log"), "w") as log:
        app = subprocess.Popen([binary, "--arm-page"], env=environment, stdout=log, stderr=log)
        running = threading.Event()
        running.set()
        latest, points_path, calibration_path = [folder/p for p in ("latest.json","points.json","plane.json")]

        def write_frames():
            frame = {"coordinate_frame": "camera_pixel", "image_width": 640, "image_height": 480,
                     "input_fraction": 1, "objects": [{"cx": 100, "cy": 100, "color": "green",
                     "class_name": "green_good", "detection_confidence": 0.95}]}
            while running.is_set():
                frame["timestamp_ms"] = time.time()*1000
                temporary = folder/"new.json"
                temporary.write_text(json.dumps(frame), encoding="utf-8")
                os.replace(str(temporary), str(latest))
                time.sleep(0.02)

        writer = threading.Thread(target=write_frames)
        writer.start()
        try:
            for _ in range(200):
                if os.path.exists(endpoint) and latest.exists():
                    break
                if app.poll() is not None:
                    raise RuntimeError("Qt simulator exited during startup")
                time.sleep(0.02)
            state = vision.request(endpoint, {"action":"connect", "mode":"simulation"})
            assert state["mode"] == "simulation"
            vision.request(endpoint, {"action":"debug-enable", "confirm": True,
                                      "baseline":[500,1495.7,539.2,970.4,1252.6,1652.5]})
            points = [{"pixel":[u,v], "xyCm":[10+u/20, 5-v/20]}
                      for u,v in ((0,0),(200,0),(200,200),(0,200),(100,100))]
            points_path.write_text(json.dumps({"points": points}), encoding="utf-8")

            def run(arguments, success=True):
                process = subprocess.run([sys.executable,str(script),"--socket",endpoint]+arguments,
                                         stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                         universal_newlines=True, timeout=5)
                assert (process.returncode == 0) == success, (process.stdout, process.stderr)
                return json.loads(process.stdout) if success else process.stderr

            run(["calibrate", "--points",str(points_path), "--latest",str(latest),
                 "--z-cm","2", "--pitch-deg","-45", "--allow-pitch-search",
                 "--output",str(calibration_path)])
            target = ["target","--calibration",str(calibration_path),"--latest",str(latest),
                      "--class-name","green_good","--duration-ms","20"]
            preview = run(target)
            assert not preview["arm"]["moving"]
            assert abs(preview["arm"]["achievedCm"][0]-15) < 1e-5
            run(target+["--move"])
            time.sleep(0.1)
            state = vision.request(endpoint, {"action":"status"})
            assert not state["moving"] and state["enabled"]
            assert max(abs(a-b) for a,b in zip(state["cartesianCm"],[15,0,2])) < 1e-5, state
            assert "calibrated observation pose" in run(target+["--move"], success=False)
            vision.request(endpoint, {"action":"off"})
            print("Real Qt simulation: calibration -> fresh NPU JSON -> XY preview/move -> FK endpoint -> changed pose rejection: PASS")
        finally:
            running.clear()
            writer.join(2)
            app.terminate()
            try:
                app.wait(timeout=3)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait()
