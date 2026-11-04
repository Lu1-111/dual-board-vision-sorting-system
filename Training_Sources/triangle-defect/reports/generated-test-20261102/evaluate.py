"""Reproduce this fixed synthetic test batch without training or threshold fitting."""
from pathlib import Path
from datetime import datetime, timezone
from zoneinfo import ZoneInfo
import argparse
import csv
import hashlib
import json
import sys
import time

import numpy as np
from PIL import Image
import torch

BATCH = Path(__file__).resolve().parent
ROOT = BATCH.parents[1]
sys.path.insert(0, str(ROOT / "src"))
from triangle_defect.predict import Predictor


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def metrics(rows, prediction_key):
    included = [r for r in rows if r["in_scope"]]
    cm = [[sum(r["actual_label"] == y and r[prediction_key]["class_id"] == p
               for r in included) for p in [0, 1]] for y in [0, 1]]
    tn, fp = cm[0]
    fn, tp = cm[1]
    return {"count": len(included), "confusion": cm,
            "accuracy": (tn + tp) / len(included), "correct": tn + tp,
            "false_positives": fp, "false_negatives": fn,
            "defect_recall": tp / (tp + fn), "normal_specificity": tn / (tn + fp),
            "precision": tp / (tp + fp) if tp + fp else None,
            "errors": [r["id"] for r in included
                       if r["actual_label"] != r[prediction_key]["class_id"]]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=BATCH / "results.json")
    args = parser.parse_args()
    plan = json.loads((BATCH / "plan.json").read_text())
    review = json.loads((BATCH / "review.json").read_text())
    freeze = plan["frozen_model"]
    before = {"weights": sha(ROOT / "models/v28/weights.pt"),
              "config": sha(ROOT / "models/v28/config.json"),
              "torchscript": sha(ROOT / "models/v28/model_float32.torchscript.pt"),
              "training_manifest": sha(ROOT / "data/manifest.json"),
              "review": sha(BATCH / "review.json"), "plan": sha(BATCH / "plan.json")}
    assert before["weights"] == freeze["weights_sha256"]
    assert before["config"] == freeze["config_sha256"]
    assert before["torchscript"] == freeze["torchscript_sha256"]
    assert before["training_manifest"] == plan["train_manifest_sha256"]
    assert before["plan"] == review["plan_sha256"]
    started_at = datetime.now(timezone.utc)
    assert datetime.fromisoformat(review["locked_at"]) < started_at
    predictor = Predictor(device="cpu", threads=4)
    scripted = torch.jit.load(str(ROOT / "models/v28/model_float32.torchscript.pt")).eval()
    rows, tensors, timings, parity = [], [], [], []
    with torch.inference_mode():
        predictor.infer_tensor(torch.zeros(1, 1, 320, 320))
        for sample in review["records"]:
            path = BATCH / sample["image"]
            assert sha(path) == sample["image_sha256"]
            started = time.perf_counter()
            x = predictor.preprocess(path)
            prediction = predictor.infer_tensor(x)
            timings.append((time.perf_counter() - started) * 1000)
            tensors.append(x)
            scripted_score = float(scripted(x)[0])
            parity.append(abs(scripted_score - prediction["score"]))
            with Image.open(path) as image:
                gray = image.convert("L").resize((224, 224), Image.Resampling.BILINEAR)
                gray = gray.resize((320, 320), Image.Resampling.BILINEAR)
                array = np.asarray(gray, dtype=np.float32) / 255.0
            secondary = predictor.infer_tensor(torch.from_numpy(array)[None, None])
            row = dict(sample, prediction=prediction, resolution_probe=secondary,
                       correct=sample["actual_label"] == prediction["class_id"],
                       primary_e2e_ms=timings[-1])
            rows.append(row)
            print(f"{row['id']} expected={row['actual_label']} predicted={prediction['class_id']} "
                  f"score={prediction['score']:.6f} probe224={secondary['score']:.6f}", flush=True)
        # Warmed batch-one forward timings; image decoding and resize excluded.
        for _ in range(10):
            predictor.infer_tensor(tensors[0])
        forwards = []
        for i in range(100):
            started = time.perf_counter()
            predictor.infer_tensor(tensors[i % len(tensors)])
            forwards.append((time.perf_counter() - started) * 1000)
    assert before["weights"] == sha(ROOT / "models/v28/weights.pt")
    assert before["config"] == sha(ROOT / "models/v28/config.json")
    assert before["training_manifest"] == sha(ROOT / "data/manifest.json")
    result = {"started_at": started_at.isoformat(),
              "local_time": started_at.astimezone(ZoneInfo("Asia/Shanghai")).isoformat(),
              "model_version": "V28", "threshold": predictor.config["threshold"],
              "review_locked_before_inference": True, "hashes": before,
              "model_and_training_data_unchanged": True,
              "primary_preprocessing": plan["primary_preprocessing"],
              "primary_metrics": metrics(rows, "prediction"),
              "resolution_probe_preprocessing": plan["secondary_probe"],
              "resolution_probe_metrics": metrics(rows, "resolution_probe"),
              "torchscript_max_score_difference": max(parity),
              "latency": {"device": "CPU", "threads": 4,
                          "forward_median_ms": float(np.median(forwards)),
                          "forward_p95_ms": float(np.percentile(forwards, 95)),
                          "forward_repeats": 100,
                          "high_resolution_png_e2e_median_ms": float(np.median(timings)),
                          "e2e_count": len(timings),
                          "scope": "Forward excludes preprocessing; E2E includes opening generated1254x1254 PNG, grayscale, bilinear320 and inference. No FPGA measurement."},
              "scope": plan["scope"], "samples": rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2))
    csv_path = args.output.with_suffix(".csv")
    with csv_path.open("w", newline="", encoding="utf-8-sig") as stream:
        columns = ["id", "scenario", "actual_label", "prediction", "score", "threshold",
                   "correct", "resolution224_prediction", "resolution224_score", "image"]
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        for row in rows:
            writer.writerow(dict(id=row["id"], scenario=row["scenario"], actual_label=row["actual_label"],
                                 prediction=row["prediction"]["class_id"], score=row["prediction"]["score"],
                                 threshold=predictor.config["threshold"], correct=row["correct"],
                                 resolution224_prediction=row["resolution_probe"]["class_id"],
                                 resolution224_score=row["resolution_probe"]["score"], image=row["image"]))
    print(json.dumps({key: value for key, value in result.items() if key != "samples"},
                     ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
