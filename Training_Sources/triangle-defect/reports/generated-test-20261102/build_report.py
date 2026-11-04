"""Build a local visual test report from the immutable reviewed test images."""
from pathlib import Path
import base64
import html
import io
import json

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties
from PIL import Image

BATCH = Path(__file__).resolve().parent


def main():
    result = json.loads((BATCH / "results.json").read_text())
    plan = json.loads((BATCH / "plan.json").read_text())
    rows = result["samples"]
    metric = result["primary_metrics"]
    title = f"V28 新生图测试：{metric['correct']}/{metric['count']} 正确"
    limits = ("本批使用原训练图片作为场景和缺角形态参考生成，属于相关的合成测试样本，"
              "不是独立实拍现场验收。明暗、大小和角度变化只是生成目标，未经精确标定；"
              "N07 的阴影较弱，N06、D10 的变化也较轻微。不能由本批 20/20 推断现场准确率为 100%。")
    cards = []
    table = []
    for row in rows:
        with Image.open(BATCH / row["image"]) as image:
            preview = image.convert("RGB")
            preview.thumbnail((500, 500))
            stream = io.BytesIO()
            preview.save(stream, format="JPEG", quality=90)
            uri = "data:image/jpeg;base64," + base64.b64encode(stream.getvalue()).decode()
        truth = "三角缺角" if row["actual_label"] else "正常"
        predicted = "三角缺角" if row["prediction"]["class_id"] else "正常"
        score = row["prediction"]["score"]
        passed = "正确" if row["correct"] else "错误"
        state = "pass" if row["correct"] else "fail"
        cards.append(f'''<article class="card {state}">
          <img src="{uri}" alt="{row['id']} {html.escape(row['scenario'])}">
          <div class="body"><div class="line"><strong>{row['id']} · {html.escape(row['scenario'])}</strong><span class="badge">{passed}</span></div>
          <p>实际：{truth}　模型：{predicted}</p><p class="score">缺角得分 {score:.6f}　/　阈值 0.88</p>
          <details><summary>查看图像审核记录</summary><p>{html.escape(row['note'])}</p></details></div>
        </article>''')
        table.append(f"<tr><td>{row['id']}</td><td>{html.escape(row['scenario'])}</td><td>{truth}</td>"
                     f"<td>{predicted}</td><td>{score:.6f}</td><td>{row['resolution_probe']['score']:.6f}</td><td>{passed}</td></tr>")
    normal_max = max(r["prediction"]["score"] for r in rows if not r["actual_label"])
    defect_min = min(r["prediction"]["score"] for r in rows if r["actual_label"])
    prompts = "".join(f"<details><summary>{c['id']} · {html.escape(c['scenario'])}</summary><pre>{html.escape(c['prompt'])}</pre></details>" for c in plan["cases"])
    document = f'''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
      <meta name="viewport" content="width=device-width,initial-scale=1"><title>{title}</title>
      <style>
      *{{box-sizing:border-box}}body{{margin:0;background:#f1f4f7;color:#18242c;font-family:system-ui,"Noto Sans CJK SC",sans-serif;line-height:1.6}}
      main{{max-width:1440px;margin:auto;padding:38px 28px 70px}}h1{{font-size:34px;line-height:1.3;margin:10px 0 14px}}h2{{font-size:24px;margin-top:34px}}
      .eyebrow{{color:#52717f;font-weight:650;letter-spacing:1px}}.sub{{color:#526370;max-width:1020px}}
      .stats{{display:grid;grid-template-columns:repeat(4,1fr);gap:14px;margin:26px 0}}.stat{{background:white;padding:18px 22px;border-radius:12px;border:1px solid #d9e2e8}}.stat strong{{display:block;font-size:30px;color:#126653}}.stat span{{color:#526370}}
      .notice{{background:#fff6dc;border-left:4px solid #c59928;padding:16px 20px;border-radius:5px}}
      .gallery{{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:18px}}.card{{background:white;border:1px solid #d8e2e8;border-radius:12px;overflow:hidden}}.card img{{width:100%;display:block;aspect-ratio:1;object-fit:contain;background:#222}}
      .body{{padding:13px 15px 17px}}.line{{display:flex;justify-content:space-between;gap:5px;align-items:start}}.line strong{{font-size:14px}}.badge{{font-size:12px;white-space:nowrap;padding:1px 7px;border-radius:8px;background:#daf2e6;color:#126653}}.fail .badge{{background:#ffe2df;color:#a51e18}}
      .body p{{margin:6px 0;font-size:13px}}.score{{color:#526370;font-variant-numeric:tabular-nums}}details{{font-size:13px;color:#526370}}summary{{cursor:pointer}}
      .sheet{{overflow:auto;background:white;padding:18px;border-radius:12px}}table{{width:100%;border-collapse:collapse;font-size:14px;white-space:nowrap}}th,td{{padding:9px 12px;border-bottom:1px solid #e4e9ed;text-align:left}}th{{background:#edf3f6}}
      pre{{white-space:pre-wrap;background:white;padding:16px;border-radius:8px;line-height:1.5}}.meta{{font-size:14px;color:#526370}}
      @media(max-width:1000px){{.gallery{{grid-template-columns:repeat(2,1fr)}}}}@media(max-width:600px){{main{{padding:22px 14px}}h1{{font-size:27px}}.stats{{grid-template-columns:repeat(2,1fr)}}.gallery{{grid-template-columns:1fr}}}}
      </style></head><body><main>
      <div class="eyebrow">三角缺角识别 · 冻结模型测试</div><h1>{title}</h1>
      <p class="sub">20 张新生成图片，10 张正常、10 张三角缺角。先查看实际图像并固定标签，再运行冻结的 V28；所有生成样本均保留，未按模型得分筛选。生图工具：内置 image_gen。</p>
      <div class="stats"><div class="stat"><strong>{metric['correct']}/{metric['count']}</strong><span>主测试正确数</span></div><div class="stat"><strong>{metric['false_positives']}/10</strong><span>正常样本误报</span></div><div class="stat"><strong>{metric['false_negatives']}/10</strong><span>缺角样本漏检</span></div><div class="stat"><strong>20/20</strong><span>224→320 缩放对照正确数</span></div></div>
      <p class="notice">{limits}</p>
      <h2>逐图结果</h2><p class="meta">得分 ≥ 0.88 判为缺角。这里的得分未经概率校准。点击审核记录可查看标签依据。</p>
      <div class="gallery">{''.join(cards)}</div>
      <h2>结果明细</h2><div class="sheet"><table><thead><tr><th>编号</th><th>设计情形</th><th>实际</th><th>预测</th><th>主测试得分</th><th>224→320 得分</th><th>判定</th></tr></thead><tbody>{''.join(table)}</tbody></table></div>
      <h2>测试条件</h2><p>主测试按现有识别入口执行：原图转灰度，双线性插值至 320×320，再除以 255。对照测试先缩到原训练图片的 224×224，再插值到 320×320；两条路径分别报告，未用于调整阈值。PyTorch 与 TorchScript 的 20 张得分差为 0。</p>
      <p>正常样本最高得分 {normal_max:.6f}；缺角样本最低得分 {defect_min:.6f}。固定阈值仍为 0.88，模型参数和原 1,009 张数据的清单校验值均未改变。本批图仅保存在报告目录，未加入训练。</p>
      <p>CPU 4 线程，预热后单张推理中位数 {result['latency']['forward_median_ms']:.2f} ms；读取本批 1254×1254 PNG、灰度插值及识别的总耗时中位数 {result['latency']['high_resolution_png_e2e_median_ms']:.2f} ms。前者不包含图片解码，后者只取 20 张样本；不是 FPGA 性能。</p>
      <h2>生图设计与完整提示词</h2><p class="meta">提示词中的百分比、角度和尺寸是生成要求，实际生成图仅作定性变化测试。参考图来自正式训练集，场景和缺角类型保持一致。</p>{prompts}
      <p class="meta">测试时间（北京时间）：{html.escape(result['local_time'])}<br>同目录：images/ 原始生成图 · plan.json 完整设计 · review.json 预测前标签 · results.json / results.csv 逐图结果 · evaluate.py 复测入口。</p>
      </main></body></html>'''
    (BATCH / "report.html").write_text(document, encoding="utf-8")

    # A scientific result figure; original test image files are never altered.
    font = FontProperties(fname="/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc")
    figure, axes = plt.subplots(4, 5, figsize=(15, 13.8), facecolor="#f1f4f7")
    for axis, row in zip(axes.flat, rows):
        with Image.open(BATCH / row["image"]) as image:
            axis.imshow(image)
        axis.set_xticks([])
        axis.set_yticks([])
        for spine in axis.spines.values():
            spine.set_color("#17825d" if row["correct"] else "#ba3424")
            spine.set_linewidth(2)
        predicted = "缺角" if row["prediction"]["class_id"] else "正常"
        axis.set_title(f"{row['id']} · {row['scenario']}\n预测 {predicted} | 得分 {row['prediction']['score']:.4f}",
                       fontproperties=font, fontsize=10, loc="left", pad=8)
    figure.suptitle(title, fontproperties=font, fontsize=24, x=.04, ha="left", y=.99)
    figure.text(.04, .946, "10 张正常无误报 · 10 张缺角无漏检 · 固定阈值 0.88 · 仅测试，未训练",
                fontproperties=font, fontsize=12, color="#526370")
    figure.text(.04, .015, "参考原训练场景生成的合成测试；样本相关且数量有限，不代表独立现场验收。",
                fontproperties=font, fontsize=11, color="#526370")
    figure.subplots_adjust(left=.04, right=.98, top=.90, bottom=.055, wspace=.12, hspace=.30)
    figure.savefig(BATCH / "overview.png", dpi=120)
    plt.close(figure)
    print("Saved report.html and overview.png")


if __name__ == "__main__":
    main()
