#!/usr/bin/env bash
set -euo pipefail
cd /root/lfm-workcell
if [[ -f train.pid ]] && kill -0 "$(cat train.pid)" 2>/dev/null; then
  echo "Training is already running. See train.log."
  exit 1
fi
[[ -s data/train.jsonl ]] || { echo 'Missing data/train.jsonl'; exit 1; }
export HF_HUB_OFFLINE=1
export TRANSFORMERS_OFFLINE=1
export TOKENIZERS_PARALLELISM=false
nohup .venv/bin/python -u train.py --config train_config.json > train.log 2>&1 < /dev/null &
echo $! > train.pid
echo "Started PID $(cat train.pid). Log: /root/lfm-workcell/train.log"
