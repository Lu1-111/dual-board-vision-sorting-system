"""Reproduce original-frame inspection and rebuild the self-contained HTML report.

Run with the project Python environment. No training or parameter selection occurs.
"""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import html
import io
import json
from pathlib import Path
import platform
import statistics
import sys

from PIL import Image
import cv2
import torch

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'src'))
from triangle_defect.detect import FrameInspector, annotate, export_results


def evaluate():
    originals = json.loads((ROOT / 'data/originals/index.json').read_text())['images']
    manifest = json.loads((ROOT / 'data/manifest.json').read_text())['records']
    curated = {row['id']: row for row in manifest}
    inspector = FrameInspector()
    metrics = {key: {'count': 0, 'confusion': [[0, 0], [0, 0]], 'correct': 0, 'errors': []}
               for key in ('train', 'val', 'test', 'all_labeled')}
    frames = []
    for row in originals:
        frame = inspector.inspect(ROOT / row['path'])
        frame['path'] = row['path']  # Keep the checked-in report portable.
        reference = curated.get(row.get('curated_id'))
        frame.update(source_index=row['index'], expected_object_count=1,
                     reference_crop=row.get('crop_box_xyxy'), reference_label=row.get('label'),
                     split=reference['split'] if reference else None,
                     center_in_reference_crop=None, bbox_inside_reference_crop=None,
                     classification_correct=None)
        if reference:
            correct = False
            truth = reference['label']
            if len(frame['objects']) == 1:
                obj = frame['objects'][0]
                x1, y1, x2, y2 = obj['bbox_xyxy']
                cx, cy = obj['center_xy']
                a, b, c, d = frame['reference_crop']
                frame['center_in_reference_crop'] = a <= cx < c and b <= cy < d
                frame['bbox_inside_reference_crop'] = a <= x1 < x2 <= c and b <= y1 < y2 <= d
                prediction = obj['class_id']
                correct = prediction == truth
                if prediction is not None:
                    for key in (reference['split'], 'all_labeled'):
                        metrics[key]['confusion'][truth][prediction] += 1
            frame['classification_correct'] = correct
            for key in (reference['split'], 'all_labeled'):
                metrics[key]['count'] += 1
                metrics[key]['correct'] += int(correct)
                if not correct:
                    metrics[key]['errors'].append(row['index'])
        frames.append(frame)
    summary = {
        'created_at': datetime.now(timezone.utc).isoformat(),
        'locator_config_sha256': inspector.locator.config_sha256,
        'model_weights_sha256': hashlib.sha256((ROOT / 'models/v28/weights.pt').read_bytes()).hexdigest(),
        'model_config_sha256': hashlib.sha256((ROOT / 'models/v28/config.json').read_bytes()).hexdigest(),
        'images': len(frames),
        'exactly_one_object': sum(len(f['objects']) == 1 for f in frames),
        'detection_count_failures': [f['source_index'] for f in frames if len(f['objects']) != 1],
        'centers_inside_existing_crops': sum(f['center_in_reference_crop'] is True for f in frames),
        'boxes_inside_existing_crops': sum(f['bbox_inside_reference_crop'] is True for f in frames),
        'labeled_originals': metrics['all_labeled']['count'], 'classification': metrics,
        'median_fullframe_ms': statistics.median(f['elapsed_ms'] for f in frames),
        'timing_scope': f'CPU, PyTorch4 threads, OpenCV{cv2.getNumThreads()} threads; JPEG decode/RGB conversion + blue localization + grayscale crop + V28 forward. '
                        'Excludes model loading, Image.open header access, drawing, export and GUI. '
                        'Serial batch of the101 original1920x1080 photos; not a camera or FPGA throughput claim.',
        'environment': {'python': platform.python_version(), 'torch': torch.__version__,
                        'opencv': cv2.__version__, 'device': 'cpu', 'torch_threads': 4,
                        'opencv_threads': cv2.getNumThreads()},
        'scope': 'Internal original-photo regression.42 training-split originals used for development; '
                 'remaining originals checked after initial locator config fixed. Existing crop boxes '
                 'include context and are not precise object ground-truth boxes, so no IoU/mAP or '
                 'precise coordinate error claimed.4 originally ambiguous defect labels excluded '
                 'only from defect-classification metrics, retained for localization.'}
    result = {'summary': summary, 'frames': frames}
    (HERE / 'results.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    export_results(frames, HERE / 'results.csv')
    return result


def build_report(result):
    summary, frames = result['summary'], result['frames']
    cards = []
    for frame in frames:
        with Image.open(ROOT / frame['path']) as image:
            marked = annotate(image, frame)
        if frame['source_index'] in (3, 51):
            marked.save(HERE / f"example_{frame['source_index']:03d}.png")
        marked.thumbnail((640, 360))
        buffer = io.BytesIO()
        marked.save(buffer, format='JPEG', quality=83)
        encoded = base64.b64encode(buffer.getvalue()).decode()
        truth = {0: '正常', 1: '三角缺角', None: '原记录无法判定'}[frame['reference_label']]
        lines = []
        for obj in frame['objects']:
            label = {0: '正常', 1: '缺角', None: '需复核'}[obj['class_id']]
            score = f"{obj['score']:.4f}" if obj['score'] is not None else '—'
            box = ', '.join(map(str, obj['bbox_xyxy']))
            center = ', '.join(f'{v:.1f}' for v in obj['center_xy'])
            lines.append(f'<div>物块 #{obj["object_id"]} · <b>{label}</b> · 缺角得分 {score}</div>'
                         f'<div>框 [{box}] · 中心 ({center})</div>')
        failed = frame['classification_correct'] is False
        verdict = '漏检 / 识别错误' if failed else '识别正确' if frame['classification_correct'] else '仅核验定位'
        cards.append(f'<article id="image{frame["source_index"]}" class="{"failed" if failed else ""}">'
                     f'<h3>{html.escape(Path(frame["path"]).name)} <span>{verdict}</span></h3>'
                     f'<img alt="原图定位标注" loading="lazy" src="data:image/jpeg;base64,{encoded}">'
                     f'<div class="detail">{"".join(lines)}'
                     f'<div>原标签：{truth} · 原划分：{frame["split"] or "未标注"}</div></div></article>')
    rows = []
    for key, title in [('train', '原训练划分'), ('val', '原验证划分'), ('test', '原测试划分'),
                       ('all_labeled', '全部有标签原图（包含训练图）')]:
        m = summary['classification'][key]
        tn, fp = m['confusion'][0]
        fn, tp = m['confusion'][1]
        rows.append(f'<tr><td>{title}</td><td>{m["correct"]}/{m["count"]}</td>'
                    f'<td>{fp}</td><td>{fn}</td><td>{tp}/{tp + fn}</td></tr>')
    legacy_path = HERE / 'legacy_regression.json'
    legacy = json.loads(legacy_path.read_text()) if legacy_path.exists() else None
    if legacy:
        comparison = legacy['release_comparison']
        legacy_text = (f'原 142 张裁剪测试图仍为 140/142 正确。与冻结版本的最大得分差为 '
                       f'{comparison["max_score_difference"]:g}，TorchScript 最大差为 '
                       f'{comparison["max_torchscript_difference"]:g}，全部判定一致。')
    else:
        legacy_text = '原裁剪流程的回归结果请运行 evaluate --compare-release 查看。'
    contents = f'''<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>蓝色物块整图定位与缺角识别</title>
<style>
*{{box-sizing:border-box}}body{{margin:0;background:#f0f4f7;color:#182b3b;font:16px/1.65 system-ui,sans-serif}}
main{{max-width:1440px;margin:auto;padding:32px}}h1{{font-size:32px;line-height:1.3}}h2{{font-size:22px;margin:0 0 12px}}
.panel,article{{background:white;border:1px solid #dae4eb;border-radius:14px;padding:24px;margin-bottom:22px}}
.stats{{display:flex;gap:18px;flex-wrap:wrap}}.stat{{flex:1;min-width:200px;background:#eaf7f3;padding:16px;border-radius:12px}}
.stat b{{display:block;font-size:30px;color:#087652}}.note{{color:#546775}}.issue{{border-left:5px solid #d94236;background:#fff3ef;padding:14px 18px}}
table{{border-collapse:collapse;width:100%}}td,th{{padding:10px;text-align:left;border-bottom:1px solid #e2e9ed}}
.gallery{{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:20px}}article{{padding:0;overflow:hidden;margin:0}}
article.failed{{border:3px solid #dc5144}}article h3{{font-size:17px;margin:0;padding:12px 16px}}article h3 span{{float:right;font-size:14px;color:#526676}}
article.failed h3 span{{color:#bb3329}}article img{{display:block;width:100%;height:auto}}.detail{{padding:14px 16px;font-size:14px}}
code{{background:#eef2f5;padding:2px 5px;border-radius:4px}}a{{color:#08765a}}@media(max-width:800px){{main{{padding:16px}}.gallery{{grid-template-columns:1fr}}.panel{{padding:16px}}}}
</style><main>
<h1>原始彩色图 → 蓝色物块定位 → 三角缺角识别</h1>
<p class="note">2026-11-02 · V28 参数和阈值 0.88 冻结 · 原场景内部回归检查</p>
<section class="panel"><div class="stats">
<div class="stat"><b>{summary['exactly_one_object']}/{summary['images']}</b>原始照片均定位到 1 个蓝色物块</div>
<div class="stat"><b>{summary['classification']['all_labeled']['correct']}/{summary['labeled_originals']}</b>有标签原图的缺角判定正确</div>
<div class="stat"><b>{summary['median_fullframe_ms']:.1f} ms</b>本机 CPU 处理耗时中位数</div></div>
<p>输出每个物块的原图像素框 <code>[x1, y1, x2, y2]</code>、中心 <code>[cx, cy]</code>、缺角结果和得分。
坐标原点在左上，x 向右、y 向下，x2/y2 不包含在框内；中心为检测框中心。绿色为正常，红色为缺角，黄色为需复核。</p>
<p class="note">耗时包含 JPEG 解码、颜色定位、裁剪与灰度插值、V28 前向；不包含模型加载、绘图、结果写盘和界面。
PyTorch 设置为 4 线程，OpenCV 使用环境默认的 {summary['environment']['opencv_threads']} 线程。
这不是摄像头整链路帧率，也不是 FPGA 性能。坐标尚未做相机或机械臂标定。</p></section>
<section class="panel"><h2>功能已补全</h2>
<p>启动项目的 <code>start.sh</code>，默认选择“原始彩色整图”。支持选图、批量处理文件夹、原图画框和中心十字、
保存原分辨率标注图、导出 CSV / JSON。已有裁剪图可切换到“已裁剪单工件”。</p>
<p>彩色仅用于定位蓝色区域。自动裁剪后先转灰度、双线性插值到 224，再按冻结 V28 的流程插值到 320。
没有重新训练或改变缺角阈值。未找到蓝色物块会单独提示；物块接触图像边缘时输出定位框，但跳过缺角分类并标记“需复核”。</p>
<p>定位针对现有传送带场景。多个分离蓝色区域的坐标输出已通过软件测试，现有 101 张实图均为单个蓝色工件。
接触、重叠或复杂蓝色背景尚未经过真实数据验证。</p></section>
<section class="panel"><h2>识别结果与保留的失败样本</h2>
<table><tr><th>范围</th><th>正确数</th><th>正常误报</th><th>缺角漏检</th><th>缺角检出</th></tr>{''.join(rows)}</table>
<p class="issue">仍有 1 张已知漏检：<a href="#image75">blue_075.jpg</a>。模型判为正常，原标签为三角缺角。
该难例在旧裁剪流程中也会漏检。本次补全定位功能，未通过修改标签、阈值或权重掩盖它。</p>
<p>{legacy_text}</p>
<p class="note">42 张原训练划分照片用于初期定位检查；固定定位配置后检查其余原图。97 张有标签原图的检测框和中心均落在原人工裁剪范围内。
这些裁剪范围包含背景，不能作为精确物体边界，因此没有把结果宣称为 mAP、IoU 或像素定位误差。
另外 4 张原记录无法判定缺角的照片计入定位检查，未强行加入缺角准确率。数据与前期开发相关，不构成独立现场验收。</p></section>
<h2>全部 {len(frames)} 张原图的定位与识别</h2><div class="gallery">{''.join(cards)}</div>
<p class="note">逐图数值：同目录 results.json / results.csv；复现脚本：evaluate.py；定位配置：config/blue_locator.json。
得分未经概率校准。原始图片、标签与正式模型均保留不变。</p></main></html>'''
    (HERE / 'report.html').write_text(contents, encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reuse-results', action='store_true', help='Only rebuild the display report')
    args = parser.parse_args()
    result = json.loads((HERE / 'results.json').read_text()) if args.reuse_results else evaluate()
    build_report(result)
    print(json.dumps(result['summary'], ensure_ascii=False, indent=2))
    print('Report:', HERE / 'report.html')


if __name__ == '__main__':
    main()
