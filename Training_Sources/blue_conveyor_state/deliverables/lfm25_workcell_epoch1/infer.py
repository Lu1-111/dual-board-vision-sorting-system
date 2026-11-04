#!/usr/bin/env python3
"""Classify one workcell photograph with the base model and epoch-one LoRA."""
import argparse
import json
from pathlib import Path

import torch
from PIL import Image, ImageOps
from peft import PeftModel
from transformers import AutoProcessor, Lfm2VlForConditionalGeneration

ROOT = Path(__file__).resolve().parent
PROMPT = "识别传送带工位状态。手进入工位时输出hand_present；无手且有物块时输出object_present；无手无物块时输出empty。只输出一个标签。"
CLASSES = {"empty", "object_present", "hand_present"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="input photo")
    parser.add_argument("--device", choices=["auto", "cpu", "cuda"], default="auto")
    args = parser.parse_args()
    device = ("cuda" if torch.cuda.is_available() else "cpu") if args.device == "auto" else args.device
    if device == "cuda" and not torch.cuda.is_available():
        parser.error("CUDA is unavailable")
    dtype = torch.bfloat16 if device == "cuda" and torch.cuda.is_bf16_supported() else torch.float32
    base = ROOT / "model" / "base"
    adapter = ROOT / "model" / "adapter"
    processor = AutoProcessor.from_pretrained(adapter, local_files_only=True)
    model = Lfm2VlForConditionalGeneration.from_pretrained(
        base, dtype=dtype, attn_implementation="sdpa", local_files_only=True
    )
    model = PeftModel.from_pretrained(model, adapter, local_files_only=True).to(device).eval()
    with Image.open(args.image) as source:
        image = ImageOps.exif_transpose(source).convert("RGB")
    messages = [{"role": "user", "content": [
        {"type": "image", "image": image}, {"type": "text", "text": PROMPT}
    ]}]
    inputs = processor.apply_chat_template(
        messages, add_generation_prompt=True, tokenize=True, return_dict=True, return_tensors="pt"
    ).to(device)
    with torch.inference_mode():
        tokens = model.generate(**inputs, max_new_tokens=12, do_sample=False, use_cache=False)
    output = processor.batch_decode(tokens[:, inputs["input_ids"].shape[1]:], skip_special_tokens=True)[0].strip()
    print(json.dumps({"image": str(args.image), "prediction": output, "valid_label": output in CLASSES,
                      "device": device}, ensure_ascii=False))


if __name__ == "__main__":
    main()
