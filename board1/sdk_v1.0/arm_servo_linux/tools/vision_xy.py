#!/usr/bin/env python3
"""One-shot NPU pixel -> calibrated plane -> Qt arm session. Python stdlib only."""
import argparse
import json
import math
import os
import socket
import struct
import time


def number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError("expected a finite number")
    return float(value)


def pair(value):
    if not isinstance(value, list) or len(value) != 2:
        raise ValueError("expected a pair")
    return [number(v) for v in value]


def load(path):
    with open(path, encoding="utf-8") as stream:
        return json.load(stream)


def solve(matrix, rhs):
    n = len(rhs)
    a = [list(row) + [rhs[i]] for i, row in enumerate(matrix)]
    for col in range(n):
        pivot = max(range(col, n), key=lambda i: abs(a[i][col]))
        if abs(a[pivot][col]) < 1e-10:
            raise ValueError("degenerate calibration: spread points across the work plane")
        a[col], a[pivot] = a[pivot], a[col]
        scale = a[col][col]
        a[col] = [v / scale for v in a[col]]
        for row in range(n):
            if row != col:
                scale = a[row][col]
                a[row] = [x - scale*y for x, y in zip(a[row], a[col])]
    return [a[i][-1] for i in range(n)]


def transform(model, pixel):
    u, v = pair(pixel)
    u = (u-model["pixelCenter"][0])/model["pixelScale"]
    v = (v-model["pixelCenter"][1])/model["pixelScale"]
    h = model["h"]
    w = h[6]*u + h[7]*v + 1
    if abs(w) < 1e-8:
        raise ValueError("pixel is on the projection horizon")
    return [number((h[0]*u+h[1]*v+h[2])/w*model["xyScale"]+model["xyCenter"][0]),
            number((h[3]*u+h[4]*v+h[5])/w*model["xyScale"]+model["xyCenter"][1])]


def cross(a, b, c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])


def hull(points):
    points = sorted(set(tuple(p) for p in points))
    halves = []
    for seq in (points, list(reversed(points))):
        half = []
        for p in seq:
            while len(half) > 1 and cross(half[-2], half[-1], p) <= 0:
                half.pop()
            half.append(p)
        halves.append(half[:-1])
    result = halves[0]+halves[1]
    if len(result) < 3:
        raise ValueError("calibration pixels must not be collinear")
    return result


def inside(polygon, point):
    return all(cross(a, polygon[(i+1) % len(polygon)], point) >= -1e-7
               for i, a in enumerate(polygon))


def fit(points, tolerance_cm):
    if not isinstance(points, list) or len(points) < 4:
        raise ValueError("at least four measured pixel/xyCm pairs are required")
    pixels = [pair(p["pixel"]) for p in points]
    xy = [pair(p["xyCm"]) for p in points]
    hull(xy)
    model = {"pixelHull": hull(pixels)}
    for prefix, values in (("pixel", pixels), ("xy", xy)):
        center = [sum(p[i] for p in values)/len(values) for i in range(2)]
        scale = math.sqrt(sum(sum((p[i]-center[i])**2 for i in range(2)) for p in values)/len(values))
        if scale < 1e-8:
            raise ValueError("calibration points coincide")
        model[prefix+"Center"], model[prefix+"Scale"] = center, scale
    rows, rhs = [], []
    for p, q in zip(pixels, xy):
        u, v = [(p[i]-model["pixelCenter"][i])/model["pixelScale"] for i in range(2)]
        x, y = [(q[i]-model["xyCenter"][i])/model["xyScale"] for i in range(2)]
        rows.extend(([u,v,1,0,0,0,-x*u,-x*v], [0,0,0,u,v,1,-y*u,-y*v]))
        rhs.extend((x, y))
    model["h"] = solve([[sum(r[i]*r[j] for r in rows) for j in range(8)] for i in range(8)],
                       [sum(r[i]*b for r, b in zip(rows, rhs)) for i in range(8)])
    # A projective pole inside the calibrated region makes interpolation invalid.
    denominators = [1+model["h"][6]*(p[0]-model["pixelCenter"][0])/model["pixelScale"]+
                    model["h"][7]*(p[1]-model["pixelCenter"][1])/model["pixelScale"] for p in model["pixelHull"]]
    if min(denominators) <= 1e-8:
        raise ValueError("projection horizon intersects the calibration region")
    model["maxFitErrorCm"] = max(math.hypot(*(a-b for a, b in zip(transform(model, p), q)))
                                  for p, q in zip(pixels, xy))
    if model["maxFitErrorCm"] > number(tolerance_cm):
        raise ValueError("calibration residual exceeds tolerance")
    return model


