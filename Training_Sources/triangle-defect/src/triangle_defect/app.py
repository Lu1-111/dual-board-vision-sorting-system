"""Desktop inspection of original color frames and existing single-object crops."""
from pathlib import Path
import queue
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from PIL import Image, ImageTk

from .detect import EXTENSIONS, FrameInspector, annotate, export_results

FULL_FRAME = '原始彩色整图：定位蓝色物块＋缺角识别'
CROP = '已裁剪单工件：缺角识别'


class App:
    def __init__(self, root=None):
        self.root = root if root is not None else tk.Tk()
        self.root.title('蓝色物块定位与三角缺角识别 · V28')
        self.root.geometry('1180x860')
        self.root.minsize(900, 650)
        self.engine = None
        self.records = []
        self.row_lookup = {}
        self.busy = False
        self.closed = False
        self.events = queue.Queue()
        self.mode = tk.StringVar(value=FULL_FRAME)
        self.status = tk.StringVar(value='正在加载模型…')
        self.help_text = tk.StringVar()
        panel = ttk.Frame(self.root, padding=16)
        panel.pack(fill='both', expand=True)
        ttk.Label(panel, text='物块定位与缺角识别', font=('', 21, 'bold')).pack(anchor='w')
        mode_bar = ttk.Frame(panel)
        mode_bar.pack(fill='x', pady=(10, 8))
        ttk.Label(mode_bar, text='输入类型：').pack(side='left')
        self.mode_choice = ttk.Combobox(mode_bar, textvariable=self.mode,
                                       values=(FULL_FRAME, CROP), state='readonly', width=46)
        self.mode_choice.pack(side='left')
        self.mode_choice.bind('<<ComboboxSelected>>', self.mode_changed)
        ttk.Label(panel, textvariable=self.help_text, wraplength=1100).pack(anchor='w')
        bar = ttk.Frame(panel)
        bar.pack(fill='x', pady=10)
        self.choose = ttk.Button(bar, text='选择图片', command=self.select)
        self.choose.pack(side='left')
        self.folder = ttk.Button(bar, text='识别整个文件夹', command=self.select_folder)
        self.folder.pack(side='left', padx=8)
        self.export_button = ttk.Button(bar, text='导出坐标和结果', command=self.export)
        self.export_button.pack(side='left')
        self.save_button = ttk.Button(bar, text='保存当前标注图', command=self.save_annotation)
        self.save_button.pack(side='left', padx=8)
        ttk.Label(panel, textvariable=self.status, wraplength=1100, font=('', 11)).pack(anchor='w', pady=(0, 8))
        self.preview_area = ttk.Frame(panel, height=340)
        self.preview_area.pack_propagate(False)
        self.preview = ttk.Label(self.preview_area, anchor='center')
        self.preview.pack(fill='both', expand=True)
        table = ttk.Frame(panel)
        self.tree = ttk.Treeview(table, columns=('file', 'object', 'result', 'bbox', 'center', 'score'),
                                 show='headings', height=7)
        for key, title, width in [('file', '图片', 200), ('object', '物块', 55),
                                  ('result', '识别结果', 145), ('bbox', '框坐标 (x1,y1,x2,y2)', 250),
                                  ('center', '中心坐标 (cx,cy)', 175), ('score', '缺角得分', 100)]:
            self.tree.heading(key, text=title)
            self.tree.column(key, width=width, minwidth=50)
        scroll_y = ttk.Scrollbar(table, orient='vertical', command=self.tree.yview)
        scroll_x = ttk.Scrollbar(table, orient='horizontal', command=self.tree.xview)
        self.tree.configure(yscrollcommand=scroll_y.set, xscrollcommand=scroll_x.set)
        self.tree.grid(row=0, column=0, sticky='nsew')
        scroll_y.grid(row=0, column=1, sticky='ns')
        scroll_x.grid(row=1, column=0, sticky='ew')
        table.columnconfigure(0, weight=1)
        self.tree.bind('<<TreeviewSelect>>', self.show_selected)
        self.footer = tk.StringVar()
        self.footer_label = ttk.Label(panel, textvariable=self.footer, wraplength=850)
        self.footer_label.pack(side='bottom', anchor='w', pady=(7, 0))
        table.pack(side='bottom', fill='x', pady=(10, 0))
        self.preview_area.pack(fill='both', expand=True)
        self.resize_id = None
        self.preview_area.bind('<Configure>', self.preview_resized)
        self.mode_changed()
        self.choose.state(['disabled'])
        self.folder.state(['disabled'])
        self.export_button.state(['disabled'])
        self.save_button.state(['disabled'])
        self.root.protocol('WM_DELETE_WINDOW', self.close)
        self.poll_id = self.root.after(50, self.poll_events)
        threading.Thread(target=self.load, daemon=True).start()

    def mode_changed(self, event=None):
        if self.mode.get() == FULL_FRAME:
            self.help_text.set('直接载入原始彩色照片。程序定位蓝色物块，再自动裁剪、灰度插值并判断三角缺角。')
            self.footer.set('整图模式：十字为检测框中心，坐标单位为原图像素，非机械臂坐标。右边界 x2、下边界 y2 不包含在框内。')
        else:
            self.help_text.set('载入已经裁剪好的单个工件图片；保留原 V28 识别流程。此模式不检测物块边框。')
            self.footer.set('裁剪模式：不输出物块检测坐标。红圈仅为缺角响应最强位置，得分未经概率校准。')

    def load(self):
        try:
            self.events.put(('loaded', FrameInspector()))
        except Exception as error:
            self.events.put(('load_error', str(error)))

    def poll_events(self):
        if self.closed:
            return
        while True:
            try:
                event, payload = self.events.get_nowait()
            except queue.Empty:
                break
            if event == 'loaded':
                self.engine = payload
                self.status.set('已就绪。可直接选择原始彩色照片。')
                self.choose.state(['!disabled'])
                self.folder.state(['!disabled'])
            elif event == 'load_error':
                self.status.set('模型加载失败：' + payload)
            elif event == 'complete':
                self.finish(*payload)
        self.poll_id = self.root.after(50, self.poll_events)

    def select(self):
        paths = filedialog.askopenfilenames(filetypes=[('图片', '*.png *.jpg *.jpeg *.bmp *.tif *.tiff')])
        self.start([Path(path) for path in paths])

    def select_folder(self):
        folder = filedialog.askdirectory()
        if folder:
            self.start(sorted(p for p in Path(folder).iterdir()
                              if p.is_file() and p.suffix.lower() in EXTENSIONS))

    def start(self, paths):
        if self.engine is None or self.busy or not paths:
            return
        self.busy = True
        self.status.set(f'正在处理 {len(paths)} 张图片…')
        self.choose.state(['disabled'])
        self.folder.state(['disabled'])
        self.mode_choice.configure(state='disabled')
        self.export_button.state(['disabled'])
        self.save_button.state(['disabled'])
        full_frame = self.mode.get() == FULL_FRAME
        threading.Thread(target=self.run, args=(paths, full_frame), daemon=True).start()

    def run(self, paths, full_frame):
        rows, errors = [], []
        for path in paths:
            if self.closed:
                return
            try:
                method = self.engine.inspect if full_frame else self.engine.inspect_crop
                rows.append(method(path.resolve()))
            except Exception as error:
                errors.append(f'{path.name}: {error}')
        self.events.put(('complete', (rows, errors)))

    def finish(self, rows, errors):
        self.busy = False
        self.choose.state(['!disabled'])
        self.folder.state(['!disabled'])
        self.mode_choice.configure(state='readonly')
        self.records = rows
        self.row_lookup.clear()
        for item in self.tree.get_children():
            self.tree.delete(item)
        for frame_index, frame in enumerate(rows):
            for obj in frame['objects'] or [None]:
                item = str(len(self.row_lookup))
                self.row_lookup[item] = (frame_index, obj['object_id'] if obj else None)
                if obj is None:
                    values = (Path(frame['path']).name, '—', '未找到蓝色物块', '—', '—', '—')
                else:
                    label = '缺角' if obj['class_id'] == 1 else '正常' if obj['class_id'] == 0 else '需复核（画面边缘）'
                    box = ', '.join(map(str, obj['bbox_xyxy'])) if obj['bbox_xyxy'] else '裁剪模式不定位'
                    center = ', '.join(f'{v:.1f}' for v in obj['center_xy']) if obj['center_xy'] else '—'
                    score = f"{obj['score']:.4f}" if obj['score'] is not None else '—'
                    values = (Path(frame['path']).name, obj['object_id'], label, box, center, score)
                self.tree.insert('', 'end', iid=item, values=values)
        objects = [obj for frame in rows for obj in frame['objects']]
        defects = sum(obj['class_id'] == 1 for obj in objects)
        reviews = sum(obj['class_id'] is None for obj in objects)
        empty = sum(not frame['objects'] for frame in rows)
        self.status.set(f'完成：{len(rows)} 张图片，{len(objects)} 个物块，缺角 {defects} 个，'
                        f'需复核 {reviews} 个，未定位到物块 {empty} 张。'
                        + (f'读取失败 {len(errors)} 张。' if errors else ''))
        self.preview.configure(image='')
        if rows:
            self.export_button.state(['!disabled'])
            self.save_button.state(['!disabled'])
            self.tree.selection_set('0')
            self.show_selected()
        if errors:
            messagebox.showwarning('部分图片未处理', '\n'.join(errors[:8]))

    def selected(self):
        selection = self.tree.selection()
        if not selection:
            return None, None
        frame_index, object_id = self.row_lookup[selection[0]]
        return self.records[frame_index], object_id

    def show_selected(self, event=None):
        frame, object_id = self.selected()
        if frame is None:
            return
        try:
            with Image.open(frame['path']) as source:
                image = annotate(source, frame, selected_id=object_id)
            self.root.update_idletasks()
            width, height = self.preview_area.winfo_width(), self.preview_area.winfo_height()
            image.thumbnail((max(1, width - 8), max(1, height - 8)) if width > 1 and height > 1 else (800, 320))
            self.photo = ImageTk.PhotoImage(image)
            self.preview.configure(image=self.photo)
            if frame['mode'] == 'crop':
                self.footer.set('裁剪模式：不输出物块检测坐标。红圈仅为缺角响应最强位置，得分未经概率校准。')
            else:
                self.footer.set('十字为检测框中心；坐标为原图像素，非机械臂坐标。x2/y2 为不包含的右下边界。得分未经概率校准。')
        except OSError as error:
            self.status.set('预览读取失败：' + str(error))

    def preview_resized(self, event=None):
        if self.resize_id is not None:
            self.root.after_cancel(self.resize_id)
        self.resize_id = self.root.after(100, self.refresh_preview)

    def refresh_preview(self):
        self.resize_id = None
        self.show_selected()

    def export(self):
        if not self.records:
            return
        path = filedialog.asksaveasfilename(defaultextension='.csv',
                                           filetypes=[('CSV 坐标表', '*.csv'), ('JSON 完整结果', '*.json')])
        if path:
            try:
                export_results(self.records, Path(path))
                self.status.set('已导出：' + path)
            except OSError as error:
                messagebox.showerror('导出失败', str(error))

    def save_annotation(self):
        frame, object_id = self.selected()
        if frame is None:
            return
        path = filedialog.asksaveasfilename(defaultextension='.png',
                                           initialfile=Path(frame['path']).stem + '_annotated.png',
                                           filetypes=[('PNG 标注图片', '*.png')])
        if path:
            try:
                if Path(path).resolve() == Path(frame['path']).resolve():
                    messagebox.showerror('保存失败', '请使用新的文件名，避免覆盖原图。')
                    return
                with Image.open(frame['path']) as source:
                    annotate(source, frame, selected_id=object_id).save(path)
                self.status.set('已保存原图分辨率标注图：' + path)
            except OSError as error:
                messagebox.showerror('保存失败', str(error))

    def close(self):
        self.closed = True
        self.root.after_cancel(self.poll_id)
        if self.resize_id is not None:
            self.root.after_cancel(self.resize_id)
        self.root.destroy()


def main():
    app = App()
    app.root.mainloop()


if __name__ == '__main__':
    main()
