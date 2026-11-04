"""Hardware-free HTTP lifecycle/backpressure regression; launches its own fixture."""
import argparse
import http.client
import json
import os
import select
import socket
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("fixture")
    parser.add_argument("--port", type=int, default=47993)
    args = parser.parse_args()
    proc = subprocess.Popen([args.fixture, str(args.port)], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, universal_newlines=True, bufsize=1)
    evidence = {}

    def line():
        assert select.select([proc.stdout], [], [], 10)[0], "fixture output timeout"
        return proc.stdout.readline().strip()

    def command(cmd):
        proc.stdin.write(cmd + "\n")
        proc.stdin.flush()
        assert line() == "ACK " + cmd

    def get(path="/v1/frame.jpg", method="GET"):
        conn = http.client.HTTPConnection("127.0.0.1", args.port, timeout=5)
        try:
            conn.request(method, path)
            response = conn.getresponse()
            body = response.read()
            headers = dict(response.getheaders())
            assert int(headers["Content-Length"]) == len(body)
            assert headers["Connection"] == "close"
            assert headers["Cache-Control"] == "no-store"
            return response.status, headers, body
        finally:
            conn.close()

    def status():
        code, _, body = get("/v1/status.json")
        assert code == 200
        return json.loads(body)

    def check_frame(reply):
        code, headers, body = reply
        assert code == 200, (code, body[:200])
        assert headers["Content-Type"] == "image/jpeg"
        assert body[:2] == b"\xff\xd8" and body[-2:] == b"\xff\xd9"
        assert len(body) <= 8 * 1024 * 1024
        assert headers["X-Camera-Protocol"] == "1"
        assert headers["X-Camera-Source"] == "simulation"
        assert (headers["X-Frame-Width"], headers["X-Frame-Height"]) == ("1920", "1080")
        assert headers["X-Frame-Sequence"].isdigit()
        assert 0 <= int(headers["X-Frame-Age-Ms"]) <= 1500
        assert len(headers["X-Camera-Session"]) == 36
        return headers

    clients = []
    try:
        assert line() == "READY"
        time.sleep(.3)
        assert status()["encode_count"] == "0", "status alone must not encode"
        first = check_frame(get())
        sequence = int(first["X-Frame-Sequence"])
        time.sleep(.75)
        later = check_frame(get())
        assert int(later["X-Frame-Sequence"]) > sequence
        assert first["X-Camera-Session"] == later["X-Camera-Session"]
        state = status()
        assert state["pending_frames"] in (0, 1)
        assert int(state["replaced_count"]) > 0
        evidence["streaming"] = state
        time.sleep(1.7)
        idle_count = status()["encode_count"]
        time.sleep(.7)
        assert status()["encode_count"] == idle_count, "encoding should stop without demand"

        command("pause")
        time.sleep(1.6)
        code, _, body = get()
        assert code == 503 and json.loads(body)["error"] == "frame_stale"
        command("stop")
        code, _, body = get()
        assert code == 503 and json.loads(body)["error"] == "fixture_stopped"
        command("begin")
        fresh = check_frame(get())
        assert fresh["X-Camera-Session"] != first["X-Camera-Session"]
        evidence["sessions"] = [first["X-Camera-Session"], fresh["X-Camera-Session"]]
        assert get("/missing")[0] == 404
        assert get(method="POST")[0] == 405
        with socket.create_connection(("127.0.0.1", args.port), timeout=5) as sock:
            sock.sendall(b"GET /v1/frame.jpg HTTP/1.1\r\nX-Large: " + b"x" * 4100 + b"\r\n\r\n")
            assert sock.recv(100).startswith(b"HTTP/1.1 431")

        # Four incomplete headers exhaust the explicit client cap; an extra
        # connection is promptly closed rather than placed in an application queue.
        for _ in range(4):
            sock = socket.create_connection(("127.0.0.1", args.port), timeout=5)
            sock.sendall(b"GET /")
            clients.append(sock)
        time.sleep(.1)
        with socket.create_connection(("127.0.0.1", args.port), timeout=5) as fifth:
            assert fifth.recv(1) == b""
        time.sleep(2.2)
        for sock in clients:
            assert sock.recv(1) == b""
            sock.close()
        clients.clear()
        assert status()["connected_clients"] == 1  # status request itself

        # A client that stops reading a noisy full-size JPEG releases its slot
        # by the absolute send timeout. Other callers remain responsive.
        slow = socket.socket()
        slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
        slow.settimeout(5)
        slow.connect(("127.0.0.1", args.port))
        slow.sendall(b"GET /v1/frame.jpg HTTP/1.1\r\nHost: test\r\n\r\n")
        clients.append(slow)
        time.sleep(.7)
        assert status()["connected_clients"] == 2
        check_frame(get())
        time.sleep(3.3)
        assert status()["connected_clients"] == 1
        evidence["final"] = status()
        command("quit")
        assert proc.wait(timeout=10) == 0
        print("LIVE_STREAM_PASS " + json.dumps(evidence, sort_keys=True))
    finally:
        for sock in clients:
            sock.close()
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=10)


if __name__ == "__main__":
    main()
