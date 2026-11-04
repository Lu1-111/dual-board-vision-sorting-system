"""Locate blue workpieces in original RGB frames and classify their crops with V28."""
import argparse
import csv
from datetime import datetime
import hashlib
import json
from pathlib import Path
import time

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

from .paths import ROOT
from .predict import Predictor

LOCATOR_CONFIG = ROOT / "config/blue_locator.json"
EXTENSIONS = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff"}
COORDINATES = "Original decoded image pixels; x2/y2 exclusive; center is bounding-box center."


class BlueLocator:
    """Deterministic scene-specific locator. It does not read annotations or labels."""

    def __init__(self, config=LOCATOR_CONFIG):
        self.config_path = Path(config)
        self.config = json.loads(self.config_path.read_text(encoding="utf-8"))
        self.config_sha256 = hashlib.sha256(self.config_path.read_bytes()).hexdigest()

    def locate(self, image):
        rgb = np.asarray(image.convert("RGB"))
        height, width = rgb.shape[:2]
        hsv = cv2.cvtColor(rgb, cv2.COLOR_RGB2HSV)
        cfg = self.config
        mask = cv2.inRange(hsv, np.array(cfg["hsv_lower"], np.uint8),
                           np.array(cfg["hsv_upper"], np.uint8))
        kernel = np.ones((cfg["close_kernel"], cfg["close_kernel"]), np.uint8)
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        objects = []
        for contour in contours:
            area = cv2.contourArea(contour)
            x, y, w, h = cv2.boundingRect(contour)
            if area < max(cfg["min_area_pixels"], width * height * cfg["min_area_fraction"]):
                continue
            if min(w, h) < cfg["min_side_pixels"]:
                continue
            solidity = area / max(cv2.contourArea(cv2.convexHull(contour)), 1)
            extent = area / (w * h)
            aspect_min, aspect_max = cfg["aspect_ratio_range"]
            if not (aspect_min <= w / h <= aspect_max and
                    solidity >= cfg["min_solidity"] and extent >= cfg["min_extent"]):
                continue
            padding = round(max(w, h) * cfg["crop_padding_fraction"])
            crop = [max(0, x - padding), max(0, y - padding),
                    min(width, x + w + padding), min(height, y + h + padding)]
            objects.append({"bbox_xyxy": [x, y, x + w, y + h],
                            "center_xy": [x + w / 2, y + h / 2],
                            "crop_bbox_xyxy": crop,
                            "partial_at_frame_border": x <= 1 or y <= 1 or
                                x + w >= width - 1 or y + h >= height - 1,
                            "localization": {"method": cfg["method"],
                                             "area_px": area, "solidity": solidity,
                                             "extent": extent}})
        objects.sort(key=lambda obj: (obj["center_xy"][0], obj["center_xy"][1]))
        for index, obj in enumerate(objects, 1):
            obj["object_id"] = index
        return objects


class FrameInspector:
    def __init__(self, device="cpu", threads=4, locator_config=LOCATOR_CONFIG):
        self.locator = BlueLocator(locator_config)
        self.predictor = Predictor(device=device, threads=threads)

    def inspect_image(self, image, path=""):
        started = time.perf_counter()
        rgb = image.convert("RGB")
        objects = self.locator.locate(rgb)
        for obj in objects:
            if obj["partial_at_frame_border"]:
                obj.update(class_id=None, label="needs_review", score=None,
                           threshold=self.predictor.config["threshold"], peak_xy_original=None,
                           warning="物块接触图像边缘，可能不完整；未进行缺角判定。")
                continue
            x1, y1, x2, y2 = obj["crop_bbox_xyxy"]
            crop = rgb.crop((x1, y1, x2, y2)).convert("L")
            reference_size = self.locator.config["crop_reference_size"]
            crop = crop.resize((reference_size, reference_size), Image.Resampling.BILINEAR)
            prediction = self.predictor.predict_image(crop)
            obj.update(prediction)
            px, py = prediction["peak_xy_normalized"]
            obj["peak_xy_original"] = [x1 + px * (x2 - x1), y1 + py * (y2 - y1)]
            obj["warning"] = ""
        return {"path": str(path), "mode": "full_frame", "image_size": list(rgb.size),
                "coordinate_system": COORDINATES, "locator_version": self.locator.config["version"],
                "locator_config_sha256": self.locator.config_sha256,
                "defect_model": "V28", "threshold": self.predictor.config["threshold"],
                "status": "ok" if objects else "no_blue_object", "objects": objects,
                "elapsed_ms": (time.perf_counter() - started) * 1000}

    def inspect(self, path):
        with Image.open(path) as image:
            return self.inspect_image(image, path)

    def inspect_crop(self, path):
        """Legacy crop mode: no fabricated object coordinates for a supplied crop."""
        with Image.open(path) as image:
            size = image.size
            prediction = self.predictor.predict_image(image)
        px, py = prediction["peak_xy_normalized"]
        obj = dict(prediction, object_id=1, bbox_xyxy=None, center_xy=None,
                   crop_bbox_xyxy=[0, 0, *size], partial_at_frame_border=False,
                   peak_xy_original=[px * size[0], py * size[1]], warning="")
        return {"path": str(path), "mode": "crop", "image_size": list(size),
                "coordinate_system": "No object coordinates in crop-only mode.",
                "defect_model": "V28", "threshold": prediction["threshold"],
                "status": "ok", "objects": [obj]}


