"""Receive photos from the existing board camera UI; no desktop viewer is installed."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import time
import uuid
from datetime import datetime, timedelta, timezone

import serial


class CameraLink:
    def __init__(self, port):
        self.serial = serial.Serial(port, 115200, timeout=0.1, write_timeout=3)
        self.buffer = bytearray()
        self.started = False

    def line(self, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if b"\n" in self.buffer:
                line, _, tail = self.buffer.partition(b"\n")
                self.buffer = bytearray(tail)
                marker = line.find(b"@CAMERA@")
                if marker >= 0:
                    try:
                        value = json.loads(line[marker + 8:])
                        if isinstance(value, dict):
                            return value
                    except (ValueError, UnicodeError):
                        pass
            else:
                self.buffer.extend(self.serial.read(max(1, min(self.serial.in_waiting, 8192))))
                if len(self.buffer) > 16384:
                    raise RuntimeError("串口数据超出上限")
        raise TimeoutError("相机串口响应超时；照片仍留在板端内存，可重新接收")

    def start(self):
        # LF works both at the login shell and in the raw-mode stdio adapter.
        # A crashed receiver may leave that adapter awaiting its next full line.
        self.serial.write(b"\n")
        time.sleep(0.5)
        observed = self.serial.read(self.serial.in_waiting)
        if b"@CAMERA@" in observed:
            self.started = True
            self.rpc("status")
            return
        if not re.search(rb"(?m)[#$]\s*$", observed):
            raise RuntimeError("请先在第一块板串口登录，再关闭终端；未向未知前台程序发送命令")
        self.serial.write(b"/home/uisrc/camera_dataset_app/serial_bridge.sh\r")
        ready = self.line()
        if not ready.get("ready") or ready.get("protocol") != 1:
            raise RuntimeError("相机传输接口未就绪")
        self.started = True
        self.rpc("status")

    def rpc(self, action, **fields):
        ident = uuid.uuid4().hex
        request = dict(fields, action=action, rpc_id=ident)
        self.serial.write((json.dumps(request, separators=(",", ":")) + "\n").encode("ascii"))
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            reply = self.line(max(0.1, deadline - time.monotonic()))
            if reply.get("rpc_id") == ident:
                if not reply.get("ok"):
                    raise RuntimeError("板端回应：" + str(reply.get("error", "请求失败")))
                return reply
        raise TimeoutError("串口请求超时")

    def download(self, photo, should_stop=None):
        size = photo.get("size")
        ident, sha = photo.get("photo_id", ""), photo.get("sha256", "")
        if type(size) is not int or not 0 < size <= 8 * 1024 * 1024:
            raise ValueError("无效照片大小")
        if not re.fullmatch(r"[0-9a-f-]{36}", ident) or str(uuid.UUID(ident)) != ident:
            raise ValueError("无效照片编号")
        if not re.fullmatch(r"[0-9a-f]{64}", sha):
            raise ValueError("无效照片校验值")
        if photo.get("format") != "jpg" or photo.get("source") not in ("imx415", "simulation"):
            raise ValueError("无效照片来源或格式")
        data = bytearray()
        last_report = time.monotonic()
        while len(data) < size:
            if should_stop is not None and should_stop():
                raise KeyboardInterrupt("采集已暂停；未接收完的照片仍在板端内存")
            reply = self.rpc("image_chunk", photo_id=ident, offset=len(data))
            chunk = base64.b64decode(reply.get("data", ""), validate=True)
            if (reply.get("offset") != len(data) or reply.get("size") != size
                    or reply.get("sha256") != sha or not 0 < len(chunk) <= 1536
                    or len(data) + len(chunk) > size):
                raise ValueError("照片分段校验失败，未确认保存")
            data.extend(chunk)
            if time.monotonic() - last_report >= 10:
                print(f"接收中：{len(data) * 100 // size}%", flush=True)
                last_report = time.monotonic()
        if (hashlib.sha256(data).hexdigest() != sha
                or not data.startswith(b"\xff\xd8") or not data.endswith(b"\xff\xd9")):
            raise ValueError("照片完整性校验失败，未确认保存")
        return bytes(data)

    def close(self):
        try:
            if self.started and self.serial.is_open:
                self.serial.write(b"QUIT\n")
                self.serial.flush()
                time.sleep(0.2)
        finally:
            self.serial.close()


def atomic_write(path, data):
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".part")
    try:
        with temporary.open("xb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        if path.exists():
            raise FileExistsError(path)
        temporary.rename(path)
    finally:
        temporary.unlink(missing_ok=True)


def save_photo(directory, photo, data):
    directory.mkdir(parents=True, exist_ok=True)
    ident = photo["photo_id"]
    matches = list(directory.glob("*_" + ident + ".jpg"))
    if matches:
        if len(matches) != 1 or hashlib.sha256(matches[0].read_bytes()).hexdigest() != photo["sha256"]:
            raise RuntimeError("电脑上已有同名但内容不符的照片，未覆盖")
        target = matches[0]
    else:
        stamp = datetime.now(timezone(timedelta(hours=8))).strftime("%Y%m%d_%H%M%S")
        prefix = "SIM_" if photo["source"] == "simulation" else ""
        target = directory / f"{prefix}{stamp}_{ident}.jpg"
        atomic_write(target, data)
    metadata = target.with_suffix(".json")
    record = dict(photo, image=target.name, received_utc=datetime.now(timezone.utc).isoformat(),
                  board_photo_storage="RAM only; released after acknowledgement")
    if not metadata.exists():
        atomic_write(metadata, json.dumps(record, ensure_ascii=False, indent=2).encode("utf-8"))
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="第一块板的 CH342-A 串口，不自动猜测")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[2] / "camera_photos")
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--capture", action="store_true", help="相机就绪后请求拍一张")
    args = parser.parse_args()
    link = CameraLink(args.port)
    requested = False
    try:
        link.start()
        print(f"已连接；照片只保存到电脑：{args.output}", flush=True)
        while True:
            status = link.rpc("status")
            if status.get("pending"):
                photo = status["photo"]
                print(f"接收真实照片：{photo['size']} 字节" if photo["source"] == "imx415"
                      else f"接收明确标记的模拟照片：{photo['size']} 字节", flush=True)
                data = link.download(photo)
                path = save_photo(args.output, photo, data)
                link.rpc("ack", photo_id=photo["photo_id"], sha256=photo["sha256"])
                print(f"PHOTO_RECEIVED={path}\nSHA256={photo['sha256']}", flush=True)
                if args.once:
                    return
            elif args.capture and not requested and status.get("camera_ready") and not status.get("busy"):
                link.rpc("capture")
                requested = True
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("接收已停止；未完成的照片仍在板端内存。", flush=True)
    finally:
        link.close()


if __name__ == "__main__":
    main()
