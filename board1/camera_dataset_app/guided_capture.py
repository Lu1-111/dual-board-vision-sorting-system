"""Bounded HDMI-guided capture; originals go to the PC, never board storage.

Run only after the user confirms readiness for the named batch. A stop file
interrupts waits/transfers; any unacknowledged photo remains available in RAM.
Every output is pending human review, never an automatically accepted label.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time

from receive_photos import CameraLink, atomic_write, save_photo


def load_plan(path):
    plan = json.loads(path.read_text(encoding="utf-8"))
    steps = plan.get("steps", [])
    if not 1 <= len(steps) <= 12:
        raise ValueError("每批必须有 1 至 12 张，先完成小批次检查")
    if plan.get("split") not in ("train", "val", "test"):
        raise ValueError("一批只能属于一个数据集分组")
    if not plan.get("batch_id") or not plan.get("pose_id"):
        raise ValueError("缺少批次或相机位置编号")
    if len({s["task_id"] for s in steps}) != len(steps):
        raise ValueError("任务编号重复")
    for step in steps:
        if not isinstance(step.get("instruction"), str) or not 1 <= len(step["instruction"]) <= 100:
            raise ValueError("摆放说明必须简短明确")
        if step.get("expected_counts") != {c: 1 for c in ("red", "yellow", "blue", "green")}:
            raise ValueError("此采集流程要求四色各一块")
    return plan


def run_batch(link, plan, output, manifest, should_stop, *, prepare_seconds=20,
              hold_seconds=5, next_prepare_seconds=25, allow_simulation=False):
    # Durations use monotonic time: a board's wall clock may be unset.
    steps = plan["steps"]
    total = len(steps)
    seen_ids = set()
    completed = 0
    def check_stop():
        if should_stop():
            raise KeyboardInterrupt("用户暂停本批采集")
    def guide(phase, text, number, seconds=0):
        check_stop()
        return link.rpc("guide", phase=phase, text=text, step=number,
                        total=total, seconds=seconds)
    def wait_for(predicate, timeout):
        deadline = time.monotonic() + timeout
        while True:
            check_stop()
            state = link.rpc("status")
            if predicate(state):
                return state
            if time.monotonic() >= deadline:
                raise TimeoutError("相机等待超时，本批已停止")
            time.sleep(.2)
    try:
        state = link.rpc("status")
        if state.get("guidance_protocol") != 1:
            raise RuntimeError("板端尚未安装 HDMI 引导版本")
        if state.get("pending") or state.get("busy"):
            raise RuntimeError("有旧照片待接收；先恢复旧照片，再开始新批次")
        if not state.get("camera_ready"):
            raise RuntimeError("相机尚未就绪")
        manifest.mkdir(parents=True, exist_ok=False)
        atomic_write(manifest / "plan.json", json.dumps(plan, ensure_ascii=False, indent=2).encode("utf-8"))
        guide("prepare", "可以摆放：" + steps[0]["instruction"], 1, prepare_seconds)
        wait_for(lambda s: s.get("guidance_seconds") == 0, prepare_seconds + 15)
        for index, step in enumerate(steps):
            number = index + 1
            guide("hold", "请移开手！保持方块静止，倒计时后拍照", number, hold_seconds)
            wait_for(lambda s: s.get("guidance_seconds") == 0, hold_seconds + 15)
            guide("capture", "请保持静止，正在拍照……", number)
            link.rpc("capture")
            state = wait_for(lambda s: s.get("pending"), 25)
            photo = state["photo"]
            if (photo["photo_id"] in seen_ids
                    or photo.get("source") not in (("imx415", "simulation") if allow_simulation else ("imx415",))
                    or (photo.get("width"), photo.get("height")) != (1920, 1080)):
                raise ValueError("重复照片或相机来源/尺寸不符；未计入数据集")
            seen_ids.add(photo["photo_id"])
            next_deadline = None
            # The captured JPEG is frozen in RAM before hands may re-enter.
            if number < total:
                guide("transfer", "已拍好，可以摆下一张：" + steps[number]["instruction"], number + 1)
                next_deadline = time.monotonic() + next_prepare_seconds
            else:
                guide("transfer", "最后一张已拍好，正在传到电脑，可以休息", number)
            data = link.download(photo, should_stop=should_stop)
            path = save_photo(output, photo, data)
            row = dict(step, batch_id=plan["batch_id"], split=plan["split"],
                       pose_id=plan["pose_id"], image=str(path.resolve()),
                       photo=photo, received_utc=datetime.now(timezone.utc).isoformat(),
                       visual_review="pending", annotation_review="pending",
                       training_eligible=False)
            # Persist the task-to-photo mapping BEFORE ACK frees board memory.
            atomic_write(manifest / (step["task_id"] + ".json"),
                         json.dumps(row, ensure_ascii=False, indent=2).encode("utf-8"))
            link.rpc("ack", photo_id=photo["photo_id"], sha256=photo["sha256"])
            completed += 1
            print(f"BATCH_PHOTO={number}/{total} TASK={step['task_id']} IMAGE={path}", flush=True)
            while next_deadline is not None and time.monotonic() < next_deadline:
                check_stop()
                time.sleep(.2)
        guide("done", f"本批 {total} 张已传到电脑！请等待照片检查和下一批任务", total)
        print(f"BATCH_COMPLETE={completed} REVIEW=pending", flush=True)
        return completed
    except BaseException:
        try:
            link.rpc("guide", phase="idle", text="采集已暂停，请等待电脑上的任务框；不会自动继续拍照",
                     seconds=0, step=completed, total=total)
        except Exception:
            pass
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--stop-file", type=Path, required=True)
    parser.add_argument("--move-seconds", type=int, default=25, choices=range(25, 61),
                        help="每张拍好后至少给出的下一张摆放时间，25 至 60 秒")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[2] / "camera_photos")
    args = parser.parse_args()
    plan = load_plan(args.plan)
    if args.stop_file.exists() or args.manifest.exists():
        parser.error("已有停止标记或批次记录，请先核对后使用新批次路径")
    link = CameraLink(args.port)
    try:
        link.start()
        run_batch(link, plan, args.output, args.manifest, args.stop_file.exists,
                  prepare_seconds=max(20, args.move_seconds - 5), next_prepare_seconds=args.move_seconds)
    except KeyboardInterrupt:
        print("BATCH_PAUSED：已暂停，已收到的原图保留，待传照片留在板端内存。", flush=True)
    finally:
        link.close()


if __name__ == "__main__":
    main()
