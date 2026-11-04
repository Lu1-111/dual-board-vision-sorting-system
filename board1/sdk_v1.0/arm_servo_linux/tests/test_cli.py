"""Exercise the real nonblocking command loop with a simulated backend."""
import os
import re
import signal
import subprocess
import sys
import time

binary = os.path.abspath(sys.argv[1])
config = os.path.abspath(sys.argv[2])

def start():
    return subprocess.Popen([binary, "--simulate", "--config", config],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, universal_newlines=True)

def send(proc, commands):
    proc.stdin.write(commands)
    proc.stdin.flush()

proc = start()
send(proc, "status\nack 1000 1500 1500 1500 1500 1500 confirm\nenable\nangles 90 90 90 90 90 90 600\n")
time.sleep(0.15)
send(proc, "stop\nstatus\noff\nstatus\nquit\n")
out, err = proc.communicate(timeout=3)
assert proc.returncode == 0, (out, err)
assert not err, err
states = re.findall(r"mode=simulation enabled=(\d) moving=(\d) initial_ack=(\d)", out)
assert states == [("0", "0", "0"), ("1", "0", "1"), ("0", "0", "0")], out
pulses = re.findall(r"commanded_us: ([^\n]+)", out)
held = float(pulses[1].split()[0])
assert 1000 < held < 1490, out  # stop was processed during interpolation
assert float(pulses[2].split()[0]) == held, out
assert "Control disconnected; use disable explicitly to turn PWM off." in out

proc = start()
send(proc, "ack 1000 1500 1500 1500 1500 1500 confirm\nenable\nangles 90 90 90 90 90 90 3600000\n")
time.sleep(0.1)
proc.send_signal(signal.SIGINT)
out, err = proc.communicate(timeout=3)
assert proc.returncode == 0 and "Control disconnected; use disable explicitly to turn PWM off." in out, (out, err)

proc = start()
send(proc, "ack 1000 1500 1500 1500 1500 1500 confirm\nenable\nangles nan 90 90 90 90 90 1000\nstatus\nquit\n")
out, err = proc.communicate(timeout=3)
assert proc.returncode == 1 and "Invalid command" in err, (out, err)
assert "enabled=1 moving=0" in out, out

bad = subprocess.run([binary, "--hardware", "--config", config,
                      "--bank2", "/dev/null", "--bank3", "/dev/zero"],
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                     universal_newlines=True, timeout=3)
assert bad.returncode == 1 and "GPIO" in bad.stderr, (bad.stdout, bad.stderr)
proc = start()
send(proc, "xy-plane 2.89 0\npreview-xy 37.03 0\nack 1000 1500 1500 1500 1500 1500 confirm\nenable\nxy 37.03 0 20\n")
time.sleep(0.15)
send(proc, "fk\nquit\n")
out, err = proc.communicate(timeout=3)
assert proc.returncode == 0 and "XY IK verified" in out and "OK xy\n" in out, (out, err)
assert "FK_cm=37.03,0,2.89" in out, out
for commands, expected in [
    ("xy 15 0\n", "configure xy-plane"),
    ("xy-plane nan 0\n", "Invalid command"),
    ("xy-plane 2.89 -90\npreview-xy 37.03 0\n", "XY IK rejected"),
    ("xy-plane 2.89 0\nxy 37.03 0 19\n", "Invalid command"),
]:
    proc = start()
    send(proc, commands+"quit\n")
    out, err = proc.communicate(timeout=3)
    assert proc.returncode == 1 and expected in err, (out, err)
print("CLI stop/signal/invalid input/GPIO/XY: PASS")
