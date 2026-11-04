"""Create reproducible V28 board vectors without touching the deployed model files.

Run with the project's export environment (torch, cv2, onnx, onnxruntime, Pillow).
All outputs are diagnostic artifacts under reports/board_vectors/.
"""
from __future__ import annotations

import copy
import gzip
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
from onnx import TensorProto, helper, numpy_helper
from PIL import Image

from common import ROOT, SOURCE, REPORTS, gray, normalized, score, session, sha, write_json

sys.path.insert(0, str(SOURCE / "src"))
from triangle_defect.detect import BlueLocator


OUTPUT = REPORTS / "board_vectors"


def gzip_tensor(name: str, tensor: np.ndarray) -> dict:
    """Write little-endian, C-contiguous NCHW bytes, with a reproducible gzip header."""
    if tensor.dtype == np.float32:
        tensor = np.asarray(tensor, dtype="<f4", order="C")
    else:
        tensor = np.ascontiguousarray(tensor)
    raw = tensor.tobytes(order="C")
    path = OUTPUT / (name + ".bin.gz")
    path.write_bytes(gzip.compress(raw, compresslevel=9, mtime=0))
    assert gzip.decompress(path.read_bytes()) == raw
    return {
        "file": path.name,
        "dtype": tensor.dtype.str,
        "shape": list(tensor.shape),
        "layout": "NCHW C-contiguous; float32 is little-endian",
        "raw_bytes": len(raw),
        "raw_sha256": hashlib.sha256(raw).hexdigest(),
        "gzip_bytes": path.stat().st_size,
        "gzip_sha256": sha(path),
    }


