"""Checks the real CLI loop continues ticking even with unfinished stdin input."""
import queue
import subprocess
import sys
import threading
import time


def main():
    process = subprocess.Popen(
        [sys.argv[1], "--simulate", "--config", "conveyor.example.ini"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        universal_newlines=True, bufsize=1,
    )
    lines = queue.Queue()
    transcript = []

    def reader():
        for line in process.stdout:
            lines.put(line.rstrip())

    threading.Thread(target=reader, daemon=True).start()

    def expect(fragment, timeout=3):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                line = lines.get(timeout=max(0.01, deadline-time.monotonic()))
            except queue.Empty:
                break
            transcript.append(line)
            if fragment in line:
                return line
        raise AssertionError(f"missing {fragment!r}: {transcript}")

    def send(text):
        process.stdin.write(text)
        process.stdin.flush()

    try:
        expect("SIMULATION; idle")
        send("arm\n")
        expect("state=waiting")
        send("xy 7 1 0 50 0\n")
        expect("state=tracking")
        send("sta")
        expect("visual feedback timed out")  # 400 ms, while line is incomplete
        send("tus\nreset\n")
        expect("state=idle")
        send("xy 7 NaN 0 50 0\n")
        expect("ERROR: expected unsigned integer")
        send("quit\n")
        process.stdin.close()
        assert process.wait(timeout=3) == 0
        # EOF exits normally and does not leave the polling loop alive.
        eof = subprocess.run([sys.argv[1], "--simulate"], input="", universal_newlines=True,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=3)
        assert eof.returncode == 0, eof.stderr
        speed = subprocess.run([sys.argv[1], "--simulate", "--config", "conveyor.cm.simulation.ini",
                                "--speed-test-ms", "2000"], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, universal_newlines=True, timeout=6)
        assert speed.returncode == 0, speed.stdout + speed.stderr
        assert "SPEED_TEST_STOP enabled=0 fault=0 simulated=1" in speed.stdout
        samples = [line.split(',') for line in speed.stdout.splitlines() if line[:1].isdigit()]
        assert len(samples) > 20, speed.stdout
        target, measured = float(samples[-1][1]), float(samples[-1][2])
        assert abs(target-measured) < target*0.3, (target, measured)
        jog = subprocess.run([sys.argv[1], "--simulate", "--encoder-jog", "50", "100"],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             universal_newlines=True, timeout=3)
        assert jog.returncode != 0 and "ENCODER_JOG_STOP enabled=0 fault=0" in jog.stdout
        assert "no encoder movement" in jog.stderr
        for args in (["--speed-test-ms", "0"], ["--speed-test-ms", "5001"],
                     ["--speed-test-ms", "200", "--encoder-monitor"],
                     ["--speed-test-ms", "200"],
                     ["--speed-test-ms", "200", "--config", "conveyor.hardware.template.ini"],
                     ["--encoder-jog", "76", "100"], ["--encoder-jog", "50", "501"],
                     ["--encoder-jog", "50", "100", "--encoder-monitor"]):
            result = subprocess.run([sys.argv[1]]+args, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, timeout=3)
            assert result.returncode != 0, args
        print("PASS CLI: partial line, camera timeout, reset, malformed input, quit, EOF")
        print("PASS speed test: synthetic tracking, bounded stop, invalid/unmeasured modes rejected")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=3)


if __name__ == "__main__":
    main()
