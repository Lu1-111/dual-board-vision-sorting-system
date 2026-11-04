"""Restore the user-confirmed BTS7960 CEP wiring and bounded commissioning mode.

COM14 board, 2026-10-28: R_EN Y8, L_EN W8, RPWM W9, LPWM Y11.
2026-11-04: encoder A=Y6 (CEP18), B=V10 (CEP16), user-confirmed level shifting.
Estop/fault pins are not assigned. Default remains bounded commissioning.
"""
from pathlib import Path
import re

PINS = {"bts_r_en": "Y8", "bts_l_en": "W8", "bts_rpwm": "W9", "bts_lpwm": "Y11", "txs_oe": "W11"}
INPUTS = {"encoder_a": "Y6", "encoder_b": "V10"}
PINS.update(INPUTS)


def replace_once(text, pattern, replacement):
    text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise ValueError("Conveyor motor integration anchor missing: " + pattern)
    return text


def add_ports(text, module):
    for name in PINS:
        direction = "input" if name in INPUTS else "output"
        if not re.search(direction + r"\s+wire\s+" + name + r"\b", text):
            text = replace_once(text, r"(module\s+" + module + r"\s*\(\s*\n)",
                                r"\1    " + direction + " wire " + name + ",\n")
    return text


def connect(text, instance):
    pattern = r"(\b" + instance + r"\s*\()(.*?)(\);)"
    match = re.search(pattern, text, re.S)
    if not match:
        raise ValueError("Conveyor instance missing: " + instance)
    body = match[2]
    for name in PINS:
        if re.search(r"\." + name + r"\s*\(", body):
            if not re.search(r"\." + name + r"\s*\(\s*" + name + r"\s*\)", body):
                raise ValueError("Unexpected conveyor connection: " + name)
        else:
            body = f"\n        .{name}({name})," + body
    return text[:match.start(2)] + body + text[match.end(2):]


def main(project, closed_loop=None, jog_limit_ms=None):
    project = Path(project)
    paths = [project / "src/system_top.v", project / "design_0/hdl/design_0.v", project / "src/constraint/io.adc"]
    original = [p.read_text(encoding="utf-8") for p in paths]
    top, design, adc = original
    # Preserve an explicitly selected profile on subsequent DI regeneration runs.
    if closed_loop is None:
        closed_loop = bool(re.search(r"conveyor_gp0\s*#\([^;]*?\.JOG_ONLY\(0\)", design))
    if jog_limit_ms is None:
        limit = re.search(r"conveyor_gp0\s*#\([^;]*?\.JOG_LIMIT_MS\((\d+)\)", design)
        jog_limit_ms = int(limit[1]) if limit else 1000
    top = connect(add_ports(top, "system_top"), "design_0_inst")
    design = connect(add_ports(design, "design_0"), "conveyor_gp0_inst")
    design = replace_once(design, r"conveyor_gp0\s+(?:#\(.*?\)\s*)?conveyor_gp0_inst\s*\(",
                          f"conveyor_gp0 #(.BENCH_ONLY(0), .JOG_ONLY({0 if closed_loop else 1}), .ENCODER_CONNECTED(1), .JOG_LIMIT_MS({jog_limit_ms})) conveyor_gp0_inst (")
    assignments = re.findall(r"set_pin_assignment\s*\{\s*([^}]+?)\s*\}\s*\{([^}]+)\}", adc)
    for name, pin in PINS.items():
        for signal, attributes in assignments:
            location = re.search(r"\bLOCATION\s*=\s*(\w+)", attributes)
            if location and location[1] == pin and signal != name:
                raise ValueError(f"Pin {pin} already belongs to {signal}")
            if signal == name and (not location or location[1] != pin):
                raise ValueError(f"Unexpected pin for {name}")
        if not any(signal == name for signal, _ in assignments):
            drive = "" if name in INPUTS else "DRIVESTRENGTH = 8; "
            adc += f"\nset_pin_assignment {{ {name} }} {{ LOCATION = {pin}; IOSTANDARD = LVCMOS33; {drive}PULLTYPE = PULLDOWN; }}\n"
    for path, before, after in zip(paths, original, (top, design, adc)):
        if before != after:
            path.write_text(after, encoding="utf-8")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--closed-loop", action="store_true", help="select encoder speed-control build; requires calibration before motion")
    group.add_argument("--jog-only", action="store_true", help="select bounded encoder commissioning build")
    parser.add_argument("--jog-limit-ms", type=int, choices=(1000,8000), default=None)
    args = parser.parse_args()
    main(Path(__file__).resolve().parents[2], True if args.closed_loop else False if args.jog_only else None, args.jog_limit_ms)