def raw_head_session(head: Path, contract: dict) -> tuple[ort.InferenceSession, str, str]:
    """Expose the existing UINT8 QDQ boundary directly; do not requantize weights."""
    model = copy.deepcopy(onnx.load(str(head)))
    boundary = contract["bridge"]["name"]
    input_q = next(n for n in model.graph.node if n.op_type == "QuantizeLinear" and n.input[0] == boundary)
    output_dq = next(n for n in model.graph.node if n.op_type == "DequantizeLinear" and n.output[0] == contract["output"]["name"])
    initializers = {i.name: numpy_helper.to_array(i) for i in model.graph.initializer}
    assert float(initializers[input_q.input[1]]) == contract["bridge"]["scale"]
    assert int(initializers[input_q.input[2]]) == contract["bridge"]["zero_point"]
    assert float(initializers[output_dq.input[1]]) == contract["output"]["scale"]
    assert int(initializers[output_dq.input[2]]) == contract["output"]["zero_point"]
    raw_output = output_dq.input[0]
    quantized_input = input_q.output[0]
    nodes = []
    for node in model.graph.node:
        if node.name in {input_q.name, output_dq.name}:
            continue
        for index, value in enumerate(node.input):
            if value == quantized_input:
                node.input[index] = boundary
        nodes.append(node)
    del model.graph.node[:]
    model.graph.node.extend(nodes)
    model.graph.input[0].type.tensor_type.elem_type = TensorProto.UINT8
    del model.graph.output[:]
    model.graph.output.extend([helper.make_tensor_value_info(raw_output, TensorProto.UINT8, contract["output"]["shape"])])
    onnx.checker.check_model(model)
    options = ort.SessionOptions()
    options.intra_op_num_threads = 4
    options.inter_op_num_threads = 1
    return ort.InferenceSession(model.SerializeToString(), options, providers=["CPUExecutionProvider"]), boundary, raw_output


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    package = ROOT / "package"
    contract_path = package / "deployment_contract.json"
    contract = json.loads(contract_path.read_text(encoding="utf-8"))
    feature_path = package / "models/triangle_v28_features_fp32.onnx"
    head_path = package / "models/triangle_v28_head_uint8.onnx"
    for record in contract["models"]:
        path = package / "models" / record["file"]
        assert sha(path) == record["sha256"], path
    feature_session = session(feature_path)
    head_session = session(head_path)
    mixed_session = session(ROOT / "artifacts/float_features.onnx")
    raw_session, raw_input, raw_output = raw_head_session(head_path, contract)
    previous = json.loads((REPORTS / "fullframe_quant_regression.json").read_text(encoding="utf-8"))
    previous = {row["image"]: row for row in previous["rows"]}
    locator = BlueLocator()
    input_scale = np.float32(contract["bridge"]["scale"])
    input_zero = contract["bridge"]["zero_point"]
    output_scale = np.float32(contract["output"]["scale"])
    output_zero = contract["output"]["zero_point"]
    records = []
    for stem, expected_label in [("blue_003", "NORMAL"), ("blue_051", "DEFECT")]:
        image_path = package / "samples" / (stem + ".jpg")
        # Verify the package sample is the same original used by regression.
        assert sha(image_path) == sha(SOURCE / "data/originals" / image_path.name)
        with Image.open(image_path) as source_image:
            image = source_image.convert("RGB")
        objects = locator.locate(image)
        assert len(objects) == 1 and not objects[0]["partial_at_frame_border"]
        obj = objects[0]
        reference = previous[image_path.name]
        assert obj["bbox_xyxy"] == reference["bbox"]
        assert obj["crop_bbox_xyxy"] == reference["crop"]
        crop = image.crop(obj["crop_bbox_xyxy"]).convert("L")
        crop224 = crop.resize((224, 224), Image.Resampling.BILINEAR)
        x = normalized(gray(crop224))
        features = feature_session.run(None, {"input": x})[0]
        head_input_u8 = np.clip(np.rint(features / input_scale) + input_zero, 0, 255).astype(np.uint8)
        bridge_f32 = (head_input_u8.astype(np.float32) - input_zero) * input_scale
        split_logits = head_session.run(None, {raw_input: bridge_f32})[0]
        direct_logits = head_session.run(None, {raw_input: features})[0]
        mixed_logits = mixed_session.run(None, {"input": x})[0]
        head_output_u8 = raw_session.run([raw_output], {raw_input: head_input_u8})[0]
        raw_dequant = (head_output_u8.astype(np.float32) - output_zero) * output_scale
        assert np.array_equal(raw_dequant, split_logits)
        assert np.array_equal(direct_logits, split_logits)
        assert np.array_equal(mixed_logits, split_logits)
        obtained_score = score(split_logits)
        assert abs(obtained_score - reference["quant_score"]) < 1e-12
        label = "DEFECT" if obtained_score >= contract["threshold"] else "NORMAL"
        assert label == expected_label
        flat = raw_dequant.reshape(-1)
        top_indices = np.argsort(-flat, kind="stable")[:4]
        top_values = flat[top_indices]
        mean_f32 = np.float32(np.float32(np.float32(top_values[0] + top_values[1]) + top_values[2]) + top_values[3]) * np.float32(.25)
        score_f32 = np.float32(1) / (np.float32(1) + np.exp(-mean_f32))
        record = {
            "image": image_path.name,
            "image_sha256": sha(image_path),
            "image_size": list(image.size),
            "bbox_xyxy": obj["bbox_xyxy"],
            "center_xy": obj["center_xy"],
            "crop_bbox_xyxy": obj["crop_bbox_xyxy"],
            "feature_range": [float(features.min()), float(features.max())],
            "feature_mean": float(features.mean()),
            "feature_nonfinite_count": int(np.count_nonzero(~np.isfinite(features))),
            "head_input_u8_range": [int(head_input_u8.min()), int(head_input_u8.max())],
            "head_input_low_saturation_count": int(np.count_nonzero(head_input_u8 == 0)),
            "head_input_high_saturation_count": int(np.count_nonzero(head_input_u8 == 255)),
            "head_output_u8_range": [int(head_output_u8.min()), int(head_output_u8.max())],
            "logit_range": [float(raw_dequant.min()), float(raw_dequant.max())],
            "top4_flat_indices": top_indices.tolist(),
            "top4_xy": [[int(i % 40), int(i // 40)] for i in top_indices],
            "top4_u8": head_output_u8.reshape(-1)[top_indices].tolist(),
            "top4_logits": top_values.tolist(),
            "top4_mean_fp64": float(top_values.astype(np.float64).mean()),
            "top4_mean_fp32": float(mean_f32),
            "score_fp64": obtained_score,
            "score_fp32_like_cpp": float(score_f32),
            "label": label,
            "threshold": contract["threshold"],
            "regression_score": reference["quant_score"],
            "score_delta_from_regression": obtained_score - reference["quant_score"],
            "bridge_vs_direct_max_logit_delta": float(np.max(np.abs(split_logits - direct_logits))),
            "split_vs_mixed_max_logit_delta": float(np.max(np.abs(split_logits - mixed_logits))),
            "raw_u8_vs_dequantized_output_exact": True,
            "vectors": {
                "normalized_input_f32": gzip_tensor(stem + "_normalized_input_f32le", x),
                "features_f32": gzip_tensor(stem + "_features_f32le", features),
                "head_input_u8": gzip_tensor(stem + "_head_input_u8", head_input_u8),
                "head_output_u8": gzip_tensor(stem + "_head_output_u8", head_output_u8),
                "local_logits_f32": gzip_tensor(stem + "_local_logits_f32le", raw_dequant),
            },
        }
        write_json(OUTPUT / (stem + ".json"), record)
        records.append(record)
        print(json.dumps({k: record[k] for k in ["image", "label", "score_fp64", "score_fp32_like_cpp", "top4_u8", "top4_logits", "head_input_u8_range", "head_output_u8_range", "feature_range"]}, ensure_ascii=False), flush=True)

    summary = {
        "purpose": "CPU ONNX Runtime reference vectors for subsequent board comparisons; no hardware execution is asserted",
        "runtime": {"python": sys.version, "onnx": onnx.__version__, "onnxruntime": ort.__version__, "numpy": np.__version__},
        "contract_sha256": sha(contract_path),
        "models": contract["models"],
        "bridge": contract["bridge"],
        "output": contract["output"],
        "layout": "Batch 1 NCHW; byte vectors are uint8; f32 files are little-endian",
        "checks": {"model_hashes": "PASS", "package_samples_match_originals": "PASS", "boxes_and_crops_match_regression": "PASS", "split_bridge_matches_mixed_exactly": "PASS", "raw_uint8_output_dequantizes_exactly": "PASS", "scores_match_fullframe_regression": "PASS"},
        "samples": records,
    }
    write_json(OUTPUT / "summary.json", summary)
    (OUTPUT / "README.md").write_text(
        "# V28 board reference vectors\n\n"
        "Generated locally with the frozen locator and Pillow preprocessing. No board or model files were changed.\n\n"
        "- `blue_003`: NORMAL, score 0.0035331058673081497.\n"
        "- `blue_051`: DEFECT, score 0.9999961405013104.\n"
        "- Head input: NCHW uint8 [1,72,40,40], scale 0.19036360085010529, zero point 138.\n"
        "- Head output: NCHW uint8 [1,1,40,40], scale 0.10496816039085388, zero point 96.\n"
        "- `*_head_input_u8.bin.gz` and `*_head_output_u8.bin.gz` are the smallest vectors needed to isolate NPU-head execution. Decompress before supplying to a binary tensor reader.\n"
        "- JSON records include raw and compressed SHA-256, tensor sizes, top-four values, and expected results. Full uint8 output equality is the strict check; near-threshold/classification checks alone can miss numerical errors.\n"
        "- ONNX graph IO is normally float32 around QDQ. The script also exposes the existing quantized boundary in memory, verifies the resulting UINT8 output against the unmodified split and mixed graphs, and does not rewrite deployed models.\n",
        encoding="utf-8",
    )
    print("BOARD_VECTORS_OK", str(OUTPUT / "summary.json"), flush=True)


if __name__ == "__main__":
    main()
