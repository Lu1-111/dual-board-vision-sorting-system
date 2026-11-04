"""Rebuild LeArm reference JSON groups using this project's C++ motion library.

No hardware access. Requires Python 3 and a C++11 g++ compiler on PATH.
Run without --check to export; --check validates the checked-in artifacts unchanged.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


UI = Path(__file__).resolve().parents[1]
SDK = UI.parent
ROOT = SDK.parent
REF = ROOT / "prj_v1.0/color_sorting_referrence/05_color_sorting/51/LeArm_C251"
OUT = UI / "action_groups/learm_reference"
CORE = SDK / "arm_servo_linux"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def verify_reference():
    # Explicitly reject a changed source recipe rather than silently exporting stale poses.
    main = re.sub(r"\s+", "", (REF / "User/main.c").read_bytes().decode("gb18030"))
    required = [
        "#defineULTRASOUND_OFFSET7.6f", "dis>=50&&dis<=100",
        "robot_arm_reset(1000);", "robot_arm_coordinate_set(12.0f,0,0.0f,0,500);",
        "robot_arm_coordinate_set((float)(dis/10)+ULTRASOUND_OFFSET,0,-7,0,1000);",
        "robot_arm_claw_set(0,200);", "robot_arm_coordinate_set(15.0f,0,15,0,1000);",
        "robot_arm_coordinate_set(5.0f,12.0f,-5,0,1000);",
        "robot_arm_coordinate_set(5.0f,-12.0f,-5,0,1000);",
        "robot_arm_coordinate_set(20.0f+ULTRASOUND_OFFSET,0,-5,0,1000);",
        "robot_arm_claw_set(90,0);", "Delay500ms();",
    ]
    for snippet in required:
        if snippet not in main:
            raise ValueError(f"LeArm source recipe changed; review conversion: {snippet}")
    global_h = (REF / "User/global.h").read_bytes().decode("gb18030")
    if not re.search(r"#define\s+ARM_SELECT\s+0\b", global_h):
        raise ValueError("Only the PWM arm reference is supported; BUS units are incompatible")
    config = dict(line.split("=", 1) for line in (CORE / "arm_config.ini").read_text().splitlines()
                  if line and not line.startswith("#"))
    for i in range(6):
        axis = f"axis{i}."
        expected = {"channel": i, "direction": 1, "min_angle_deg": 0,
                    "max_angle_deg": 90 if i == 0 else 180, "min_pulse_us": 500,
                    "max_pulse_us": 1500 if i == 0 else 2499.2, "offset_us": 0}
        for field, value in expected.items():
            if abs(float(config[axis + field]) - value) > 1e-9:
                raise ValueError("Template mapping changed; review reference pulse/angle conversion")
        home = float(re.search(rf"#define\s+PWM_SERVO{i+1}_RESET_DUTY\s+(\d+)", global_h)[1])
        lo = float(config[axis + "min_pulse_us"])
        span = float(config[axis + "max_pulse_us"]) - lo
        angle_span = float(config[axis + "max_angle_deg"])
        if abs(lo + float(config[axis + "home_angle_deg"]) / angle_span * span - home) > 1e-8:
            raise ValueError("Reference reset pulses differ from template home")
    return config


def validate_group(group):
    frames = group["frames"]
    if not 1 <= len(frames) <= 1000:
        raise ValueError("Invalid frame count")
    for frame in frames:
        angles = frame["anglesDeg"]
        if len(angles) != 6:
            raise ValueError("Expected six logical axes")
        for i, angle in enumerate(angles):
            if type(angle) not in (int, float) or not math.isfinite(angle) or not 0 <= angle <= (90 if i == 0 else 180):
                raise ValueError(f"Invalid axis {i} angle")
        for name, low in (("durationMs", 20), ("holdMs", 0)):
            if type(frame[name]) is not int or not low <= frame[name] <= 60000:
                raise ValueError(f"Invalid {name}")
        for i, (angle, pulse, quantized) in enumerate(zip(angles, frame["requestedPulsesUs"], frame["quantizedPulsesUs"])):
            maximum = 1500 if i == 0 else 2499.2
            mapped = 500 + angle / (90 if i == 0 else 180) * (maximum - 500)
            if abs(mapped - pulse) > 1e-8 or not 500 <= quantized <= maximum:
                raise ValueError("Pulse mapping or quantized limits mismatch")
        if "targetCm" in frame and frame["positionResidualCm"] >= 1e-5:
            raise ValueError("Unverified Cartesian pose")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    config = verify_reference()
    compiler = shutil.which(args.cxx)
    if not compiler:
        raise SystemExit("C++ compiler missing; install g++ or pass --cxx")
    with tempfile.TemporaryDirectory(prefix="learm-export-") as temp:
        exe = Path(temp) / "export_learm_actions.exe"
        sources = [UI / "tools/export_learm_actions.cpp"] + [CORE / f for f in
                   ("arm_control.cpp", "kinematics.cpp", "robot_arm.cpp")]
        subprocess.run([compiler, "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(CORE), *map(str, sources), "-o", str(exe)], check=True)
        result = subprocess.run([str(exe), str(CORE / "arm_config.ini")],
                                check=True, capture_output=True, text=True)
        audit = json.loads(result.stdout)
    files = {}
    groups = audit["groups"]
    if len(groups) != 20 or len({g["file"] for g in groups}) != 20:
        raise ValueError("Expected home + ready + 18 color/distance groups")
    for group in groups:
        validate_group(group)
        frames = [{k: frame[k] for k in ("anglesDeg", "durationMs", "holdMs")} for frame in group["frames"]]
        data = encoded({"version": 1, "frames": frames})
        # Parse bytes again, just as a file consumer does, before publication.
        parsed = json.loads(data)
        if parsed["frames"] != frames or len(data) > 1024 * 1024:
            raise ValueError("Invalid JSON serialization or UI size limit")
        if group["file"].startswith("sort_"):
            if [f["label"] for f in group["frames"]] != ["ready", "open", "approach", "close", "lift",
                                                             "place_" + group["file"].split("_")[1], "release", "return"]:
                raise ValueError("Sorting stages out of order")
            distance = group["distanceMmMin"]
            if group["frames"][2]["targetCm"] != [distance // 10 + 7.6, 0, -7]:
                raise ValueError("Distance integer-division mapping changed")
        group["jsonSha256"] = hashlib.sha256(data).hexdigest()
        group["totalDurationMs"] = sum(f["durationMs"] + f["holdMs"] for f in frames)
        files[group["file"]] = data
    referenced = [REF / p for p in ("User/main.c", "User/global.h", "Hiwonder/Arm/src/robot_arm.c",
                                   "Hiwonder/Arm/lnc/kinematics.h", "Hiwonder/Arm/lnc/robot_arm.h",
                                   "Hiwonder/Peripherals/src/pwm_servos.c")]
    referenced += [CORE / p for p in ("arm_config.ini", "kinematics.hpp", "kinematics.cpp", "robot_arm.cpp",
                                     "arm_control.hpp", "arm_control.cpp")]
    referenced += [UI / "arm_actions.cpp", UI / "tools/export_learm_actions.cpp", Path(__file__).resolve()]
    coordinates = [f for g in groups for f in g["frames"] if "targetCm" in f]
    boundaries = [{"file": g["file"], "stage": f["label"], "logicalAxesOneBased": [i+1 for i in range(2, 6)
                  if min(abs(f["anglesDeg"][i]), abs(f["anglesDeg"][i]-180)) < 1e-6]}
                  for g in groups for f in g["frames"] if any(
                      min(abs(f["anglesDeg"][i]), abs(f["anglesDeg"][i]-180)) < 1e-6 for i in range(2, 6))]
    inventory = sorted(p for p in REF.rglob("*") if p.is_file() and p.suffix.lower() in
                       {".c", ".h", ".py", ".json", ".rob", ".xml"})
    audit.update({
        "format": "LeArm conversion audit; this is not a loadable action group",
        "hardwareValidated": False,
        "mapping": "arm_config.ini template; normal mode; NOT full-range debug mapping",
        "axisOrder": [config[f"axis{i}.name"] for i in range(6)],
        "sourceFiles": [{"path": p.relative_to(ROOT).as_posix(), "sha256": sha(p)} for p in referenced],
        "referenceInventory": [{"path": p.relative_to(ROOT).as_posix(), "sha256": sha(p)} for p in inventory],
        "statistics": {
            "actionGroups": len(groups), "frames": sum(len(g["frames"]) for g in groups),
            "maxNominalQuantizedPositionErrorCm": max(f["quantizedPositionErrorCm"] for f in coordinates),
            "maxPitchAdjustmentDeg": max(abs(f["selectedPitchDeg"]-f["preferredPitchDeg"]) for f in coordinates),
            "jointBoundaryFrames": boundaries,
        },
        "warnings": [
            "SIMULATION / REVIEW TEMPLATES ONLY. All six axes and link geometry require physical calibration.",
            "JSON loader checks numeric ranges only. It does not enforce mapping hashes or hardwareValidated.",
            "Current full-range debug mode changes claw mapping; do not play these with that mapping.",
            "Static groups omit color/distance sensor gating and require manual distance/color selection.",
            "Original dis/10 truncates integer millimeters; 50..59 etc share a pose; 100 is a separate bucket.",
            "Pitch 0 is a preference; selected pitch may change substantially to reach XYZ.",
            "Many solutions touch mathematical joint limits. No mechanical safety clearance is established.",
            "FK verification covers commanded endpoints. No collision, load, velocity or physical feedback checks.",
            "First frame commands all six axes, fixes wrist roll to template home, and initially uses home claw angle.",
            "Startup overlap is serialized into ready 500ms then open 20ms. Source time=0 clamps to 20ms.",
            "Release is 20ms motion + 480ms hold. Sensor delays are omitted. UI scheduling can add latency.",
            "Original flash servo offsets are unknown. These groups use project template offsets, all zero.",
            "Joint mapping uses project maximum 2499.2us, versus original 2500us with integer truncation.",
            "Quantized values are nominal software output, not measured pin pulses or measured servo positions.",
            "UI save keeps only version/frames. Keep this manifest and README alongside the files.",
        ],
    })
    files["manifest.json"] = encoded(audit)
    OUT.mkdir(parents=True, exist_ok=True)
    if args.check:
        errors = [name for name, data in files.items() if not (OUT / name).exists() or (OUT / name).read_bytes() != data]
        if errors:
            raise SystemExit("Export differs or missing: " + ", ".join(errors))
    else:
        for name, data in files.items():
            path = OUT / name
            pending = path.with_suffix(".json.tmp")
            pending.write_bytes(data)
            pending.replace(path)
    frames = sum(len(g["frames"]) for g in groups)
    coordinate_frames = [f for g in groups for f in g["frames"] if "targetCm" in f]
    max_error = max(f["quantizedPositionErrorCm"] for f in coordinate_frames)
    max_pitch = max(abs(f["selectedPitchDeg"] - f["preferredPitchDeg"]) for f in coordinate_frames)
    print(f"PASS: {len(groups)} action groups, {frames} frames; JSON bounds, mock interpolation, FK and pulse limits verified.")
    print(f"Largest nominal quantized endpoint error: {max_error:.6f} cm; largest pitch adjustment: {max_pitch:.6f} degrees.")
    print("No hardware accessed. Templates require calibration and collision review.")


if __name__ == "__main__":
    main()
