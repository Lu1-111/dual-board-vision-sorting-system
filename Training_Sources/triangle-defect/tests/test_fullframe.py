"""Software checks; geometric fixtures are not defect-accuracy evaluation data."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from triangle_defect.detect import BlueLocator, FrameInspector, annotate, export_results


def scene(boxes=()):
    image = Image.new('RGB', (800, 600), (35, 135, 75))
    draw = ImageDraw.Draw(image)
    draw.rectangle((15, 350, 150, 480), fill='red')
    draw.rectangle((250, 350, 390, 490), fill='yellow')
    for box in boxes:
        draw.rectangle(box, fill=(25, 60, 220))
    return image


class LocatorTests(unittest.TestCase):
    def test_separated_objects_original_coordinates_and_other_colors(self):
        objects = BlueLocator().locate(scene([(100, 150, 240, 280), (480, 100, 620, 240)]))
        self.assertEqual([o['bbox_xyxy'] for o in objects], [[100, 150, 241, 281], [480, 100, 621, 241]])
        self.assertEqual([o['center_xy'] for o in objects], [[170.5, 215.5], [550.5, 170.5]])
        self.assertEqual([o['object_id'] for o in objects], [1, 2])
        self.assertTrue(all(not o['partial_at_frame_border'] for o in objects))

    def test_empty_grayscale_and_small_speck(self):
        locator = BlueLocator()
        for image in (scene(), scene([(200, 150, 205, 155)]), Image.new('L', (800, 600), 80)):
            self.assertEqual(locator.locate(image), [])


class InspectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.inspector = FrameInspector()

    def test_frame_edge_requires_review_and_crop_is_clipped(self):
        result = self.inspector.inspect_image(scene([(0, 150, 140, 280)]))
        obj = result['objects'][0]
        self.assertTrue(obj['partial_at_frame_border'])
        self.assertEqual(obj['crop_bbox_xyxy'][0], 0)
        self.assertEqual(obj['label'], 'needs_review')
        self.assertIsNone(obj['class_id'])
        self.assertIsNone(obj['score'])

    def test_actual_photos_annotation_and_export(self):
        results = []
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            for index, expected in [(3, 0), (51, 1)]:
                path = ROOT / f'data/originals/blue_{index:03d}.jpg'
                result = self.inspector.inspect(path)
                self.assertEqual(len(result['objects']), 1)
                obj = result['objects'][0]
                self.assertEqual(obj['class_id'], expected)
                x1, y1, x2, y2 = obj['bbox_xyxy']
                self.assertEqual(obj['center_xy'], [(x1 + x2) / 2, (y1 + y2) / 2])
                with Image.open(path) as image:
                    original = image.tobytes()
                    marked = annotate(image, result)
                    self.assertEqual(marked.size, (1920, 1080))
                    self.assertEqual(image.tobytes(), original)
                    self.assertNotEqual(marked.tobytes(), original)
                results.append(result)
            empty = self.inspector.inspect_image(scene(), 'empty.png')
            self.assertEqual(empty['status'], 'no_blue_object')
            results.append(empty)
            export_results(results, folder / 'results.json')
            export_results(results, folder / 'results.csv')
            stored = json.loads((folder / 'results.json').read_text())
            self.assertEqual(stored['frames'], results)
            with (folder / 'results.csv').open(encoding='utf-8-sig') as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(len(rows), 3)
            self.assertEqual(float(rows[0]['center_x']), results[0]['objects'][0]['center_xy'][0])
            self.assertEqual(rows[2]['frame_status'], 'no_blue_object')
            self.assertEqual(rows[2]['label'], '')

    def test_crop_mode_preserves_prediction_without_inventing_coordinates(self):
        path = ROOT / 'data/images/test/normal/blue_004_historic.png'
        expected = self.inspector.predictor.predict(path)
        result = self.inspector.inspect_crop(path)
        obj = result['objects'][0]
        self.assertEqual(obj['score'], expected['score'])
        self.assertIsNone(obj['bbox_xyxy'])
        self.assertIsNone(obj['center_xy'])

    def test_cli_batch_reports_bad_files_and_keeps_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            inputs, output = directory / 'inputs', directory / 'output'
            inputs.mkdir()
            source = ROOT / 'data/originals/blue_051.jpg'
            (inputs / source.name).symlink_to(source)
            scene().save(inputs / 'empty.png')
            (inputs / 'broken.jpg').write_text('not an image')
            command = [sys.executable, str(ROOT / 'run.py'), 'detect', str(inputs), '--output', str(output)]
            completed = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 1, completed.stderr)
            summary = json.loads(completed.stdout)
            self.assertEqual((summary['images'], summary['blue_objects'], summary['errors']), (2, 1, 1))
            self.assertEqual(summary['no_object_images'], 1)
            frames = json.loads((output / 'results.json').read_text())['frames']
            for frame in frames:
                with Image.open(output / frame['annotated_image']) as image:
                    self.assertEqual(list(image.size), frame['image_size'])
            self.assertEqual(len(json.loads((output / 'errors.json').read_text())), 1)
            rerun = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(rerun.returncode, 0)
            self.assertIn('输出目录已存在', rerun.stderr)


if __name__ == '__main__':
    unittest.main()
