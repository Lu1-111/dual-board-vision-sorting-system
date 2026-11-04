#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ -n "${TRIANGLE_PYTHON:-}" ]]; then
  PROJECT_PYTHON="$TRIANGLE_PYTHON"
elif [[ -x "$PROJECT_DIR/.venv/bin/python" ]]; then
  PROJECT_PYTHON="$PROJECT_DIR/.venv/bin/python"
elif [[ -x "$HOME/.venvs/cube-threecolor-cu128/bin/python" ]]; then
  PROJECT_PYTHON="$HOME/.venvs/cube-threecolor-cu128/bin/python"
else
  PROJECT_PYTHON=python3
fi
if [[ $# -eq 0 ]]; then
  set -- gui
fi
exec "$PROJECT_PYTHON" "$PROJECT_DIR/run.py" "$@"
