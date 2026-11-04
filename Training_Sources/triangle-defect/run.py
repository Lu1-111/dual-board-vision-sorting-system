#!/usr/bin/env python3
"""Single entry point for the portable triangle-defect project."""
import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "src"))
COMMANDS = {"gui": "app", "predict": "predict", "train": "train",
            "evaluate": "evaluate", "verify": "verify", "export": "export", "detect": "detect"}


def main():
    if len(sys.argv) < 2 or sys.argv[1] in {"-h", "--help"}:
        print("用法: python run.py {gui,detect,predict,train,evaluate,verify,export} [参数]")
        print("gui 打开界面；detect 彩色整图定位与识别；predict 已裁剪图识别；train 训练；evaluate 复测；verify 校验；export 导出。")
        return
    command = sys.argv.pop(1)
    if command not in COMMANDS:
        raise SystemExit(f"未知命令: {command}")
    importlib.import_module(f"triangle_defect.{COMMANDS[command]}").main()


if __name__ == "__main__":
    main()