def request(path, command):
    data = (json.dumps(command, allow_nan=False, separators=(",", ":"))+"\n").encode()
    if len(data) > 4096:
        raise ValueError("arm request exceeds packet limit")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as peer:
        peer.settimeout(3)
        peer.connect(path)
        # Same peer rule as the Qt client; this is a local, same-UID interface.
        if hasattr(socket, "SO_PEERCRED"):
            _, uid, _ = struct.unpack("3i", peer.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            if uid != os.geteuid():
                raise ValueError("arm socket peer UID differs")
        peer.sendall(data)
        response = b""
        while b"\n" not in response:
            block = peer.recv(1024)
            if not block:
                raise ValueError("arm console closed before replying")
            response += block
            if len(response) > 4096:
                raise ValueError("arm response exceeds packet limit")
    result = json.loads(response.split(b"\n", 1)[0])
    if not result.get("ok"):
        raise ValueError(result.get("error", "arm request failed"))
    return result


def observation(status):
    if not (status.get("connected") and status.get("enabled") and status.get("initialPoseAcknowledged")
            and status.get("coordinateFrameCompatible")) or status.get("moving") or status.get("actionPlaying"):
        raise ValueError("arm must hold the calibrated observation pose with output enabled")
    reference = status.get("visionReference")
    if not isinstance(reference, dict) or len(reference.get("pulses", [])) != 6:
        raise ValueError("running Qt program needs the vision XY update")
    return reference


def fresh_frame(path, after_ms, max_age_ms=500, wait_seconds=3):
    deadline = time.monotonic()+wait_seconds
    while True:
        frame = load(path)
        stamp = number(frame.get("timestamp_ms"))
        age = time.time()*1000-stamp
        if frame.get("coordinate_frame") != "camera_pixel":
            raise ValueError("expected NPU camera_pixel result")
        if 0 <= age <= max_age_ms and stamp >= after_ms:
            return frame
        if time.monotonic() >= deadline:
            raise ValueError("no fresh detection after the stationary observation check")
        time.sleep(0.05)


def select_target(frame, calibration, color=None, class_name=None, index=None, min_confidence=0.5):
    if [frame.get("image_width"), frame.get("image_height")] != calibration["imageSize"] or \
            frame.get("input_fraction", 1) != calibration["inputFraction"]:
        raise ValueError("camera image size/crop changed; recalibrate")
    candidates = [o for o in frame["objects"] if (color is None or o.get("color") == color)
                  and (class_name is None or o.get("class_name") == class_name)
                  and min_confidence <= number(o.get("detection_confidence", 0)) <= 1]
    if index is None and len(candidates) != 1:
        raise ValueError("expected one matching object; use --color, --class-name or --index")
    if index is not None and (index < 0 or index >= len(candidates)):
        raise ValueError("object index outside the filtered list")
    obj = candidates[0 if index is None else index]
    pixel = pair([obj["cx"], obj["cy"]])
    if not inside(calibration["projection"]["pixelHull"], pixel):
        raise ValueError("target outside calibrated pixel region")
    return obj, transform(calibration["projection"], pixel)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", default=os.environ.get("HDMI_UI_ARM_SOCKET", "/tmp/hdmi-ui-arm.sock"))
    commands = parser.add_subparsers(dest="command")
    calibrate = commands.add_parser("calibrate")
    calibrate.add_argument("--points", required=True)
    calibrate.add_argument("--latest", required=True)
    calibrate.add_argument("--output", required=True)
    calibrate.add_argument("--z-cm", type=float, required=True)
    calibrate.add_argument("--pitch-deg", type=float, required=True)
    calibrate.add_argument("--allow-pitch-search", action="store_true")
    calibrate.add_argument("--tolerance-cm", type=float, default=0.3)
    target = commands.add_parser("target")
    target.add_argument("--calibration", required=True)
    target.add_argument("--latest", required=True)
    target.add_argument("--color")
    target.add_argument("--class-name")
    target.add_argument("--index", type=int)
    target.add_argument("--duration-ms", type=int, default=1000)
    target.add_argument("--move", action="store_true", help="otherwise preview only")
    args = parser.parse_args()
    if args.command is None:
        parser.error("choose calibrate or target")
    reference = observation(request(args.socket, {"action": "status"}))
    # Wait for a new detector result after a stationary hold. Never use an old
    # file left by a stopped detector or a frame from the preceding arm pose.
    frame = fresh_frame(args.latest, time.time()*1000+300)
    if args.command == "calibrate":
        z, pitch = number(args.z_cm), number(args.pitch_deg)
        if not -180 <= pitch <= 360 or not 0 < number(args.tolerance_cm) <= 1:
            raise ValueError("invalid pitch or fit tolerance")
        points = load(args.points)["points"]
        size = [number(frame["image_width"]), number(frame["image_height"])]
        if any(n <= 0 for n in size) or any(not (0 <= pair(p["pixel"])[0] < size[0] and
                0 <= pair(p["pixel"])[1] < size[1]) for p in points):
            raise ValueError("calibration pixels outside image")
        calibration = {"version": 1, "imageSize": size, "inputFraction": frame.get("input_fraction", 1),
                       "visionReference": reference, "zCm": z, "pitchDeg": pitch,
                       "allowPitchSearch": args.allow_pitch_search,
                       "projection": fit(points, args.tolerance_cm)}
        if observation(request(args.socket, {"action": "status"})) != reference:
            raise ValueError("arm pose changed during calibration")
        # Exclusive creation avoids replacing an earlier measured calibration.
        with open(args.output, "x", encoding="utf-8") as stream:
            json.dump(calibration, stream, indent=2, allow_nan=False)
        print(json.dumps({"ok": True, "calibration": args.output,
                          "maxFitErrorCm": calibration["projection"]["maxFitErrorCm"]}))
    else:
        calibration = load(args.calibration)
        if calibration.get("version") != 1 or reference != calibration["visionReference"]:
            raise ValueError("return to the calibrated observation pose and coordinate model")
        if not 20 <= args.duration_ms <= 60000:
            raise ValueError("duration must be 20..60000 ms")
        obj, xy = select_target(frame, calibration, args.color, args.class_name, args.index)
        command = {"action": "xy-move" if args.move else "xy-preview", "xy": xy,
                   "zCm": number(calibration["zCm"]), "pitchDeg": number(calibration["pitchDeg"]),
                   "allowPitchSearch": calibration.get("allowPitchSearch", False),
                   "visionReference": reference, "durationMs": args.duration_ms, "confirm": bool(args.move)}
        result = request(args.socket, command)
        print(json.dumps({"pixel": [obj["cx"], obj["cy"]], "xyCm": xy, "arm": result}, allow_nan=False))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError, OverflowError) as error:
        raise SystemExit("vision_xy: "+str(error))
