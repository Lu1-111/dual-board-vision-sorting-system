"""Evaluate the fixed V28 release without changing weights or threshold."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import torch

from .paths import ROOT, MODEL, load_manifest, project_path
from .predict import Predictor


def evaluate(split="test", device="cpu", compare_release=False):
    rows = [row for row in load_manifest()["records"] if row["split"] == split]
    predictor = Predictor(device=device)
    scripted = torch.jit.load(str(MODEL / "model_float32.torchscript.pt"),
                              map_location=device).eval() if compare_release else None
    reference = json.loads((ROOT / "reports/evidence/test_results.json").read_text())
    previous = {r["id"]: r["prediction"] for r in reference["samples"]}
    confusion = [[0, 0], [0, 0]]
    samples = []
    differences = []
    script_differences = []
    decisions_agree = True
    with torch.inference_mode():
        for row in rows:
            x = predictor.preprocess(project_path(row["path"]))
            prediction = predictor.infer_tensor(x)
            confusion[row["label"]][prediction["class_id"]] += 1
            samples.append(dict(id=row["id"], path=row["path"], label=row["label"],
                                prediction=prediction,
                                correct=row["label"] == prediction["class_id"]))
            if compare_release:
                old = previous[row["id"]]
                score = float(scripted(x)[0])
                differences.append(abs(old["score"] - prediction["score"]))
                script_differences.append(abs(score - prediction["score"]))
                decisions_agree &= (int(score >= predictor.config["threshold"])
                                    == prediction["class_id"] == old["class_id"])
    tn, fp = confusion[0]
    fn, tp = confusion[1]
    result = {"evaluated_at": datetime.now(timezone.utc).isoformat(), "split": split,
              "device": device, "threshold": predictor.config["threshold"],
              "metrics": {"count": len(rows), "confusion": confusion,
                          "accuracy": (tn + tp) / len(rows),
                          "defect_recall": tp / (tp + fn),
                          "normal_specificity": tn / (tn + fp)},
              "errors": [r["id"] for r in samples if not r["correct"]],
              "scope": "Internal grouped regression; not independent field acceptance.",
              "samples": samples}
    if compare_release:
        result["release_comparison"] = {"all_decisions_agree": decisions_agree,
                                        "max_score_difference": max(differences),
                                        "max_torchscript_difference": max(script_differences)}
        if not decisions_agree:
            raise RuntimeError("Current predictions differ from the frozen release")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--split", choices=["val", "test"], default="test")
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--compare-release", action="store_true")
    parser.add_argument("--output", type=Path, default=ROOT / "runs/evaluation.json")
    args = parser.parse_args()
    if args.compare_release and args.split != "test":
        parser.error("--compare-release requires --split test")
    result = evaluate(args.split, args.device, args.compare_release)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "samples"}, ensure_ascii=False, indent=2))
    print(f"结果保存至: {args.output}")