def annotate(image, result, selected_id=None):
    """Draw boxes and centers in original coordinates; preserve the source file."""
    canvas = image.convert("RGB").copy()
    draw = ImageDraw.Draw(canvas)
    width, height = canvas.size
    font_size = max(12, round(max(width, height) / 80))
    font = ImageFont.load_default(size=font_size)
    line_width = max(2, round(max(width, height) / 500))
    for obj in result["objects"]:
        color = "#ed493d" if obj["class_id"] == 1 else "#1bc48b" if obj["class_id"] == 0 else "#ffbb33"
        bbox = obj["bbox_xyxy"]
        status = "DEFECT" if obj["class_id"] == 1 else "NORMAL" if obj["class_id"] == 0 else "REVIEW"
        score = f" {obj['score']:.3f}" if obj["score"] is not None else ""
        if bbox is not None:
            x1, y1, x2, y2 = bbox
            thickness = line_width + (2 if obj["object_id"] == selected_id else 0)
            draw.rectangle((x1, y1, x2 - 1, y2 - 1), outline=color, width=thickness)
            cx, cy = obj["center_xy"]
            radius = max(4, font_size // 3)
            draw.line((cx - radius, cy, cx + radius, cy), fill=color, width=line_width)
            draw.line((cx, cy - radius, cx, cy + radius), fill=color, width=line_width)
            lines = [f"#{obj['object_id']} {status}{score}",
                     f"box ({x1},{y1})-({x2},{y2}) center ({cx:.1f},{cy:.1f})"]
            label = "\n".join(lines)
            box = draw.multiline_textbbox((0, 0), label, font=font, spacing=3)
            label_w, label_h = box[2] - box[0] + 10, box[3] - box[1] + 10
            tx = max(0, min(x1, width - label_w))
            ty = max(0, y1 - label_h - 4)
            draw.rectangle((tx, ty, tx + label_w, ty + label_h), fill="#10232d")
            draw.multiline_text((tx + 5, ty + 3), label, font=font, fill=color, spacing=3)
        elif obj["class_id"] == 1:
            px, py = obj["peak_xy_original"]
            radius = max(5, round(min(canvas.size) * .035))
            draw.ellipse((px - radius, py - radius, px + radius, py + radius),
                         outline=color, width=line_width)
    if not result["objects"]:
        draw.rectangle((0, 0, min(width, font_size * 26), font_size * 2), fill="#10232d")
        draw.text((6, 4), "NO BLUE WORKPIECE FOUND", fill="#ffbb33", font=font)
    return canvas


CSV_COLUMNS = ["path", "mode", "frame_status", "object_id", "label", "score", "threshold",
               "x1", "y1", "x2", "y2", "center_x", "center_y", "warning"]


def flat_rows(results):
    for frame in results:
        for obj in frame["objects"] or [None]:
            row = dict.fromkeys(CSV_COLUMNS, "")
            row.update(path=frame["path"], mode=frame["mode"], frame_status=frame["status"])
            if obj is not None:
                row.update({k: obj[k] for k in ["object_id", "label", "score", "threshold", "warning"]})
                if obj["bbox_xyxy"] is not None:
                    row.update(zip(["x1", "y1", "x2", "y2"], obj["bbox_xyxy"]))
                    row.update(zip(["center_x", "center_y"], obj["center_xy"]))
            yield row


def export_results(results, path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.suffix.lower() == ".json":
        path.write_text(json.dumps({"coordinate_system": COORDINATES, "frames": results},
                                  ensure_ascii=False, indent=2), encoding="utf-8")
    else:
        with path.open("w", newline="", encoding="utf-8-sig") as stream:
            writer = csv.DictWriter(stream, fieldnames=CSV_COLUMNS, lineterminator="\n")
            writer.writeheader()
            writer.writerows(flat_rows(results))


def main():
    parser = argparse.ArgumentParser(description="原始彩色整图：定位蓝色物块、输出像素坐标并识别缺角")
    parser.add_argument("input", type=Path, help="原始彩色图片或图片文件夹")
    parser.add_argument("--output", type=Path, default=ROOT / "runs" / datetime.now().strftime("detect-%Y%m%d-%H%M%S"))
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--threads", type=int, default=4, help="PyTorch CPU 线程数")
    parser.add_argument("--config", type=Path, default=LOCATOR_CONFIG)
    args = parser.parse_args()
    if not args.input.exists():
        parser.error(f"找不到输入路径：{args.input}")
    paths = [args.input] if args.input.is_file() else sorted(
        p for p in args.input.iterdir() if p.is_file() and p.suffix.lower() in EXTENSIONS)
    if not paths:
        parser.error("输入文件夹中没有支持的图片。")
    if args.output.exists():
        parser.error("输出目录已存在，请换一个名称，避免覆盖之前的结果。")
    inspector = FrameInspector(device=args.device, threads=args.threads, locator_config=args.config)
    args.output.mkdir(parents=True)
    annotated = args.output / "annotated"
    annotated.mkdir()
    results, errors = [], []
    for number, path in enumerate(paths, 1):
        try:
            with Image.open(path) as image:
                frame = inspector.inspect_image(image, path.resolve())
                filename = f"{number:04d}_{path.stem}.png"
                annotate(image, frame).save(annotated / filename)
            frame["annotated_image"] = f"annotated/{filename}"
            results.append(frame)
        except (OSError, ValueError) as error:
            errors.append({"path": str(path), "error": str(error)})
    export_results(results, args.output / "results.json")
    export_results(results, args.output / "results.csv")
    (args.output / "errors.json").write_text(json.dumps(errors, ensure_ascii=False, indent=2))
    summary = {"images": len(results), "blue_objects": sum(len(r["objects"]) for r in results),
               "defects": sum(o["class_id"] == 1 for r in results for o in r["objects"]),
               "no_object_images": sum(not r["objects"] for r in results),
               "errors": len(errors), "output": str(args.output.resolve())}
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    if errors:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
