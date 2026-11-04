"""Resolve native TD paths before work and preserve historical run folders."""
from pathlib import Path
import tempfile
import tkinter
import unittest


class VersionedBuildTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix='fpga path with spaces ')
        self.addCleanup(self.folder.cleanup)
        self.project = Path(self.folder.name)
        driver = Path(__file__).resolve().parents[1] / 'build_video_dma.tcl'
        self.driver = self.project / 'build_video_dma.tcl'
        self.driver.write_bytes(driver.read_bytes())
        self.tcl = tkinter.Tcl()
        self.tcl.eval('''
            rename source real_source
            proc source {args} {
                set path [lindex $args end]
                if {[file tail $path] eq "build_resolved.tcl"} {
                    set ::generated_worker $path
                } else {uplevel 1 [linsert $args 0 real_source]}
            }
            proc exit {code} {error "TEST_EXIT_$code"}
        ''')
        old_run = self.tcl.eval('expr {[info exists ::env(VIDEO_DMA_RUN)] ? $::env(VIDEO_DMA_RUN) : ""}')
        self.addCleanup(self.tcl.setvar, 'env(VIDEO_DMA_RUN)', old_run)
        self.tcl.setvar('env(VIDEO_DMA_RUN)', 'test_run')

    def run_driver(self):
        return self.tcl.call('real_source', str(self.driver))

    def test_all_native_outputs_have_literal_paths_even_with_spaces(self):
        self.run_driver()
        worker = Path(str(self.tcl.getvar('generated_worker')))
        text = worker.read_text()
        self.assertNotIn('@ARG_', text)
        self.assertNotIn('@PROJECT_', text)
        self.assertNotIn('@OUTPUT_', text)
        outputs = []
        for line in text.splitlines():
            words = line.strip().split()
            if words and words[0] in {'export_db', 'report_area', 'report_timing_summary',
                                       'report_timing_exception', 'report_path_skew', 'check_timing', 'optimize_gate'}:
                self.assertNotIn('$', line)
                self.assertNotIn('[file join', line)
                args = self.tcl.splitlist(line.strip())
                path = Path(args[-1])
                self.assertEqual(path.parent, worker.parent)
                outputs.append(args)
        self.assertGreater(len(outputs), 15)

    def test_existing_run_cannot_be_overwritten(self):
        self.run_driver()
        worker = Path(str(self.tcl.getvar('generated_worker')))
        original = worker.read_bytes()
        with self.assertRaisesRegex(tkinter.TclError, 'TEST_EXIT_1'):
            self.run_driver()
        self.assertEqual(worker.read_bytes(), original)

    def test_run_name_cannot_escape_run_directory(self):
        self.tcl.setvar('env(VIDEO_DMA_RUN)', '../unexpected')
        with self.assertRaisesRegex(tkinter.TclError, 'TEST_EXIT_1'):
            self.run_driver()
        self.assertFalse((self.project / 'unexpected').exists())


if __name__ == '__main__':
    unittest.main()
