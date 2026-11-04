"""Regeneration checks for the board CAN0 connection and PS clock isolation."""
from pathlib import Path
import hashlib
import importlib.util
import shutil
import tempfile
import unittest
import xml.etree.ElementTree as ET

PROJECT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("integrate_can", PROJECT / "src/conveyor/integrate_can.py")
can = importlib.util.module_from_spec(spec)
spec.loader.exec_module(can)


def settings(path):
    return {n.get("referenceId"): n.text for n in ET.parse(path).getroot().iter()
            if n.get("referenceId")}


class CanIntegration(unittest.TestCase):
    def test_repeat_generation_preserves_non_can_configuration(self):
        # Exercise the actual legacy generated files, with the CAN path removed.
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory)
            ip = project / "design_0/ip" / can.PS
            shutil.copytree(PROJECT / "design_0/ip" / can.PS, ip)
            (project / "design_0/hdl").mkdir(parents=True)
            for relative in ["Design.xml", "design_0.xml", "hdl/design_0.v"]:
                shutil.copy2(PROJECT / "design_0" / relative, project / "design_0" / relative)
            for path in project.rglob("*.v"):
                text = path.read_text(encoding="utf-8")
                import re
                text = re.sub(r"^.*(?:input wire can0_rx_in|output wire can0_tx_out|\.can0_tx_out\().*\n", "", text, flags=re.M)
                text = re.sub(r"^.*\.can0_rx_in\(can0_rx_in\).*\n", "", text, flags=re.M)
                if path.parent.name == "RTL":
                    text = text.replace(".can1_rx_in(1'b0),", ".can0_rx_in(1'b0),\n.can1_rx_in(1'b0),")
                path.write_text(text, encoding="utf-8")
            before = settings(project / "design_0/Design.xml")
            can.main(project)
            after = settings(project / "design_0/Design.xml")
            self.assertEqual({k: v for k, v in before.items() if not k.startswith("CAN0_")},
                             {k: v for k, v in after.items() if not k.startswith("CAN0_")})
            self.assertEqual(after["CAN0_Available"], "1")
            self.assertEqual(after["CAN0_Dot_IO"], '"PL IO"')
            hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in project.rglob("*") if p.is_file()}
            can.main(project)
            self.assertEqual(hashes, {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in hashes})
            self.assertIn(".can0_tx_out(can0_tx_out)", (ip / "RTL/ARM_Processor_System_9c4afc9a72e9.v").read_text())


if __name__ == "__main__":
    unittest.main()
