"""GUI integration test; needs Tk and a display. No modal dialogs are shown."""
import csv
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))


@unittest.skipUnless(os.environ.get('DISPLAY'), 'Tk requires a display')
class GuiTests(unittest.TestCase):
    def test_fullframe_crop_preview_and_export(self):
        import tkinter as tk
        from triangle_defect.app import App, CROP, FULL_FRAME

        root = tk.Tk()
        root.withdraw()
        app = App(root)

        def wait_until(predicate):
            deadline = time.monotonic() + 30
            while not predicate() and time.monotonic() < deadline:
                root.update()
                time.sleep(.02)
            self.assertTrue(predicate(), app.status.get())

        try:
            wait_until(lambda: app.engine is not None)
            self.assertEqual(app.mode.get(), FULL_FRAME)
            app.start([ROOT / 'data/originals/blue_003.jpg', ROOT / 'data/originals/blue_051.jpg'])
            wait_until(lambda: not app.busy)
            self.assertEqual(len(app.tree.get_children()), 2)
            self.assertIn('缺角 1 个', app.status.get())
            self.assertTrue(app.photo.width() > 0)
            self.assertTrue(app.tree.item('0', 'values')[3])
            root.deiconify()
            for geometry in ('900x650', '1180x860'):
                root.geometry(geometry)
                root.update()
                app.show_selected()
                self.assertLessEqual(app.photo.height(), app.preview_area.winfo_height())
                self.assertLessEqual(app.photo.width(), app.preview_area.winfo_width())
                self.assertLess(app.tree.winfo_rooty() + app.tree.winfo_height(),
                                root.winfo_rooty() + root.winfo_height())
                self.assertLessEqual(app.footer_label.winfo_rooty() + app.footer_label.winfo_height(),
                                     root.winfo_rooty() + root.winfo_height())
            root.withdraw()
            with tempfile.TemporaryDirectory() as directory:
                folder = Path(directory)
                for extension in ('csv', 'json'):
                    path = folder / f'results.{extension}'
                    with patch('triangle_defect.app.filedialog.asksaveasfilename', return_value=str(path)):
                        app.export()
                    self.assertTrue(path.is_file())
                with (folder / 'results.csv').open(encoding='utf-8-sig') as stream:
                    self.assertEqual(len(list(csv.DictReader(stream))), 2)
                self.assertEqual(len(json.loads((folder / 'results.json').read_text())['frames']), 2)
                path = folder / 'annotation.png'
                with patch('triangle_defect.app.filedialog.asksaveasfilename', return_value=str(path)):
                    app.save_annotation()
                with Image.open(path) as image:
                    self.assertEqual(image.size, (1920, 1080))
                empty_path = folder / 'empty.png'
                Image.new('RGB', (800, 600), 'green').save(empty_path)
                app.start([empty_path])
                wait_until(lambda: not app.busy)
                self.assertEqual(app.tree.item('0', 'values')[2], '未找到蓝色物块')
                app.mode.set(CROP)
                app.mode_changed()
                app.start([ROOT / 'data/images/test/normal/blue_004_historic.png'])
                wait_until(lambda: not app.busy)
                self.assertEqual(app.tree.item('0', 'values')[3], '裁剪模式不定位')
                self.assertIsNone(app.records[0]['objects'][0]['center_xy'])
        finally:
            app.close()


if __name__ == '__main__':
    unittest.main()
