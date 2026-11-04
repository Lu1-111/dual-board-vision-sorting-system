#!/usr/bin/env python3
"""Fixed-plane belt pixel -> centimetre calibration. Never commands a motor."""
import argparse
import importlib.util
import json
from pathlib import Path

# Reuse the project's normalized homography and convex-hull implementation.
# Importing it does not connect to Qt, request GPIO, or move the robot arm.
_source = Path(__file__).resolve().parents[2] / "arm_servo_linux/tools/vision_xy.py"
_spec = importlib.util.spec_from_file_location("arm_plane_math", _source)
plane = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(plane)


def calibrate(points, width, height, z_cm, tolerance_cm):
    if width <= 0 or height <= 0 or not 0 < tolerance_cm <= 1:
        raise ValueError("invalid image size or fit tolerance")
    plane.number(z_cm)
    for point in points:
        u, v = plane.pair(point["pixel"])
        if not (0 <= u < width and 0 <= v < height):
            raise ValueError("calibration pixel outside image")
    projection = plane.fit(points, tolerance_cm)
    return {"version": 1, "units": "cm", "imageSize": [width, height],
            "planeZCm": z_cm, "projection": projection}


def project(calibration, u, v):
    if calibration.get("version") != 1 or calibration.get("units") != "cm":
        raise ValueError("unsupported belt calibration")
    pixel = plane.pair([u, v])
    width, height = plane.pair(calibration["imageSize"])
    if not (0 <= u < width and 0 <= v < height):
        raise ValueError("pixel outside calibrated image")
    if not plane.inside(calibration["projection"]["pixelHull"], pixel):
        raise ValueError("pixel outside measured calibration region")
    return plane.transform(calibration["projection"], pixel)


def encoder_scale(travel_cm, before, after):
    travel_cm = plane.number(travel_cm)
    if travel_cm <= 0 or not all(isinstance(n, int) and 0 <= n <= 0xffffffff for n in (before, after)):
        raise ValueError("travel must be positive and raw counts must be uint32")
    delta = (after-before) & 0xffffffff
    if delta >= 0x80000000:
        delta -= 0x100000000
    if delta == 0 or delta == -0x80000000:
        raise ValueError("encoder count difference is zero or ambiguous")
    return {"cm_per_count": travel_cm/abs(delta), "encoder_sign": 1 if delta > 0 else -1,
            "x4_count_difference": delta}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command")
    commands.required = True
    cal = commands.add_parser("calibrate")
    cal.add_argument("--points", required=True)
    cal.add_argument("--width", type=int, required=True)
    cal.add_argument("--height", type=int, required=True)
    cal.add_argument("--plane-z-cm", type=float, required=True)
    cal.add_argument("--tolerance-cm", type=float, default=0.2)
    cal.add_argument("--output", required=True)
    xy = commands.add_parser("project")
    xy.add_argument("--calibration", required=True)
    xy.add_argument("--u", type=float, required=True)
    xy.add_argument("--v", type=float, required=True)
    enc = commands.add_parser("encoder-scale")
    enc.add_argument("--travel-cm", type=float, required=True)
    enc.add_argument("--before", type=lambda s: int(s, 0), required=True)
    enc.add_argument("--after", type=lambda s: int(s, 0), required=True)
    args = parser.parse_args()
    if args.command == "calibrate":
        result = calibrate(plane.load(args.points)["points"], args.width, args.height,
                           args.plane_z_cm, args.tolerance_cm)
        # Do not silently overwrite an existing measured calibration.
        with open(args.output, "x", encoding="utf-8") as stream:
            json.dump(result, stream, indent=2, allow_nan=False)
        print(json.dumps({"output": args.output, "max_fit_error_cm": result["projection"]["maxFitErrorCm"]}))
    elif args.command == "project":
        print(json.dumps({"xyCm": project(plane.load(args.calibration), args.u, args.v)}, allow_nan=False))
    else:
        print(json.dumps(encoder_scale(args.travel_cm, args.before, args.after), allow_nan=False))


if __name__ == "__main__":
    main()
