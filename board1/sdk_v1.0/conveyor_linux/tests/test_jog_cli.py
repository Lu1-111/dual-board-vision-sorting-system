"""Check command limits, simulated start/stop and rejection before device access."""
import subprocess
import sys
import time

exe = sys.argv[1]
for args in [[], ["--uio", "/dev/uio999", "--jog", "101", "500", "forward"],
             ["--uio", "/dev/uio999", "--jog", "10", "901", "forward"],
             ["--uio", "/dev/uio999", "--jog", "nan", "500", "forward"],
             ["--uio", "/dev/uio999", "--jog", "10", "500", "unknown"],
             ["--simulate", "test", "--jog", "0", "500", "forward"],
             ["--simulate", "test", "--jog", "10", "0", "forward"],
             ["--simulate", "test", "--stop", "extra"]]:
    result = subprocess.run([exe, *args], capture_output=True, text=True, timeout=3)
    assert result.returncode != 0, args
    assert "HARDWARE_JOG" not in result.stdout and "ID=" not in result.stdout, result
for percent, direction in ((1, "forward"), (50, "forward"), (75, "reverse"), (100, "forward"), (100, "reverse")):
    start = time.monotonic()
    result = subprocess.run([exe, "--simulate", "test", "--jog", str(percent), "100", direction],
                            capture_output=True, text=True, timeout=3)
    assert result.returncode == 0, result
    assert "STOP_CONFIRMED" in result.stdout and "status=0x0 duty_ticks=0" in result.stdout, result
    assert 0.09 <= time.monotonic() - start < 2, result
for action in ("--status", "--stop", "--clear-fault"):
    result = subprocess.run([exe, "--simulate", "test", action], capture_output=True, text=True, timeout=3)
    assert result.returncode == 0 and "JOG" not in result.stdout, result
result = subprocess.run([exe, "--simulate", "test", "--jog", "20", "900", "forward"],
                        capture_output=True, text=True, timeout=3)
assert result.returncode == 0 and "STOP_CONFIRMED" in result.stdout, result
print("PASS jog CLI: invalid inputs rejected before I/O, bounded forward/reverse and stop readback")
