"""Resolve project assets independently of the shell's working directory."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "data/manifest.json"
MODEL = ROOT / "models/v28"
INITIALIZATION = ROOT / "models/initialization/mobilenet_v3_features.pt"


def project_path(value):
    path = Path(value)
    return path if path.is_absolute() else ROOT / path


def load_manifest(path=MANIFEST):
    return json.loads(Path(path).read_text(encoding="utf-8"))
