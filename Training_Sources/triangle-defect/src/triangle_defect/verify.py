"""Check every retained image, group split and released model digest."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json

from .paths import ROOT, MANIFEST, MODEL, INITIALIZATION, load_manifest, project_path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify():
    manifest = load_manifest()
    rows = manifest["records"]
    failures = []
    groups = defaultdict(set)
    image_hashes = defaultdict(set)
    counts = defaultdict(Counter)
    ids = set()
    paths = set()
    for row in rows:
        path = project_path(row["path"])
        if row["id"] in ids or row["path"] in paths:
            failures.append(f"Duplicate record: {row['id']}")
        ids.add(row["id"])
        paths.add(row["path"])
        if not path.is_file() or digest(path) != row["sha256"]:
            failures.append(f"Image missing or changed: {row['path']}")
        groups[row["group"]].add(row["split"])
        image_hashes[row["gray320_sha256"]].add(row["split"])
        counts[row["split"]][str(row["label"])] += 1
    actual = {str(p.relative_to(ROOT)) for p in (ROOT / "data/images").rglob("*.png")}
    if actual != paths:
        failures.append("Curated image directory does not match manifest")
    if len(rows) != manifest["total"] or dict(counts) != manifest["counts"]:
        failures.append("Dataset counts do not match manifest")
    if any(len(splits) != 1 for splits in groups.values()):
        failures.append("Related image group crosses splits")
    if any(len(splits) != 1 for splits in image_hashes.values()):
        failures.append("Identical grayscale image crosses splits")
    originals = json.loads((ROOT / "data/originals/index.json").read_text())
    for row in originals["images"]:
        path = project_path(row["path"])
        if not path.is_file() or digest(path) != row["sha256"]:
            failures.append(f"Original capture missing or changed: {row['path']}")
    release = json.loads((MODEL / "release.json").read_text())
    for filename, key in [("weights.pt", "weights_sha256"),
                          ("model_float32.torchscript.pt", "torchscript_sha256"),
                          ("config.json", "config_sha256")]:
        path = MODEL / filename
        if not path.is_file() or digest(path) != release[key]:
            failures.append(f"Released model changed: {filename}")
    provenance = json.loads((INITIALIZATION.parent / "provenance.json").read_text())
    if not INITIALIZATION.is_file() or digest(INITIALIZATION) != provenance["features_sha256"]:
        failures.append("Training initialization missing or changed")
    if failures:
        raise RuntimeError("\n".join(failures))
    return {"passed": True, "curated_images": len(rows),
            "main_images": sum(r["dataset"] == "main961" for r in rows),
            "original_captures": len(originals["images"]),
            "counts": dict(counts), "groups": len(groups),
            "manifest_sha256": digest(MANIFEST),
            "model_hashes_unchanged": True, "group_splits_disjoint": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    print(json.dumps(verify(), ensure_ascii=False, indent=2))
